#include "shared.h"
#include "tdu_detail.h"

#include "config/gs_config.h"

#include "render/camera/camera_game.h"
#include "render/camera/gs_camera.h"

#include "world/gs_world.h"
#include "world/sector_manager.h"
#include "world/streaming_manager.h"

#include <string>

static constexpr float kNearHeadingRangeX = 1.9f;         // DAT_00f41e1c
static constexpr float kNearHeadingBiasX = 0.95f;         // DAT_00f4195c
static constexpr float kNearHeadingRangeZ = 0.6f;         // DAT_00f416c8
static constexpr float kNearHeadingBiasZ = 0.8f;          // DAT_00f416d0
static constexpr float kFarHeadingRangeX = 0.6f;          // DAT_00f416c8
static constexpr float kFarHeadingBiasX = 0.3f;           // FLOAT_00f416bc
static constexpr float kFarHeadingRangeZ = 0.9f;          // DAT_00f416a0
static constexpr float kFarHeadingBiasZ = 0.1f;           // FLOAT_00f41668
static constexpr float kPlacementAngleMin = 0.7853982f;   // DAT_00f41cb0
static constexpr float kPlacementAngleMax = 2.3561945f;   // DAT_00f41e14
static constexpr float kPlacementAngleRange = 1.5707964f; // DAT_00f41e18

static constexpr float kDetailGroundMissSentinel = 100000.0f;

TDUDetail::TDUDetail()
    : bank(),
      collection2D(),
      pLoaded2DB( nullptr ),
      num2DB( 0 ),
      pLoaded2DM( nullptr ),
      num2DM( 0 ),
      bInitialized( false ),
      bEnabled( true ),
      pName( "Unknow" ),
      hash( 0x6e6f6e65 ) {
    // FUN_008a7f10
}

TDUDetail::~TDUDetail() {
    // FUN_008a7b80
}

void TDUDetail::loadBank( const char *pBankName ) {
    // FUN_008a8020
    num2DB = 0;
    pLoaded2DB = nullptr;
    num2DM = 0;
    pLoaded2DM = nullptr;

    std::string path = gpConfig->getResRootPath();
    path += "Details/";
    path += pBankName;

    bank.loadBootBank( path.c_str(), nullptr );
}

bool TDUDetail::load2DResources( Render2DB *p2DBArray,
                                 Render2DM *p2DMArray,
                                 int32_t num2DMToLoad,
                                 int32_t num2DBToLoad ) {
    // FUN_008a7d70
    if ( !bank.isLoaded() ) {
        return false;
    }

    if ( num2DBToLoad > 0 ) {
        pLoaded2DB = p2DBArray;
        uint32_t resSize = 0;
        void *pEntry = bank.getFirstEntry( AG_Detail, AT_2DB, &resSize );
        while ( num2DB < num2DBToLoad && pEntry != nullptr ) {
            if ( pLoaded2DB[num2DB].initialize( pEntry ) ) {
                collection2D.register2DB( &pLoaded2DB[num2DB] );
                num2DB++;
            }
            pEntry = bank.getNextEntry( AG_Detail, AT_2DB, &resSize );
        }
    }

    if ( num2DMToLoad > 0 ) {
        pLoaded2DM = p2DMArray;
        uint32_t resSize = 0;
        void *pEntry = bank.getFirstEntry( AG_Detail, AT_2DM, &resSize );
        while ( num2DM < num2DMToLoad && pEntry != nullptr ) {
            if ( pLoaded2DM[num2DM].initialize( pEntry ) ) {
                collection2D.register2DM( &pLoaded2DM[num2DM] );
                num2DM++;
            }
            pEntry = bank.getNextEntry( AG_Detail, AT_2DM, &resSize );
        }
        // Resolve the freshly loaded materials' bitmap references against the
        // detail's own 2DBs
        for ( int32_t i = 0; i < num2DM; i++ ) {
            collection2D.bindBitmapReferences( &pLoaded2DM[i] );
        }
    }

    return true;
}

void TDUDetail::unload2DResources() {
    // FUN_008a7b30
    for ( int32_t i = 0; i < num2DM; i++ ) {
        collection2D.unregister2DM( &pLoaded2DM[i] );
        pLoaded2DM[i].destroy();
    }
    for ( int32_t i = 0; i < num2DB; i++ ) {
        collection2D.unregister2DB( &pLoaded2DB[i] );
        pLoaded2DB[i].destroy();
    }
    num2DM = 0;
    pLoaded2DM = nullptr;
    num2DB = 0;
    pLoaded2DB = nullptr;
}

void ComputeDetailSpawnPosition( float minDistance,
                                 float maxDistance,
                                 float mediumDistance,
                                 Eigen::Vector4f &outPosition,
                                 Eigen::Vector4f &outDirection ) {
    // FUN_008a7700
    outPosition = Eigen::Vector4f::Zero();
    outDirection = Eigen::Vector4f( 1.0f, 0.0f, 0.0f, 0.0f );
    if ( gpCamera == nullptr ) {
        return;
    }

    const Matrix4x4f_t &camToWorld =
        gpCamera->getDefaultCamera()->getCamToWorld();
    const Eigen::Vector4f cameraPosition = camToWorld.row( 3 ).transpose();

    const float distance =
        DetailRandom() * ( maxDistance - minDistance ) + minDistance;
    const bool bNearSpawn = distance < mediumDistance;

    Eigen::Vector4f headingSeed = Eigen::Vector4f::Zero();
    if ( bNearSpawn ) {
        headingSeed.x() =
            DetailRandom() * kNearHeadingRangeX - kNearHeadingBiasX;
        headingSeed.z() =
            DetailRandom() * kNearHeadingRangeZ - kNearHeadingBiasZ;
    } else {
        headingSeed.x() = DetailRandom() * kFarHeadingRangeX - kFarHeadingBiasX;
        headingSeed.z() = DetailRandom() * kFarHeadingRangeZ + kFarHeadingBiasZ;
    }

    Eigen::Vector4f heading = RotateByMatrix( camToWorld, headingSeed );
    heading.y() = 0.0f;
    outDirection = heading / heading.norm();

    float placementAngle;
    if ( bNearSpawn ) {
        placementAngle = ( headingSeed.x() >= 0.0f ) ? kPlacementAngleMin
                                                     : kPlacementAngleMax;
    } else {
        placementAngle =
            DetailRandom() * kPlacementAngleRange + kPlacementAngleMin;
    }

    const Eigen::Vector4f placement( std::cos( placementAngle ), 0.0f,
                                     std::sin( placementAngle ), 0.0f );
    outPosition =
        cameraPosition - RotateByMatrix( camToWorld, placement ) * distance;
}

bool SampleDetailGroundHeight( const Eigen::Vector4f &worldPosition,
                               float &outHeight ) {
    // FUN_007cb910
    outHeight = 0.0f;
    if ( gpWorld == nullptr ) {
        return false;
    }
    StreamingManager *pStreaming = gpWorld->getStreamingManager();
    if ( pStreaming == nullptr ) {
        return false;
    }
    Sector *pSector =
        pStreaming->sectorManager.getSectorAtPosition( worldPosition );
    if ( pSector == nullptr || pSector->state != 1 ||
         pSector->geometry.pHeightmapInstance == nullptr ) {
        return false;
    }
    const float height = SampleGroundHeight(
        pSector->geometry.pHeightmapInstance, pSector->heightmap,
        worldPosition.x(), worldPosition.z() );
    if ( height >= kDetailGroundMissSentinel ) {
        return false;
    }
    outHeight = height;
    return true;
}
