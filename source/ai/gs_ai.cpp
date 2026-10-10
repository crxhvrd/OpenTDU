#include "shared.h"
#include "gs_ai.h"

#include "ai/ai_pool.h"
#include "ai/roadmap_upgrader.h"
#include "game/game_object.h"
#include "physics/gs_physics.h"
#include "filesystem/bank.h"

AIConfig gAIConfig = {}; // DAT_0143f720
GSAI *gpAI = nullptr;

bool gbDisablePathOpt = false;        // DAT_010e73d2
bool gbDisableObstacleParser = false; // DAT_010e73d3

static constexpr int32_t kNumPathNodes = 0xc00;
static constexpr int32_t kNumRouteElems = 0x400;

static AiPool gAiPool; // DAT_01451070

GSAI gGSAI; // DAT_013a70d0

GSAI::GSAI()
    : GameSystem(),
      roadBuckets(),
      pathPool(),
      pathSolver(),
      pAIPool( &gAiPool ),
      roadMap(),
      bRoadMapLoaded( false ),
      pRoadMapStream( nullptr ),
      pRoadDetailsStream( nullptr ),
      pUpgradedRoadMap( nullptr ),
      pUpgradedRoadDetails( nullptr ),
      bDisableCops( true ),
      bDisableBots( true ),
      bPauseTraffic( false ),
      bCopsActive( false ),
      bBotsActive( false ),
      bCullFromCamera( false ),
      bLocationsFromCarState( false ) {
    OTDU_ASSERT( gpAI == nullptr );
    gpAI = this;
}

GSAI::~GSAI() {
    if ( pUpgradedRoadMap != nullptr ) {
        std::free( pUpgradedRoadMap );
    }
    if ( pUpgradedRoadDetails != nullptr ) {
        std::free( pUpgradedRoadDetails );
    }
    gpAI = nullptr;
}

bool GSAI::initialize( TestDriveGameInstance * ) {
    // FUN_00969900
    OTDU_UNIMPLEMENTED_ONCE; // TODO: traffic, cops, bots, road buckets
    pathPool.initialize( kNumPathNodes, kNumRouteElems );
    roadBuckets.initialize( &roadMap, &gpPhysics->getRoadPhantomManager() );
    pathSolver.initialize( &pathPool );
    return true;
}

void GSAI::tick( float, float deltaTime ) {
    // FUN_00969e70
    static constexpr float kMaxLocationDeltaTime = 0.06f; // DAT_00f41e08

    OTDU_UNIMPLEMENTED_ONCE; // TODO: traffic, cops, bots, AI vehicles
    if ( bRoadMapLoaded && !bLocationsFromCarState ) {
        gGameObjectDico.updateLocations(
            std::min( deltaTime, kMaxLocationDeltaTime ), false );
    }
}

void GSAI::terminate() { roadBuckets.terminate(); }

void GSAI::loadRoadMap( Bank &levelBank ) {
    // FUN_00969060
    if ( bRoadMapLoaded ) {
        return;
    }

    uint32_t roadMapSize = 0;
    uint32_t roadDetailsSize = 0;
    pRoadMapStream = levelBank.getFirstEntry(
        AG_AIRoadMap, static_cast<eAssetType>( 0 ), &roadMapSize );
    pRoadDetailsStream = levelBank.getFirstEntry(
        AG_AIRoadMap, static_cast<eAssetType>( 1 ), &roadDetailsSize );
    if ( pRoadMapStream == nullptr || pRoadDetailsStream == nullptr ) {
        return;
    }

#if OTDU_64_BITS
    size_t upgradedRoadMapSize = 0;
    size_t upgradedRoadDetailsSize = 0;
    pUpgradedRoadMap = UpgradeRoadMapStream( pRoadMapStream, roadMapSize,
                                             &upgradedRoadMapSize );
    pUpgradedRoadDetails = UpgradeRoadDetailsStream(
        pRoadDetailsStream, roadDetailsSize, &upgradedRoadDetailsSize );
    if ( pUpgradedRoadMap == nullptr || pUpgradedRoadDetails == nullptr ) {
        OTDU_LOG_ERROR( "Failed to upgrade the AI roadmap!\n" );
        return;
    }
    pRoadMapStream = pUpgradedRoadMap;
    pRoadDetailsStream = pUpgradedRoadDetails;
    roadMapSize = static_cast<uint32_t>( upgradedRoadMapSize );
    roadDetailsSize = static_cast<uint32_t>( upgradedRoadDetailsSize );
#endif

    roadMap.reset();
    roadMap.load( pRoadMapStream, roadMapSize );
    roadMap.installDetails( pRoadDetailsStream, roadDetailsSize );
    bRoadMapLoaded = true;
    roadBuckets.build();

    OTDU_LOG_INFO( "AI roadmap loaded (%d roads)\n", roadMap.getNumRoads() );
}

void GSAI::setTrafficCategories( const bool bResetPool,
                                 const uint32_t categoryMask ) {
    // FUN_00969b50 / FUN_016adb51
    static constexpr uint32_t kCategoryOpponents = 1;
    static constexpr uint32_t kCategoryParked = 2;
    static constexpr uint32_t kCategoryCops = 4;
    static constexpr uint32_t kCategoryBots = 8;

    pAIPool->setField0x7C( false );
    if ( categoryMask == 0u ) {
        pAIPool->removeAllVehicles( bResetPool );
    } else {
        if ( ( categoryMask & kCategoryCops ) == 0u ) {
            bCopsActive = false;
            pAIPool->removeCops();
            bPauseTraffic = true;
            pAIPool->setField0x85( false );
        }
        if ( ( categoryMask & kCategoryBots ) == 0u ) {
            bBotsActive = false;
            pAIPool->removeBots();
            bPauseTraffic = true;
        }
        if ( ( categoryMask & kCategoryParked ) == 0u ) {
            pAIPool->removeParkedVehicles();
        }
        if ( ( categoryMask & kCategoryOpponents ) == 0u ) {
            pAIPool->removeOpponents();
        }
        pAIPool->removeFlaggedVehicles();
    }
    pAIPool->setField0xC( true );
}
