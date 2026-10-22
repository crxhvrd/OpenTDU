#include "shared.h"
#include "otdu_lights.h"

#include "config/gs_config.h"
#include "filesystem/bank.h"
#include "game/scenaric/gs_scenaric.h"
#include "game/scenaric/spots.h"
#include "game/vehicle/car_state.h"
#include "game/vehicle/gs_car_state.h"
#include "game/vehicle/vehicle_base.h"
#include "render/hiearchy_node.h"
#include "render/3dd.h"
#include "render/instance.h"
#include "core/maths_helper.h"
#include "filesystem/gs_file.h"
#include "render/2db.h"
#include "render/2dm.h"
#include "render/camera/camera_base.h"
#include "render/draw_list.h"
#include "render/ed_render.h"
#include "render/gs_render.h"
#include "render/material.h"
#include "render/scene_renderer.h"
#include "render/render_buckets.h"
#include "render/render_scene.h"
#include "world/gs_world.h"
#include "world/gs_world_streaming.h"
#include "world/impostor.h"
#include "world/sector_manager.h"
#include "world/spot_stream.h"
#include "world/streaming_manager.h"

#include <atomic>
#include <filesystem>
#include <fstream>
#include <thread>
#include <unordered_map>

// Lights further than this from the camera are never sent to the shaders.
static constexpr float kMaxLightDistance = 150.0f;
// Lights fade out over this distance before the selection cutoff (the max
// distance, or the first light that does not fit), so none pops in or out.
static constexpr float kLightFadeDistance = 20.0f;

// Lamp glows reuse the traffic light halo billboard shader: the sprite grows
// with the distance (x = start, y = range, z = extra scale) so far lamps keep
// a few pixels on screen, and is pulled toward the camera by w * distance.
static constexpr uint64_t kGlowShaderGUID = 0xfcdaa96bdeadbeef;
static constexpr uint32_t kGlowParameterType = 0x7a;
static constexpr const char *kGlowBitmapName = "O_HALO_2";
// Glow count / distance per quality level (eOTDUGlowQuality, Options > Video).
struct OTDUGlowQualityDesc {
    uint32_t MaxNumGlows;
    float MaxDistance;
};
static constexpr OTDUGlowQualityDesc kGlowQualities[GQ_Count] = {
    {256,  1000.0f},
    {1024, 2000.0f},
    {2048, 4000.0f},
    {4096, 8000.0f},
};
static constexpr uint32_t kMaxNumGlows =
    kGlowQualities[GQ_Count - 1].MaxNumGlows;
static eOTDUGlowQuality gGlowQuality = GQ_Default;
static constexpr float kGlowHalfSize = 0.5f;
static const Eigen::Vector4f kGlowDistanceScale( 10.0f, 1500.0f, 12.0f, 0.02f );

// Fake volumetric light shafts below the nearest lamps: a trapezoid hanging
// from the lamp, turned about the vertical axis to face the camera. Same
// shader with no sprite offset (null normals, no distance scale), sampling a
// haze gradient from the night pack (u = across, v = lamp to ground; see
// tools/night_light_shaft_texture.py). No shafts without the pack bitmap.
static constexpr const char *kShaftBitmapPath = "FX/LightShaft_OpenTDU.2db";
static constexpr uint64_t kBoundTextureHashcode = 0x0000000100000000ull;
static constexpr uint32_t kMaxNumShafts = 64;
static constexpr uint32_t kNumShaftRows = 8;
static constexpr uint32_t kNumShaftVertices = ( kNumShaftRows + 1 ) * 2;
static constexpr float kMaxShaftDistance = 150.0f;
static constexpr float kShaftFadeDistance = 100.0f;
static constexpr float kShaftTopHalfWidth = 0.3f;
static constexpr float kShaftSpread = 0.7f; // ground half width / height
static constexpr float kShaftAlpha = 0.35f;
// Seen from below the lamp the flat shaft would show: fade it out.
static constexpr float kShaftMinHorizontalDistance = 1.5f;
static constexpr float kShaftFadeHorizontalDistance = 5.0f;
// The shaft alpha is per material: per lamp fades pick one of these levels.
static constexpr uint32_t kNumShaftFadeLevels = 16;
static const Eigen::Vector4f kShaftDistanceScale( 0.0f, 1.0f, 0.0f, 0.0f );

struct OTDUGlowMaterial {
    void *pMemory = nullptr;
    Render2DM render2DM;
    Material *pMaterial = nullptr;
    bool bBitmapBound = false;
};

struct GlowCandidate {
    float Distance;
    Eigen::Vector3f Position; // render space
    int32_t Model;
};

struct OTDULightGlows {
    OTDUGlowMaterial glow;
    void *pShaftBitmapResource = nullptr;
    Render2DB shaftBitmap;
    bool bShaftsEnabled = false;
    std::array<OTDUGlowMaterial, kNumShaftFadeLevels> shafts;
    DrawList drawList;
    std::vector<GlowCandidate> candidates;
};

static std::vector<OTDULightModel> gLightModels;
static OTDULightGlows *gpLightGlows = nullptr;
static bool gbLightGlowsFailed = false;

// A model can carry several lights (e.g. a double arm lamp post); declaring
// the same light again replaces it.
void RegisterOTDULightModel( const OTDULightModel &model ) {
    for ( OTDULightModel &existing : gLightModels ) {
        if ( existing.Hashcode == model.Hashcode &&
             existing.Offset.isApprox( model.Offset, 1e-3f ) ) {
            existing = model;
            return;
        }
    }
    gLightModels.push_back( model );
}

const OTDULightModel *FindOTDULightModel( uint64_t hashcode,
                                          int32_t *pOutIndex ) {
    for ( size_t i = 0; i < gLightModels.size(); i++ ) {
        if ( gLightModels[i].Hashcode == hashcode ) {
            if ( pOutIndex != nullptr ) {
                *pOutIndex = static_cast<int32_t>( i );
            }
            return &gLightModels[i];
        }
    }
    return nullptr;
}

// Appends the lights of every light model entry sharing the model's hashcode
// (absolute position, model index).
static void AppendModelLights( std::vector<OTDUPlacedLight> &lights,
                               const std::vector<OTDULightModel> &models,
                               int32_t modelIndex,
                               const Matrix4x4f_t &instanceMatrix,
                               const Eigen::Vector4f &origin ) {
    const uint64_t hashcode = models[modelIndex].Hashcode;
    for ( size_t i = modelIndex; i < models.size(); i++ ) {
        if ( models[i].Hashcode != hashcode ) {
            continue;
        }
        const Eigen::Vector3f &offset = models[i].Offset;
        const Eigen::Vector4f position = instanceMatrix.row( 0 ) * offset.x() +
                                         instanceMatrix.row( 1 ) * offset.y() +
                                         instanceMatrix.row( 2 ) * offset.z() +
                                         instanceMatrix.row( 3 );
        lights.push_back( OTDUPlacedLight(
            position.x() + origin.x(), position.y() + origin.y(),
            position.z() + origin.z(), static_cast<float>( i ) ) );
    }
}

void PlaceOTDULight( ImpostorGrid &grid,
                     int32_t modelIndex,
                     const Matrix4x4f_t &instanceMatrix,
                     const Eigen::Vector4f &streamOrigin ) {
    AppendModelLights( grid.lights, gLightModels, modelIndex, instanceMatrix,
                       streamOrigin );
}

// Lights of every sector of the level, so the glows of distant lamps do not
// stop at the streamed sectors. Scanned once per level by a worker thread
// that reads the sector banks' impostor / object PMI entries only (loose
// files under the resource root; mod packs replacing sector banks are not
// seen).
struct OTDUWorldLights {
    std::thread worker;
    std::atomic<bool> bReady{ false };
    std::atomic<bool> bCancel{ false };
    std::string levelName;
    std::vector<OTDUPlacedLight> lights; // valid once bReady
};
static OTDUWorldLights *gpWorldLights = nullptr;

static bool ReadFileRange( std::ifstream &file,
                           uint64_t offset,
                           size_t size,
                           std::vector<uint8_t> &output ) {
    output.resize( size );
    file.seekg( static_cast<std::streamoff>( offset ) );
    file.read( reinterpret_cast<char *>( output.data() ),
               static_cast<std::streamsize>( size ) );
    return file.good();
}

static void ScanSectorBankLights( const std::filesystem::path &path,
                                  const Eigen::Vector4f &worldCorner,
                                  const std::vector<OTDULightModel> &models,
                                  std::vector<OTDUPlacedLight> &lights ) {
    std::ifstream file( path, std::ios::binary );
    std::vector<uint8_t> buffer;
    if ( !file ||
         !ReadFileRange( file, 0, 8 + sizeof( BankHeader ), buffer ) ) {
        return;
    }
    BankHeader header;
    memcpy( &header, buffer.data() + 8, sizeof( BankHeader ) );
    if ( !header.isVersionValid() || header.NumEntries <= 0 ) {
        return;
    }

    // Section payloads start after their 8 byte section header.
    std::vector<uint8_t> entries;
    std::vector<uint8_t> types;
    if ( !ReadFileRange( file, header.SectionOffsetTable + 8,
                         sizeof( BankSection ) * header.NumEntries, entries ) ||
         !ReadFileRange( file, header.SectionOffsetTypes + 8,
                         sizeof( BankType ) * header.NumEntries, types ) ) {
        return;
    }

    for ( int32_t i = 0; i < header.NumEntries; i++ ) {
        BankSection section;
        BankType type;
        memcpy( &section, entries.data() + i * sizeof( BankSection ),
                sizeof( BankSection ) );
        memcpy( &type, types.data() + i * sizeof( BankType ),
                sizeof( BankType ) );
        // Group 5, type 10 = impostor grid, type 0xd = dynamic object grid.
        if ( type.TypeUnknown != 5 || ( type.Type != 10 && type.Type != 0xd ) ||
             section.Size <= 0 ||
             !ReadFileRange( file, section.Offset, section.Size, buffer ) ) {
            continue;
        }
        ForEachPMIInstance(
            buffer.data(), static_cast<uint32_t>( section.Size ),
            ( type.Type == 10 ) ? 0 : 1, worldCorner,
            [&]( uint64_t hashcode ) {
                for ( const OTDULightModel &model : models ) {
                    if ( model.Hashcode == hashcode ) {
                        return true;
                    }
                }
                return false;
            },
            [&]( uint64_t hashcode, const Matrix4x4f_t &instanceMatrix ) {
                for ( size_t m = 0; m < models.size(); m++ ) {
                    if ( models[m].Hashcode == hashcode ) {
                        AppendModelLights(
                            lights, models, static_cast<int32_t>( m ),
                            instanceMatrix, Eigen::Vector4f::Zero() );
                        break;
                    }
                }
            } );
    }
}

// Sector banks are Level/<level>/Area-X/Area-X-Y/Sector-X-Y-SX-SY.bnk; their
// world corner follows StreamingManager::installSector.
static void ScanWorldLights( OTDUWorldLights *pWorldLights,
                             std::filesystem::path levelFolder,
                             uint32_t numSectors,
                             uint32_t numSubSectors,
                             std::vector<OTDULightModel> models ) {
    static constexpr int32_t kCellSize = 0x300;

    std::vector<OTDUPlacedLight> lights;
    uint32_t numBanks = 0;
    std::error_code errorCode;
    for ( const auto &entry : std::filesystem::recursive_directory_iterator(
              levelFolder, errorCode ) ) {
        if ( pWorldLights->bCancel ) {
            return;
        }
        int32_t areaX, areaY, sectorX, sectorY;
        const std::string filename = entry.path().filename().string();
        if ( sscanf( filename.c_str(), "Sector-%d-%d-%d-%d.bnk", &areaX, &areaY,
                     &sectorX, &sectorY ) != 4 ) {
            continue;
        }
        int32_t worldX =
            static_cast<int32_t>( numSectors ) * areaX * kCellSize +
            sectorX * kCellSize;
        if ( sectorY & 1 ) {
            worldX += kCellSize / 2;
        }
        const int32_t worldZ =
            static_cast<int32_t>( numSubSectors ) * areaY * kCellSize +
            sectorY * kCellSize;
        ScanSectorBankLights(
            entry.path(),
            Eigen::Vector4f( static_cast<float>( worldX ), 0.0f,
                             static_cast<float>( worldZ ), 0.0f ),
            models, lights );
        numBanks++;
    }

    OTDU_LOG_INFO( "Found %zu lamp(s) in %u sector bank(s) of '%s'\n",
                   lights.size(), numBanks, levelFolder.string().c_str() );
    pWorldLights->lights = std::move( lights );
    pWorldLights->bReady = true;
}

static void StopWorldLightScan() {
    if ( gpWorldLights == nullptr ) {
        return;
    }
    gpWorldLights->bCancel = true;
    if ( gpWorldLights->worker.joinable() ) {
        gpWorldLights->worker.join();
    }
    delete gpWorldLights;
    gpWorldLights = nullptr;
}

static void UpdateWorldLightScan() {
    const char *pLevelName = gpConfig->pLevelName;
    if ( pLevelName[0] == '\0' || ( gpWorldLights != nullptr &&
                                    gpWorldLights->levelName == pLevelName ) ) {
        return;
    }
    StopWorldLightScan();

    const SectorManager &sectorManager =
        gpWorld->getStreamingManager()->sectorManager;
    std::filesystem::path levelFolder = gpConfig->getResRootPath();
    levelFolder /= "Level";
    levelFolder /= pLevelName;

    gpWorldLights = new OTDUWorldLights();
    gpWorldLights->levelName = pLevelName;
    gpWorldLights->worker = std::thread(
        ScanWorldLights, gpWorldLights, levelFolder, sectorManager.numSectors,
        sectorManager.numSubSectors, gLightModels );
}

template <typename Callback>
static void ForEachLoadedOTDULight( const Eigen::Vector4f &streamOrigin,
                                    Callback callback ) {
    SectorManager &sectorManager =
        gpWorld->getStreamingManager()->sectorManager;
    for ( Sector &sector : sectorManager.sectors ) {
        if ( sector.state != 1 ) {
            continue;
        }
        for ( ImpostorGrid &grid : sector.geometry.impostorGrids ) {
            for ( const OTDUPlacedLight &light : grid.lights ) {
                const Eigen::Vector3f position( light.x() - streamOrigin.x(),
                                                light.y() - streamOrigin.y(),
                                                light.z() - streamOrigin.z() );
                if ( !callback( position,
                                static_cast<int32_t>( light.w() ) ) ) {
                    return;
                }
            }
        }
    }
}

static Eigen::Vector4f GetStreamOrigin() {
    return gpWorldStreaming->isEnabled() ? gpWorldStreaming->getPosition()
                                         : Eigen::Vector4f::Zero();
}

static Eigen::Vector3f GetNearCameraPosition() {
    return gpRender->getRenderPass<eRenderPass::RP_Near>()
        .pCamera->getCamToWorld()
        .row( 3 )
        .head<3>();
}

void UpdateOTDULights() {
    gEdRender.NumLamps = 0;
    if ( gLightModels.empty() || gpWorld == nullptr ||
         gpWorld->getStreamingManager() == nullptr ) {
        return;
    }

    UpdateWorldLightScan();

    const Eigen::Vector3f camera = GetNearCameraPosition();

    struct Candidate {
        float DistanceSquared;
        Eigen::Vector3f Position; // render space
        int32_t Model;
    };
    // One extra slot: the first light left out sets the fade cutoff.
    std::array<Candidate, edRender::kMaxNumLamps + 1> nearest;
    uint32_t numNearest = 0;

    ForEachLoadedOTDULight(
        GetStreamOrigin(),
        [&]( const Eigen::Vector3f &position, int32_t model ) {
            const float distanceSquared = ( position - camera ).squaredNorm();
            if ( distanceSquared > kMaxLightDistance * kMaxLightDistance ) {
                return true;
            }

            // Insertion into the sorted list of the nearest lights.
            uint32_t slot = numNearest;
            while ( slot > 0 &&
                    nearest[slot - 1].DistanceSquared > distanceSquared ) {
                if ( slot < nearest.size() ) {
                    nearest[slot] = nearest[slot - 1];
                }
                slot--;
            }
            if ( slot < nearest.size() ) {
                nearest[slot] = { distanceSquared, position, model };
                numNearest = Min<uint32_t>(
                    numNearest + 1, static_cast<uint32_t>( nearest.size() ) );
            }
            return true;
        } );

    const uint32_t numLamps = Min( numNearest, edRender::kMaxNumLamps );
    const float cutoff = numNearest > edRender::kMaxNumLamps
                             ? std::sqrt( nearest[numLamps].DistanceSquared )
                             : kMaxLightDistance;
    for ( uint32_t i = 0; i < numLamps; i++ ) {
        const OTDULightModel &model = gLightModels[nearest[i].Model];
        const float fade =
            Clamp( ( cutoff - std::sqrt( nearest[i].DistanceSquared ) ) /
                       kLightFadeDistance,
                   0.0f, 1.0f );
        gEdRender.LampPositions[i] =
            Eigen::Vector4f( nearest[i].Position.x(), nearest[i].Position.y(),
                             nearest[i].Position.z(), model.Radius );
        gEdRender.LampColors[i] = Eigen::Vector4f(
            model.Color.x(), model.Color.y(), model.Color.z(), fade );
    }
    gEdRender.NumLamps = numLamps;
}

static bool InitializeOTDUGlowMaterial( OTDUGlowMaterial &glowMaterial,
                                        const Eigen::Vector4f &distanceScale ) {
    const uint32_t materialSize = Render2DM::CalcSize( 1, 0x10 );
    glowMaterial.pMemory = TestDrive::Alloc( materialSize );
    if ( glowMaterial.pMemory == nullptr ) {
        return false;
    }

    Material *pMaterial = Render2DM::Create( glowMaterial.pMemory,
                                             kGlowShaderGUID, 1, 1, 0x10, 0 );
    if ( pMaterial == nullptr ) {
        return false;
    }

    MaterialShaderParameterArray *pParameters =
        (MaterialShaderParameterArray *)pMaterial->getParameterByIndex( 0 );
    pParameters->Flags.NumTextures = 1;
    pParameters->Flags.NumUVMaps = 1;
    pParameters->Flags.bUsingNormals = 1;

    MaterialShaderParameter *pDistanceScale =
        pParameters->getParameterByIndex( 0 );
    pDistanceScale->Type = kGlowParameterType;
    pDistanceScale->bBindToVS = 1;
    memcpy( pParameters->getParameterDataByIndex( 0 ), distanceScale.data(),
            sizeof( float ) * 4 );

    MaterialLayer *pLayer = pParameters->getLayer( 0 );
    pLayer->pLayerTextures[0].TextureTypeHashcode =
        GetIdentifier64bit( "COLOR" );
    pLayer->pLayerTextures[0].Hashcode = GetIdentifier64bit( kGlowBitmapName );
    pLayer->pLayerTextures[0].SamplerAddress[0] = MTM_Clamp;
    pLayer->pLayerTextures[0].SamplerAddress[1] = MTM_Clamp;
    pLayer->pLayerTextures[0].MinMagFilter = TF_Linear;
    pLayer->pLayerTextures[0].MipFilter = TF_Linear;

    if ( !glowMaterial.render2DM.initialize( glowMaterial.pMemory ) ) {
        return false;
    }

    // Traffic light halo render states, set after initialize() resets them.
    pMaterial->OT = RB_FullAlpha;
    pMaterial->DepthTest = 1;
    pMaterial->DepthWrite = 2;
    pMaterial->BackfaceCullingMode = CM_Disabled;
    pMaterial->AlphaTest = CF_Never;
    pMaterial->AlphaRef = 0x28;
    pMaterial->SrcBlend = BM_SrcAlpha;
    pMaterial->DstBlend = BM_One;
    pMaterial->BlendOP = BO_Add;

    glowMaterial.pMaterial = pMaterial;
    return true;
}

static bool BindOTDUGlowBitmap( OTDUGlowMaterial &glowMaterial ) {
    if ( !glowMaterial.bBitmapBound ) {
        gpWorld->bindWorldBitmapReferences( &glowMaterial.render2DM );
        MaterialLayer *pLayer =
            glowMaterial.pMaterial->getParameterByIndex( 0 )->getLayer( 0 );
        glowMaterial.bBitmapBound =
            pLayer->pLayerTextures[0].pTextureSection != nullptr;
    }
    return glowMaterial.bBitmapBound;
}

static void ReleaseOTDUGlowMaterial( OTDUGlowMaterial &glowMaterial ) {
    glowMaterial.render2DM.destroy();
    if ( glowMaterial.pMemory != nullptr ) {
        TestDrive::Free( glowMaterial.pMemory );
        glowMaterial.pMemory = nullptr;
    }
    glowMaterial.pMaterial = nullptr;
}

static bool LoadOTDUShaftBitmap() {
    std::string path = gpConfig->getResRootPath();
    path += kShaftBitmapPath;
    FileMapEntry *pEntry = nullptr;
    FileMap *pMap = nullptr;
    if ( !gpFile->findMapEntriesForFile( path.c_str(), &pEntry, &pMap ) ||
         !gpFile->loadFile( path.c_str(), &gpLightGlows->pShaftBitmapResource,
                            nullptr ) ||
         !gpLightGlows->shaftBitmap.initialize(
             gpLightGlows->pShaftBitmapResource ) ) {
        return false;
    }
    Texture *pTexture = gpLightGlows->shaftBitmap.getFirstBitmap();
    return pTexture != nullptr && Render2DB::UploadTextureToGPU( pTexture );
}

static bool InitializeOTDULightGlows() {
    gpLightGlows = new OTDULightGlows();
    if ( !InitializeOTDUGlowMaterial( gpLightGlows->glow,
                                      kGlowDistanceScale ) ) {
        return false;
    }
    gpLightGlows->bShaftsEnabled = LoadOTDUShaftBitmap();
    for ( OTDUGlowMaterial &shaft : gpLightGlows->shafts ) {
        if ( !gpLightGlows->bShaftsEnabled ) {
            break;
        }
        if ( !InitializeOTDUGlowMaterial( shaft, kShaftDistanceScale ) ) {
            return false;
        }
        MaterialLayerTexture &texture =
            shaft.pMaterial->getParameterByIndex( 0 )
                ->getLayer( 0 )
                ->pLayerTextures[0];
        // AlphaTest 1 disables the test (0 = greater than AlphaRef, see
        // RenderContext::setAlphaTest): the faint haze must not be clipped.
        shaft.pMaterial->AlphaTest = static_cast<eComparisonFunction>( 1 );
        texture.Hashcode = kBoundTextureHashcode;
        texture.pTextureSection = gpLightGlows->shaftBitmap.getBitmapSection();
        shaft.bBitmapBound = true;
    }

    DrawStreams streams;
    streams.Normals = 1;
    streams.UVMap = 1;
    streams.bIndex16 = 1;
    return gpLightGlows->drawList.initialize(
        kMaxNumGlows + kMaxNumShafts * 2,
        kMaxNumGlows * 4 + kMaxNumShafts * 2 * kNumShaftVertices, 0, true,
        &streams, "OTDULightGlows", false );
}

// Object space with the camera on the -Z axis (the halo shader shrinks the
// sprite as the camera leaves it) and Y = world up (M11 scales the sprite).
static Matrix4x4f_t GetGlowTransform( const Eigen::Vector3f &position,
                                      const Eigen::Vector3f &camera ) {
    const Eigen::Vector3f up = Eigen::Vector3f::UnitY();
    const Eigen::Vector3f z = ( position - camera ).normalized();

    Eigen::Vector3f x = up.cross( z );
    x = x.squaredNorm() > 1e-6f ? x.normalized() : Eigen::Vector3f::UnitX();

    // Below a lamp Z gets parallel to up: tilt Y to keep the matrix invertible.
    const float tilt = Max( 0.0f, ( std::abs( z.y() ) - 0.9f ) * 10.0f );
    const Eigen::Vector3f y = up + x.cross( up ) * tilt;

    Matrix4x4f_t transform = Matrix4x4f_t::Identity();
    transform.row( 0 ) << x.x(), x.y(), x.z(), 0.0f;
    transform.row( 1 ) << y.x(), y.y(), y.z(), 0.0f;
    transform.row( 2 ) << z.x(), z.y(), z.z(), 0.0f;
    transform.row( 3 ) << position.x(), position.y(), position.z(), 1.0f;
    return transform;
}

static void PushGlow( DrawList &drawList,
                      const Eigen::Vector3f &position,
                      const Eigen::Vector3f &camera ) {
    static constexpr float kCorners[4][2] = {
        {-1.0f, 1.0f },
        {-1.0f, -1.0f},
        {1.0f,  1.0f },
        {1.0f,  -1.0f}
    };

    Matrix4x4f_t transform = GetGlowTransform( position, camera );
    drawList.setActiveTransformMatrix( transform );
    if ( !drawList.beginPrimitive( PT_TriangleStrip, 4 ) ) {
        return;
    }
    for ( const auto &corner : kCorners ) {
        drawList.setNormal( 0, Eigen::Vector3f( corner[0] * kGlowHalfSize,
                                                corner[1] * kGlowHalfSize,
                                                0.0f ) );
        drawList.setTexCoords( 0, Eigen::Vector2f( corner[0] * 0.5f + 0.5f,
                                                   0.5f - corner[1] * 0.5f ) );
        drawList.pushVertex( 0.0f, 0.0f, 0.0f );
    }
    drawList.commitPrimitive();
}

// One half of the shaft (side = -1 left, 1 right), center column to edge.
static void PushShaftHalf( DrawList &drawList,
                           const Eigen::Vector3f &position,
                           const Eigen::Vector3f &lateral,
                           float height,
                           float side ) {
    if ( !drawList.beginPrimitive( PT_TriangleStrip, kNumShaftVertices ) ) {
        return;
    }
    drawList.setNormal( 0, Eigen::Vector3f::Zero() );
    for ( uint32_t i = 0; i <= kNumShaftRows; i++ ) {
        const float depth =
            static_cast<float>( i ) / static_cast<float>( kNumShaftRows );
        const float halfWidth =
            kShaftTopHalfWidth + depth * height * kShaftSpread;
        const Eigen::Vector3f center =
            position - Eigen::Vector3f::UnitY() * ( depth * height );
        const Eigen::Vector3f edge = center + lateral * ( halfWidth * side );

        drawList.setTexCoords( 0, Eigen::Vector2f( 0.5f, depth ) );
        drawList.pushVertex( center.x(), center.y(), center.z() );
        drawList.setTexCoords( 0,
                               Eigen::Vector2f( 0.5f + 0.5f * side, depth ) );
        drawList.pushVertex( edge.x(), edge.y(), edge.z() );
    }
    drawList.commitPrimitive();
}

static void PushShaft( DrawList &drawList,
                       const Eigen::Vector3f &position,
                       float height,
                       const Eigen::Vector3f &lateral ) {
    drawList.setIdentityTransform();
    PushShaftHalf( drawList, position, lateral, height, -1.0f );
    PushShaftHalf( drawList, position, lateral, height, 1.0f );
}

void DrawOTDULightGlows() {
    const float intensity = Min( gEdRender.LampParams.x(), 1.0f );
    if ( gLightModels.empty() || intensity <= 0.0f || gbLightGlowsFailed ||
         gpWorld == nullptr || gpWorld->getStreamingManager() == nullptr ) {
        return;
    }

    if ( gpLightGlows == nullptr && !InitializeOTDULightGlows() ) {
        OTDU_LOG_WARN( "Failed to create the lamp glow resources\n" );
        ReleaseOTDULightGlows();
        gbLightGlowsFailed = true;
        return;
    }

    if ( !BindOTDUGlowBitmap( gpLightGlows->glow ) ) {
        return;
    }
    for ( uint32_t i = 0;
          gpLightGlows->bShaftsEnabled && i < kNumShaftFadeLevels; i++ ) {
        OTDUGlowMaterial &shaft = gpLightGlows->shafts[i];
        shaft.pMaterial->DiffuseColor.w() =
            intensity * kShaftAlpha * static_cast<float>( i + 1 ) /
            static_cast<float>( kNumShaftFadeLevels );
    }

    // The halo alpha is scaled by the light diffuse alpha (material * sun).
    gpLightGlows->glow.pMaterial->DiffuseColor.w() = intensity;

    const Eigen::Vector3f camera = GetNearCameraPosition();
    DrawList &drawList = gpLightGlows->drawList;
    drawList.reset();

    // Every lamp of the level once the scan is done, else the loaded sectors'
    // (the scan reads the same PMI data: same positions). Nearest first when
    // there are more than the draw list holds.
    std::vector<GlowCandidate> &candidates = gpLightGlows->candidates;
    candidates.clear();
    auto addCandidate = [&]( const Eigen::Vector3f &position, int32_t model ) {
        const float distance = ( position - camera ).norm();
        if ( distance <= kGlowQualities[gGlowQuality].MaxDistance ) {
            candidates.push_back( { distance, position, model } );
        }
        return true;
    };
    const Eigen::Vector4f streamOrigin = GetStreamOrigin();
    // Packed level data (file maps) is not scanned: no lights, keep the loaded
    // sectors'.
    if ( gpWorldLights != nullptr && gpWorldLights->bReady &&
         !gpWorldLights->lights.empty() ) {
        for ( const OTDUPlacedLight &light : gpWorldLights->lights ) {
            addCandidate( Eigen::Vector3f( light.x() - streamOrigin.x(),
                                           light.y() - streamOrigin.y(),
                                           light.z() - streamOrigin.z() ),
                          static_cast<int32_t>( light.w() ) );
        }
    } else {
        ForEachLoadedOTDULight( streamOrigin, addCandidate );
    }
    const uint32_t maxNumGlows = kGlowQualities[gGlowQuality].MaxNumGlows;
    if ( candidates.size() > maxNumGlows ) {
        std::nth_element( candidates.begin(), candidates.begin() + maxNumGlows,
                          candidates.end(),
                          []( const GlowCandidate &a, const GlowCandidate &b ) {
                              return a.Distance < b.Distance;
                          } );
        candidates.resize( maxNumGlows );
    }

    uint32_t numGlows = 0;
    uint32_t numShafts = 0;
    for ( const GlowCandidate &candidate : candidates ) {
        const Eigen::Vector3f &position = candidate.Position;
        const float distance = candidate.Distance;

        drawList.setActiveMaterial( gpLightGlows->glow.pMaterial );
        PushGlow( drawList, position, camera );
        numGlows++;

        if ( gpLightGlows->bShaftsEnabled && distance < kMaxShaftDistance &&
             numShafts < kMaxNumShafts ) {
            const Eigen::Vector3f toCamera = camera - position;
            const Eigen::Vector3f horizontal( toCamera.x(), 0.0f,
                                              toCamera.z() );
            const float horizontalDistance = horizontal.norm();
            const float nearFade =
                Clamp( ( horizontalDistance - kShaftMinHorizontalDistance ) /
                           ( kShaftFadeHorizontalDistance -
                             kShaftMinHorizontalDistance ),
                       0.0f, 1.0f );
            const float distanceFade =
                1.0f - Max( 0.0f, distance - kShaftFadeDistance ) /
                           ( kMaxShaftDistance - kShaftFadeDistance );
            const int32_t level = static_cast<int32_t>(
                std::round( nearFade * distanceFade *
                            static_cast<float>( kNumShaftFadeLevels ) ) );
            if ( level > 0 ) {
                const Eigen::Vector3f lateral =
                    Eigen::Vector3f::UnitY().cross( horizontal ) /
                    horizontalDistance;
                drawList.setActiveMaterial(
                    gpLightGlows->shafts[level - 1].pMaterial );
                PushShaft( drawList, position,
                           gLightModels[candidate.Model].Offset.y(), lateral );
                numShafts++;
            }
        }
    }

    if ( numGlows + numShafts != 0 ) {
        gpRender->getRenderPass<eRenderPass::RP_Near>()
            .pScene->enqueueDynamicDrawList( &drawList );
    }
}

void SetOTDUGlowQuality( eOTDUGlowQuality quality ) {
    gGlowQuality = eOTDUGlowQuality(
        Clamp( int32_t( quality ), 0, int32_t( GQ_Count ) - 1 ) );
}

eOTDUGlowQuality GetOTDUGlowQuality() { return gGlowQuality; }

void ReleaseOTDULightGlows() {
    StopWorldLightScan();
    if ( gpLightGlows == nullptr ) {
        return;
    }
    if ( gpLightGlows->drawList.isInitialized() ) {
        gpLightGlows->drawList.destroy();
    }
    ReleaseOTDUGlowMaterial( gpLightGlows->glow );
    for ( OTDUGlowMaterial &shaft : gpLightGlows->shafts ) {
        ReleaseOTDUGlowMaterial( shaft );
    }
    gpLightGlows->shaftBitmap.destroy();
    if ( gpLightGlows->pShaftBitmapResource != nullptr ) {
        TestDrive::Free( gpLightGlows->pShaftBitmapResource );
    }
    delete gpLightGlows;
    gpLightGlows = nullptr;
}

static OTDUHeadlightConfig gHeadlightConfig;
static bool gbHeadlightConfigured = false;
bool gbOTDUPlayerHighBeams = false;

void SetOTDUHeadlightConfig( const OTDUHeadlightConfig &config ) {
    gHeadlightConfig = config;
    gbHeadlightConfigured = true;
}

float GetOTDUTailLightLevel() { return gHeadlightConfig.TailLightLevel; }

bool AreOTDUHeadlightsOn() {
    return gbHeadlightConfigured && gEdRender.LampParams.x() > 0.0f;
}

// Render space center of an instance's LOD0 geometry (bounding sphere).
static bool GetInstanceCenter( Instance *pInstance,
                               Eigen::Vector3f &outCenter ) {
    if ( pInstance->getNumLODs() == 0 ||
         pInstance->getLOD( 0 ).pHashcode == nullptr ) {
        return false;
    }
    const uint32_t *pObject = reinterpret_cast<const uint32_t *>(
        (RenderFile::Section *)pInstance->getLOD( 0 ).pHashcode->pEntry );
    if ( pObject == nullptr || *pObject == kHeightmapMagic ) {
        return false;
    }
    const Eigen::Vector4f &sphere =
        reinterpret_cast<const RenderObject *>( pObject + 4 )->BoundingSphere;
    const Matrix4x4f_t &model = pInstance->getModelMatrix();
    const Eigen::Vector4f center = model.row( 0 ) * sphere.x() +
                                   model.row( 1 ) * sphere.y() +
                                   model.row( 2 ) * sphere.z() + model.row( 3 );
    outCenter = center.head<3>();
    return true;
}

void UpdateOTDUHeadlights() {
    gEdRender.NumHeadlights = 0;
    if ( !AreOTDUHeadlightsOn() || gpCarState == nullptr ) {
        return;
    }

    static constexpr uint32_t kMaxNumCars = edRender::kMaxNumHeadlights / 2;
    struct CarCandidate {
        float DistanceSquared;
        CarState *pCarState;
        Eigen::Vector3f Lights[2]; // render space
    };
    std::array<CarCandidate, kMaxNumVehicles> candidates;
    uint32_t numCandidates = 0;

    const Eigen::Vector3f camera = GetNearCameraPosition();
    CarState *pCarStates = gpCarState->getCarStates();
    for ( uint32_t i = 0; i < kMaxNumVehicles; i++ ) {
        if ( !gpCarState->isCarStateUsed( i ) ) {
            continue;
        }
        CarState &carState = pCarStates[i];
        VehicleBase *pVehicle = carState.getVehicleInstance();
        if ( pVehicle == nullptr || !pVehicle->isDisplayed() ||
             !carState.getBody()->bHeadlightsOn ) {
            continue;
        }

        CarCandidate candidate;
        bool bValid = true;
        for ( int32_t side = 0; side < 2 && bValid; side++ ) {
            const HiearchyNode *pNodes[VehicleBase::kNumHeadlightNodes];
            const int32_t numNodes =
                pVehicle->getHeadlightNodes( side, pNodes );
            // Several lamps on a side light as one, at their average.
            Eigen::Vector3f sum = Eigen::Vector3f::Zero();
            int32_t numCenters = 0;
            for ( int32_t n = 0; n < numNodes; n++ ) {
                Eigen::Vector3f center;
                if ( pNodes[n]->pInstance != nullptr &&
                     GetInstanceCenter( pNodes[n]->pInstance, center ) ) {
                    sum += center;
                    numCenters++;
                }
            }
            bValid = numCenters > 0;
            if ( bValid ) {
                candidate.Lights[side] = sum / static_cast<float>( numCenters );
            }
        }
        if ( !bValid ) {
            continue;
        }

        // The local player always gets its headlights.
        const Eigen::Vector3f center =
            ( candidate.Lights[0] + candidate.Lights[1] ) * 0.5f;
        candidate.DistanceSquared =
            ( carState.getOwner() == CarState::eOwner::CSO_LocalPlayer )
                ? -1.0f
                : ( center - camera ).squaredNorm();
        candidate.pCarState = &carState;
        candidates[numCandidates++] = candidate;
    }

    const uint32_t numCars = Min( numCandidates, kMaxNumCars );
    std::partial_sort( candidates.begin(), candidates.begin() + numCars,
                       candidates.begin() + numCandidates,
                       []( const CarCandidate &a, const CarCandidate &b ) {
                           return a.DistanceSquared < b.DistanceSquared;
                       } );

    const float cosOuter = std::cos( gHeadlightConfig.OuterAngle * kDegToRad );
    const float cosInner = std::cos( gHeadlightConfig.InnerAngle * kDegToRad );
    const float pitch = gHeadlightConfig.Pitch * kDegToRad;
    const Eigen::Vector4f streamOrigin = GetStreamOrigin();

    uint32_t numHeadlights = 0;
    for ( uint32_t c = 0; c < numCars; c++ ) {
        const CarCandidate &candidate = candidates[c];
        const Matrix4x4f_t &world = candidate.pCarState->getWorldMatrix();
        const Eigen::Vector3f carCenter( world( 3, 0 ) - streamOrigin.x(),
                                         world( 3, 1 ) - streamOrigin.y(),
                                         world( 3, 2 ) - streamOrigin.z() );
        Eigen::Vector3f forward = world.row( 2 ).head<3>().normalized();
        const Eigen::Vector3f up = world.row( 1 ).head<3>().normalized();
        const Eigen::Vector3f lightsCenter =
            ( candidate.Lights[0] + candidate.Lights[1] ) * 0.5f;
        if ( forward.dot( lightsCenter - carCenter ) < 0.0f ) {
            forward = -forward;
        }
        const Eigen::Vector3f direction =
            ( forward * std::cos( pitch ) - up * std::sin( pitch ) )
                .normalized();

        const bool bHighBeams = candidate.pCarState->getOwner() ==
                                    CarState::eOwner::CSO_LocalPlayer &&
                                gbOTDUPlayerHighBeams;
        const float range = bHighBeams ? gHeadlightConfig.HighBeamRange
                                       : gHeadlightConfig.LowBeamRange;
        for ( const Eigen::Vector3f &light : candidate.Lights ) {
            gEdRender.HeadlightPositions[numHeadlights] =
                Eigen::Vector4f( light.x(), light.y(), light.z(), range );
            gEdRender.HeadlightDirections[numHeadlights] = Eigen::Vector4f(
                direction.x(), direction.y(), direction.z(), cosOuter );
            gEdRender.HeadlightColors[numHeadlights] = Eigen::Vector4f(
                gHeadlightConfig.Color.x(), gHeadlightConfig.Color.y(),
                gHeadlightConfig.Color.z(), cosInner );
            numHeadlights++;
        }
    }
    gEdRender.NumHeadlights = numHeadlights;
    gEdRender.HeadlightIntensity =
        gHeadlightConfig.Intensity * Min( gEdRender.LampParams.x(), 1.0f );
}

// OTDU_ADDITION: c125 = (warm color, lit fraction), c126 = (cool color, cool
// ratio), c127 = (intensity, instance seed, -, -); see world/buildings.frag.
static constexpr uint32_t kWindowRegisterPS = 125;

static std::vector<OTDUWindowProfile> gWindowProfiles;
static std::vector<uint64_t> gWindowExcludedTextures;
static std::unordered_map<uint64_t, int32_t> gWindowProfileCache;

void RegisterOTDUWindowProfile( const OTDUWindowProfile &profile ) {
    for ( OTDUWindowProfile &existing : gWindowProfiles ) {
        if ( existing.Prefix == profile.Prefix ) {
            existing = profile;
            gWindowProfileCache.clear();
            return;
        }
    }
    gWindowProfiles.push_back( profile );
    gWindowProfileCache.clear();
}

void ExcludeOTDUWindowTexture( uint64_t textureHashcode ) {
    gWindowExcludedTextures.push_back( textureHashcode );
}

static float gWindowSpotRadius = 0.0f;
static constexpr float kWindowSpotCellSize = 256.0f;
static std::unordered_map<uint64_t, std::vector<Eigen::Vector2f>>
    gWindowSpotGrid;
static uint32_t gWindowSpotGridCount = 0;

void SetOTDUWindowSpotRadius( float radius ) {
    gWindowSpotRadius = Max( radius, 0.0f );
}

static uint64_t GetWindowSpotCellKey( int32_t x, int32_t z ) {
    return ( static_cast<uint64_t>( static_cast<uint32_t>( x ) ) << 32 ) |
           static_cast<uint32_t>( z );
}

// Spot buildings (houses, dealers, shops...) use the gloss slot for light
// maps and shiny trims, whichever bank holds their geometry.
static bool IsNearBuildingSpot( float x, float z ) {
    if ( gWindowSpotRadius <= 0.0f || gpScenaric == nullptr ) {
        return false;
    }
    Spots &spots = gpScenaric->getSpotList();
    const uint32_t numSpots = spots.getNumWorldSpots();
    if ( numSpots != gWindowSpotGridCount ) {
        gWindowSpotGrid.clear();
        gWindowSpotGridCount = numSpots;
        for ( uint32_t i = 0; i < numSpots; i++ ) {
            const SpotMap *pSpot =
                spots.getWorldSpot( static_cast<int32_t>( i ) );
            if ( pSpot == nullptr || pSpot->Type == eSpotType::ST_Challenge ) {
                continue;
            }
            const int32_t cellX = static_cast<int32_t>(
                std::floor( pSpot->X / kWindowSpotCellSize ) );
            const int32_t cellZ = static_cast<int32_t>(
                std::floor( pSpot->Z / kWindowSpotCellSize ) );
            gWindowSpotGrid[GetWindowSpotCellKey( cellX, cellZ )].emplace_back(
                pSpot->X, pSpot->Z );
        }
    }

    const float radiusSq = gWindowSpotRadius * gWindowSpotRadius;
    const int32_t cellX =
        static_cast<int32_t>( std::floor( x / kWindowSpotCellSize ) );
    const int32_t cellZ =
        static_cast<int32_t>( std::floor( z / kWindowSpotCellSize ) );
    const int32_t numCells = static_cast<int32_t>(
        std::ceil( gWindowSpotRadius / kWindowSpotCellSize ) );
    for ( int32_t dz = -numCells; dz <= numCells; dz++ ) {
        for ( int32_t dx = -numCells; dx <= numCells; dx++ ) {
            auto it = gWindowSpotGrid.find(
                GetWindowSpotCellKey( cellX + dx, cellZ + dz ) );
            if ( it == gWindowSpotGrid.end() ) {
                continue;
            }
            for ( const Eigen::Vector2f &spot : it->second ) {
                if ( ( spot - Eigen::Vector2f( x, z ) ).squaredNorm() <=
                     radiusSq ) {
                    return true;
                }
            }
        }
    }
    return false;
}

static bool IsInLoadedSpot( const void *pAddress ) {
    return gpWorld != nullptr && gpWorld->getStreamingManager() != nullptr &&
           gpWorld->getStreamingManager()->spotStream.isInLoadedSpot(
               pAddress );
}

// The gloss map is the third texture of the Buildings materials.
static bool IsWindowTextureExcluded( const Material *pMaterial ) {
    if ( pMaterial == nullptr || gWindowExcludedTextures.empty() ) {
        return false;
    }
    MaterialParameter *pParameter =
        const_cast<Material *>( pMaterial )->getParameterByIndex( 0 );
    if ( pParameter == nullptr ) {
        return false;
    }
    MaterialLayer *pLayer = pParameter->getLayer( 0 );
    if ( pLayer == nullptr || pLayer->NumTextures < 3 ) {
        return false;
    }
    const uint64_t hashcode = pLayer->pLayerTextures[2].Hashcode;
    return std::find( gWindowExcludedTextures.begin(),
                      gWindowExcludedTextures.end(),
                      hashcode ) != gWindowExcludedTextures.end();
}

// Model hashcodes of names up to 8 characters are the packed name.
static int32_t FindWindowProfile( uint64_t hashcode ) {
    auto it = gWindowProfileCache.find( hashcode );
    if ( it != gWindowProfileCache.end() ) {
        return it->second;
    }

    // "0x..." profiles match one hashcode (models named longer than 8
    // characters, as the object picker shows them).
    for ( size_t i = 0; i < gWindowProfiles.size(); i++ ) {
        const std::string &prefix = gWindowProfiles[i].Prefix;
        if ( prefix.rfind( "0x", 0 ) == 0 &&
             strtoull( prefix.c_str(), nullptr, 16 ) == hashcode ) {
            gWindowProfileCache[hashcode] = static_cast<int32_t>( i );
            return static_cast<int32_t>( i );
        }
    }

    char name[9] = {};
    memcpy( name, &hashcode, sizeof( hashcode ) );
    int32_t best = -1;
    size_t bestLength = 0;
    for ( size_t i = 0; i < gWindowProfiles.size(); i++ ) {
        const std::string &prefix = gWindowProfiles[i].Prefix;
        const bool bMatch = prefix.empty() || ( prefix.rfind( "0x", 0 ) != 0 &&
                                                strncmp( name, prefix.c_str(),
                                                         prefix.size() ) == 0 );
        if ( bMatch && ( best < 0 || prefix.size() > bestLength ) ) {
            best = static_cast<int32_t>( i );
            bestLength = prefix.size();
        }
    }
    gWindowProfileCache[hashcode] = best;
    return best;
}

void BindOTDUWindowConstants( const Instance *pInstance ) {
    std::array<Eigen::Vector4f, 3> constants = { Eigen::Vector4f::Zero(),
                                                 Eigen::Vector4f::Zero(),
                                                 Eigen::Vector4f::Zero() };
    const float lamps = Min( gEdRender.LampParams.x(), 1.0f );
    int32_t profileIndex = ( pInstance != nullptr && lamps > 0.0f )
                               ? FindWindowProfile( pInstance->getHashcode() )
                               : -1;
    if ( profileIndex >= 0 &&
         ( gWindowProfiles[profileIndex].Intensity <= 0.0f ||
           IsInLoadedSpot( gSceneRenderer.pActiveMaterial ) ||
           IsWindowTextureExcluded( gSceneRenderer.pActiveMaterial ) ) ) {
        profileIndex = -1;
    }
    Eigen::Vector4f position = Eigen::Vector4f::Zero();
    if ( profileIndex >= 0 ) {
        position = pInstance->getModelMatrix().row( 3 ).transpose() +
                   GetStreamOrigin();
        if ( IsNearBuildingSpot( position.x(), position.z() ) ) {
            profileIndex = -1;
        }
    }
    if ( profileIndex >= 0 ) {
        const OTDUWindowProfile &profile = gWindowProfiles[profileIndex];
        // Instances of a model share object space: the seed comes from the
        // absolute instance position.
        const uint32_t cellX = static_cast<uint32_t>(
            static_cast<int32_t>( std::floor( position.x() ) ) );
        const uint32_t cellZ = static_cast<uint32_t>(
            static_cast<int32_t>( std::floor( position.z() ) ) );
        const float seed = static_cast<float>(
            ( ( cellX * 73856093u ) ^ ( cellZ * 19349663u ) ) % 4096u );
        constants[0] =
            Eigen::Vector4f( profile.WarmColor.x(), profile.WarmColor.y(),
                             profile.WarmColor.z(), profile.LitFraction );
        constants[1] =
            Eigen::Vector4f( profile.CoolColor.x(), profile.CoolColor.y(),
                             profile.CoolColor.z(), profile.CoolRatio );
        constants[2] =
            Eigen::Vector4f( profile.Intensity * lamps, seed, 0.0f, 0.0f );
    }
    // Off by day: no upload while the registers already hold zeros.
    static bool bUploadedOff = false;
    if ( profileIndex < 0 && bUploadedOff ) {
        return;
    }
    bUploadedOff = profileIndex < 0;
    gEdRender.pRenderDevice->setFloatConstants(
        eShaderType::ST_Pixel, constants[0].data(), kWindowRegisterPS, 3 );
}

// OTDU_ADDITION: c250 = (emissive scale, -, -, -), see lighting/lighting.vert.
static constexpr uint32_t kEmissiveRegisterVS = 250;

static std::vector<uint64_t> gEmissiveTextures;
static bool gbEmissiveUploaded = false;

void RegisterOTDUEmissiveTexture( uint64_t textureHashcode ) {
    gEmissiveTextures.push_back( textureHashcode );
}

static bool IsEmissiveMaterial( const Material *pMaterial ) {
    if ( pMaterial == nullptr ) {
        return false;
    }
    const Eigen::Vector4f &emissive = pMaterial->EmissiveColor;
    if ( Max( emissive.x(), Max( emissive.y(), emissive.z() ) ) > 0.0f ) {
        return true;
    }
    if ( gEmissiveTextures.empty() ) {
        return false;
    }
    MaterialParameter *pParameter =
        const_cast<Material *>( pMaterial )->getParameterByIndex( 0 );
    if ( pParameter == nullptr ) {
        return false;
    }
    MaterialLayer *pLayer = pParameter->getLayer( 0 );
    if ( pLayer == nullptr || pLayer->NumTextures < 1 ) {
        return false;
    }
    const uint64_t hashcode = pLayer->pLayerTextures[0].Hashcode;
    return std::find( gEmissiveTextures.begin(), gEmissiveTextures.end(),
                      hashcode ) != gEmissiveTextures.end();
}

void BindOTDUEmissiveConstants() {
    const float scale = gEdRender.LampParams.z();
    if ( scale <= 0.0f ||
         !IsEmissiveMaterial( gSceneRenderer.pActiveMaterial ) ) {
        return;
    }
    Eigen::Vector4f constants( scale, 0.0f, 0.0f, 0.0f );
    gEdRender.pRenderDevice->setFloatConstants(
        eShaderType::ST_Vertex, constants.data(), kEmissiveRegisterVS, 1 );
    gbEmissiveUploaded = true;
}

void UnbindOTDUEmissiveConstants() {
    if ( !gbEmissiveUploaded ) {
        return;
    }
    Eigen::Vector4f constants = Eigen::Vector4f::Zero();
    gEdRender.pRenderDevice->setFloatConstants(
        eShaderType::ST_Vertex, constants.data(), kEmissiveRegisterVS, 1 );
    gbEmissiveUploaded = false;
}
