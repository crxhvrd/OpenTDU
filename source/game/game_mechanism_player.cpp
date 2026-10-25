#include "shared.h"
#include "game_mechanism_player.h"

#include "game/scenaric/entity/entity_extra_challenge.h"

#include "game/freeride/gm_freeride.h"
#include "game/scenaric/entity/base_entity.h"
#include "game/scenaric/entity/entity_factory.h"
#include "game/scenaric/entity/entity_spot_mesh.h"
#include "game/scenaric/gs_scenaric.h"
#include "game/scenaric/local_player.h"
#include "game/scenaric/spots.h"
#include "game/vehicle/car_state.h"
#include "game/vehicle/vehicle_base.h"
#include "ai/gs_ai.h"
#include "core/time.h"
#include "gs_timer.h"
#include "physics/gs_physics.h"
#include "physics/vehicle/physics_vehicle.h"
#include "render/camera/gs_camera.h"
#include "world/gs_world.h"
#include "world/mng_world.h"
#include "game/scenaric/entity/entity_driver.h"
#include "player_data/gs_playerdata.h"
#include "player_data/player_data_interface.h"
#include "player_data/saved_garage.h"

// DAT_00f97070
static const char *gpSpotDisplayTypeNames[PlayerGameMech::SDT_Count] = {
    "solo",
    "multi",
    "club",
    "GMT_Error",
};

// DAT_00f97080
static const char *gpSpotDisplayNames[PlayerGameMech::SDN_Count] = {
    "timeAttack", "speed",     "driving", "race",        "bikeDealer",
    "carDealer",  "carRental", "carWash", "estates",     "goodies",
    "tuning",     "home",      "forsale", "club",        "driveIn",
    "carPaint",   "sgirl",     "hiker",   "transporter", "convoy",
    "carPaint_d",
};

const char *GetSpotDisplayTypeName( const int32_t index ) {
    return gpSpotDisplayTypeNames[index];
}

const char *GetSpotDisplayName( const int32_t index ) {
    return gpSpotDisplayNames[index];
}

int32_t GetSpotChallengeKind( const SpotMap *pSpot ) {
    // FUN_007d69b0
    if ( pSpot == nullptr || pSpot->Type != ST_Challenge ) {
        return -1;
    }

    const int32_t kind = pSpot->SubType >> 4;
    return kind == 0xf ? -1 : kind;
}

int32_t GetSpotGameModeID( const SpotMap *pSpot ) {
    // FUN_007d6980
    if ( pSpot == nullptr ) {
        return 0xc0c3;
    }

    if ( pSpot->Type == 6 ) {
        return 0xc0c0;
    }
    if ( pSpot->Type == ST_Challenge ) {
        return 0xc0c0 + ( pSpot->SubType & 0xf );
    }
    return 0xc0c3;
}

void GetSpotDisplayIndices( const SpotMap *pSpot,
                            int32_t *pOutTypeIndex,
                            int32_t *pOutNameIndex ) {
    // FUN_007d7760
    *pOutTypeIndex = PlayerGameMech::SDT_Solo;
    *pOutNameIndex = -1;

    if ( pSpot == nullptr ) {
        return;
    }

    switch ( pSpot->Type ) {
    case ST_House: {
        bool bOwned = false;
        if ( gpPlayerData != nullptr &&
             gpPlayerData->getLoadedSave() != nullptr ) {
            Garage *pGarage = gpPlayerData->getLoadedSave()->getGarage();
            bOwned =
                pGarage != nullptr && pGarage->isHouseOwned( pSpot->Hashcode );
        }
        *pOutNameIndex =
            bOwned ? PlayerGameMech::SDN_Home : PlayerGameMech::SDN_ForSale;
    } break;
    case 1:
        *pOutNameIndex = PlayerGameMech::SDN_CarDealer;
        break;
    case 3:
        *pOutNameIndex = PlayerGameMech::SDN_Goodies;
        break;
    case 4:
        *pOutNameIndex = PlayerGameMech::SDN_Tuning;
        break;
    case 5:
        *pOutNameIndex = PlayerGameMech::SDN_Club;
        break;
    case 6:
        *pOutNameIndex = PlayerGameMech::SDN_DriveIn;
        break;
    case ST_Challenge: {
        const int32_t kind = GetSpotChallengeKind( pSpot );
        if ( kind == 1 ) {
            *pOutNameIndex = PlayerGameMech::SDN_Speed;
        } else if ( kind == 3 ) {
            *pOutNameIndex = PlayerGameMech::SDN_Race;
        } else {
            *pOutNameIndex = PlayerGameMech::SDN_TimeAttack;
        }

        const int32_t gameModeID = GetSpotGameModeID( pSpot );
        if ( gameModeID == 0xc0c1 ) {
            *pOutTypeIndex = PlayerGameMech::SDT_Multi;
        } else if ( gameModeID == 0xc0c2 ) {
            *pOutTypeIndex = PlayerGameMech::SDT_Club;
        } else {
            *pOutTypeIndex = PlayerGameMech::SDT_Solo;
        }
    } break;
    case ST_Unknown8:
        switch ( pSpot->SubType ) {
        case 0:
            *pOutNameIndex = PlayerGameMech::SDN_CarWash;
            break;
        case 1:
            *pOutNameIndex = PlayerGameMech::SDN_CarPaint;
            break;
        case 2:
        case 3:
            *pOutNameIndex = PlayerGameMech::SDN_CarRental;
            break;
        case 4:
            *pOutNameIndex = PlayerGameMech::SDN_Estates;
            break;
        case 9:
            *pOutNameIndex = PlayerGameMech::SDN_CarPaintDeluxe;
            break;
        }
        break;
    default:
        *pOutNameIndex = PlayerGameMech::SDN_BikeDealer;
        break;
    }
}

void GetExtraChallengeDisplayIndices( const Entity *pEntity,
                                      int32_t *pOutTypeIndex,
                                      int32_t *pOutNameIndex ) {
    // FUN_007dc930
    *pOutTypeIndex = PlayerGameMech::SDT_Solo;
    *pOutNameIndex = -1;

    if ( pEntity == nullptr ) {
        return;
    }

    switch ( pEntity->getType() ) {
    case eEntityType::ET_SHOG:
        *pOutNameIndex = PlayerGameMech::SDN_ShoppingGirl;
        break;
    case eEntityType::ET_CVOY:
        *pOutNameIndex = PlayerGameMech::SDN_Convoy;
        break;
    case eEntityType::ET_HIHI:
        *pOutNameIndex = PlayerGameMech::SDN_Hiker;
        break;
    case eEntityType::ET_TRAN:
        *pOutNameIndex = PlayerGameMech::SDN_Transporter;
        break;
    default:
        break;
    }
}

PlayerGameMech::PlayerGameMech()
    : GameMechanism( 4, 0xc0c3 ),
      currentActivityHashcode( 0ull ),
      pCurrentSpot( nullptr ),
      pCurrentSpotEntity( nullptr ),
      spotDisplayTypeIndex( -1 ),
      spotDisplayNameIndex( -1 ),
      pCurrentExtraChallenge( nullptr ),
      spawnFrame( Matrix4x4f_t::Identity() ),
      loadingMapStartTime( 0.0f ),
      botsReenableCountdown( -1 ) {
    // FUN_00737730
    OTDU_UNIMPLEMENTED; // TODO: GameLogic
}

PlayerGameMech::~PlayerGameMech() {}

void PlayerGameMech::clearCurrentActivity() {
    // FUN_00738220 (inlined)
    spotDisplayTypeIndex = -1;
    spotDisplayNameIndex = -1;
    pCurrentExtraChallenge = nullptr;
    pCurrentSpot = nullptr;
}

bool PlayerGameMech::onMessage( Entity *pSender,
                                const int32_t eventCode,
                                void *pParam ) {
    // FUN_00738220
    switch ( eventCode ) {
    case ME_CheckpointPassed:
        return true;

    case ME_SpotEntered: {
        if ( pSender == nullptr ) {
            return true;
        }

        SpotMap *pSpot =
            static_cast<EntitySpotMesh *>( pSender )->getSpotWorld();
        if ( pSpot == nullptr ) {
            return true;
        }

        if ( currentActivityHashcode != pSpot->Hashcode ||
             spotDisplayNameIndex == -1 ) {
            currentActivityHashcode = pSpot->Hashcode;
            pCurrentSpotEntity = pSender;

            GetSpotDisplayIndices( pSpot, &spotDisplayTypeIndex,
                                   &spotDisplayNameIndex );

            pCurrentSpot = pSpot;
            pCurrentExtraChallenge = nullptr;
        }
        return true;
    }

    case ME_ExtraChallengeEntered: {
        CarState *pLocalCar = ( gpScenaric != nullptr )
                                  ? gpScenaric->getLocalPlayer().getCarState()
                                  : nullptr;
        if ( pLocalCar == nullptr || pLocalCar->getPoliceState().isChased() ||
             pLocalCar->getPoliceState().isArrested() ) {
            return true;
        }

        const uint64_t challengeKey =
            static_cast<uint64_t>( reinterpret_cast<uintptr_t>( pSender ) );
        if ( currentActivityHashcode != challengeKey ||
             spotDisplayNameIndex == -1 ) {
            currentActivityHashcode = challengeKey;

            GetExtraChallengeDisplayIndices( pSender, &spotDisplayTypeIndex,
                                             &spotDisplayNameIndex );

            pCurrentExtraChallenge = pSender;
            pCurrentSpot = nullptr;
        }
        return true;
    }

    case ME_SpotLeft:
    case ME_SpotUnloaded:
        clearCurrentActivity();
        return true;

    default:
        return GameMechanism::onMessage( pSender, eventCode, pParam );
    }
}

static constexpr int32_t kBotsReenableDelay = 210;

bool PlayerGameMech::stateProcess( const int32_t state,
                                   const float deltaTime,
                                   const eStateProcess process ) {
    // FUN_00737d50
    if ( pComponents[CS_Player] != nullptr ) {
        pComponents[CS_Player]->onMechStateProcess( state, deltaTime, process );
    }

    if ( !gpPlayerData->isOnline() || !gpPlayerData->getOnlineOp( 0 ) ) {
        if ( botsReenableCountdown >= 0 ) {
            if ( botsReenableCountdown == 0 ) {
                gpAI->setDisableBots( false );
            } else {
                botsReenableCountdown--;
            }
        }
    }

    switch ( state ) {
    case MS_FR_LoadingMap:
        return processLoadingMap( process );
    case MS_FR_Playing:
        return processPlaying( process );
    case MS_FR_LogginIn:
    case MS_FR_LogginIn_Retrying:
        OTDU_UNIMPLEMENTED_ONCE; // TODO: ONLINE
        return process == SP_Init || process == SP_Update;
    default:
        return false;
    }
}

bool PlayerGameMech::processLoadingMap( const eStateProcess process ) {
    // FUN_00737d50 (inlined)
    CarState *pLocalCar = gpScenaric->getLocalPlayer().getCarState();
    PhysicsVehicle *pPhysicsVehicle =
        ( pLocalCar != nullptr ) ? pLocalCar->getPhysicsVehicle() : nullptr;

    switch ( process ) {
    case SP_Init: {
        loadingMapStartTime = GetTimeSeconds();
        if ( pPhysicsVehicle != nullptr ) {
            PhysicsLock physLock;
            pPhysicsVehicle->setPostponedPause( 1 );
        }

        Eigen::Vector4f streamingPosition = spawnFrame.row( 3 ).transpose();
        gpWorld->setStreamingPosition( &streamingPosition );
        gpMngWorld->setDisableStreamingOnCar( true );
        return false;
    }
    case SP_Update: {
        const Eigen::Vector4f spawnPosition = spawnFrame.row( 3 ).transpose();
        if ( !gpWorld->isStreamedAround( &spawnPosition, true, true ) ||
             pLocalCar == nullptr ) {
            return true;
        }

        pLocalCar->resetBackOnTrack();
        if ( pPhysicsVehicle == nullptr || !pPhysicsVehicle->isSimulated() ) {
            return true;
        }

        pPhysicsVehicle->resetToTransform( spawnFrame, true );
        if ( pPhysicsVehicle->isPaused() ) {
            PhysicsLock physLock;
            pPhysicsVehicle->setPostponedPause( 2 );
        }

        if ( pPhysicsVehicle->isKeyframed() ) {
            // FUN_0082e370
            PhysicsLock physLock;
            pPhysicsVehicle->setKeyframed( false, false );
        }

        gpMngWorld->setDisableStreamingOnCar( false );
        stateChange( MS_FR_Playing );
        return true;
    }
    case SP_Term:
        if ( pLocalCar != nullptr &&
             pLocalCar->getVehicleInstance() != nullptr ) {
            pLocalCar->getVehicleInstance()->setShouldDraw( true );
            EntityDriver *pDriver = pLocalCar->getDriver();
            if ( pDriver != nullptr ) {
                pDriver->addInstanceReference();
            }
        }
        return true;
    }
    return false;
}

bool PlayerGameMech::processPlaying( const eStateProcess process ) {
    // FUN_00737d50 (inlined)
    switch ( process ) {
    case SP_Init:
        botsReenableCountdown = kBotsReenableDelay;
        OTDU_UNIMPLEMENTED_ONCE; // TODO: ONLINE
        return true;
    case SP_Update:
        OTDU_UNIMPLEMENTED_ONCE; // TODO: ONLINE
        gpAI->setDisableCops( false );
        return true;
    default:
        return false;
    }
}

bool PlayerGameMech::terminate() {
    // FUN_00737970
    gpCamera->resetVehicleCameraSelection();
    OTDU_UNIMPLEMENTED_ONCE; // TODO: SCENARIC (driver avatar +0x1c18 counter)
    OTDU_UNIMPLEMENTED_ONCE; // TODO: ONLINE (spectator reset, free ride
                             // session scan, unregister the P2P listener)
    gpCamera->setMouseCam( true );
    return GameMechanism::terminate();
}

const char *PlayerGameMech::getDisplayTypeName() const {
    // FUN_0042c4e0
    if ( spotDisplayTypeIndex != -1 ) {
        return gpSpotDisplayTypeNames[spotDisplayTypeIndex];
    }
    return gpSpotDisplayTypeNames[0];
}

const char *PlayerGameMech::getDisplayName() const {
    // FUN_0042c4c0
    if ( spotDisplayNameIndex != -1 ) {
        return gpSpotDisplayNames[spotDisplayNameIndex];
    }
    return gpSpotDisplayNames[0];
}

bool PlayerGameMech::enterCurrentActivity() {
    // FUN_007381c0
    if ( pCurrentSpot != nullptr ) {
        if ( pOwner != nullptr && pOwner->getGameMechType() == 4 ) {
            static_cast<GMFreeride *>( pOwner )->enterSpot( pCurrentSpot );
        }
        return true;
    }

    if ( pCurrentExtraChallenge != nullptr ) {
        if ( pOwner != nullptr && pOwner->getGameMechType() == 4 ) {
            static_cast<GMFreeride *>( pOwner )->enterExtraChallenge(
                static_cast<EntityExtraChallenge *>( pCurrentExtraChallenge ) );
        }
        return true;
    }

    return false;
}
