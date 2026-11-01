#include "shared.h"
#include "mission_extra_challenge.h"

#include "ai/ai_config.h"
#include "ai/ai_pool.h"
#include "ai/ai_road_iterator.h"
#include "ai/ai_road_map.h"
#include "ai/ai_road_query.h"
#include "ai/ai_road_shape.h"
#include "ai/gs_ai.h"
#include "core/maths_helper.h"
#include "game/mission/gm_mission.h"
#include "game/avatar/avatar_clothes.h"
#include "game/avatar/gs_avatar.h"
#include "game/scenaric/entity/entity_avatar_hitch_hiker.h"
#include "game/scenaric/entity/entity_avatar_shopping_girl.h"
#include "game/scenaric/entity/entity_check_point.h"
#include "game/scenaric/entity/entity_cinematic.h"
#include "game/scenaric/entity/entity_convoy.h"
#include "game/scenaric/entity/entity_hitch_hiker.h"
#include "game/scenaric/entity/entity_passenger_goodies.h"
#include "game/scenaric/entity/entity_shopping_girl.h"
#include "game/scenaric/entity/entity_transporter.h"
#include "game/scenaric/event_parameters.h"
#include "game/scenaric/extra_challenge_cine_bank.h"
#include "game/scenaric/mng_actor.h"
#include "game/vehicle/car_state.h"
#include "game/vehicle/gs_car_state.h"
#include "physics/gs_physics.h"
#include "physics/vehicle/physics_vehicle.h"
#include "player_data/driver.h"
#include "player_data/gs_playerdata.h"
#include "player_data/player_data_interface.h"
#include "player_data/saved_extra_challenges.h"
#include "player_data/saved_garage.h"
#include "render/camera/gs_camera.h"
#include "world/gs_world.h"
#include "world/gs_world_streaming.h"

static constexpr uint32_t kSoloSpotType = 0xc0c0;
static constexpr float kMillisecondsPerSecond = 1000.0f; // DAT_00f41718
static constexpr float kPenaltyCreditsScale = 10.0f;     // FLOAT_00f4166c
static constexpr float kRoadStartT = 0.1f;               // FLOAT_00f41668
static constexpr float kRoadEndT = 0.9f;                 // DAT_00f416a0
static constexpr uint64_t kPassengerAvatarHashcode = 0xffa1845bfa4713bcull;
static constexpr int32_t kShoppingGirlAvatarType = 5;
static constexpr int32_t kHitchHikerAvatarType = 7;

static Eigen::Vector4f GetWorldShift() {
    if ( gpWorldStreaming != nullptr && gpWorldStreaming->isEnabled() ) {
        return gpWorldStreaming->getPosition();
    }
    return Eigen::Vector4f::Zero();
}

static void ResetAIPoolForPlayer() {
    AiPool *pAIPool = gpAI->getAIPool();
    pAIPool->setTrafficPaused( false );
    pAIPool->setField0x7C( true );
    pAIPool->setField0x85( true );
    gpAI->setPauseTraffic( false );
}

static void ReleaseLocalPlayerCar() {
    LocalPlayer &localPlayer = gpScenaric->getLocalPlayer();
    CarState *pLocalCar = localPlayer.getCarState();
    if ( pLocalCar == nullptr ) {
        return;
    }

    VehicleBaseResources *pResource = pLocalCar->getResource();
    gpCarState->releaseCarState( pLocalCar, false );
    if ( pResource != nullptr ) {
        pResource->unload();
        gpCarState->releaseResourceEntry( pResource );
    }
    localPlayer.setCarState( nullptr );
}

static EntityCinematic *FindExtraChallengeCinematic( const char *pName ) {
    return static_cast<EntityCinematic *>(
        gpScenaric->getExtraChallengesCineBank().getBank().findEntity(
            eEntityType::ET_CINE, pName ) );
}

MissionExtraChallenge::MissionExtraChallenge( GMMission *pOwnerGM )
    : pOwner( pOwnerGM ),
      pChallenge( nullptr ),
      pMngActor( nullptr ),
      pStartCheckpoint( nullptr ),
      pFinishCheckpoint( nullptr ),
      pCarSpawn( nullptr ),
      pOutroCinematic( nullptr ),
      pLoopCinematic( nullptr ),
      outroFrame( Matrix4x4f_t::Identity() ),
      field_0x70( false ),
      pPassenger( nullptr ),
      pCargo( nullptr ),
      bTeleportToStart( false ),
      field_0x7d( false ),
      bSavedInfAllButCollision( false ),
      bInfAllButCollision( false ) {
    // FUN_0073eb10 (inlined)
}

bool MissionExtraChallenge::executeAction( int32_t action,
                                           void *pSource,
                                           void * /*pData*/ ) {
    // FUN_0073dd70
    if ( action != CE_Ended ) {
        return false;
    }

    if ( pSource == pOutroCinematic ) {
        pOutroCinematic = nullptr;
        if ( pLoopCinematic != nullptr ) {
            Matrix4x4f_t frame = outroFrame;
            frame.row( 3 ).head<3>() -= GetWorldShift().head<3>().transpose();
            pLoopCinematic->setTransformOffset( frame );
            pLoopCinematic->start( true );
        }
    }
    return true;
}

bool MissionExtraChallenge::onPenalty( const float /*remainingRatio*/,
                                       const float pendingRatio ) {
    // FUN_0073d2d0
    switch ( pChallenge->getType() ) {
    case eEntityType::ET_HIHI:
        static_cast<EntityAvatarHitchHiker *>( pPassenger )
            ->reactToPenalty( pendingRatio );
        break;
    case eEntityType::ET_SHOG:
        static_cast<EntityAvatarShoppingGirl *>( pPassenger )
            ->reactToPenalty( pendingRatio );
        break;
    default:
        break;
    }
    return true;
}

void MissionExtraChallenge::allocatePassenger( const int32_t avatarType ) {
    // FUN_0073fe80 (inlined)
    EntityAvatar *pAvatar = gpScenaric->getAvatarManager().allocateAvatar(
        kPassengerAvatarHashcode, avatarType );
    PooledAvatarEntry *pResource =
        ( pAvatar != nullptr ) ? pAvatar->getPooledEntry() : nullptr;
    if ( pResource != nullptr ) {
        if ( pResource->ActiveClotheSet == kNoClotheSet ) {
            gpAvatar->bakeAvatar( pAvatar, &pChallenge->getAvatarConfig(), 0,
                                  false, false );
        } else if ( pResource->ActiveClotheSet != 0 ) {
            gpAvatar->installClothes( pAvatar, &pChallenge->getAvatarConfig(),
                                      0, false, false );
        }
    }
    pPassenger = static_cast<EntityAvatarPassenger *>( pAvatar );
}

void MissionExtraChallenge::configureTrafficAndCops() {
    // FUN_0073eda0
    EventParameters *pEventParameters = pOwner->getMissionDescription();
    pEventParameters->getCops().setValue( 1, false );
    pEventParameters->getCopsDensity().setValue( -1, false );
    pEventParameters->getTraffic().setValue( 1, false );
    pEventParameters->getTrafficDensity().setValue( -1, false );
    gpAI->setDisableCops( false );
    OTDU_UNIMPLEMENTED; // TODO: ONLINE (GSAI bOnlineSession DAT_013d1a74 = 0)
}

bool MissionExtraChallenge::findRouteEntities() {
    // FUN_0073d1a0
    for ( int32_t i = 0; i < pMngActor->getActorCount(); i++ ) {
        Entity *pActor = pMngActor->getActor( i );
        switch ( pActor->getType() ) {
        case eEntityType::ET_CHPT: {
            EntityCheckPoint *pCheckpoint =
                static_cast<EntityCheckPoint *>( pActor );
            if ( pCheckpoint->getLEDData()->Type == 1 ) {
                pStartCheckpoint = pCheckpoint;
            } else if ( pCheckpoint->getLEDData()->Type == 2 ) {
                pFinishCheckpoint = pCheckpoint;
            }
            break;
        }
        case eEntityType::ET_CARP:
            pCarSpawn = pActor;
            break;
        default:
            break;
        }
    }
    return pStartCheckpoint != nullptr && pFinishCheckpoint != nullptr &&
           pCarSpawn != nullptr;
}

void MissionExtraChallenge::placeRouteEntities() {
    // FUN_0073df40
    if ( !findRouteEntities() ) {
        return;
    }

    Matrix4x4f_t startFrame;
    Matrix4x4f_t endFrame;
    Matrix4x4f_t startFrameCopy;
    pChallenge->computeRoadRoute( startFrame, endFrame, startFrameCopy,
                                  nullptr );
    pStartCheckpoint->propagateTransform( startFrame, true );
    pFinishCheckpoint->propagateTransform( endFrame, true );
    pCarSpawn->propagateTransform( startFrameCopy, true );
}

void MissionExtraChallenge::setup( MngActor *pActorManager,
                                   EntityExtraChallenge *pNewChallenge ) {
    // FUN_0073fe80
    pMngActor = pActorManager;
    pChallenge = pNewChallenge;
    gpScenaric->getExtraChallengesCineBank().load(
        ExtraChallengeCineBank::ECBM_Outro );
    pOwner->setGameMechType( GameMechanism::MT_ExtraChallenge );
    pOwner->setGameMechID( kSoloSpotType );

    EventParameters *pEventParameters = pOwner->getMissionDescription();
    if ( pNewChallenge->getTimeLimit() <= 0.0f ) {
        pEventParameters->getCountdownMode().setValue( 0, false );
        pEventParameters->getGoldTime().setValue( 0, false );
    } else {
        pEventParameters->getCountdownMode().setValue( 1, false );
        pEventParameters->getGoldTime().setValue(
            int32_t( pNewChallenge->getTimeLimit() * kMillisecondsPerSecond ),
            false );
    }
    pEventParameters->getOffRoad().setValue( 0, false );
    pEventParameters->getCollisions().setValue( 0, false );

    configureTrafficAndCops();
    placeRouteEntities();

    bSavedInfAllButCollision = gAIConfig.bInfAllButCollision;
    pPassenger = nullptr;
    pCargo = nullptr;

    switch ( pChallenge->getType() ) {
    case eEntityType::ET_CVOY: {
        EntityConvoy *pConvoy = static_cast<EntityConvoy *>( pChallenge );
        pEventParameters->getOffRoad().setValue( 1, false );
        pEventParameters->getCollisions().setValue( 1, false );
        pEventParameters->getPenaltyCredits().setValue(
            int32_t( uint32_t( float( pConvoy->getOffRoad() ) *
                               kPenaltyCreditsScale ) ),
            false );
        gpPlayerData->getLoadedSave()->getGarage()->beginTestDrive(
            pConvoy->getCarID(), int32_t( pConvoy->getCarColorID() ),
            int32_t( pConvoy->getCarRimID() ),
            int32_t( pConvoy->getCarInteriorColorID() ) );
        ReleaseLocalPlayerCar();
        bInfAllButCollision = false;
        return;
    }
    case eEntityType::ET_SHOG: {
        EntityShoppingGirl *pShoppingGirl =
            static_cast<EntityShoppingGirl *>( pChallenge );
        pEventParameters->getOffRoad().setValue( 1, false );
        pEventParameters->getCollisions().setValue( 1, false );
        pEventParameters->getPenaltyCredits().setValue(
            int32_t( uint32_t( float( pShoppingGirl->getOffRoad() ) *
                               kPenaltyCreditsScale ) ),
            false );
        allocatePassenger( kShoppingGirlAvatarType );
        if ( pPassenger != nullptr ) {
            static_cast<EntityAvatarShoppingGirl *>( pPassenger )
                ->initializeVoice( pChallenge->getAvatarVoiceType() );
        }
        bInfAllButCollision = true;
        return;
    }
    case eEntityType::ET_HIHI: {
        EntityHitchHiker *pHitchHiker =
            static_cast<EntityHitchHiker *>( pChallenge );
        pEventParameters->getOffRoad().setValue( 1, false );
        pEventParameters->getCollisions().setValue( 1, false );
        pEventParameters->getPenaltyCredits().setValue(
            int32_t( uint32_t( float( pHitchHiker->getOffRoad() ) *
                               kPenaltyCreditsScale ) ),
            false );
        allocatePassenger( kHitchHikerAvatarType );
        if ( pPassenger != nullptr ) {
            static_cast<EntityAvatarHitchHiker *>( pPassenger )
                ->initializeVoice( pChallenge->getAvatarVoiceType() );
        }
        bInfAllButCollision = true;
        return;
    }
    case eEntityType::ET_TRAN: {
        Entity *pGoodies =
            static_cast<EntityTransporter *>( pChallenge )->getGoodies();
        if ( pGoodies != nullptr ) {
            pCargo = static_cast<EntityPassengerGoodies *>(
                pMngActor->instanciateActor( pGoodies ) );
        }
        bInfAllButCollision = true;
        return;
    }
    default:
        bInfAllButCollision = true;
        return;
    }
}

void MissionExtraChallenge::start() {
    // FUN_0073ee20
    pOwner->getGameMech()->setPenaltyListener( this );

    LocalPlayer &localPlayer = gpScenaric->getLocalPlayer();
    if ( localPlayer.getCarState() != nullptr ) {
        gpCarState->setPoliceStateJob( localPlayer.getCarState(),
                                       &localPlayer.getInfringementCheck() );
    }
    if ( pPassenger != nullptr ) {
        pPassenger->resetVoice();
        gpScenaric->placePassenger( localPlayer.getCarState(), pPassenger );
    } else if ( pCargo != nullptr ) {
        gpScenaric->placeCargo( localPlayer.getCarState(), pCargo );
    }

    gAIConfig.bInfAllButCollision = bInfAllButCollision;
    field_0x7d = false;
    gpAI->setDisableBots( true );
    gpAI->setBotsActive( false );
    gpAI->getAIPool()->removeBots();

    if ( pChallenge->getType() == eEntityType::ET_TRAN ) {
        IPlayerData *pSave = gpPlayerData->getLoadedSave();
        Driver *pDriver = pSave->getDriver();
        const uint32_t deposit =
            static_cast<EntityTransporter *>( pChallenge )->getDeposit();
        pSave->getExtraChallenges()->addCounter( CounterIndex( 0xf0dc ),
                                                 int32_t( deposit ) );
        pDriver->setMoneyAndNotify( int32_t( pDriver->getMoney() - deposit ) );
    }
}

void MissionExtraChallenge::stopCinematics() {
    // FUN_0073d9d0
    if ( pOutroCinematic != nullptr ) {
        if ( pOutroCinematic->isPlaying() ) {
            pOutroCinematic->stop( true );
        }
        pOutroCinematic = nullptr;
    }
    if ( pLoopCinematic != nullptr ) {
        if ( pLoopCinematic->isPlaying() ) {
            pLoopCinematic->stop( true );
        }
        pLoopCinematic = nullptr;
    }
    gpPhysics->setPlayerSlowDownForced( false );
    ResetAIPoolForPlayer();
}

void MissionExtraChallenge::AlignFrameOnRoad( Matrix4x4f_t &frame ) {
    // FUN_0073da50
    AiRoadMap::Road *pRoad =
        gpAI->getRoadMap().findNearestRoadExhaustive( frame, true );
    if ( pRoad == nullptr ) {
        return;
    }

    AiRoadIt it;
    ProjectOnRoadShape( frame, it, nullptr, false, pRoad->getShape() );
    const AiRoadProfile *pProfile = it.getEntity()->pProfile;

    float lateral = 0.0f;
    float height = 0.0f;
    pProfile->computeLaneOffset( it.bWay, 0, lateral, height );
    const float widthRatio = lateral / ( pProfile->InnerWidth * 0.5f );
    if ( it.T == 0.0f ) {
        it.T = kRoadStartT;
    } else if ( it.T == 1.0f ) {
        it.T = kRoadEndT;
    }
    pProfile->computeWidthOffset( widthRatio, lateral, height );

    const Eigen::Vector4f position =
        it.computeOffsetPosition( lateral, height );
    const Eigen::Vector4f direction = it.computeDirection();
    frame = MakeRotationMatY( -atan2f( -direction.x(), direction.z() ) );
    frame.row( 3 ) = position.transpose();
}

void MissionExtraChallenge::end( const bool bWon ) {
    // FUN_007405c0
    pOutroCinematic = nullptr;
    pLoopCinematic = nullptr;
    if ( pPassenger != nullptr ) {
        pPassenger->resetVoice();
    }

    CarState *pLocalCar = gpScenaric->getLocalPlayer().getCarState();
    if ( pLocalCar != nullptr ) {
        gpCarState->setPoliceStateJob( pLocalCar, nullptr );
    }
    // NOTE: Retail resets the infringement through a null car when there is
    // no local car.
    if ( pLocalCar != nullptr ) {
        pLocalCar->getPoliceState().reset();
    }

    const char *pOutroName = nullptr;
    const char *pLoopName = nullptr;
    if ( bWon ) {
        outroFrame = pFinishCheckpoint->getModelMatrix();
        AlignFrameOnRoad( outroFrame );
        pFinishCheckpoint->setVisible( false );
        switch ( pChallenge->getType() ) {
        case eEntityType::ET_SHOG:
            pOutroName = "C_WIN_SG";
            pLoopName = "C_WIN_L";
            break;
        case eEntityType::ET_CVOY:
            pOutroName = "C_WIN_CONVOY";
            pLoopName = "C_CONVOY_WIN_L";
            break;
        case eEntityType::ET_HIHI:
            pOutroName = "C_WIN_HH";
            pLoopName = "C_WIN_L";
            break;
        case eEntityType::ET_TRAN:
            pOutroName = "C_WIN_TRANS";
            pLoopName = "C_TRANS_WIN_L";
            break;
        default:
            break;
        }
        if ( pOutroName != nullptr ) {
            pOutroCinematic = FindExtraChallengeCinematic( pOutroName );
            pLoopCinematic = FindExtraChallengeCinematic( pLoopName );
        }
    } else {
        switch ( pChallenge->getType() ) {
        case eEntityType::ET_SHOG:
            pOutroCinematic = FindExtraChallengeCinematic( "C_LOOSE_SG" );
            break;
        case eEntityType::ET_CVOY:
            pOutroCinematic = FindExtraChallengeCinematic( "C_LOOSE_CONVOY" );
            bTeleportToStart = true;
            break;
        case eEntityType::ET_HIHI:
            pOutroCinematic = FindExtraChallengeCinematic( "C_LOOSE_HH" );
            break;
        case eEntityType::ET_TRAN:
            pOutroCinematic = FindExtraChallengeCinematic( "C_LOOSE_TRANS" );
            bTeleportToStart = true;
            break;
        default:
            break;
        }
        pLoopCinematic = FindExtraChallengeCinematic( "C_LOOSE_L" );

        PhysicsVehicle *pVehicle =
            ( pLocalCar != nullptr ) ? pLocalCar->getPhysicsVehicle() : nullptr;
        if ( pVehicle == nullptr ) {
            outroFrame = gpWorld->getPreviousTransform();
        } else {
            {
                PhysicsLock lock;
                outroFrame = pVehicle->getChassisWorldMatrix(); // FUN_0082e1e0
            }
            AiRoadLocation location;
            ProjectOnRoad( outroFrame.row( 3 ).transpose(), nullptr, nullptr,
                           false, &location );
            outroFrame = location.Frame;
        }
        AlignFrameOnRoad( outroFrame );
    }

    if ( pOutroCinematic == nullptr ) {
        cleanup();
        return;
    }

    gpPhysics->setPlayerSlowDownForced( true );
    pOutroCinematic->setGameModeInterface( this );
    Matrix4x4f_t shiftedFrame = outroFrame;
    shiftedFrame.row( 3 ).head<3>() -= GetWorldShift().head<3>().transpose();
    pOutroCinematic->setTransformOffset( shiftedFrame );
    pOutroCinematic->setRestoreCineMix( false );
    pOutroCinematic->start( true );

    if ( pLoopCinematic != nullptr ) {
        pLoopCinematic->setGameModeInterface( this );
        pLoopCinematic->setLooping( true );
        pLoopCinematic->setTransformOffset( outroFrame );
        pLoopCinematic->createTDUInterface();
    }
    gpAI->setTrafficCategories( false, 2 );
}

void MissionExtraChallenge::cleanup() {
    // FUN_0073fb80
    if ( pChallenge == nullptr ) {
        return;
    }

    switch ( pChallenge->getType() ) {
    case eEntityType::ET_SHOG:
    case eEntityType::ET_HIHI:
        if ( pPassenger != nullptr ) {
            gpScenaric->unloadPassenger(
                gpScenaric->getLocalPlayer().getCarState(), pPassenger );
            gpScenaric->getAvatarManager().releaseAvatar( pPassenger );
            pPassenger = nullptr;
        }
        bTeleportToStart = false;
        break;
    case eEntityType::ET_CVOY: {
        gpPlayerData->getLoadedSave()->getGarage()->endRental();

        LocalPlayer &localPlayer = gpScenaric->getLocalPlayer();
        EntityDriver *pDriver = nullptr;
        if ( localPlayer.getCarState() != nullptr ) {
            pDriver = localPlayer.getCarState()->getDriver();
            localPlayer.getCarState()->unloadDriver();
            ReleaseLocalPlayerCar();
        }

        VehicleSpawnParams params;
        params.PhantomMode = 1;
        if ( bTeleportToStart ) {
            params.Transform = pStartCheckpoint->getModelMatrix();
            params.bSetPosition = true;
            params.bCastToGround = true;
            params.bResetCar = true;
        }
        CarDesc *pCurrentCar = gpPlayerData->getCurrentCar();
        CarState *pCarState = gpCarState->spawnVehicle(
            int32_t( pCurrentCar->getCarConfigID() ), 0, pCurrentCar->rimID,
            pCurrentCar->packHashcode, params, -1, 0, VIT_Player );
        localPlayer.setCarState( pCarState );
        gpScenaric->placeDriver( pCarState, pDriver );
        gpPlayerData->applyDrivingOptions();
        bTeleportToStart = true;
        break;
    }
    case eEntityType::ET_TRAN:
        if ( pCargo != nullptr ) {
            gpScenaric->unloadCargo( gpScenaric->getLocalPlayer().getCarState(),
                                     pCargo );
            MngActor::Uninstanciate( pMngActor, pCargo );
            pCargo = nullptr;
        }
        bTeleportToStart = true;
        break;
    default:
        break;
    }

    if ( bTeleportToStart ) {
        Matrix4x4f_t frame = pStartCheckpoint->getModelMatrix();
        AlignFrameOnRoad( frame );
        gpWorld->setPreviousTransform( frame );
        frame.row( 3 ).head<3>() -= GetWorldShift().head<3>().transpose();
        CarState *pLocalCar = gpScenaric->getLocalPlayer().getCarState();
        if ( pLocalCar != nullptr ) {
            pLocalCar->getBody()->WorldMatrix = frame;
        }
    }

    if ( pPassenger != nullptr ) {
        gpScenaric->getAvatarManager().releaseAvatar( pPassenger );
        pPassenger = nullptr;
    }
}

void MissionExtraChallenge::terminate() {
    // FUN_0073fdf0
    LocalPlayer &localPlayer = gpScenaric->getLocalPlayer();
    if ( localPlayer.getCarState() != nullptr ) {
        gpCarState->setPoliceStateJob( localPlayer.getCarState(),
                                       &localPlayer.getInfringementCheck() );
    }
    gAIConfig.bInfAllButCollision = bSavedInfAllButCollision;
    stopCinematics();
    gpCamera->restoreCamera();
    cleanup();
    pChallenge = nullptr;
    ResetAIPoolForPlayer();
}

bool MissionExtraChallenge::isSucceeded() const {
    // FUN_0073d220
    switch ( pChallenge->getType() ) {
    case eEntityType::ET_HIHI:
        return static_cast<const EntityAvatarHitchHiker *>( pPassenger )
            ->isVoicePlaying();
    case eEntityType::ET_SHOG:
        return static_cast<const EntityAvatarShoppingGirl *>( pPassenger )
            ->isVoicePlaying();
    default:
        return false;
    }
}
