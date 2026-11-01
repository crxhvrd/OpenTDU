#include "shared.h"
#include "gm_mission.h"

#include "ai/ai_game_session.h"
#include "ai/ai_config.h"
#include "ai/ai_pool.h"
#include "ai/gs_ai.h"
#include "flash/flash_mouse.h"
#include "flash/gs_flash.h"
#include "flash/implementation/flash_player.h"
#include "flash/movie_player.h"
#include "ai/ai_road_map.h"
#include "ai/ai_road_shape.h"
#include "core/maths_helper.h"
#include "game/game_mechanism.h"
#include "game/mission/game_mech_race_hud.h"
#include "game/mission/game_mech_speed_gauge.h"
#include "game/mission/game_mech_speed_radar.h"
#include "game/scenaric/entity/entity_check_point.h"
#include "game/mng_hud.h"
#include "ai/ai_race.h"
#include "game/mng_gps.h"
#include "game/scenaric/entity/entity_cinematic.h"
#include "game/scenaric/entity/entity_extra_challenge.h"
#include "game/scenaric/event_parameters.h"
#include "game/scenaric/gs_scenaric.h"
#include "game/scenaric/ige_file.h"
#include "render/camera/gs_camera.h"
#include "game/scenaric/scenaric_bank.h"
#include "game/vehicle/car_state.h"
#include "audio/gs_audio.h"
#include "audio/implementation/ed_audio_stream_entry.h"
#include "game/scenaric/entity/entity_sound_emitter.h"
#include "online/p2p_game_service.h"
#include "physics/gs_physics.h"
#include "physics/replay.h"
#include "physics/vehicle/physics_vehicle.h"
#include "render/particles/gs_particles.h"
#include "tdu_instance.h"
#include "world/mng_panel.h"
#include "system/gs_pc_options.h"
#include "world/gs_world.h"
#include "world/mng_ocean.h"
#include "world/mng_sector_name.h"

MissionInfo *gpMissionInfo = nullptr; // DAT_013d1a68

static constexpr uint32_t kSoloSpotType = 0xc0c0;
static constexpr uint32_t kMultiSpotType = 0xc0c1;
static constexpr uint32_t kClubSpotType = 0xc0c2;
static constexpr int32_t kMissionSpawnMode = 1;
static constexpr const char *kOffroadWarningClient = "ClientOffroadWarning";
static constexpr const char *kOffroadWarningEmitter = "Driving_gauge";

static constexpr float kRestartFadeDuration = 0.5f; // FLOAT_00f41674
static constexpr FlashMessage kMsgPauseIn = FlashMessage( "pain" );
static constexpr FlashMessage kMsgPauseOut = FlashMessage( "paou" );
static constexpr FlashMessage kMsgQuit = FlashMessage( "quit" );
static constexpr FlashMessage kMsgRestart = FlashMessage( "rest" );
static constexpr FlashMessage kMsgGoChallenge = FlashMessage( "goch" );
static constexpr FlashMessage kMsgCountdown = FlashMessage( "321g" );
static constexpr FlashMessage kMsgRequestRestart = FlashMessage( "rqre" );
static constexpr FlashMessage kMsgResults = FlashMessage( "resu" );
static constexpr FlashMessage kMsgEndRace = FlashMessage( "endr" );
static constexpr FlashMessage kMsgUnknownRfhd = FlashMessage( "rfhd" );
static constexpr FlashMessage kMsgUnknownRewa = FlashMessage( "rewa" );
static constexpr FlashMessage kMsgUnknownVgcd = FlashMessage( "vgcd" );

static void SetConfigPCChallengeState( const int32_t state ) {
    uint32_t levelIndex = 0;
    uint32_t movieIndex = 0;
    if ( !gpFlash->getFlashMovieIndex( &levelIndex, &movieIndex,
                                       "CONFIGPC" ) ) {
        return;
    }

    MoviePlayer *pMovie = gpFlash->getMoviePlayer( levelIndex, movieIndex );
    if ( pMovie != nullptr && pMovie->MovieBank.isLoaded() &&
         pMovie->MovieBank.getFlashPlayer() != nullptr ) {
        pMovie->MovieBank.getFlashPlayer()->setVariableValue(
            "/:state_Challenge", state );
    }
}

GMMission::GMMission()
    : GameModeWorld( 6 ),
      mngInGameMenu(),
      mngChat(),
      mngFontPlayers(),
      pCheckpointBank( nullptr ),
      bFromLobby( false ),
      spawnFrame( Matrix4x4f_t::Identity() ),
      pGameSession( nullptr ),
      missionInfo( this ),
      bOnlineSession( false ),
      pOffroadWarningClient( nullptr ),
      pOffroadWarningEntry( nullptr ),
      bRestartRequested( false ),
      bSavedFreeRideBool3( true ),
      bQuitRequested( false ),
      bLastPlayerPromptShown( false ),
      bRankingShown( false ),
      field_0x107098( false ),
      bRelockChallengeRoads( false ),
      extraChallenge( this ) {
    // FUN_0073eb10
    registerManager( &mngBlckRds );
    registerManager( &mngFontPlayers );
    registerManager( &mngInGameMenu );
    registerManager( &mngNavPathDisplay );
    registerManager( &mngChat );

    gpMissionInfo = &missionInfo;
    gpAI->setRace( this );
}

GMMission::~GMMission() {
    // FUN_0073ea30
    gpMissionInfo = nullptr;
    gpAI->setRace( nullptr );
}

int32_t GMMission::getNumCheckpoints() const {
    // FUN_00801080
    GameMechRaceHUD *pRaceHUD =
        ( pGameMech != nullptr )
            ? static_cast<GameMechRaceHUD *>(
                  pGameMech->getComponent( GameMechanism::CS_RaceHUD ) )
            : nullptr;
    return ( pRaceHUD != nullptr ) ? pRaceHUD->getNumSteps() : 0;
}

bool GMMission::getCheckpointRoadLocation( const int32_t index,
                                           AiRoadIt &outIt ) {
    // FUN_00801310
    GameMechRaceHUD *pRaceHUD = static_cast<GameMechRaceHUD *>(
        pGameMech->getComponent( GameMechanism::CS_RaceHUD ) );
    EntityCheckPoint *pCheckpoint =
        pRaceHUD->findCheckpointForStep( pRaceHUD->wrapStep( index ) );
    if ( pCheckpoint == nullptr ) {
        outIt = AiRoadIt();
        return false;
    }

    outIt = pCheckpoint->getComputedRoadIt();
    if ( outIt.pRoad == nullptr ) {
        const Matrix4x4f_t &frame = pCheckpoint->getModelMatrix();
        AiRoadMap::Road *pRoad =
            gpAI->getRoadMap().findNearestRoadExhaustive( frame, true );
        if ( pRoad != nullptr ) {
            ProjectOnRoadShape( frame, outIt, nullptr, false,
                                pRoad->getShape() );
        }
        pCheckpoint->setComputedRoadIt( outIt );
    }
    return true;
}

void GMMission::setCheckpointRoadLocation( const int32_t index,
                                           const AiRoadIt &it ) {
    // FUN_00801050 + FUN_00408150
    GameMechRaceHUD *pRaceHUD = static_cast<GameMechRaceHUD *>(
        pGameMech->getComponent( GameMechanism::CS_RaceHUD ) );
    EntityCheckPoint *pCheckpoint =
        pRaceHUD->findCheckpointForStep( pRaceHUD->wrapStep( index ) );
    pCheckpoint->setComputedRoadIt( it );
}

void GMMission::endExtraChallenge( const bool bWon ) {
    // FUN_007408d0 / FUN_007408b0
    extraChallenge.end( bWon );
}

bool GMMission::isExtraChallengeOutroPending() const {
    // GM +0x106780 (inlined)
    return extraChallenge.isOutroCinematicPending();
}

bool GMMission::isExtraChallengeSucceeded() const {
    // FUN_0073d220
    return extraChallenge.isSucceeded();
}

void GMMission::initialize() {
    // FUN_00741060
    if ( gpMngOcean != nullptr ) {
        gbOceanEnabled = true;
    }
    OTDU_UNIMPLEMENTED; // TODO: ONLINE (lobby network service listener,
                        // CP2PGameService +0x26b84 = -1, FUN_00431f40 x16)

    pCheckpointBank = gpScenaric->getCheckpointBank();
    if ( pCheckpointBank == nullptr ) {
        pCheckpointBank = new ( TestDrive::AllocAligned(
            sizeof( ScenaricBank ), alignof( ScenaricBank ) ) ) ScenaricBank();
        gpScenaric->setCheckpointBank( pCheckpointBank );
        gpScenaric->loadPackBank(
            "Scenarics/Packs",
            ( gpScenaric->getActiveExtraChallenge() == nullptr )
                ? "pack_cp_virtual"
                : "pack_cp_extra",
            pCheckpointBank );
    }

    EventParameters &eventParameters = gpScenaric->getSelectedEventParameters();
    switch ( eventParameters.getSource() ) {
    case EventParameters::ES_Buffer:
    case EventParameters::ES_IGEFile:
        if ( eventParameters.getSource() == EventParameters::ES_Buffer ) {
            if ( !gpScenaric->isFromDriveIn() ) {
                bFromLobby = true;
            }
            ParseIGEFile( nullptr, gpScenaric->getTestIGEBuffer(),
                          &gIGEFileRequest, &mngActor,
                          gpScenaric->getTestIGESize(), false );
        } else {
            LoadIGEFile( &gIGEFileRequest, eventParameters.getRequestedName(),
                         &mngActor, false, nullptr, false, nullptr, nullptr );
        }

        gameMechType = getMissionDescription()->getGameType();
        gameMechID = uint32_t( getMissionDescription()->getSpotType() );
        if ( gpScenaric->getActiveExtraChallenge() != nullptr ) {
            extraChallenge.setup( &mngActor,
                                  static_cast<EntityExtraChallenge *>(
                                      gpScenaric->getActiveExtraChallenge() ) );
        }
        if ( gpScenaric->isFromDriveIn() || bFromLobby ) {
            OTDU_UNIMPLEMENTED; // TODO: ONLINE (ChangeSessionMode(8))
        }
        if ( !gpScenaric->isInstantChallenge() &&
             ( gameMechID == kMultiSpotType || gameMechID == kClubSpotType ) ) {
            OTDU_UNIMPLEMENTED; // TODO: ONLINE (GameSpy session parameters,
                                // phantom mode, FUN_0088e2c0 road discovery)
        }
        break;
    case EventParameters::ES_Online:
        ParseIGEFile( nullptr, gpScenaric->getICIGEBuffer(), &gIGEFileRequest,
                      &mngActor, gpScenaric->getICIGESize(), false );
        gpScenaric->releaseICIGEBuffer();
        gpScenaric->setInstantChallenge( true );
        gameMechType = EventParameters::EGT_Race;
        gameMechID = gpScenaric->getICEChallengers()[0].bBot ? 0xc0c0 : 0xc0c1;
        break;
    case EventParameters::ES_CustomChallenge:
        OTDU_UNIMPLEMENTED; // TODO: SCENARIC (drive-in custom challenge IGE
                            // buffer)
        break;
    default:
        break;
    }

    mngActor.preparePlayerSpawnStreaming( kMissionSpawnMode );

    if ( gameMechID == kMultiSpotType && !gpScenaric->isTrainingLevel() ) {
        OTDU_UNIMPLEMENTED; // TODO: ONLINE (GameSessionOnline FUN_00807230)
        pGameSession = nullptr;
        bOnlineSession = true;
    } else {
        gpAI->getGameSession().setMissionInfo( &missionInfo );
        gameMechID = kSoloSpotType;
        bOnlineSession = false;
        pGameSession = &gpAI->getGameSession();
    }
    bLastPlayerPromptShown = false;

    GameModeWorld::initialize();

    if ( gameMechID == kSoloSpotType ) {
        EventParameters *pDescription = getMissionDescription();
        missionInfo.selectOpponents( &pDescription->getOpponentVehicleType(),
                                     &pDescription->getOpponentRestriction() );
    }
    if ( gpScenaric->isFromDriveIn() ) {
        OTDU_UNIMPLEMENTED; // TODO: SCENARIC (drive-in stats FUN_00883860)
    }

    gpAI->setTrafficCategories( false, 1 );

    if ( pGameMech != nullptr ) {
        pGameMech->setGameSession( pGameSession );
        pGameMech->setUnknown75C( bFromLobby );
        pOffroadWarningClient =
            ( gpAudio != nullptr )
                ? gpAudio->createClient( kOffroadWarningClient )
                : nullptr;
        if ( pOffroadWarningClient != nullptr &&
             pOffroadWarningClient->getStereoPipeline() != nullptr ) {
            // FUN_004039b0
            pOffroadWarningEntry = new edAudioStreamEntry();
            pOffroadWarningClient->getStereoPipeline()->addStereoEntry(
                pOffroadWarningEntry );
        }
        attachOffroadWarningSample();
    }

    EventParameters *pMissionDescription = getMissionDescription();
    if ( pMissionDescription != nullptr &&
         pMissionDescription->getForc().getValue() != 0 ) {
        mngBlckRds.setEnabled( true );
        mngBlckRds.enable();
        if ( mngBlckRds.isEnabled() ) {
            registerManager( mngBlckRds.getMngActor() );
        }
    } else {
        mngBlckRds.setEnabled( false );
    }
    if ( gpMngHud != nullptr &&
         ( gameMechType == EventParameters::EGT_TimeAttack ||
           gameMechType == EventParameters::EGT_Race ||
           gameMechType == GameMechanism::MT_ExtraChallenge ) ) {
        gpMngHud->setDisplayTrace( false );
    }
}

void GMMission::reset() {
    // FUN_00740ab0
    bool bApplySpawn = true;
    if ( gameMechType == GameMechanism::MT_Race &&
         gameMechID == kSoloSpotType ) {
        EventParameters *pEventParameters = getMissionDescription();
        if ( pEventParameters != nullptr ) {
            spawnFrame = mngActor.placePlayerVehicle( kMissionSpawnMode, true );
            const int32_t numSlots =
                int32_t( pEventParameters->getNumOpponents().getValue() ) + 1;
            gpWorld->setPreviousTransform( computeGridMatrix( 0, numSlots ) );
            bApplySpawn = false;
        }
    }

    GameModeWorld::reset();

    gpAI->setDisableBots( true );
    OTDU_UNIMPLEMENTED; // TODO: GSAI (bot manager +1 = 0)
    gpAI->getAIPool()->removeBots();

    spawnFrame = mngActor.placePlayerVehicle( kMissionSpawnMode, bApplySpawn );
    if ( pGameMech != nullptr ) {
        pGameMech->reset();
    }
    if ( gpScenaric->getActiveExtraChallenge() != nullptr ) {
        extraChallenge.start();
    }

    FlashPlayer *pHudPlayer =
        ( gpMngHud != nullptr ) ? gpMngHud->getHudPlayer() : nullptr;
    gReplay.setRemotePlayer( pHudPlayer );
    mngInGameMenu.setFlashPlayer( pHudPlayer );
    mngInGameMenu.refreshDrivingHelpLabel();

    if ( gpScenaric->isFromDriveIn() ) {
        OTDU_UNIMPLEMENTED; // TODO: SCENARIC (drive-in fee and stats)
    }

    if ( gpMngSectorName != nullptr ) {
        gpMngSectorName->bEnabled = false;
    }

    OTDU_UNIMPLEMENTED; // TODO: ONLINE ("/:WhatPlayer" host / guest)

    SetConfigPCChallengeState( 1 );

    bQuitRequested = false;
    field_0x107098 = false;
    bRankingShown = false;
}

void GMMission::tick( float deltaTime, float totalTime ) {
    // FUN_007408f0
    GameModeWorld::tick( deltaTime, totalTime );

    if ( pGameMech != nullptr && !gpScenaric->isInstantChallenge() ) {
        OTDU_UNIMPLEMENTED_ONCE; // TODO: ONLINE (session lost / last player
                                 // left prompt)
    }

    if ( bRestartRequested ) {
        bRestartRequested = false;
        restart();
    }

    const uint32_t fadeFlags = mngFade.getTransitionFlags();
    gpPCOptions->setMainMenuAllowed( ( fadeFlags & 1 ) == 0 &&
                                     ( ( fadeFlags >> 3 ) & 1 ) == 0 );
}

static bool HasMissionCops( MissionInfo &missionInfo ) {
    // FUN_00801180
    if ( gpScenaric->isInstantChallenge() ) {
        return true;
    }
    EventParameters *pDescription = missionInfo.pOwner->getMissionDescription();
    return pDescription->getCops().getValue() != 0 &&
           pDescription->getCopsDensity().getValue() != 0;
}

static bool HasMissionTraffic( MissionInfo &missionInfo ) {
    // FUN_00801130
    if ( gpScenaric->isInstantChallenge() ) {
        return true;
    }
    EventParameters *pDescription = missionInfo.pOwner->getMissionDescription();
    return pDescription->getTraffic().getValue() != 0 &&
           pDescription->getTrafficDensity().getValue() != 0;
}

static bool IsInstantReplayAllowed( MissionInfo &missionInfo ) {
    // FUN_0042df50
    return !HasMissionCops( missionInfo ) && !HasMissionTraffic( missionInfo );
}

static void ReleasePlayerAutopilot() {
    // FUN_00740440 / FUN_00740d80 (inlined)
    CarState *pLocalCar = gpScenaric->getLocalPlayer().getCarState();
    if ( pLocalCar->getSimulationType() ==
             CarState::eSimulationType::CSST_Local &&
         pLocalCar->getOwner() == CarState::eOwner::CSO_LocalPlayer &&
         pLocalCar->getAIController() != nullptr ) {
        gpAI->getAIPool()->togglePlayerAutopilot( false );
    }
}

void GMMission::pause() {
    // FUN_00740440
    if ( pGameMech != nullptr && pGameMech->getCinematic() != nullptr ) {
        pGameMech->getCinematic()->stop( true );
    }
    if ( pGameMech != nullptr ) {
        pGameMech->pause();
    }

    if ( gpMngSectorName != nullptr ) {
        gpMngSectorName->bEnabled = true;
    }

    if ( gpScenaric->getLocalPlayer().getCarState() != nullptr ) {
        gpScenaric->getLocalPlayer().getCarState()->getPoliceState().reset();
        ReleasePlayerAutopilot();
    }

    gReplay.resetSession();
    OTDU_UNIMPLEMENTED; // TODO: AI (DAT_013d1a77 = 0)
    AiPool *pAIPool = gpAI->getAIPool();
    pAIPool->setTrafficPaused( false ); // +0xc
    pAIPool->setField0x7C( true );
    pAIPool->setField0x85( true );
    pAIPool->setTrafficScale( 1.0f );       // +0x78
    pAIPool->setIgnoreZoneDensity( false ); // +0x7d
    if ( gpScenaric->getActiveExtraChallenge() != nullptr ) {
        extraChallenge.terminate();
    }

    GameModeWorld::pause();

    OTDU_UNIMPLEMENTED; // TODO: ONLINE (CP2PGameService slot 2(1))
    gpPhysics->setPlayerSlowDownForced( false );
    gpPhysics->setPlayerParkedForced( false );
    gpScenaric->setFromDriveIn( false );
    gpScenaric->setFromGameInvite( false );
    SetConfigPCChallengeState( 0 );
}

void GMMission::destroy() {
    // FUN_007402f0
    gpCamera->setViewChangeAllowed( true );
    OTDU_UNIMPLEMENTED; // TODO: ONLINE (lobby network service listener =
                        // null)
    if ( pGameSession != nullptr ) {
        pGameSession->onEvent( GameSession::E_Release );
    }
    if ( bRelockChallengeRoads ) {
        gpScenaric->delockChallengeRoads( true );
    }

    GameModeWorld::destroy();

    if ( pGameMech != nullptr ) {
        pGameMech->setGameSession( nullptr );
    }
    if ( pOffroadWarningClient != nullptr ) {
        if ( pOffroadWarningEntry != nullptr ) {
            // FUN_006136b0
            if ( pOffroadWarningClient->getStereoPipeline() != nullptr ) {
                pOffroadWarningClient->getStereoPipeline()->removeStereoEntry(
                    pOffroadWarningEntry );
            }
            delete pOffroadWarningEntry;
            pOffroadWarningEntry = nullptr;
        }

        pOffroadWarningClient = nullptr;
    }
    if ( bOnlineSession && pGameSession != nullptr ) {
        OTDU_UNIMPLEMENTED; // TODO: ONLINE (free GameSessionOnline)
    }
    pGameSession = nullptr;
    bLastPlayerPromptShown = false;

    if ( gpScenaric->getActiveExtraChallenge() != nullptr ) {
        gpScenaric->getExtraChallengesCineBank().load(
            ExtraChallengeCineBank::ECBM_Intro );
        gpScenaric->setActiveExtraChallenge( nullptr );
    }

    const EventParameters::eSource source =
        gpScenaric->getSelectedEventParameters().getSource();
    if ( source == EventParameters::ES_Buffer ||
         source == EventParameters::ES_IGEFile ||
         source == EventParameters::ES_Online ||
         source == EventParameters::ES_CustomChallenge ) {
        gpScenaric->unloadBank( pCheckpointBank );
    }
    if ( pCheckpointBank != nullptr ) {
        pCheckpointBank->ScenaricBank::~ScenaricBank();
        TestDrive::FreeAligned( pCheckpointBank );
    }
    pCheckpointBank = nullptr;
    gpScenaric->setCheckpointBank( nullptr );
    gpScenaric->setTrainingLevel( false );
    gpScenaric->setFromDriveIn( false );
    gpScenaric->setFromGameInvite( false );
}

bool GMMission::onMessage( FlashMessage *pMessage, FlashPlayer *pPlayer ) {
    // FUN_0073f350
    if ( ( pGameMech == nullptr ||
           pGameMech->getMechType() != GameMechanism::MT_ExtraChallenge ) &&
         gpMngInGameMenu != nullptr &&
         gpMngInGameMenu->onMessage( pMessage ) ) {
        return true;
    }
    if ( *pMessage == kMsgRequestRestart ) {
        gpPCOptions->setMainMenuAllowed( false );
    }
    if ( gReplay.onFlashMessage( *pMessage ) ) {
        return true;
    }

    if ( *pMessage == kMsgPauseIn ) {
        OTDU_UNIMPLEMENTED_ONCE; // TODO: SCENARIC ("/:state_club")
        return GameModeWorld::onMessage( pMessage, pPlayer );
    }
    if ( *pMessage == kMsgPauseOut ) {
        char whereTo[0x20] = {};
        FlashPlayer *pHudPlayer =
            ( gpMngHud != nullptr ) ? gpMngHud->getHudPlayer() : nullptr;
        if ( pHudPlayer != nullptr ) {
            pHudPlayer->getVariableAsString( "/:WhereTo", whereTo );
        }
        const FlashMessage destination( whereTo );
        if ( destination == kMsgQuit ) {
            OTDU_UNIMPLEMENTED_ONCE; // TODO: ONLINE (ChangeSessionMode(-1))
            bQuitRequested = true;
            exitMission();
        } else if ( destination == kMsgRestart ) {
            return true;
        }
        return GameModeWorld::onMessage( pMessage, pPlayer );
    }
    if ( *pMessage == kMsgGoChallenge ) {
        gpPhysics->setPlayerSlowDownForced( false );
        return false;
    }
    if ( *pMessage == kMsgCountdown ) {
        FlashPlayer *pHudPlayer =
            ( gpMngHud != nullptr ) ? gpMngHud->getHudPlayer() : nullptr;
        char whatCam[0x20] = {};
        if ( pHudPlayer != nullptr ) {
            pHudPlayer->getVariableAsString( "/:WhatCam", whatCam );
        }

        CarState *pLocalCar = gpScenaric->getLocalPlayer().getCarState();
        if ( whatCam[0] == 'g' ) {
            gpPhysics->setPlayerSlowDownForced( false );
            gpPhysics->setPlayerParkedForced( false );
            if ( pGameMech != nullptr ) {
                pGameMech->setCountdownGo( true );
            }
            if ( pLocalCar != nullptr &&
                 pLocalCar->getPhysicsVehicle() != nullptr ) {
                pLocalCar->resetBackOnTrack();
            }
        } else if ( whatCam[0] == '3' ) {
            if ( pLocalCar != nullptr &&
                 pLocalCar->getPhysicsVehicle() != nullptr ) {
                pLocalCar->resetBackOnTrack();
                PhysicsVehicle *pVehicle = pLocalCar->getPhysicsVehicle();
                if ( pVehicle->getEngineState() != eEngineState::ES_Running ) {
                    pVehicle->setEngineState( eEngineState::ES_Running );
                }
            }
            if ( pGameMech == nullptr || ( !pGameMech->getUnknown75C() &&
                                           !pGameMech->isEndlessLoopRace() ) ) {
                if ( IsInstantReplayAllowed( missionInfo ) &&
                     pGameMech != nullptr &&
                     !pGameMech->getGameSession()->isOnline() ) {
                    if ( pHudPlayer != nullptr ) {
                        pHudPlayer->setVariableValue( "/:IsReplay", 1 );
                    }
                    gpPhysics->markInstantReplayVehicles();
                    gReplay.setupRings(
                        gpPhysics->getNumVehiclesInInstantReplay() );
                    gReplay.setState( eReplayState::RS_Recording );
                }
            } else if ( pHudPlayer != nullptr ) {
                pHudPlayer->setVariableValue( "/:IsReplay", 0 );
            }
            if ( pGameMech != nullptr ) {
                if ( gpMngGPS != nullptr ) {
                    gpMngGPS->onPlayerJoined(
                        pGameMech->getGameSession()->getNumParticipants(), -1 );
                }
                pGameMech->getGameSession()->sendRaceMessage(
                    AiRaceMessage{ 0, ARM_Countdown } );
            }
        } else if ( whatCam[0] == '1' && pGameMech != nullptr ) {
            pGameMech->getGameSession()->sendRaceMessage(
                AiRaceMessage{ 0, ARM_CountdownLast } );
        }
        return false;
    }
    if ( *pMessage == kMsgQuit ) {
        OTDU_UNIMPLEMENTED_ONCE; // TODO: ONLINE (ChangeSessionMode(-1))
        quitMission();
        return false;
    }
    if ( *pMessage == kMsgRestart ) {
        startRestartFade();
        return true;
    }
    if ( *pMessage == kMsgEndRace ) {
        if ( pGameMech != nullptr &&
             pGameMech->getComponent( GameMechanism::CS_SpeedRadar ) !=
                 nullptr ) {
            static_cast<GameMechSpeedRadar *>(
                pGameMech->getComponent( GameMechanism::CS_SpeedRadar ) )
                ->onHudMessage( *pMessage );
        }
        if ( pGameMech != nullptr &&
             pGameMech->getComponent( GameMechanism::CS_SpeedGauge ) !=
                 nullptr ) {
            static_cast<GameMechSpeedGauge *>(
                pGameMech->getComponent( GameMechanism::CS_SpeedGauge ) )
                ->onHudMessage( *pMessage );
        }
        return false;
    }
    if ( *pMessage == kMsgResults ) {
        gpFlash->getMouse()->field12_0x30 = false;
        FlashPlayer *pHudPlayer =
            ( gpMngHud != nullptr ) ? gpMngHud->getHudPlayer() : nullptr;
        if ( pHudPlayer == nullptr ) {
            return false;
        }
        const bool bEndlessLoop =
            pGameMech != nullptr && pGameMech->getUnknown75C();
        if ( 0 < gReplay.getNumFrames() && pGameMech != nullptr &&
             !pGameMech->getGameSession()->isOnline() &&
             IsInstantReplayAllowed( missionInfo ) && !bEndlessLoop ) {
            gReplay.requestStop();
            pHudPlayer->setVariableValue( "/:IsReplay", 1 );
        } else {
            pHudPlayer->setVariableValue( "/:IsReplay", 0 );
        }
        return false;
    }
    if ( *pMessage == kMsgUnknownRfhd ) {
        return true;
    }
    if ( *pMessage == kMsgUnknownRewa || *pMessage == kMsgUnknownVgcd ) {
        return false;
    }

    OTDU_UNIMPLEMENTED_ONCE; // TODO: GMMission (replay "/:IsReplay", online
                             // and instant challenge messages)
    return GameModeWorld::onMessage( pMessage, pPlayer );
}

bool GMMission::restart() {
    // FUN_00740d80
    gpFlash->getMouse()->field12_0x30 = true;
    if ( pGameMech != nullptr && pGameMech->getCinematic() != nullptr ) {
        pGameMech->getCinematic()->stop( true );
    }
    if ( gpScenaric->getLocalPlayer().getCarState() != nullptr &&
         !gAIConfig.bAIReplacePlayer &&
         ( !gpScenaric->isTrainingLevel() || !gAIConfig.Unknown0xE4 ) ) {
        ReleasePlayerAutopilot();
    }
    gReplay.setState( eReplayState::RS_Off );
    gpPhysics->clearInstantReplayVehicles();

    FlashPlayer *pHudPlayer =
        ( gpMngHud != nullptr ) ? gpMngHud->getHudPlayer() : nullptr;
    if ( pHudPlayer != nullptr ) {
        pHudPlayer->goToFrameAndPlay( 0, "/", true );
    }
    OTDU_UNIMPLEMENTED; // TODO: HUD
    OTDU_UNIMPLEMENTED; // TODO: WORLD (FUN_007047e0, streaming position =
                        // previous transform, streaming tick, file flush)

    if ( isPaused() ) {
        bPaused = false;
        onMenuPauseExit( true );
    }
    if ( pGameMech != nullptr ) {
        pGameMech->restart();
    }
    OTDU_UNIMPLEMENTED; // TODO: GSAI (FUN_0091f730 AiPool)

    MngActor &mngActor = getActorManager();
    for ( int32_t i = 0; i < mngActor.getActorCount(); i++ ) {
        mngActor.getActor( i )->reset();
    }
    OTDU_UNIMPLEMENTED; // TODO: RENDER (FUN_0042e940 tire skids reset)
    if ( gpMngPanel != nullptr ) {
        gpMngPanel->pause();
        gpMngPanel->reset();
    }
    gpParticles->stopAllParticleSystems();

    if ( gpScenaric->isFromDriveIn() ) {
        OTDU_UNIMPLEMENTED; // TODO: GMMission (drive-in fee: return false when
                            // the player cannot pay, else charge it)
    }
    OTDU_UNIMPLEMENTED; // TODO: GSAI (FUN_009100d0)
    return true;
}

void GMMission::onPauseMenuRequested( bool bOpened ) {
    // FUN_0073dfe0
    const bool bSolo = isSoloEvent();
    if ( !bSolo ) {
        OTDU_UNIMPLEMENTED; // TODO: ONLINE (FUN_0073de90)
    }
    GameModeWorld::onPauseMenuRequested( bSolo );
}

void GMMission::onMenuPauseExit( bool bRestoreState ) {
    // FUN_0073dfb0
    const bool bSolo = isSoloEvent();
    if ( !bSolo ) {
        OTDU_UNIMPLEMENTED; // TODO: ONLINE (FUN_0073de00)
    }
    GameModeWorld::onMenuPauseExit( bSolo );
}

bool GMMission::isSoloEvent() {
    // FUN_0073d270
    if ( ( pGameMech != nullptr && pGameMech->getUnknown75C() ) ||
         gpScenaric->isTrainingLevel() ) {
        return true;
    }

    EventParameters *pEventParameters = getMissionDescription();
    if ( pEventParameters == nullptr ) {
        return true;
    }
    const int32_t spotType = pEventParameters->getSpotType();
    return spotType != int32_t( kMultiSpotType ) &&
           spotType != int32_t( kClubSpotType );
}

void GMMission::quitMission() {
    // FUN_0073f350 (inlined)
    bQuitRequested = true;
    exitMission();
    gpScenaric->getIntroTutorial().onMissionQuit();
}

bool GMMission::attachOffroadWarningSample() {
    // FUN_0073d450
    if ( pOffroadWarningEntry != nullptr ) {
        ScenaricBank *pBank = gpScenaric->getWorldScenaricBank();
        for ( int32_t i = 0; i < pBank->getEntityCount(); i++ ) {
            Entity *pEntity = pBank->getEntity( i );
            if ( pEntity->getType() != eEntityType::ET_SDEM ||
                 StringCmpI( pEntity->getInstanceName().c_str(),
                             kOffroadWarningEmitter ) != 0 ) {
                continue;
            }

            pOffroadWarningEntry->setSample(
                static_cast<EntitySoundEmitter *>( pEntity )->getSample() );
            pGameMech->setOffroadWarningEntry( pOffroadWarningEntry );
            return true;
        }
    }

    pGameMech->setOffroadWarningEntry( nullptr );
    return false;
}

void GMMission::exitMission() {
    // FUN_0073f150
    gReplay.setState( eReplayState::RS_Off );
    gpPhysics->clearInstantReplayVehicles();
    OTDU_UNIMPLEMENTED; // TODO: ONLINE (ChangeSessionMode(-1) on a challenge
                        // spot)
    if ( pGameMech != nullptr ) {
        pGameMech->stateChange( StateMachine::kNoState );
    }

    if ( bFromLobby ) {
        requestExit();
        gpTestDriveInstance->setNextGameMode( GM_GameModeEdit );
        gpScenaric->setGMEStartPosition( gpScenaric->getLocalPlayer()
                                             .getCarState()
                                             ->getBody()
                                             ->WorldMatrix.row( 3 )
                                             .transpose() );
        return;
    }

    if ( gpScenaric->isFromDriveIn() ) {
        requestExit();
        gpTestDriveInstance->setNextGameMode( GM_DriveIn );
        return;
    }
    if ( false /* gGSScenaric +0x23424 club */ ) {
        OTDU_UNIMPLEMENTED; // TODO: ONLINE (FUN_0073ef60 club)
        return;
    }
    requestExit();
    if ( !gpScenaric->isTrainingLevel() && !gpScenaric->isFromGameInvite() ) {
        gpTestDriveInstance->setNextGameMode( GM_FreeRide );
        return;
    }
    gpTestDriveInstance->setNextGameMode( GM_Lobby );
}

void GMMission::startRestartFade() {
    // FUN_0073d530
    bRestartRequested = false;
    OTDU_UNIMPLEMENTED; // TODO: FLASH (movie draw mode |= 2)
    bSavedFreeRideBool3 = gpCP2PGameService->FreeRideMgr.bUnknownBool3;
    gpCP2PGameService->FreeRideMgr.bUnknownBool3 = false;
    mngFade.startTransition(
        ( ( mngFade.getTransitionFlags() | 2u ) & ~0xcu ) | 1u,
        kRestartFadeDuration, &GMMission::OnRestartFadedOut, nullptr, this );
}

void GMMission::OnRestartFadedOut( void *pData, void * /*pSource*/ ) {
    // LAB_0073d140
    GMMission *pMission = static_cast<GMMission *>( pData );
    pMission->mngFade.setTransitionFlags(
        pMission->mngFade.getTransitionFlags() | 8u );
    pMission->mngFade.setRenderLogo( true ); // +0x7a2c
    pMission->bRestartRequested = true;
}

static constexpr int32_t kNumSlotsSingleFile = 2;
static constexpr float kGridRowSpacing = 10.0f;       // FLOAT_00f4166c
static constexpr float kGridStaggerSpacing = 5.0f;    // DAT_00f41750
static constexpr float kGridStartBackOff = 7.0f;      // DAT_00f41844
static constexpr float kLaneHalfWidthMin = 0.05f;     // DAT_00f41778
static constexpr float kLaneHalfWidthDefault = 0.5f;  // FLOAT_00f41674
static constexpr float kFallbackRowSpacing = -3.5f;   // DAT_00f41bac
static constexpr float kFallbackSideOffset = -2.25f;  // DAT_00f41ba8
static constexpr float kFallbackColumnSpacing = 4.5f; // DAT_00f41ba4
static constexpr float kFallbackSingleSpacing = 3.0f; // DAT_00f416b4
static constexpr float kFallbackForwardOffset = 0.0f; // FLOAT_00f41660

static float ComputeGridLaneHalfWidth( const AiRoadProfile *pProfile ) {
    // FUN_0073e010 (inlined)
    float ratioStart = 0.0f;
    float ratioEnd = 0.0f;
    pProfile->computeLaneWidthRatios( 0, true, ratioStart, ratioEnd );
    const float halfWidth = std::abs( ( ratioEnd - ratioStart ) * 0.5f );
    return ( kLaneHalfWidthMin > halfWidth ) ? kLaneHalfWidthDefault
                                             : halfWidth;
}

static inline bool IsOdd( const int32_t value ) { return ( value % 2 ) != 0; }

Matrix4x4f_t GMMission::computeGridMatrix( const int32_t slotIndex,
                                           const int32_t numSlots ) {
    // FUN_0073e010
    const bool bStaggered = numSlots > kNumSlotsSingleFile;
    const bool bOnline = pGameSession != nullptr && pGameSession->isOnline();

    if ( gameMechType == GameMechanism::MT_Race ) {
        AiRoadIt checkpointIt;
        getCheckpointRoadLocation( 1, checkpointIt );

        AiRoadMap::Road *pRoad = checkpointIt.pRoad;
        AiRoadIt spawnIt;
        if ( pRoad != nullptr ) {
            ProjectOnRoadShape( spawnFrame, spawnIt, nullptr, false,
                                pRoad->getShape() );
            if ( spawnIt.bWay != checkpointIt.bWay ) {
                checkpointIt.T = 1.0f - checkpointIt.T;
            }
            checkpointIt.bWay = spawnIt.bWay;
        }

        if ( pRoad != nullptr && !bOnline ) {
            AiRoadShape *pShape = pRoad->getShape();
            const float startFraction =
                pShape->computeRoadFraction( checkpointIt );
            const float invRoadLength = 1.0f / pShape->TotalLength;
            const AiRoadProfile *pProfile =
                pRoad->pDetail->pEntities[checkpointIt.IndEntity]->pProfile;
            const bool bMultiLane =
                pProfile->NumRightLanes + pProfile->NumLeftLanes >= 2;

            // Back of the grid must stay on the road.
            float gridLength = 0.0f;
            if ( bStaggered ) {
                const float spacing =
                    bMultiLane ? kGridStaggerSpacing : kGridRowSpacing;
                gridLength =
                    ( float( numSlots - 1 ) * spacing + kGridStartBackOff ) *
                    invRoadLength;
            } else {
                int32_t numRows = numSlots;
                if ( bMultiLane ) {
                    numRows =
                        ( IsOdd( numSlots ) ? numSlots + 1 : numSlots ) / 2;
                }
                gridLength =
                    ( float( numRows ) * kGridRowSpacing + kGridStartBackOff ) *
                    invRoadLength;
            }
            if ( spawnIt.bWay ) {
                gridLength = -gridLength;
            }

            const float backFraction = gridLength + startFraction;
            if ( backFraction < 1.0f && backFraction > 0.0f ) {
                const int32_t rowFromBack = numSlots - slotIndex - 1;

                float lateralRatio = 0.0f;
                float spacing = kGridRowSpacing;
                int32_t row = rowFromBack;
                if ( bMultiLane ) {
                    const float laneHalfWidth =
                        ComputeGridLaneHalfWidth( pProfile );
                    lateralRatio =
                        IsOdd( rowFromBack ) ? laneHalfWidth : -laneHalfWidth;
                    if ( bStaggered ) {
                        spacing = kGridStaggerSpacing;
                    } else {
                        row = ( IsOdd( rowFromBack ) ? rowFromBack - 1
                                                     : rowFromBack ) /
                              2;
                    }
                }

                float alongFraction =
                    ( float( row ) * spacing + kGridStartBackOff ) *
                    invRoadLength;
                if ( spawnIt.bWay ) {
                    alongFraction = -alongFraction;
                } else {
                    lateralRatio = -lateralRatio;
                }

                AiRoadIt slotIt;
                pShape->computeIt( alongFraction + startFraction, slotIt );
                if ( spawnIt.bWay != slotIt.bWay ) {
                    slotIt.T = 1.0f - slotIt.T;
                }
                slotIt.bWay = spawnIt.bWay;

                float lateral = 0.0f;
                float height = 0.0f;
                slotIt.getEntity()->pProfile->computeWidthOffset(
                    lateralRatio, lateral, height );
                Eigen::Vector4f position =
                    slotIt.computeOffsetPosition( lateral, height );
                const Eigen::Vector4f roadDirection = slotIt.computeDirection();

                Matrix4x4f_t gridMatrix = spawnFrame;
                const Eigen::Vector4f forward = gridMatrix.row( 2 ).transpose();
                gridMatrix.row( 3 ) = Eigen::Vector4f( 0.0f, 0.0f, 0.0f, 1.0f );
                const float angle =
                    std::atan2( roadDirection.z() * forward.x() -
                                    forward.z() * roadDirection.x(),
                                roadDirection.x() * forward.x() +
                                    roadDirection.z() * forward.z() );
                MatrixRotateY( gridMatrix, -angle );
                position.w() = 1.0f;
                gridMatrix.row( 3 ) = position.transpose();
                return gridMatrix;
            }
        }
    }

    const Eigen::Vector4f side = spawnFrame.row( 0 ).transpose();
    const Eigen::Vector4f forward = spawnFrame.row( 2 ).transpose();
    Eigen::Vector4f position = spawnFrame.row( 3 ).transpose();
    float sideOffset = 0.0f;
    if ( bStaggered ) {
        const int32_t rowFromBack = numSlots - slotIndex - 1;
        position += forward * ( float( rowFromBack ) * kFallbackRowSpacing ) +
                    side * kFallbackSideOffset;
        sideOffset = float( rowFromBack % 2 ) * kFallbackColumnSpacing;
    } else {
        const float spacing =
            ( numSlots == 1 ) ? kFallbackSingleSpacing : kFallbackColumnSpacing;
        position += forward * kFallbackForwardOffset +
                    side * ( ( float( numSlots ) - 1.0f ) * 0.5f * spacing );
        sideOffset = float( slotIndex - 1 ) * spacing;
    }

    Matrix4x4f_t gridMatrix = spawnFrame;
    gridMatrix.row( 3 ) = ( side * sideOffset + position ).transpose();
    return gridMatrix;
}
