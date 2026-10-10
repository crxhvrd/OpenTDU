#include "shared.h"
#include "mng_road_network.h"

#include "ai/gs_ai.h"
#include "player_data/gs_playerdata.h"
#include "player_data/player_data_interface.h"
#include "player_data/saved_road_discovery.h"
#include "render/camera/gs_camera.h"
#include "render/gs_render.h"
#include "render/material.h"
#include "render/render_scene.h"
#include "world/gs_world_streaming.h"

#include "system/system_shared.h"

MngRoadNetwork *gpMngRoadNetwork = nullptr; // DAT_010e8014

static uint8_t gRoadNetworkFrameStamp = 0; // DAT_010e8018

static constexpr float kMinFadeWidth = 0.3f;  // DAT_00f416bc
static constexpr float kLaneHalfWidth = 3.0f; // DAT_00f416b4

// Road material hashcode (alpha-blended World/Road shader).
static constexpr uint64_t kRoadMaterialHashcode = 0x73078865ca37336dull;

static constexpr uint32_t kNumStreams = 4;
static constexpr uint32_t kMaxNumVertex = 20000;
static constexpr uint32_t kMaxNumIndex = 21000;

// DAT_00f9c298
const MngRoadNetwork::Band MngRoadNetwork::kBands[4] = {
    {1666.0f,  1000.0f,  600.0f,   { 2, 2, 2 }, 0.6f},
    {7666.0f,  2400.0f,  1700.0f,  { 2, 2, 1 }, 1.0f},
    {38833.0f, 28249.0f, 8212.0f,  { 2, 1, 0 }, 1.0f},
    {99999.0f, 99999.0f, 30000.0f, { 1, 0, 0 }, 1.0f},
};

// DAT_00f9c308 (RGB per layer: discovered, then locked/unavailable)
static constexpr uint32_t kRoadColors[3][2][3] = {
    {{ 0x00, 0x00, 0xff }, { 0x28, 0x28, 0x28 }},
    {{ 0x00, 0x64, 0xff }, { 0x3c, 0x3c, 0x3c }},
    {{ 0x00, 0xc8, 0xff }, { 0x50, 0x50, 0x50 }},
};

static Matrix4x4f_t ComputeWorldShiftMatrix() {
    Matrix4x4f_t matrix = Matrix4x4f_t::Identity();
    if ( gpWorldStreaming != nullptr && gpWorldStreaming->isEnabled() ) {
        const Eigen::Vector4f &shift = gpWorldStreaming->getPosition();
        matrix.row( 3 ) =
            Eigen::Vector4f( -shift.x(), -shift.y(), -shift.z(), 1.0f )
                .transpose();
    }
    return matrix;
}

static Eigen::Vector4f ComputeSide( const Eigen::Vector4f &direction ) {
    // direction x up, normalized on 4 components
    Eigen::Vector4f side( -direction.z(), 0.0f, direction.x(), direction.w() );
    return side / side.norm();
}

void MngRoadNetwork::Writer::emit( const Eigen::Vector4f &position,
                                   const uint32_t color ) {
    // FUN_004322f0 / FUN_00432370
    *pPosition++ = position.head<3>();
    *pDiffuse++ = color;
    *pIndex++ = NextIndex++;
}

MngRoadNetwork::MngRoadNetwork()
    : Manager(),
      bInitialized( false ),
      roadList(),
      ribbonWriter(),
      lineWriter(),
      p2DMBuffer( nullptr ),
      road2DM(),
      pRoadMaterial( nullptr ),
      pBand( nullptr ),
      pNextBand( nullptr ),
      layerAlpha(),
      layerWidth(),
      ribbonAlpha( 0 ),
      cameraDistance( 0.0f ),
      halfExtentX( 0.0f ),
      halfExtentZ( 0.0f ),
      viewCenter( Eigen::Vector4f::Zero() ),
      bLastBand( false ),
      pClusters(),
      numClusters( 0 ),
      entries(),
      numEntries( 0 ),
      totalNumVertex( 0 ),
      totalNumIndex( 0 ),
      bDebugDrawBounds( false ) {
    // FUN_006bde40
    gpMngRoadNetwork = this;
}

MngRoadNetwork::~MngRoadNetwork() {
    // FUN_006bd9d0
    gpMngRoadNetwork = nullptr;
}

bool MngRoadNetwork::initialize( TestDriveGameInstance * ) {
    // FUN_006bd880
    DrawStreams streams;
    streams.Diffuse = 1;
    streams.bIndex16 = 1;
    if ( !roadList.initialize( kNumStreams, kMaxNumVertex, kMaxNumIndex, false,
                               &streams, "MngRoadNetwork", true ) ) {
        return false;
    }

    const uint32_t bufferSize = Render2DM::CalcSize( 0, 0 );
    p2DMBuffer = TestDrive::Alloc( bufferSize );
    if ( p2DMBuffer == nullptr ) {
        return false;
    }

    pRoadMaterial =
        Render2DM::Create( p2DMBuffer, kRoadMaterialHashcode, 0, 0, 0, 1 );
    if ( pRoadMaterial == nullptr ) {
        return false;
    }
    // Alpha-blended, double-sided road ribbons
    pRoadMaterial->DepthTest = 2;
    pRoadMaterial->DepthWrite = 2;
    pRoadMaterial->BackfaceCullingMode = (eCullMode)3;
    pRoadMaterial->AlphaTest = (eComparisonFunction)0;
    pRoadMaterial->AlphaRef = 10;
    pRoadMaterial->SrcBlend = (eBlendMode)5;
    pRoadMaterial->DstBlend = (eBlendMode)6;
    pRoadMaterial->BlendOP = (eBlendOP)1;

    if ( !road2DM.initialize( p2DMBuffer ) ) {
        return false;
    }
    pRoadMaterial->OT = 0x22;

    bInitialized = true;
    return true;
}

void MngRoadNetwork::reset() {
    // FUN_006bd860
    pBand = nullptr;
    pNextBand = nullptr;
    bDebugDrawBounds = false;
}

void MngRoadNetwork::pause() {
    // FUN_006bd850
}

void MngRoadNetwork::terminate() {
    // FUN_006bd7f0
    if ( p2DMBuffer != nullptr ) {
        road2DM.destroy();
        TestDrive::Free( p2DMBuffer );
        pRoadMaterial = nullptr;
        p2DMBuffer = nullptr;
    }
    if ( roadList.isInitialized() ) {
        roadList.destroy();
    }
    bInitialized = false;
}

bool MngRoadNetwork::updateBand() {
    // FUN_006bdbc0
    CameraGame *pRenderCamera = gpCamera->getRenderCamera();
    cameraDistance =
        ( pRenderCamera->getWorldPosition() - pRenderCamera->getTarget() )
            .norm();
    pBand = nullptr;
    pNextBand = nullptr;
    bLastBand = false;
    for ( int32_t i = 0; i < 4; i++ ) {
        if ( kBands[i].MaxDistance > cameraDistance ) {
            pBand = &kBands[i];
            if ( i < 3 ) {
                pNextBand = &kBands[i + 1];
            } else {
                bLastBand = true;
            }
            break;
        }
    }
    if ( pBand == nullptr ) {
        return false;
    }

    float fade = 1.0f;
    if ( cameraDistance > pBand->FadeStart ) {
        fade = 1.0f - ( cameraDistance - pBand->FadeStart ) /
                          ( pBand->MaxDistance - pBand->FadeStart );
    }
    const float alphaScale = ( 1.0f - fade ) + fade * pBand->AlphaScale;
    ribbonAlpha =
        static_cast<uint8_t>( static_cast<int32_t>( 255.0f * alphaScale ) );
    for ( int32_t layer = 0; layer < kNumLayers; layer++ ) {
        layerAlpha[layer] = 0xff;
        layerWidth[layer] = 1.0f;
        if ( cameraDistance > pBand->FadeStart && pNextBand != nullptr &&
             pNextBand->Mode[layer] != pBand->Mode[layer] ) {
            layerAlpha[layer] = static_cast<uint8_t>(
                static_cast<int32_t>( fade * alphaScale * 255.0f ) );
            layerWidth[layer] = ( kMinFadeWidth > fade ) ? kMinFadeWidth : fade;
        }
    }

    const Eigen::Vector3f target = gpCamera->getActiveCamera()->getTarget();
    viewCenter = Eigen::Vector4f( target.x(), target.y(), target.z(), 1.0f );
    halfExtentZ = pBand->HalfExtent;
    halfExtentX = GetScreenAspectRatio() * halfExtentZ;
    return true;
}

uint16_t
MngRoadNetwork::computeNumSamples( const AiRoadMap::Road *pRoad,
                                   const AiRoadMap::Entity *pEntity ) const {
    // FUN_00432280
    const uint8_t curvature = pRoad->Curvature;
    if ( pBand->Mode[pEntity->Layer] == DM_Line ) {
        if ( bLastBand || curvature < 20 ) {
            return 2;
        }
        return ( curvature < 40 ) ? 3 : 4;
    }
    if ( curvature < 5 ) {
        return 2;
    }
    if ( curvature < 10 ) {
        return 3;
    }
    return ( curvature < 0x80 ) ? 4 : 5;
}

void MngRoadNetwork::drawDebugBounds() {
    // FUN_006bda20
    Matrix4x4f_t shiftMatrix = ComputeWorldShiftMatrix();
    roadList.setActiveTransformMatrix( shiftMatrix );
    roadList.setActiveMaterial( pRoadMaterial );
    if ( !roadList.beginPrimitive( PT_TriangleStrip, 4 ) ) {
        return;
    }
    roadList.setDiffuse( 0, ColorRGBA( 0x800000ffu ) );
    const float minX = viewCenter.x() - halfExtentX;
    const float maxX = viewCenter.x() + halfExtentX;
    const float minZ = viewCenter.z() - halfExtentZ;
    const float maxZ = viewCenter.z() + halfExtentZ;
    roadList.pushVertex( minX, 0.0f, minZ );
    roadList.pushVertex( minX, 0.0f, maxZ );
    roadList.pushVertex( maxX, 0.0f, minZ );
    roadList.pushVertex( maxX, 0.0f, maxZ );
    roadList.commitPrimitive();
}

void MngRoadNetwork::allocateWriter( Writer &writer,
                                     const ePrimitiveType type,
                                     const uint32_t numVertex,
                                     const uint32_t numIndex ) {
    writer.PrimitiveIndex =
        roadList.allocatePrimitive( type, numVertex, numIndex );
    if ( writer.PrimitiveIndex == -1 ) {
        return;
    }
    ActivePrimitive &primitive = roadList.getActivePrimitive();
    writer.pPosition = primitive.pVertexPosition;
    writer.pDiffuse = primitive.pVertexDiffuse[0];
    writer.pIndex = primitive.pVertexIndices16;
    writer.NextIndex = 0;
}

void MngRoadNetwork::tick( float, float ) {
    // FUN_006be810
    if ( !bInitialized ) {
        return;
    }

    numEntries = 0;
    roadList.reset();
    if ( !updateBand() ) {
        return;
    }

    gRoadNetworkFrameStamp++;
    if ( bDebugDrawBounds ) {
        drawDebugBounds();
    }

    const AiRoadMap::EntityArray *pEntityArray =
        gpAI->getRoadMap().getEntityArray();
    if ( pEntityArray == nullptr ) {
        return;
    }
    const Eigen::Vector4f boundsMin =
        viewCenter + Eigen::Vector4f( -halfExtentX, 0.0f, -halfExtentZ, 0.0f );
    const Eigen::Vector4f boundsMax =
        viewCenter + Eigen::Vector4f( halfExtentX, 0.0f, halfExtentZ, 0.0f );
    numClusters = pEntityArray->gatherClusters( pClusters, kMaxNumClusters,
                                                boundsMin, boundsMax );
    if ( numClusters == 0 ) {
        return;
    }

    Entry *pEntry = entries;
    Entry *pPrevious = nullptr;
    for ( int32_t c = 0; c < numClusters && numEntries < kMaxNumEntries; c++ ) {
        const AiRoadMap::EntityCluster *pCluster = pClusters[c];
        for ( uint32_t r = 0; r < pCluster->NumEntities; r++ ) {
            const AiRoadMap::EntityRef &ref = pCluster->Refs[r];
            AiRoadMap::Road *pRoad = ref.pRoad;
            AiRoadMap::Entity *pEntity =
                pRoad->pDetail->pEntities[ref.EntityIndex];

            if ( pEntity->Flags == gRoadNetworkFrameStamp ||
                 pEntity->Layer >= kNumLayers ) {
                continue;
            }
            pEntity->Flags = gRoadNetworkFrameStamp;

            pEntry->Mode = pBand->Mode[pEntity->Layer];
            if ( pEntry->Mode == DM_Hidden || pEntity->getNumCurves() <= 0 ) {
                continue;
            }

            pEntry->pEntity = pEntity;
            pEntry->pRoad = pRoad;
            pEntry->EntityIndex = static_cast<uint16_t>( ref.EntityIndex );
            pEntry->NumSamples = computeNumSamples( pRoad, pEntity );
            if ( pPrevious == nullptr ) {
                pEntry->bStartCap = false;
            } else if ( pPrevious->pRoad == pRoad &&
                        pPrevious->EntityIndex == pEntry->EntityIndex - 1 ) {
                pEntry->bStartCap = false;
                pPrevious->bEndCap = false;
            } else {
                pEntry->bStartCap = true;
            }
            pEntry->bEndCap = true;

            if ( ++numEntries == kMaxNumEntries ) {
                break;
            }
            pPrevious = pEntry;
            pEntry++;
        }
    }

    struct ListHead {
        Entry *pFirst;
        Entry *pLast;
    };
    ListHead heads[kNumLayers] = {};
    uint32_t numLineVertex = 0;
    uint32_t numLineIndex = 0;
    uint32_t numRibbonVertex = 0;
    uint32_t numRibbonIndex = 0;
    totalNumVertex = 0;
    totalNumIndex = 0;
    int32_t numKept = 0;
    for ( ; numKept < numEntries; numKept++ ) {
        Entry &entry = entries[numKept];
        const int32_t numWaypoints = entry.pEntity->getNumWaypoints();
        uint32_t numVertex =
            ( numWaypoints - 2 ) * ( entry.NumSamples - 1 ) + entry.NumSamples;
        uint32_t numIndex = 0;
        if ( entry.Mode == DM_Ribbon ) {
            numVertex *= 2;
            numIndex = numVertex + ( entry.bEndCap ? 2 : 0 );
        } else {
            numVertex += entry.bStartCap ? 1 : 0;
            numVertex += entry.bEndCap ? 1 : 0;
            numIndex = numVertex;
        }

        totalNumVertex += numVertex;
        totalNumIndex += numIndex;
        if ( totalNumVertex > kMaxNumVertex || totalNumIndex > kMaxNumIndex ) {
            break;
        }

        if ( entry.Mode == DM_Ribbon ) {
            numRibbonVertex += numVertex;
            numRibbonIndex += numIndex;
        } else {
            numLineVertex += numVertex;
            numLineIndex += numIndex;
        }

        ListHead &head = heads[entry.pEntity->Layer];
        if ( head.pLast == nullptr ) {
            head.pFirst = &entry;
        } else {
            head.pLast->pNext = &entry;
        }
        head.pLast = &entry;
        entry.pNext = nullptr;
    }
    numEntries = numKept;

    Entry *pFirst = nullptr;
    Entry *pLast = nullptr;
    for ( int32_t layer = kNumLayers - 1; layer >= 0; layer-- ) {
        if ( heads[layer].pFirst == nullptr ) {
            continue;
        }
        if ( pLast == nullptr ) {
            pFirst = heads[layer].pFirst;
        } else {
            pLast->pNext = heads[layer].pFirst;
        }
        pLast = heads[layer].pLast;
    }

    Matrix4x4f_t shiftMatrix = ComputeWorldShiftMatrix();
    roadList.setActiveMaterial( pRoadMaterial );
    roadList.setActiveTransformMatrix( shiftMatrix );
    if ( static_cast<int32_t>( numLineVertex ) > 0 ) {
        allocateWriter( lineWriter, PT_LineStrip, numLineVertex, numLineIndex );
    }
    if ( static_cast<int32_t>( numRibbonVertex ) > 0 ) {
        allocateWriter( ribbonWriter, PT_TriangleStrip, numRibbonVertex,
                        numRibbonIndex );
    }

    for ( Entry *pIt = pFirst; pIt != nullptr; pIt = pIt->pNext ) {
        if ( pIt->Mode == DM_Ribbon && ribbonWriter.PrimitiveIndex != -1 ) {
            emitRibbon( *pIt );
        } else if ( pIt->Mode == DM_Line && lineWriter.PrimitiveIndex != -1 ) {
            emitLine( *pIt );
        }
    }
}

uint32_t MngRoadNetwork::computeColor( const Entry &entry,
                                       const uint8_t alpha ) const {
    const SavedRoadDiscovery *pDiscovery =
        gpPlayerData->getLoadedSave()->getRoadDiscovery();
    const bool bLocked =
        pDiscovery->isRoadLocked( static_cast<int32_t>( entry.pRoad->ID ),
                                  entry.EntityIndex ) ||
        !IsRoadAvailable( entry.pRoad->ID );
    const uint32_t *pRGB = kRoadColors[entry.pEntity->Layer][bLocked ? 1 : 0];
    return ( static_cast<uint32_t>( alpha ) << 24 ) | ( pRGB[0] << 16 ) |
           ( pRGB[1] << 8 ) | pRGB[2];
}

void MngRoadNetwork::emitRibbon( const Entry &entry ) {
    // FUN_006be1a0
    const AiRoadMap::Entity *pEntity = entry.pEntity;
    const AiRoadMap::Road *pRoad = entry.pRoad;
    const uint32_t color = computeColor( entry, ribbonAlpha );
    int32_t numSamples = entry.NumSamples;
    const float step = 1.0f / static_cast<float>( numSamples - 1 );
    const bool bStartJunction =
        entry.EntityIndex == 0 && pRoad->pJunctionBegin != nullptr;
    const bool bEndJunction =
        pRoad->pJunctionEnd != nullptr &&
        entry.EntityIndex == pRoad->pDetail->NumEntities - 1;
    const int32_t numCurves = pEntity->getNumCurves();
    const float halfWidth =
        static_cast<float>( pEntity->NumLeftLanes + pEntity->NumRightLanes ) *
        kLaneHalfWidth * layerWidth[pEntity->Layer];

    Eigen::Vector4f center;
    Eigen::Vector4f direction;
    if ( bStartJunction ) {
        center = pRoad->pJunctionBegin->Position.toVector4();
        direction = pEntity->computePosition( 0, 0.0f ) - center;
    } else {
        center = pEntity->computePosition( 0, 0.0f );
        direction = pEntity->computeTangent( 0, 0.0f );
        direction /= direction.norm();
    }
    Eigen::Vector4f side = ComputeSide( direction );
    ribbonWriter.emit( center + side * -halfWidth, color );
    ribbonWriter.emit( center + side * halfWidth, color );

    for ( int32_t curve = 0; curve < numCurves; curve++ ) {
        if ( bEndJunction && curve == numCurves - 1 ) {
            numSamples--;
        }
        if ( numSamples <= 1 ) {
            continue;
        }
        float t = step;
        for ( int32_t k = numSamples - 1; k != 0; k-- ) {
            center = pEntity->computePosition( curve, t );
            direction = pEntity->computeTangent( curve, t );
            direction /= direction.norm();
            side = ComputeSide( direction );
            ribbonWriter.emit( center - side * halfWidth, color );
            ribbonWriter.emit( center + side * halfWidth, color );
            t += step;
            if ( t > 1.0f ) {
                t = 1.0f;
            }
        }
    }

    if ( bEndJunction ) {
        const Eigen::Vector4f junction =
            pRoad->pJunctionEnd->Position.toVector4();
        direction = junction - pEntity->computePosition( numCurves - 1, 1.0f );
        side = ComputeSide( direction );
        ribbonWriter.emit( junction - side * halfWidth, color );
        ribbonWriter.emit( junction + side * halfWidth, color );
    }

    if ( entry.bEndCap ) {
        *ribbonWriter.pIndex++ = ribbonWriter.NextIndex - 1;
        *ribbonWriter.pIndex++ = ribbonWriter.NextIndex;
    }
}

void MngRoadNetwork::emitLine( const Entry &entry ) {
    // FUN_006bde90
    const AiRoadMap::Entity *pEntity = entry.pEntity;
    const AiRoadMap::Road *pRoad = entry.pRoad;
    const uint32_t color = computeColor( entry, layerAlpha[pEntity->Layer] );
    int32_t numSamples = entry.NumSamples;
    const float step = 1.0f / static_cast<float>( numSamples - 1 );
    const bool bStartJunction =
        entry.EntityIndex == 0 && pRoad->pJunctionBegin != nullptr;
    const bool bEndJunction =
        pRoad->pJunctionEnd != nullptr &&
        entry.EntityIndex == pRoad->pDetail->NumEntities - 1;
    const int32_t numCurves = pEntity->getNumCurves();

    Eigen::Vector4f position = bStartJunction
                                   ? pRoad->pJunctionBegin->Position.toVector4()
                                   : pEntity->computePosition( 0, 0.0f );
    if ( entry.bStartCap ) {
        // FUN_004323f0
        lineWriter.emit( position, 0u );
    }
    lineWriter.emit( position, color );

    for ( int32_t curve = 0; curve < numCurves; curve++ ) {
        if ( bEndJunction && curve == numCurves - 1 ) {
            numSamples--;
        }
        if ( numSamples <= 1 ) {
            continue;
        }
        float t = step;
        for ( int32_t k = numSamples - 1; k != 0; k-- ) {
            position = pEntity->computePosition( curve, t );
            lineWriter.emit( position, color );
            t += step;
            if ( t > 1.0f ) {
                t = 1.0f;
            }
        }
    }

    if ( bEndJunction ) {
        position = pRoad->pJunctionEnd->Position.toVector4();
        lineWriter.emit( position, color );
    }
    if ( entry.bEndCap ) {
        lineWriter.emit( position, 0u );
    }
}

void MngRoadNetwork::draw() {
    // FUN_006be150
    if ( roadList.getNumPrimitives() > 0 ) {
        gpRender->getRenderPass<eRenderPass::RP_Near>()
            .pScene->enqueueDynamicDrawList( &roadList );
    }
}
