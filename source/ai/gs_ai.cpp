#include "shared.h"
#include "gs_ai.h"

#include "ai/ai_debug_draw.h"
#include "ai/ai_debug_race.h"
#include "ai/ai_path_optimiser.h"
#include "ai/ai_pool.h"
#include "ai/ai_veh_locomotion.h"
#include "ai/ai_vehicle.h"
#include "audio/vehicle/gs_vehicle_sound.h"
#include "game/scenaric/entity/entity_driver.h"
#include "game/vehicle/vehicle_base.h"
#include "ai/ai_road_query.h"
#include "ai/roadmap_upgrader.h"
#include "config/gs_config.h"
#include "core/random/bcpl.h"
#include "database/gs_database.h"
#include "filesystem/gs_file.h"
#include "game/game_object.h"
#include "game/scenaric/game_intro_tutorial.h"
#include "game/scenaric/gs_scenaric.h"
#include "game/vehicle/car_state.h"
#include "gs_timer.h"
#include "physics/gs_physics.h"
#include "filesystem/bank.h"

GSAI *gpAI = nullptr;

std::vector<std::string> gForcedCarModels; // DAT_010c4018

bool gbDisablePathOpt = false;        // DAT_010e73d2
bool gbDisableObstacleParser = false; // DAT_010e73d3
bool gbForceOptimWhenPaused = false;  // DAT_010e73c5

static constexpr int32_t kNumPathNodes = 0xc00;
static constexpr int32_t kNumRouteElems = 0x400;

static constexpr int32_t kSignalPhaseDurationMs = 20000; // DAT_00faa99c
static constexpr int32_t kSignalOrangeDurationMs = 5000; // DAT_00faa998

static float gSignalSyncTime = -1.0f; // DAT_00fa9094

static AiPool gAiPool;                         // DAT_01451070
static AiBotPersonalities gAiBotPersonalities; // DAT_014775a0
static RandomGen gAiRandom;                    // DAT_00fa9120
AiLODSorter gAiLODSorter;
static AiCullingShape gAiCullingShape = { 100.0f,     250.0f,   200.0f,
                                          kPI / 4.0f, 10000.0f, 62500.0f,
                                          40000.0f }; // DAT_00fa90a0

GSAI gGSAI; // DAT_013a70d0

GSAI::GSAI()
    : GameSystem(),
      roadBuckets(),
      pathPool(),
      pathSolver(),
      floodSolver(),
      pAIPool( &gAiPool ),
      pLODSorter( &gAiLODSorter ),
      pBotPersonalities( &gAiBotPersonalities ),
      pSimBubble( &gAiSimBubble ),
      pRandom( &gAiRandom ),
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
      pBotManager( &gAiBotManager ),
      bLocationsFromCarState( false ),
      pSignalSyncTime( &gSignalSyncTime ),
      pRace( nullptr ),
      gameSession( this ),
      pPathOptimiser( nullptr ),
      pCullingShape( &gAiCullingShape ),
      pDebugDraw( &gAiDebugDraw ) {
    OTDU_ASSERT( gpAI == nullptr );
    gpAI = this;
    gAiRandom.setSeed( 0x3039 );
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

uint32_t GetTrafficSignalState( int32_t synchro, float *pSyncTime ) {
    // FUN_0092c470
    const AiRoadMap &roadMap = gGSAI.getRoadMap();
    if ( roadMap.getSizeRoadmap() == 0 || !roadMap.isDetailInstalled() ||
         synchro < 0 || synchro >= roadMap.getNumTrafficSignalTypes() ) {
        return 0;
    }

    // Synchro packs (numPhases, phase): groups of 2, 3, 4... phases laid out
    // back to back.
    int32_t numPhases = 2;
    while ( numPhases <= synchro ) {
        synchro -= numPhases;
        numPhases++;
    }

    float syncTime = gpTimer->GameTotalTime;
    const bool bInOnlineSession = false; // TODO: ONLINE (gpCP2PGameService)
    if ( bInOnlineSession ) {
        OTDU_UNIMPLEMENTED; // TODO: ONLINE (session clock FUN_0096a980)
    }
    *pSyncTime = syncTime;

    const int32_t cycleTimeMs = static_cast<int32_t>( syncTime * 1000.0f ) %
                                ( kSignalPhaseDurationMs * numPhases );
    if ( cycleTimeMs / kSignalPhaseDurationMs != synchro ) {
        return 1;
    }

    const int32_t phaseTimeLeftMs =
        kSignalPhaseDurationMs - cycleTimeMs % kSignalPhaseDurationMs;
    return ( phaseTimeLeftMs > kSignalOrangeDurationMs ) ? 4 : 2;
}

bool GSAI::initialize( TestDriveGameInstance * ) {
    // FUN_00969900
    loadAIConfig();
    loadBotPersonalities();
    pAIPool->initialize();
    pathPool.initialize( kNumPathNodes, kNumRouteElems );
    roadBuckets.initialize( &roadMap, &gpPhysics->getRoadPhantomManager() );
    OTDU_UNIMPLEMENTED_ONCE; // TODO: culling shape +0x64 array (FUN_0040b990)
                             // and +0x70 = -1
    pDebugDraw->createBackground();
    pathSolver.initialize( &pathPool );
    floodSolver.initialize( &pathPool );
    OTDU_UNIMPLEMENTED_ONCE; // TODO: AiCopMng
    pBotManager->initialize();
    gAiPathOptimiser.initialize();
    pPathOptimiser = &gAiPathOptimiser;
    pSimBubble->initialize();
    pedestrianManager.initialize();
    OTDU_UNIMPLEMENTED_ONCE; // TODO: distribution zones, net interpolation,
                             // MngFontPlayers lists
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
    static constexpr bool bOnlineSession = false;
    OTDU_UNIMPLEMENTED_ONCE; // TODO: cop manager active flag (GSAI+0x2a984)
    const bool bBotsWanted =
        ( !bDisableBots && !bOnlineSession && !bPauseTraffic ) ||
        gbForceBotsEnabled;
    if ( bBotsWanted ) {
        pBotManager->setActive( true );
    } else if ( pBotManager->isActive() ) {
        pBotManager->setActive( false );
        pAIPool->removeBots();
    }

    pAIPool->tick( dt, totalTime );
    TickAiDebugOverlay( dt, totalTime );
    gGameObjectDico.updateLocations( dt, false );
    pSimBubble->tick( dt, totalTime );
    gAiDebugRace.tick();
    OTDU_UNIMPLEMENTED_ONCE; // TODO: online zones, net interpolation
    if ( gpTimer->GameSpeed != 0.0f && dt != 0.0f ) {
        OTDU_UNIMPLEMENTED_ONCE; // TODO: cop manager tick (FUN_00915010)
        pBotManager->tick();
    }
    if ( ( dt > 0.0f || gbForceOptimWhenPaused ) &&
         pPathOptimiser != nullptr ) {
        pPathOptimiser->tick();
    }
}

void GSAI::draw( float deltaTime ) {
    // FUN_00969260
    (void)deltaTime;
    if ( bRoadMapLoaded ) {
        SubmitAiDebugDrawLists();
    }
}

void GSAI::terminate() {
    if ( pPathOptimiser != nullptr ) {
        pPathOptimiser->release();
        pPathOptimiser = nullptr;
    }
    pedestrianManager.terminate();
    pDebugDraw->release();
    pDebugDraw->releaseBackground();
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

void GSAI::broadcastRaceMessage( const AiRaceMessage &message ) {
    // FUN_00968ed0
    auto dispatch = [&message]( AiVehicle *pVehicle ) {
        if ( pVehicle->getBehaviour() != nullptr ) {
            pVehicle->getBehaviour()->onRaceMessage( message );
        }
        if ( pVehicle->getController() != nullptr ) {
            pVehicle->getController()->onRaceMessage( message );
        }
    };

    AiVehicleList &opponents = pAIPool->getList(
        AiPool::AVL_FirstCategory +
        static_cast<int32_t>( eAiVehicleCategory::AVC_Opponent ) );
    for ( int32_t i = 0; i < opponents.Count; i++ ) {
        dispatch( opponents.Items[i] );
    }

    AiVehicleList &network = pAIPool->getList(
        AiPool::AVL_FirstCategory +
        static_cast<int32_t>( eAiVehicleCategory::AVC_Network ) );
    if ( network.Count != 0 ) {
        dispatch( network.Items[0] );
    }
}

void GSAI::setOpponentsHidden( const bool bHidden ) {
    // FUN_00968f60
    pLODSorter->bNoOpponentPhysics = bHidden;

    AiVehicleList &opponents = pAIPool->getList(
        AiPool::AVL_FirstCategory +
        static_cast<int32_t>( eAiVehicleCategory::AVC_Opponent ) );
    for ( int32_t i = 0; i < opponents.Count; i++ ) {
        AiVehicle *pOpponent = opponents.Items[i];
        if ( pOpponent == nullptr ) {
            continue;
        }
        CarState *pCarState = pOpponent->getCarState();
        VehicleBase *pVehicle = ( pCarState != nullptr )
                                    ? pCarState->getVehicleInstance()
                                    : nullptr;
        EntityDriver *pDriver =
            ( pCarState != nullptr ) ? pCarState->getDriver() : nullptr;
        VehicleSoundInstance *pSound =
            ( pCarState != nullptr ) ? pCarState->getVehicleSound() : nullptr;

        if ( bHidden ) {
            if ( pOpponent->getLocomotion() != nullptr ) {
                pOpponent->getLocomotion()->setPhysicsActive( false, false );
            }
            if ( pVehicle != nullptr ) {
                pVehicle->setShouldDraw( false );
            }
            if ( pDriver != nullptr ) {
                pDriver->removeInstanceReference();
            }
            if ( pSound != nullptr ) {
                pSound->Model.pause();
            }
        } else {
            if ( pVehicle != nullptr ) {
                pVehicle->setShouldDraw( true );
            }
            if ( pDriver != nullptr ) {
                pDriver->addInstanceReference();
            }
            if ( pSound != nullptr ) {
                pSound->Model.resume();
            }
        }
    }
}

void GSAI::loadBotPersonalities() {
    // FUN_00969610
    char path[256];
    snprintf( path, sizeof( path ), "%sDataBase/AIBotPers.xml",
              gpConfig->getResRootPath() );

    void *pContent = nullptr;
    uint32_t contentSize = 0;
    if ( gpFile->loadFile( path, &pContent, &contentSize ) ) {
        uint32_t remainingBytes = 0;
        Bank::DecryptTEA( pContent, contentSize, &remainingBytes );
        const int32_t numPatterns =
            pBotPersonalities->parse( static_cast<const char *>( pContent ) );
        OTDU_LOG_INFO( "AI bot personalities loaded (%d patterns)\n",
                       numPatterns );
    }
    if ( pContent != nullptr ) {
        TestDrive::Free( pContent );
    }
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

void GSAI::resumeTraffic( const Eigen::Vector4f *pBubblePosition ) {
    // FUN_00968e40
    pAIPool->setTrafficPaused( false );
    pAIPool->setField0x7C( true );
    pAIPool->setField0x85( true );
    OTDU_UNIMPLEMENTED_ONCE; // TODO: AI (GSAI+0x2a9a7 flag)

    if ( pBubblePosition != nullptr ) {
        pSimBubble->setPosition( *pBubblePosition );
    }
}

void GSAI::setCopsLevel( const float scale,
                         const bool bHighLevel,
                         const bool bMaxLevel ) {
    // FUN_00969540
    if ( scale <= 0.0f ) {
        bDisableCops = true;
        bCopsActive = false;
        pAIPool->removeCops();
    } else {
        bDisableCops = false;
    }
    pAIPool->setPoliceScale( scale );
    pAIPool->setForceDensity( true );
    OTDU_UNIMPLEMENTED; // TODO: AI (cop manager GSAI+0x2a984 +0x44 / +0x45 =
                        // bHighLevel / bMaxLevel)
    (void)bHighLevel;
    (void)bMaxLevel;
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
            pBotManager->setActive( false );
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
    if ( ImGui::Button( "Spawn parked car ahead" ) ) {
        spawnDebugParked();
    }
    ImGui::SameLine();
    if ( ImGui::Button( "Remove traffic" ) ) {
        pAIPool->removeTraffic();
    }

    ImGui::SeparatorText( "Bots" );
    ImGui::Text( "Bots %d / wanted %d (max %d, limited %d) | manager %s",
                 pAIPool->getNumBots(), pBotManager->getWantedCount(),
                 gAIConfig.MaxNumBots, gAIConfig.MaxNumBotsLimited,
                 pBotManager->isActive() ? "active" : "inactive" );
    ImGui::Text( "EnableBots %d | disabled %d | traffic paused %d | tutorial "
                 "step %d | can spawn %d",
                 gAIConfig.bEnableBots ? 1 : 0, bDisableBots ? 1 : 0,
                 bPauseTraffic ? 1 : 0,
                 gpScenaric->getIntroTutorial().getStep(),
                 pBotManager->canSpawn() ? 1 : 0 );
    ImGui::Checkbox( "Force bots enabled", &gbForceBotsEnabled );
    ImGui::SameLine();
    if ( ImGui::Button( "Remove bots" ) ) {
        pAIPool->removeBots();
    }

    gAiDebugRace.drawDebug();

    if ( ImGui::TreeNode( "Debug overlay" ) ) {
        ImGui::Checkbox( "Enable 3D overlay", &gDistributionZoneDebugDraw );
        ImGui::Checkbox( "Road map", &gDisplayRoadMap );
        ImGui::SameLine();
        ImGui::Checkbox( "Traffic lights", &gDisplayTrafficLights );
        ImGui::Checkbox( "Sim bubble", &gDisplaySimBubble );
        ImGui::SameLine();
        ImGui::Checkbox( "Culling shapes", &gDisplayCullingShapes );
        ImGui::SliderFloat( "Bound box width", &gBoundBoxWidth, 100.0f,
                            5000.0f );
        ImGui::RadioButton( "No vehicle state", &gDebugAiVehicleState, 0 );
        ImGui::SameLine();
        ImGui::RadioButton( "Player road frame", &gDebugAiVehicleState, 4 );
        ImGui::Checkbox( "Junction IDs", &gDisplayJunctionsID );
        ImGui::SameLine();
        ImGui::Checkbox( "Entity names", &gDisplayEntitiesNames );
        ImGui::Checkbox( "PID steer graph", &gTogglePIDSteerGraph );
        ImGui::SameLine();
        ImGui::Checkbox( "PID speed graph", &gTogglePIDSpeedGraph );
        ImGui::SameLine();
        ImGui::Checkbox( "Speed graph", &gToggleSpeedGraph );
        ImGui::Checkbox( "Debug page", &gbAiDebugPageOpen );
        static const char *kDebugPageNames[] = {
            "None",      "Memory / counts", "Path optim",   "Police",
            "Templates", "Sim bubble",      "Net (online)", "Removed vehicles",
        };
        ImGui::Combo( "Page", &gAiDebugPage, kDebugPageNames,
                      IM_ARRAYSIZE( kDebugPageNames ) );
        if ( ImGui::Button( "Next handmade junction" ) ) {
            SelectNextHandmadeJunction();
        }
        ImGui::SameLine();
        ImGui::Checkbox( "Flush instanciation", &gFlushInstanciation );
        ImGui::TreePop();
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
        if ( ImGui::Button( "Stop debugging vehicle" ) ) {
            SetDebuggedAiVehicle( nullptr );
        }
        for ( AiVehicle *pVehicle : pAIPool->getVehicles() ) {
            CarState *pCarState = pVehicle->getCarState();
            const Eigen::Vector4f position =
                pVehicle->getInfos().GroundMatrix.row( 3 );
            ImGui::PushID( pVehicle );
            if ( ImGui::SmallButton( "Debug" ) ) {
                SetDebuggedAiVehicle( pVehicle );
            }
            ImGui::PopID();
            ImGui::SameLine();
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

void GSAI::spawnDebugParked() {
    // FUN_00971330
    static constexpr float kSpawnDistance = 5.0f; // DAT_00f41750

    CarState *pPlayerCar = gpScenaric->getLocalPlayer().getCarState();
    if ( pPlayerCar == nullptr ) {
        return;
    }
    Matrix4x4f_t matrix = pPlayerCar->getWorldMatrix();
    Eigen::Vector4f forward = matrix.row( 2 );
    forward = forward / forward.norm() * kSpawnDistance;
    forward.w() = 0.0f;
    matrix.row( 3 ) += forward.transpose();
    pAIPool->requestParked( matrix, -1, nullptr );
}
