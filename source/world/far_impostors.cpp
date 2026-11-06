#include "shared.h"
#include "far_impostors.h"

#include "core/maths_helper.h"
#include "render/3dd.h"
#include "render/ed_render.h"
#include "render/file_collection_3d.h"
#include "render/gs_render.h"
#include "render/heightmap_renderer.h"
#include "render/instance.h"
#include "render/material.h"
#include "render/render_buckets.h"
#include "render/render_context.h"
#include "render/render_scene.h"
#include "render/render_target.h"
#include "render/shaders/shader_binary_cache.h"
#include "render/state_cache.h"
#include "render/camera/gs_camera.h"
#include "world/area_geometry.h"
#include "world/gs_world_streaming.h"
#include "world/impostor.h"
#include "world/sector_manager.h"
#include "world/streaming_manager.h"
#include "world/world_level.h"

#include <Eigen/EulerAngles>

#include <algorithm>
#include <cfloat>
#include <functional>
#include <fstream>

// .PMI decode constants (see sector_manager.cpp)
static constexpr float kScaleFactor = 1.0f / 16384.0f; // DAT_00f41ec0
static constexpr float kRotationFactor =               // DAT_00f41ec4
    2.0f * 3.14159265358979f / 65536.0f;
static constexpr float kPositionFactor = 1.0f / 64.0f; // DAT_00f41ebc
static constexpr float kPositionOffset = 128.0f;       // DAT_00f4176c

static constexpr float kCellSize = 768.0f;
static constexpr float kSectorCenterOffset = 384.0f;
static constexpr float kSectorRadius = 724.1f;
static constexpr float kSectorCullHeight = 600.0f;
static constexpr uint32_t kGPUCellsPerAxis = 4;
static constexpr float kGPUCellSize = 256.0f;
static constexpr float kGPUCellDistanceMargin = 32.0f;
static constexpr float kGPUCellFrustumMargin = 64.0f;

static constexpr eRenderBucket kOpaqueBucket = RB_FullAlphaFar;

static constexpr uint64_t kGPUExpandedVertexShaderHashcode =
    0xf4a1e0f1a0000002ull;
static constexpr uint32_t kGPUSpriteSize = 3 * sizeof( Eigen::Vector4f );
static constexpr uint64_t kCullComputeShaderHashcode = 0xf4a1e0f1c0000001ull;

static constexpr uint32_t kGPUScaleShift = 5; // 1/16384 -> 1/512
static constexpr uint32_t kGPUScaleMask = 0x7ff;
static constexpr uint32_t kGPUGreyShift = 3; // 8 -> 5 bits

static VertexLayoutAttribute gFarImpostorExpandedVertexLayout[] = {
    {0, 0,    eVertexAttributeFormat::VAF_POSITION_R32G32B32_SFLOAT,    0, 0x0,
     0x0}, // Quad corner
    {1, 0x00, eVertexAttributeFormat::VAF_INSTANCE_R32G32B32A32_SFLOAT, 0, 0x5,
     0x1}, // Center, u min (Texcoord1)
    {1, 0x10, eVertexAttributeFormat::VAF_INSTANCE_R32G32B32A32_SFLOAT, 0, 0x5,
     0x2}, // Right, v top (Texcoord2)
    {1, 0x20, eVertexAttributeFormat::VAF_INSTANCE_R32G32B32A32_SFLOAT, 0, 0x5,
     0x3}, // Up, color (Texcoord3)
    kVertexLayoutSentinel
};

static const RenderObject *GetImpostorRenderObject( Instance *pInstance ) {
    Render3DDHashEntry *pHashEntry = pInstance->getLOD( 0 ).pHashcode;
    if ( pHashEntry == nullptr ) {
        return nullptr;
    }
    RenderFile::Section *pObjectSection = pHashEntry->pEntry;
    if ( pObjectSection == nullptr ) {
        return nullptr;
    }
    return (const RenderObject *)( pObjectSection + 1 );
}

static Matrix4x4f_t ComputeImpostorBillboardMatrix() {
    const Matrix4x4f_t &camToWorld =
        gpCamera->getRenderCamera()->getCamToWorld();
    const float cameraYaw = atan2f( camToWorld( 0, 2 ), camToWorld( 2, 2 ) );
    Eigen::EulerAnglesYXZf eulerAngles( cameraYaw, 0.0f, 0.0f );
    Matrix4x4f_t billboardMatrix = Matrix4x4f_t::Identity();
    billboardMatrix.block<3, 3>( 0, 0 ) = eulerAngles.toRotationMatrix();
    return billboardMatrix;
}

FarImpostorField::FarImpostorField()
    : packData(),
      pRuns( nullptr ),
      pRecords( nullptr ),
      sectors(),
      types(),
      numSectors( 0 ),
      numSubSectors( 0 ),
      maxFarDist( 0.0f ),
      maxScale( 0.0f ),
      pLevel( nullptr ),
      drawList(),
      numSprites( 0 ),
      bHasSprites( false ),
      pInstanceBuffer( nullptr ),
      pQuadVertexBuffer( nullptr ),
      pQuadIndexBuffer( nullptr ),
      gpuOrigin( Eigen::Vector4f::Zero() ),
      gpuCamera( Eigen::Vector4f::Zero() ),
      gpuRight( Eigen::Vector4f::Zero() ),
      gpuUp( Eigen::Vector4f::Zero() ),
      pCullPipeline( nullptr ),
      pSpriteBuffer( nullptr ),
      pArgumentBuffer( nullptr ),
      pJobBuffers{ nullptr, nullptr, nullptr },
      pTypeParamBuffers{ nullptr, nullptr, nullptr },
      pExpandedVertexLayout( nullptr ),
      gpuJobs(),
      segmentCounts(),
      segmentArguments(),
      typeParams(),
      numJobInstances( 0 ),
      pHeightBuffer( nullptr ),
      pTerrainBuffers{ nullptr, nullptr, nullptr },
      gpuTerrain(),
      uploadedTerrain(),
      frameTerrain(),
      beginSceneCallback( 0 ),
      bComputePending( false ) {}

FarImpostorField::~FarImpostorField() { release(); }

void FarImpostorField::initialize( Level &level,
                                   const char *pLevelName,
                                   uint32_t sectorsPerArea,
                                   uint32_t subSectorsPerArea ) {
    release();

    numSectors = sectorsPerArea;
    numSubSectors = subSectorsPerArea;
    if ( !loadPack( pLevelName ) ) {
        return;
    }
    bindTypes( level );

    DrawStreams streams;
    streams.Diffuse = 1;
    streams.UVMap = 1;
    streams.bIndex16 = 1;
    drawList.initialize( kMaxNumPrimitives, 4, 6, true, &streams,
                         "FarImpostors", true );

    if ( !createGPUResources() || !createComputeResources() ) {
        OTDU_LOG_WARN( "Far impostors: GPU resources unavailable\n" );
        releaseComputeResources();
        releaseGPUResources();
        drawList.destroy();
    }
}

void FarImpostorField::release() {
    if ( drawList.isInitialized() ) {
        drawList.destroy();
    }
    releaseComputeResources();
    releaseGPUResources();

    if ( pLevel != nullptr ) {
        for ( TypeInfo &type : types ) {
            if ( type.pPrototype != nullptr ) {
                pLevel->getGlobalCollection().uninstantiate( type.pPrototype );
            }
        }
    }
    types.clear();
    sectors.clear();
    packData.clear();
    pRuns = nullptr;
    pRecords = nullptr;
    pLevel = nullptr;
    numSprites = 0;
    bHasSprites = false;
}

bool FarImpostorField::loadPack( const char *pLevelName ) {
    std::string path = FarImpostorPack::kLevelsFolder;
    path += pLevelName;
    path += '/';
    path += FarImpostorPack::kFileName;

    std::ifstream file( path, std::ios::binary | std::ios::ate );
    if ( !file ) {
        OTDU_LOG_WARN( "Far impostors: '%s' not found (run the asset "
                       "extractor)\n",
                       path.c_str() );
        return false;
    }
    const std::streamsize size = file.tellg();
    file.seekg( 0 );
    packData.resize( static_cast<size_t>( size ) );
    file.read( reinterpret_cast<char *>( packData.data() ), size );
    if ( !file || packData.size() < sizeof( FarImpostorPack::Header ) ) {
        packData.clear();
        return false;
    }

    const FarImpostorPack::Header *pHeader =
        reinterpret_cast<const FarImpostorPack::Header *>( packData.data() );
    const size_t expectedSize =
        sizeof( FarImpostorPack::Header ) +
        pHeader->NumTypes * sizeof( uint64_t ) +
        pHeader->NumSectors * sizeof( FarImpostorPack::Sector ) +
        pHeader->NumRuns * sizeof( FarImpostorPack::Run ) +
        static_cast<size_t>( pHeader->NumRecords ) *
            sizeof( FarImpostorPack::Record );
    if ( pHeader->Magic != FarImpostorPack::kMagic ||
         pHeader->Version != FarImpostorPack::kVersion ||
         packData.size() != expectedSize ) {
        OTDU_LOG_WARN( "Far impostors: '%s' is invalid or outdated\n",
                       path.c_str() );
        packData.clear();
        return false;
    }

    const uint8_t *pCursor =
        packData.data() + sizeof( FarImpostorPack::Header );
    pCursor += pHeader->NumTypes * sizeof( uint64_t );
    const FarImpostorPack::Sector *pSectors =
        reinterpret_cast<const FarImpostorPack::Sector *>( pCursor );
    pCursor += pHeader->NumSectors * sizeof( FarImpostorPack::Sector );
    pRuns = reinterpret_cast<const FarImpostorPack::Run *>( pCursor );
    pCursor += pHeader->NumRuns * sizeof( FarImpostorPack::Run );
    pRecords = reinterpret_cast<const FarImpostorPack::Record *>( pCursor );

    sectors.resize( pHeader->NumSectors );
    for ( uint32_t i = 0; i < pHeader->NumSectors; i++ ) {
        const FarImpostorPack::Sector &sector = pSectors[i];
        float worldX =
            ( numSectors * sector.AreaX + sector.SectorX ) * kCellSize;
        if ( sector.SectorY & 1 ) {
            worldX += kCellSize * 0.5f;
        }
        const float worldZ =
            ( numSubSectors * sector.AreaY + sector.SectorY ) * kCellSize;
        sectors[i].pSector = &sector;
        sectors[i].WorldCorner = Eigen::Vector3f( worldX, 0.0f, worldZ );
    }

    types.clear();
    types.resize( pHeader->NumTypes );

    OTDU_LOG_INFO( "Far impostors: %u sector(s), %u type(s), %u instance(s)\n",
                   pHeader->NumSectors, pHeader->NumTypes,
                   pHeader->NumRecords );
    return true;
}

void FarImpostorField::bindTypes( Level &level ) {
    pLevel = &level;
    FileCollection3D &collection = level.getGlobalCollection();

    const FarImpostorPack::Header *pHeader =
        reinterpret_cast<const FarImpostorPack::Header *>( packData.data() );
    const uint64_t *pHashcodes = reinterpret_cast<const uint64_t *>(
        packData.data() + sizeof( FarImpostorPack::Header ) );

    maxFarDist = 0.0f;
    uint32_t numBound = 0;
    for ( uint32_t i = 0; i < pHeader->NumTypes; i++ ) {
        TypeInfo &type = types[i];
        const uint64_t hashcode = pHashcodes[i];

        Render3DD *pTemplate3DD = nullptr;
        RenderFile::Section *pDef =
            collection.findInstance( hashcode, &pTemplate3DD );
        if ( pDef == nullptr ) {
            continue;
        }
        Instance *pInstance = collection.instantiate(
            reinterpret_cast<InstanceEntry *>( pDef + 1 ), hashcode );
        if ( pInstance == nullptr ) {
            continue;
        }
        pInstance->getFlagsWrite() = ( pInstance->getFlags() | 0x30 ) & ~0x3u;
        pInstance->calculateBiggestScale();
        type.pPrototype = pInstance;

        const uint32_t numLOD = pInstance->getNumLODs();
        if ( numLOD <= 1 ) {
            continue;
        }

        MassInstanceImpostor mi{};
        mi.pInstance = pInstance;
        mi.Scale = 1.0f;
        PreComputeImpostorDeltaXZ( mi );
        if ( !InstallImpostorMaterial( mi ) || mi.pMaterial == nullptr ) {
            continue;
        }

        type.pObject = GetImpostorRenderObject( pInstance );
        if ( type.pObject == nullptr ) {
            continue;
        }
        type.SizeDelta = mi.SpriteSizeDelta;
        type.StretchUV = mi.StretchUV;
        type.FarDist = pInstance->getLOD( numLOD - 1 ).Distances[0];
        type.FadeStart = pInstance->getLOD( numLOD - 2 ).Distances[0];
        type.SphereCenter = type.pObject->BoundingSphere.head<3>();
        type.pMaterial = mi.pMaterial;
        type.pMaterialOpaque =
            type.pMaterial->copyTo( type.pBufferMaterialOpaque, 0 );
        if ( type.pMaterialOpaque != nullptr ) {
            type.pMaterialOpaque->pOTNode = nullptr;
            type.pMaterialOpaque->OT = kOpaqueBucket;
            type.pMaterialOpaque->BlendOP = eBlendOP::BO_Disabled;
            type.pMaterialOpaque->AlphaTest = eComparisonFunction::CF_Never;
            type.pMaterialOpaque->DepthWrite = 1;
        }
        type.pMaterialFar =
            type.pMaterial->copyTo( type.pBufferMaterialFar, 0 );
        if ( type.pMaterialFar != nullptr ) {
            type.pMaterialFar->pOTNode = nullptr;
            type.pMaterialFar->SrcBlend = eBlendMode::BM_SrcAlpha;
            type.pMaterialFar->DstBlend = eBlendMode::BM_InvSrcAlpha;
            type.pMaterialFar->BlendOP = eBlendOP::BO_Add;
            type.pMaterialFar->AlphaSource = 0;
            type.pMaterialFar->DepthWrite = 2;
            type.pMaterialFar->OT = RB_ImpostorFar;
            type.pMaterialFar->AlphaRef = 0;
        }

        maxFarDist = Max( maxFarDist, type.FarDist );
        numBound++;
    }

    for ( uint32_t i = 0; i < pHeader->NumTypes; i++ ) {
        for ( uint32_t pass = 0; pass < 2; pass++ ) {
            types[i].gpuDraws[pass].pField = this;
            types[i].gpuDraws[pass].TypeIndex = i;
            types[i].gpuDraws[pass].bFadePass = pass != 0;
        }
    }

    maxScale = 0.0f;
    for ( uint32_t i = 0; i < pHeader->NumRuns; i++ ) {
        maxScale = Max( maxScale, pRuns[i].MaxScale * kScaleFactor );
    }

    OTDU_LOG_INFO( "Far impostors: %u/%u type(s) bound\n", numBound,
                   pHeader->NumTypes );
}

bool FarImpostorField::createGPUResources() {
    RenderDevice *pDevice = gEdRender.pRenderDevice;
    const FarImpostorPack::Header *pHeader =
        reinterpret_cast<const FarImpostorPack::Header *>( packData.data() );
    if ( pDevice == nullptr || pHeader->NumRecords == 0 ) {
        return false;
    }

    // Corners in MngImpostor::pushSprite order (-right+up, -right-up, ...)
    static constexpr float kQuadCorners[4][3] = {
        {-1.0f, 1.0f,  0.0f},
        {-1.0f, -1.0f, 0.0f},
        {1.0f,  1.0f,  0.0f},
        {1.0f,  -1.0f, 0.0f}
    };
    static constexpr uint16_t kQuadIndices[6] = { 0, 1, 2, 2, 1, 3 };

    GPUBufferDesc quadDesc;
    quadDesc.BufferType = GPUBufferDesc::Type::BT_VertexBuffer;
    quadDesc.Size = sizeof( kQuadCorners );
    quadDesc.Stride = sizeof( kQuadCorners[0] );
    pQuadVertexBuffer = pDevice->createBuffer( &quadDesc );

    GPUBufferDesc indexDesc;
    indexDesc.BufferType = GPUBufferDesc::Type::BT_IndexBuffer;
    indexDesc.Size = sizeof( kQuadIndices );
    indexDesc.Stride = sizeof( uint16_t );
    pQuadIndexBuffer = pDevice->createBuffer( &indexDesc );

    GPUBufferDesc instanceDesc;
    instanceDesc.BufferType = GPUBufferDesc::Type::BT_VertexBuffer;
    instanceDesc.Size = pHeader->NumRecords * sizeof( Eigen::Vector4f );
    instanceDesc.Stride = sizeof( Eigen::Vector4f );
    instanceDesc.bStorage = true;
    pInstanceBuffer = pDevice->createBuffer( &instanceDesc );

    if ( pQuadVertexBuffer == nullptr || pQuadIndexBuffer == nullptr ||
         pInstanceBuffer == nullptr ) {
        return false;
    }

    void *pQuadVertices = pDevice->lockBuffer( pQuadVertexBuffer, 0, 0, true );
    void *pIndices = pDevice->lockBuffer( pQuadIndexBuffer, 0, 0, true );
    float *pInstances = static_cast<float *>(
        pDevice->lockBuffer( pInstanceBuffer, 0, 0, true ) );
    if ( pQuadVertices == nullptr || pIndices == nullptr ||
         pInstances == nullptr ) {
        return false;
    }
    memcpy( pQuadVertices, kQuadCorners, sizeof( kQuadCorners ) );
    memcpy( pIndices, kQuadIndices, sizeof( kQuadIndices ) );

    gpuCells.clear();
    gpuScales.assign( pHeader->NumRecords, 0.0f );
    uint32_t nextInstance = 0u;
    std::vector<uint32_t> cellRecords[kGPUCellsPerAxis * kGPUCellsPerAxis];
    for ( SectorInfo &info : sectors ) {
        const FarImpostorPack::Sector &sector = *info.pSector;
        info.FirstGPUCell = static_cast<uint32_t>( gpuCells.size() );
        for ( uint32_t r = 0; r < sector.NumRuns; r++ ) {
            const FarImpostorPack::Run &run = pRuns[sector.FirstRun + r];
            for ( std::vector<uint32_t> &records : cellRecords ) {
                records.clear();
            }
            for ( uint32_t n = 0; n < run.NumRecords; n++ ) {
                const FarImpostorPack::Record &record =
                    pRecords[run.FirstRecord + n];
                const uint32_t cellX = Min<uint32_t>(
                    static_cast<uint32_t>( record.X * kPositionFactor /
                                           kGPUCellSize ),
                    kGPUCellsPerAxis - 1 );
                const uint32_t cellZ = Min<uint32_t>(
                    static_cast<uint32_t>( record.Z * kPositionFactor /
                                           kGPUCellSize ),
                    kGPUCellsPerAxis - 1 );
                cellRecords[cellZ * kGPUCellsPerAxis + cellX].push_back(
                    run.FirstRecord + n );
            }

            for ( std::vector<uint32_t> &records : cellRecords ) {
                if ( records.empty() ) {
                    continue;
                }
                std::sort( records.begin(), records.end(),
                           [this]( uint32_t lhs, uint32_t rhs ) {
                               return pRecords[lhs].Scale > pRecords[rhs].Scale;
                           } );

                GPUCell cell;
                cell.FirstInstance = nextInstance;
                cell.NumInstances = static_cast<uint32_t>( records.size() );
                cell.TypeIndex = run.TypeIndex;

                Eigen::Vector3f boundsMin =
                    Eigen::Vector3f::Constant( FLT_MAX );
                Eigen::Vector3f boundsMax =
                    Eigen::Vector3f::Constant( -FLT_MAX );
                for ( const uint32_t recordIndex : records ) {
                    const FarImpostorPack::Record &record =
                        pRecords[recordIndex];
                    const uint32_t packed =
                        ( record.Angle >> 8 ) |
                        ( Min<uint32_t>( record.Scale >> kGPUScaleShift,
                                         kGPUScaleMask )
                          << 8 ) |
                        ( static_cast<uint32_t>( record.Color >> kGPUGreyShift )
                          << 19 );
                    const Eigen::Vector3f position(
                        info.WorldCorner.x() + record.X * kPositionFactor -
                            kPositionOffset,
                        record.Y * sector.CellSizeY + sector.OriginY,
                        info.WorldCorner.z() + record.Z * kPositionFactor -
                            kPositionOffset );
                    float *pInstance = pInstances + nextInstance * 4;
                    pInstance[0] = position.x();
                    pInstance[1] = position.y();
                    pInstance[2] = position.z();
                    pInstance[3] = static_cast<float>( packed );
                    gpuScales[nextInstance] = record.Scale * kScaleFactor;
                    boundsMin = boundsMin.cwiseMin( position );
                    boundsMax = boundsMax.cwiseMax( position );
                    nextInstance++;
                }
                cell.Center = ( boundsMin + boundsMax ) * 0.5f;
                cell.Radius = ( boundsMax - boundsMin ).norm() * 0.5f;
                gpuCells.push_back( cell );
            }
        }
        info.NumGPUCells =
            static_cast<uint32_t>( gpuCells.size() ) - info.FirstGPUCell;
    }

    pDevice->unlockBuffer( pQuadVertexBuffer );
    pDevice->unlockBuffer( pQuadIndexBuffer );
    pDevice->unlockBuffer( pInstanceBuffer );

    OTDU_LOG_INFO(
        "Far impostors: %u instance(s) in %zu cell(s) uploaded (%u KB)\n",
        pHeader->NumRecords, gpuCells.size(), instanceDesc.Size / 1024 );
    return bindGPUShader();
}

void FarImpostorField::releaseGPUResources() {
    RenderDevice *pDevice = gEdRender.pRenderDevice;
    if ( pDevice != nullptr ) {
        if ( pInstanceBuffer != nullptr ) {
            pDevice->destroyBuffer( pInstanceBuffer );
        }
        if ( pQuadVertexBuffer != nullptr ) {
            pDevice->destroyBuffer( pQuadVertexBuffer );
        }
        if ( pQuadIndexBuffer != nullptr ) {
            pDevice->destroyBuffer( pQuadIndexBuffer );
        }
    }
    pInstanceBuffer = nullptr;
    pQuadVertexBuffer = nullptr;
    pQuadIndexBuffer = nullptr;
}

bool FarImpostorField::bindGPUShader() {
    VertexShader *pVertexShader = reinterpret_cast<VertexShader *>(
        gpRenderContext->getShaderHashTable().getShader(
            kGPUExpandedVertexShaderHashcode ) );
    if ( pVertexShader == nullptr ) {
        return false;
    }
    for ( TypeInfo &type : types ) {
        if ( type.pMaterialOpaque != nullptr ) {
            type.pMaterialOpaque->pVertexShaders[0] = pVertexShader;
        }
        if ( type.pMaterialFar != nullptr ) {
            type.pMaterialFar->pVertexShaders[0] = pVertexShader;
        }
    }
    return true;
}

bool FarImpostorField::createComputeResources() {
    RenderDevice *pDevice = gEdRender.pRenderDevice;
    const ShaderBytecode_t cullShader =
        gShaderBinaryCache.getComputeBytecode( kCullComputeShaderHashcode );
    const uint32_t *pCullSPIRV =
        static_cast<const uint32_t *>( std::get<0>( cullShader ) );
    const size_t cullSPIRVSize =
        static_cast<size_t>( std::get<1>( cullShader ) );
    if ( pCullSPIRV == nullptr || cullSPIRVSize == 0u ||
         ( cullSPIRVSize % sizeof( uint32_t ) ) != 0u ) {
        return false;
    }
    pCullPipeline = pDevice->createComputePipeline(
        pCullSPIRV, cullSPIRVSize, 7, 4 * sizeof( Eigen::Vector4f ) );
    pExpandedVertexLayout =
        pDevice->createVertexLayout( gFarImpostorExpandedVertexLayout );
    if ( pCullPipeline == nullptr || pExpandedVertexLayout == nullptr ) {
        return false;
    }

    const uint32_t numSegments = static_cast<uint32_t>( types.size() ) * 2;

    GPUBufferDesc spriteDesc;
    spriteDesc.BufferType = GPUBufferDesc::Type::BT_VertexBuffer;
    spriteDesc.Size = kMaxGPUSprites * kGPUSpriteSize;
    spriteDesc.Stride = kGPUSpriteSize;
    spriteDesc.bStorage = true;
    spriteDesc.bDeviceLocal = true;
    pSpriteBuffer = pDevice->createBuffer( &spriteDesc );

    GPUBufferDesc argumentDesc;
    argumentDesc.BufferType = GPUBufferDesc::Type::BT_VertexBuffer;
    argumentDesc.Size = numSegments * sizeof( DrawIndexedIndirectCommand );
    argumentDesc.Stride = sizeof( DrawIndexedIndirectCommand );
    argumentDesc.bStorage = true;
    argumentDesc.bIndirect = true;
    argumentDesc.bDeviceLocal = true;
    pArgumentBuffer = pDevice->createBuffer( &argumentDesc );
    if ( pSpriteBuffer == nullptr || pArgumentBuffer == nullptr ) {
        return false;
    }

    for ( uint32_t i = 0; i < PendingFrameCount; i++ ) {
        GPUBufferDesc jobDesc;
        jobDesc.BufferType = GPUBufferDesc::Type::BT_VertexBuffer;
        jobDesc.Size = kMaxGPUJobs * sizeof( GPUJob );
        jobDesc.Stride = sizeof( GPUJob );
        jobDesc.bStorage = true;
        pJobBuffers[i] = pDevice->createBuffer( &jobDesc );

        GPUBufferDesc typeDesc;
        typeDesc.BufferType = GPUBufferDesc::Type::BT_VertexBuffer;
        typeDesc.Size = numSegments * sizeof( Eigen::Vector4f );
        typeDesc.Stride = sizeof( Eigen::Vector4f );
        typeDesc.bStorage = true;
        pTypeParamBuffers[i] = pDevice->createBuffer( &typeDesc );
        if ( pJobBuffers[i] == nullptr || pTypeParamBuffers[i] == nullptr ) {
            return false;
        }
    }

    static_assert( sizeof( GPUTerrainMap ) == 368,
                   "must match TerrainMap (std430) in far_impostor_cull.comp" );
    GPUBufferDesc heightDesc;
    heightDesc.BufferType = GPUBufferDesc::Type::BT_VertexBuffer;
    heightDesc.Size = kNumTerrainSlots * kTerrainGridSize * kTerrainGridSize *
                      sizeof( float );
    heightDesc.Stride = sizeof( float );
    heightDesc.bStorage = true;
    heightDesc.bDeviceLocal = true;
    pHeightBuffer = pDevice->createBuffer( &heightDesc );
    if ( pHeightBuffer == nullptr ) {
        return false;
    }
    for ( uint32_t i = 0; i < PendingFrameCount; i++ ) {
        GPUBufferDesc terrainDesc;
        terrainDesc.BufferType = GPUBufferDesc::Type::BT_VertexBuffer;
        terrainDesc.Size = sizeof( GPUTerrain );
        terrainDesc.Stride = sizeof( GPUTerrainMap );
        terrainDesc.bStorage = true;
        pTerrainBuffers[i] = pDevice->createBuffer( &terrainDesc );
        if ( pTerrainBuffers[i] == nullptr ) {
            return false;
        }
    }
    for ( uint32_t i = 0; i < kNumTerrainSlots; i++ ) {
        uploadedTerrain[i] = TerrainSlotKey();
        frameTerrain[i] = TerrainSlotKey();
    }
    gpuTerrain = GPUTerrain();

    segmentCounts.assign( numSegments, 0u );
    segmentArguments.assign( numSegments, DrawIndexedIndirectCommand() );
    typeParams.assign( numSegments, Eigen::Vector4f::Zero() );
    gpuJobs.reserve( kMaxGPUJobs );
    beginSceneCallback =
        pDevice->addBeginSceneCallback( [this]() { recordCompute(); } );
    return true;
}

void FarImpostorField::releaseComputeResources() {
    RenderDevice *pDevice = gEdRender.pRenderDevice;
    if ( pDevice != nullptr ) {
        if ( beginSceneCallback != 0 ) {
            pDevice->removeBeginSceneCallback( beginSceneCallback );
        }
        pDevice->destroyComputePipeline( pCullPipeline );
        GPUBuffer *pBuffers[] = {
            pSpriteBuffer,        pArgumentBuffer,      pJobBuffers[0],
            pJobBuffers[1],       pJobBuffers[2],       pTypeParamBuffers[0],
            pTypeParamBuffers[1], pTypeParamBuffers[2], pHeightBuffer,
            pTerrainBuffers[0],   pTerrainBuffers[1],   pTerrainBuffers[2] };
        for ( GPUBuffer *pBuffer : pBuffers ) {
            if ( pBuffer != nullptr ) {
                pDevice->destroyBuffer( pBuffer );
            }
        }
        if ( pExpandedVertexLayout != nullptr ) {
            pDevice->destroyVertexLayout( pExpandedVertexLayout );
        }
    }
    beginSceneCallback = 0;
    pCullPipeline = nullptr;
    pSpriteBuffer = nullptr;
    pArgumentBuffer = nullptr;
    for ( uint32_t i = 0; i < PendingFrameCount; i++ ) {
        pJobBuffers[i] = nullptr;
        pTypeParamBuffers[i] = nullptr;
        pTerrainBuffers[i] = nullptr;
    }
    pHeightBuffer = nullptr;
    pExpandedVertexLayout = nullptr;
    gpuJobs.clear();
    bComputePending = false;
}

void FarImpostorField::prepareComputeFrame( const float distanceScaleSq ) {
    uint32_t base = 0u;
    for ( uint32_t segment = 0; segment < segmentCounts.size(); segment++ ) {
        DrawIndexedIndirectCommand &arguments = segmentArguments[segment];
        arguments.IndexCount = 6u;
        arguments.InstanceCount = 0u;
        arguments.FirstIndex = 0u;
        arguments.VertexOffset = 0;
        arguments.FirstInstance = base;
        base += segmentCounts[segment];
    }
    for ( uint32_t t = 0; t < types.size(); t++ ) {
        const TypeInfo &type = types[t];
        const float sphereRadius =
            type.pObject != nullptr ? type.pObject->BoundingSphere.w() : 0.0f;
        typeParams[t * 2] =
            Eigen::Vector4f( type.FarDist * distanceScaleSq, type.FadeStart,
                             type.SizeDelta, type.StretchUV );
        typeParams[t * 2 + 1] =
            Eigen::Vector4f( type.SphereCenter.y(), sphereRadius, 0.0f, 0.0f );
    }
    bComputePending = !gpuJobs.empty();
}

void FarImpostorField::recordCompute() {
    if ( !bComputePending ) {
        return;
    }
    bComputePending = false;

    RenderDevice *pDevice = gEdRender.pRenderDevice;
    const uint32_t slot = pDevice->getFrameIndex();

    void *pJobs = pDevice->lockBuffer( pJobBuffers[slot], 0, 0, true );
    void *pTypes = pDevice->lockBuffer( pTypeParamBuffers[slot], 0, 0, true );
    if ( pJobs == nullptr || pTypes == nullptr ) {
        return;
    }
    void *pTerrain = pDevice->lockBuffer( pTerrainBuffers[slot], 0, 0, true );
    if ( pTerrain == nullptr ) {
        pDevice->unlockBuffer( pJobBuffers[slot] );
        pDevice->unlockBuffer( pTypeParamBuffers[slot] );
        return;
    }
    memcpy( pTerrain, &gpuTerrain, sizeof( GPUTerrain ) );
    pDevice->unlockBuffer( pTerrainBuffers[slot] );
    memcpy( pJobs, gpuJobs.data(), gpuJobs.size() * sizeof( GPUJob ) );
    memcpy( pTypes, typeParams.data(),
            typeParams.size() * sizeof( Eigen::Vector4f ) );
    pDevice->unlockBuffer( pJobBuffers[slot] );
    pDevice->unlockBuffer( pTypeParamBuffers[slot] );

    pDevice->insertMemoryBarrier( VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT |
                                      VK_PIPELINE_STAGE_VERTEX_INPUT_BIT |
                                      VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                  0,
                                  VK_PIPELINE_STAGE_TRANSFER_BIT |
                                      VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                  0 );
    uploadTerrainHeights();
    pDevice->updateBuffer(
        pArgumentBuffer, 0, segmentArguments.data(),
        static_cast<uint32_t>( segmentArguments.size() *
                               sizeof( DrawIndexedIndirectCommand ) ) );
    pDevice->insertMemoryBarrier(
        VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_HOST_BIT,
        VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_HOST_WRITE_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT );

    GPUBuffer *pBuffers[7] = {
        pInstanceBuffer, pJobBuffers[slot], pTypeParamBuffers[slot],
        pSpriteBuffer,   pArgumentBuffer,   pTerrainBuffers[slot],
        pHeightBuffer };
    const Eigen::Vector4f parameters[4] = { gpuOrigin, gpuCamera, gpuRight,
                                            gpuUp };
    pDevice->dispatchCompute( pCullPipeline, pBuffers, parameters,
                              static_cast<uint32_t>( gpuJobs.size() ), 1, 1 );

    GPUTexture *pMainTarget = nullptr;
    const RenderPass &nearPass =
        gpRender->getRenderPassAtIndex( eRenderPass::RP_Near );
    if ( nearPass.pFramebufferAttachments != nullptr &&
         nearPass.pFramebufferAttachments->pAttachments[0] != nullptr ) {
        pMainTarget = nearPass.pFramebufferAttachments->pAttachments[0]
                          ->getTextureColor();
    }
    pDevice->deferMemoryBarrier( pMainTarget,
                                 VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                 VK_ACCESS_SHADER_WRITE_BIT,
                                 VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT |
                                     VK_PIPELINE_STAGE_VERTEX_INPUT_BIT,
                                 VK_ACCESS_INDIRECT_COMMAND_READ_BIT |
                                     VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT );
}

void FarImpostorField::addTerrainMap( const uint32_t slot,
                                      const AreaGeometry &geometry ) {
    const float *pHeights = nullptr;
    int32_t stride = 0;
    float quadSize = 0.0f;
    Instance *pInstance = geometry.pHeightmapInstance;
    if ( pInstance == nullptr ||
         !GetHeightmapSampleGrid( pInstance, &pHeights, &stride, &quadSize ) ||
         stride != static_cast<int32_t>( kTerrainGridSize ) ||
         !pInstance->isHeightmapLODCacheDriven() ) {
        return;
    }

    quadSize = pInstance->getModelMatrix()( 0, 0 );
    if ( quadSize <= 0.0f ) {
        return;
    }

    GPUTerrainMap &map = gpuTerrain.Maps[gpuTerrain.Header.NumMaps++];
    map.OriginX = geometry.heightmapAbsPos.x();
    map.OriginZ = geometry.heightmapAbsPos.z();
    map.QuadSize = quadSize;
    map.InvQuadSize = 1.0f / quadSize;
    map.HeightBase = slot * kTerrainGridSize * kTerrainGridSize;
    frameTerrain[slot].pHeights = pHeights;
    frameTerrain[slot].OriginX = map.OriginX;
    frameTerrain[slot].OriginZ = map.OriginZ;

    const uint8_t *pLODCache = pInstance->getHeightmapLODCache();
    const uint8_t *pTileMask = gbDebugHmapDisableStitchMask
                                   ? nullptr
                                   : pInstance->getHeightmapTileMask();
    uint8_t *pLODs = reinterpret_cast<uint8_t *>( map.LODs );
    for ( uint32_t i = 0; i < kTerrainNumTile * kTerrainNumTile; i++ ) {
        uint8_t lod = pLODCache[i];
        if ( pTileMask != nullptr ) {
            const uint8_t mask = pTileMask[i];
            if ( mask == kHeightmapMaskCull ) {
                lod = kHeightmapLODCulled;
            } else if ( mask != kHeightmapMaskNone &&
                        lod != kHeightmapLODCulled &&
                        lod != kHeightmapLODFlat ) {
                lod = ( mask == kHeightmapMaskFlat )
                          ? kHeightmapLODFlat
                          : static_cast<uint8_t>( mask - 1 );
            }
        }
        if ( gbDebugHmapForceFinestLOD && lod != kHeightmapLODCulled &&
             lod != kHeightmapLODFlat ) {
            lod = 0;
        }
        pLODs[i] = lod;
    }
}

void FarImpostorField::gatherTerrain( const AreaGrid &areaGrid,
                                      const AreaGeometry &levelGeometry ) {
    gpuTerrain.Header.NumMaps = 0;
    for ( uint32_t i = 0; i < kNumTerrainSlots; i++ ) {
        frameTerrain[i] = TerrainSlotKey();
    }
    if ( !bComputePending ) {
        return;
    }

    static_assert( sizeof( areaGrid.areas ) / sizeof( areaGrid.areas[0] ) ==
                       kNumTerrainSlots - 1,
                   "one terrain slot per area" );
    for ( uint32_t i = 0; i < kNumTerrainSlots - 1; i++ ) {
        if ( areaGrid.areas[i].state == 1 ) {
            addTerrainMap( i, areaGrid.areas[i].geometry );
        }
    }
    addTerrainMap( kNumTerrainSlots - 1, levelGeometry );
}

void FarImpostorField::uploadTerrainHeights() {
    static constexpr uint32_t kChunkSize = 65536u;
    RenderDevice *pDevice = gEdRender.pRenderDevice;
    for ( uint32_t slot = 0; slot < kNumTerrainSlots; slot++ ) {
        const TerrainSlotKey &key = frameTerrain[slot];
        if ( key.pHeights == nullptr || key == uploadedTerrain[slot] ) {
            continue;
        }
        const uint32_t size =
            kTerrainGridSize * kTerrainGridSize * sizeof( float );
        const uint32_t base = slot * size;
        const uint8_t *pSource =
            reinterpret_cast<const uint8_t *>( key.pHeights );
        for ( uint32_t offset = 0; offset < size; offset += kChunkSize ) {
            pDevice->updateBuffer( pHeightBuffer, base + offset,
                                   pSource + offset,
                                   std::min( kChunkSize, size - offset ) );
        }
        uploadedTerrain[slot] = key;
    }
}

void FarImpostorField::tick( const SectorManager &sectorManager ) {
    numSprites = 0;
    bHasSprites = false;
    bComputePending = false;
    gpuJobs.clear();
    numJobInstances = 0;
    std::fill( segmentCounts.begin(), segmentCounts.end(), 0u );
    for ( TypeInfo &type : types ) {
        type.bDrawn[0] = false;
        type.bDrawn[1] = false;
    }
    if ( !drawList.isInitialized() ) {
        return;
    }
    drawList.reset();
    drawList.setIdentityTransform();

    if ( gpMngImpostor == nullptr || gpRender == nullptr ||
         gpMngImpostor->isForceTopImpostor() ) {
        return;
    }
    const float distanceScale = gpRender->getImpostorFarDistanceScale();
    if ( distanceScale <= 1.0f ) {
        return;
    }
    const float distanceScaleSq = distanceScale * distanceScale;
    const float hideDistanceUnit = gpMngImpostor->getHideDistanceUnit();
    if ( hideDistanceUnit <= 0.0f ) {
        return;
    }

    RenderScene *pScene =
        gpRender->getRenderPassAtIndex( eRenderPass::RP_Near ).pScene;
    Camera *pCamera = gpCamera->getRenderCamera();
    if ( pScene == nullptr || pCamera == nullptr ) {
        return;
    }
    Frustum &frustum = pScene->getFrustumWrite();
    const Matrix4x4f_t &worldToCam = pCamera->getWorldToCam();

    const Eigen::Vector3f camPos =
        gpMngImpostor->getViewMatrix().row( 3 ).head<3>();
    const Eigen::Vector3f streamPos =
        gpWorldStreaming->isEnabled()
            ? Eigen::Vector3f( gpWorldStreaming->getPosition().head<3>() )
            : Eigen::Vector3f::Zero();

    const float maxRangeSq =
        maxFarDist * distanceScaleSq * maxScale * maxScale / hideDistanceUnit;

    struct Candidate {
        uint32_t sectorIndex;
        float minDistanceSq;
    };
    std::vector<Candidate> candidates;
    for ( uint32_t i = 0; i < sectors.size(); i++ ) {
        const SectorInfo &info = sectors[i];
        const FarImpostorPack::Sector &sector = *info.pSector;

        const float centerX =
            info.WorldCorner.x() + kSectorCenterOffset - streamPos.x();
        const float centerZ =
            info.WorldCorner.z() + kSectorCenterOffset - streamPos.z();
        const float dx = centerX - camPos.x();
        const float dz = centerZ - camPos.z();
        const float minDistance =
            Max( 0.0f, sqrtf( dx * dx + dz * dz ) - kSectorRadius );
        const float minDistanceSq = minDistance * minDistance;
        if ( minDistanceSq >= maxRangeSq ) {
            continue;
        }

        bool bLoaded = false;
        for ( const Sector &loaded : sectorManager.sectors ) {
            if ( loaded.state == 1 &&
                 loaded.installedArea.posX == sector.AreaX &&
                 loaded.installedArea.posY == sector.AreaY &&
                 loaded.installedSector.posX == sector.SectorX &&
                 loaded.installedSector.posY == sector.SectorY ) {
                bLoaded = true;
                break;
            }
        }
        if ( bLoaded ) {
            continue;
        }

        Eigen::Vector4f viewCenter = TDUMat4x4MulVec(
            worldToCam, Eigen::Vector4f( centerX, camPos.y(), centerZ, 1.0f ) );
        if ( frustum.testBoundingSphere(
                 viewCenter, kSectorRadius + kSectorCullHeight ) == 0 ) {
            continue;
        }

        candidates.push_back( { i, minDistanceSq } );
    }

    if ( !bindGPUShader() ) {
        return;
    }

    const Matrix4x4f_t billboardMatrix = ComputeImpostorBillboardMatrix();
    gpuOrigin = Eigen::Vector4f( streamPos.x(), streamPos.y(), streamPos.z(),
                                 hideDistanceUnit );
    gpuCamera = Eigen::Vector4f( camPos.x(), camPos.y(), camPos.z(), 0.0f );
    gpuRight = TDUMat4x4MulVec( billboardMatrix,
                                Eigen::Vector4f( 1.0f, 0.0f, 0.0f, 0.0f ) );
    gpuUp = TDUMat4x4MulVec( billboardMatrix,
                             Eigen::Vector4f( 0.0f, 1.0f, 0.0f, 0.0f ) );

    std::sort( candidates.begin(), candidates.end(),
               []( const Candidate &lhs, const Candidate &rhs ) {
                   return lhs.minDistanceSq < rhs.minDistanceSq;
               } );

    for ( const Candidate &candidate : candidates ) {
        submitGPUCells( sectors[candidate.sectorIndex], frustum, worldToCam,
                        camPos, streamPos, hideDistanceUnit, distanceScaleSq );
    }

    prepareComputeFrame( distanceScaleSq );
    emitGPUDraws();
}

void FarImpostorField::submitGPUCells( const SectorInfo &info,
                                       Frustum &frustum,
                                       const Matrix4x4f_t &worldToCam,
                                       const Eigen::Vector3f &camPos,
                                       const Eigen::Vector3f &streamPos,
                                       const float hideDistanceUnit,
                                       const float distanceScaleSq ) {
    auto countScalesAbove = []( const float *pScales, uint32_t count,
                                float minScale ) {
        return static_cast<uint32_t>(
            std::upper_bound( pScales, pScales + count, minScale,
                              std::greater<float>() ) -
            pScales );
    };

    for ( uint32_t c = 0; c < info.NumGPUCells; c++ ) {
        const GPUCell &cell = gpuCells[info.FirstGPUCell + c];
        TypeInfo &type = types[cell.TypeIndex];
        if ( type.pMaterial == nullptr || type.FarDist <= 0.0f ) {
            continue;
        }

        const Eigen::Vector3f center = cell.Center - streamPos;
        const float centerDistance = ( center - camPos ).norm();
        const float radius = cell.Radius + kGPUCellDistanceMargin;
        const float minDistance = Max( 0.0f, centerDistance - radius );
        const float maxDistance = centerDistance + radius;

        const float farDist = type.FarDist * distanceScaleSq;
        const float threshold =
            type.FadeStart + ( farDist - type.FadeStart ) * 0.66f;
        const float *pScales = gpuScales.data() + cell.FirstInstance;

        const uint32_t numVisible = countScalesAbove(
            pScales, cell.NumInstances,
            minDistance * sqrtf( hideDistanceUnit / farDist ) );
        if ( numVisible == 0u ) {
            continue;
        }

        Eigen::Vector4f viewCenter = TDUMat4x4MulVec(
            worldToCam,
            Eigen::Vector4f( center.x(), center.y(), center.z(), 1.0f ) );
        if ( frustum.testBoundingSphere(
                 viewCenter, cell.Radius + kGPUCellFrustumMargin ) == 0 ) {
            continue;
        }

        const uint32_t numOpaque =
            Min( numVisible,
                 countScalesAbove(
                     pScales, cell.NumInstances,
                     minDistance * sqrtf( hideDistanceUnit / threshold ) ) );
        const uint32_t numAlwaysOpaque =
            Min( numVisible,
                 countScalesAbove(
                     pScales, cell.NumInstances,
                     maxDistance * sqrtf( hideDistanceUnit / threshold ) ) );

        auto addJob = [&]( uint32_t first, uint32_t count, uint32_t pass ) {
            if ( count == 0u || gpuJobs.size() >= kMaxGPUJobs ||
                 numJobInstances + count > kMaxGPUSprites ) {
                return;
            }
            const uint32_t segment = cell.TypeIndex * 2u + pass;
            gpuJobs.push_back( { first, count, segment, 0u } );
            segmentCounts[segment] += count;
            numJobInstances += count;
            type.bDrawn[pass] = true;
        };
        addJob( cell.FirstInstance, numOpaque, 0u );
        addJob( cell.FirstInstance + numAlwaysOpaque,
                numVisible - numAlwaysOpaque, 1u );
    }
    numSprites = numJobInstances;
}

void FarImpostorField::emitGPUDraws() {
    for ( TypeInfo &type : types ) {
        Material *pMaterials[2] = { type.pMaterialOpaque, type.pMaterialFar };
        for ( uint32_t pass = 0; pass < 2; pass++ ) {
            if ( pMaterials[pass] == nullptr || !type.bDrawn[pass] ) {
                continue;
            }
            drawList.setActiveMaterial( pMaterials[pass] );
            if ( drawList.allocatePrimitive( PT_Triangle, 0, 0 ) < 0 ) {
                return;
            }
            drawList.setLastPrimitiveCustomDraw( &type.gpuDraws[pass] );
            bHasSprites = true;
        }
    }
}

void FarImpostorField::GPUDraw::draw( Primitive * ) {
    FarImpostorField &field = *pField;
    if ( !field.types[TypeIndex].bDrawn[bFadePass ? 1 : 0] ) {
        return;
    }
    gStateCache.bindVertexLayout( field.pExpandedVertexLayout );
    gStateCache.bindVertexBuffer( field.pQuadVertexBuffer, 0, 0,
                                  3 * sizeof( float ) );
    gStateCache.bindVertexBuffer( field.pSpriteBuffer, 1, 0, kGPUSpriteSize );
    gStateCache.bindIndexBuffer( field.pQuadIndexBuffer );
    const uint32_t segment = TypeIndex * 2u + ( bFadePass ? 1u : 0u );
    gEdRender.pRenderDevice->drawIndexedIndirectBuffer(
        PT_Triangle, field.pArgumentBuffer,
        segment * sizeof( DrawIndexedIndirectCommand ), 1u );
}

void FarImpostorField::submit() {
    if ( !bHasSprites ) {
        return;
    }
    RenderScene *pScene =
        gpRender->getRenderPassAtIndex( eRenderPass::RP_Near ).pScene;
    if ( pScene != nullptr ) {
        pScene->enqueueDynamicDrawList( &drawList );
    }
}
