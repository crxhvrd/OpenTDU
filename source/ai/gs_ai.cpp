#include "shared.h"
#include "gs_ai.h"

#include "ai/ai_pool.h"
#include "ai/ai_road_query.h"
#include "ai/roadmap_upgrader.h"
#include "config/gs_config.h"
#include "database/gs_database.h"
#include "filesystem/gs_file.h"
#include "game/game_object.h"
#include "game/scenaric/gs_scenaric.h"
#include "game/vehicle/car_state.h"
#include "physics/gs_physics.h"
#include "filesystem/bank.h"

GSAI *gpAI = nullptr;

std::vector<std::string> gForcedCarModels; // DAT_010c4018

bool gbDisablePathOpt = false;        // DAT_010e73d2
bool gbDisableObstacleParser = false; // DAT_010e73d3

static constexpr int32_t kNumPathNodes = 0xc00;
static constexpr int32_t kNumRouteElems = 0x400;

static AiPool gAiPool; // DAT_01451070
AiLODSorter gAiLODSorter;

GSAI gGSAI; // DAT_013a70d0

GSAI::GSAI()
    : GameSystem(),
      roadBuckets(),
      pathPool(),
      pathSolver(),
      pAIPool( &gAiPool ),
      pLODSorter( &gAiLODSorter ),
      pSimBubble( &gAiSimBubble ),
      roadMap(),
      pedestrianManager(),
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
    loadAIConfig();
    OTDU_UNIMPLEMENTED_ONCE; // TODO: AIBotPers.xml
    pAIPool->initialize();
    pathPool.initialize( kNumPathNodes, kNumRouteElems );
    roadBuckets.initialize( &roadMap, &gpPhysics->getRoadPhantomManager() );
    OTDU_UNIMPLEMENTED_ONCE; // TODO: debug render list, AiTrainer
    pathSolver.initialize( &pathPool );
    OTDU_UNIMPLEMENTED_ONCE; // TODO: second path solver
    pSimBubble->initialize();
    pedestrianManager.initialize();
    OTDU_UNIMPLEMENTED_ONCE; // TODO: distribution zones, net interpolation,
                             // MngFontPlayers lists, AiCopMng, bot manager,
                             // path optimiser
    return true;
}

void GSAI::tick( float totalTime, float deltaTime ) {
    // FUN_00969e70
    static constexpr float kMaxDeltaTime = 0.06f; // DAT_00f41e08

    const float dt = std::min( deltaTime, kMaxDeltaTime );
    if ( !bRoadMapLoaded || bLocationsFromCarState ) {
        return;
    }

    OTDU_UNIMPLEMENTED_ONCE; // TODO: Online
    pAIPool->tick( dt, totalTime );
    OTDU_UNIMPLEMENTED_ONCE; // TODO: debug overlay (FUN_00975a10), AiTrainer
    gGameObjectDico.updateLocations( dt, false );
    pSimBubble->tick( dt, totalTime );
    OTDU_UNIMPLEMENTED_ONCE; // TODO: online zones, net interpolation, cops,
                             // bots, path optimiser
}

void GSAI::terminate() {
    pedestrianManager.terminate();
    roadBuckets.terminate();
}

void GSAI::reset() {
    // FUN_00969c10
    setTrafficCategories( true, 0u );
    pAIPool->setTrafficPaused( true );
    OTDU_UNIMPLEMENTED_ONCE; // TODO: AiCopMng reset
}

void GSAI::pause() {
    // FUN_00968ea0
    pAIPool->setTrafficPaused( true );
}

void GSAI::loadAIConfig() {
    // FUN_009696f0
    char path[256];
    snprintf( path, sizeof( path ), "%sDataBase/AIConfig.xml",
              gpConfig->getResRootPath() );

    void *pContent = nullptr;
    uint32_t contentSize = 0;
    if ( gpFile->loadFile( path, &pContent, &contentSize ) ) {
        uint32_t remainingBytes = 0;
        Bank::DecryptTEA( pContent, contentSize, &remainingBytes );
        gAIConfig.parse( static_cast<const char *>( pContent ) );
        OTDU_LOG_INFO( "AI config loaded (%d zones, %d vehicle physics)\n",
                       gAIConfig.NumZones, gAIConfig.NumVehiclePhysics );
    }
    if ( pContent != nullptr ) {
        TestDrive::Free( pContent );
    }

    if ( gForcedCarModels.empty() ) {
        return;
    }

    for ( AIZone &zone : gAIConfig.Zones ) {
        for ( AIZoneVehicle &vehicle : zone.Vehicles ) {
            vehicle.Frequency = 0.0f;
        }
    }

    for ( const std::string &forcedModel : gForcedCarModels ) {
        if ( forcedModel.empty() ) {
            continue;
        }

        // The last character of the model name is the version
        const std::string model =
            forcedModel.substr( 0, forcedModel.size() - 1 );
        const std::string version =
            forcedModel.substr( forcedModel.size() - 1 );
        const int32_t vehicleId = gpDatabase->getVehicleId(
            "TRAFFIC", model.c_str(), version.c_str(), nullptr );
        for ( AIZone &zone : gAIConfig.Zones ) {
            for ( AIZoneVehicle &vehicle : zone.Vehicles ) {
                if ( vehicle.DatabaseId == vehicleId ) {
                    vehicle.Frequency = 1.0f;
                }
            }
        }
    }
}

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
        pAIPool->removeTraffic();
    }
    pAIPool->setTrafficPaused( true );
}

bool GSAI::spawnDebugTraffic() {
    static constexpr float kSpawnAheadDistance = 80.0f;

    CarState *pLocalCar = gpScenaric->getLocalPlayer().getCarState();
    if ( !bRoadMapLoaded || pLocalCar == nullptr ) {
        return false;
    }

    const Matrix4x4f_t &carMatrix = pLocalCar->getWorldMatrix();
    const Eigen::Vector4f position =
        carMatrix.row( 3 ).transpose() +
        carMatrix.row( 2 ).transpose() * kSpawnAheadDistance;

    AiRoadLocation location;
    if ( !ProjectOnRoad( position, nullptr, nullptr, false, &location ) ||
         location.It.pRoad == nullptr ) {
        OTDU_LOG_WARN( "No road to spawn a traffic car on\n" );
        return false;
    }
    return pAIPool->spawnTrafficOnRoad( location.It, false );
}

void GSAI::drawDebug( float deltaTime ) {
    (void)deltaTime;

    AiPoolTraffic &traffic = pAIPool->getTraffic();
    ImGui::SeparatorText( "Traffic" );
    ImGui::Text(
        "Zone %d | templates %d loaded, %d loading, %d total",
        pSimBubble->getCurrentZoneIndex(), traffic.getNumLoadedTemplates(),
        traffic.getNumLoadingTemplates(), traffic.getNumTotalTemplates() );
    ImGui::Text(
        "AI vehicles %d | spawn request state %d", pAIPool->getVehicles().Count,
        static_cast<int32_t>( pAIPool->getSpawnRequest().getState() ) );

    if ( ImGui::Button( "Spawn traffic car ahead" ) ) {
        spawnDebugTraffic();
    }
    ImGui::SameLine();
    if ( ImGui::Button( "Remove traffic" ) ) {
        pAIPool->removeTraffic();
    }

    if ( ImGui::TreeNode( "Templates" ) ) {
        ImGui::Text( "#   ID  L C #I #USE #TOT RATE" );
        for ( int32_t i = 0; i < traffic.getNumTotalTemplates(); i++ ) {
            const AiPoolTraffic::Template &entry = traffic.getTemplateAt( i );
            ImGui::Text( "%02d %4d %c %c %2d %4d %4d %0.2f", i, entry.IDCar,
                         entry.isLoaded() ? 'X' : ' ',
                         entry.bMarkedForChange ? 'X' : ' ',
                         entry.pResource ? entry.pResource->RefCount : 0,
                         entry.NumUsed, entry.NumTotalUsed, entry.Frequency );
        }
        ImGui::TreePop();
    }

    if ( ImGui::TreeNode( "Vehicles" ) ) {
        for ( AiVehicle *pVehicle : pAIPool->getVehicles() ) {
            CarState *pCarState = pVehicle->getCarState();
            const Eigen::Vector4f position =
                pVehicle->getInfos().GroundMatrix.row( 3 );
            ImGui::Text( "slot %d car %d cat %d | %.1f %.1f %.1f | %s",
                         pVehicle->getCarStateIndex(),
                         pCarState ? pCarState->getCarID() : -1,
                         static_cast<int32_t>( pVehicle->getCategory() ),
                         position.x(), position.y(), position.z(),
                         pVehicle->isPhysicsDriven() ? "physics"
                                                     : "kinematic" );
        }
        ImGui::TreePop();
    }
}
