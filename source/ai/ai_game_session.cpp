#include "shared.h"
#include "ai_game_session.h"

#include "ai_bot_manager.h"
#include "ai_config.h"
#include "ai_pool.h"
#include "ai_vehicle.h"
#include "ai_veh_behaviour.h"
#include "ai_race.h"
#include "gs_ai.h"
#include "game/game_mechanism.h"
#include "game/mission/gm_mission.h"
#include "game/scenaric/gs_scenaric.h"
#include "game/scenaric/local_player.h"
#include "game/scenaric/avatar_manager.h"
#include "game/scenaric/entity/entity_avatar.h"
#include "game/scenaric/entity/entity_driver.h"
#include "game/avatar/gs_avatar.h"
#include "game/vehicle/gs_car_state.h"
#include "game/vehicle/vehicle_base.h"
#include "audio/vehicle/gs_vehicle_sound.h"
#include "database/gs_database.h"
#include "database/db_bot.h"
#include "game/vehicle/car_state.h"
#include "physics/gs_physics.h"
#include "physics/vehicle/physics_vehicle.h"
#include "player_data/driver.h"

static constexpr int32_t kRaceGameType = 3;
static constexpr int32_t kOpponentMissionType = 5;
static constexpr int32_t kOpponentAILevel = 100;
static constexpr int32_t kRaceMessageStartPositions = 6;
static constexpr int32_t kOpponentAvatarType = 3;
static constexpr int32_t kBikeClotheSet = 1;

int32_t GetNumOpponents( MissionInfo *pMissionInfo ) {
    // FUN_00801260
    if ( gpScenaric->isTrainingLevel() ||
         pMissionInfo->pOwner->getGameMechType() != kRaceGameType ) {
        return 0;
    }
    if ( gAIConfig.NumOpponentsOverride >= 0 ) {
        return gAIConfig.NumOpponentsOverride;
    }
    if ( gpScenaric->isInstantChallenge() &&
         gpScenaric->getICEChallengers()[0].bBot ) {
        return 1;
    }
    return int32_t( pMissionInfo->pOwner->getMissionDescription()
                        ->getNumOpponents()
                        .getValue() );
}

static const MissionInfo::Participant &
GetMissionParticipant( MissionInfo *pMissionInfo, const int32_t index ) {
    const int32_t lastIndex = std::max( pMissionInfo->NumEntries - 1, 0 );
    return pMissionInfo->Participants[std::clamp( index, 0, lastIndex )];
}

static void GetParticipantName( MissionInfo *pMissionInfo,
                                char *pOutName,
                                const int32_t nameLength,
                                const int32_t index ) {
    // FUN_008011e0
    if ( index == 0 ) {
        strncpy( pOutName, gpScenaric->getLocalPlayer().getDriver()->Nickname,
                 nameLength );
        pOutName[nameLength - 1] = '\0';
        return;
    }

    // FUN_0090c1d0
    const DBEntryBot *pBot =
        GetBotDBEntry( GetMissionParticipant( pMissionInfo, index ).BotID );
    if ( nameLength >= GameParticipantOffline::kNameLength &&
         pBot != nullptr ) {
        strncpy( pOutName, pBot->FirstName.pAsString,
                 GameParticipantOffline::kNameLength );
        return;
    }
    snprintf( pOutName, nameLength, "Opponent %02d", index ); // DAT_00f2a8d8
}

GameParticipantOffline::GameParticipantOffline()
    : GameParticipant(),
      bIsLocalPlayer( false ),
      StartPositionMat( Matrix4x4f_t::Identity() ),
      CarDBID( -1 ),
      CarPaintIndex( -1 ),
      BotID( -1 ),
      pPNJName{},
      OpponentIndex( -1 ),
      AILevel( 0 ),
      bActive( false ),
      bMaster( false ),
      bAddedToWorld( false ),
      pVehicleRsc( nullptr ),
      pCarState( nullptr ),
      bPhysicsInstanciated( false ),
      pName{} {
    // FUN_00807650
    strncpy( pName, "not initialised", kNameLength - 1 );
}

static void ApplyOpponentPaint( CarState *pCarState,
                                const int32_t paintIndex ) {
    // FUN_00807d90 (inlined)
    VehicleBase *pVehicle = pCarState->getVehicleInstance();
    if ( pVehicle != nullptr ) {
        pVehicle->applyPaint( pCarState->getCarID(), ePaintType::PT_Default,
                              paintIndex, 0, 0, nullptr, nullptr, nullptr,
                              nullptr );
    }
}

static void SetOpponentVisible( CarState *pCarState, const bool bVisible ) {
    VehicleBase *pVehicle = pCarState->getVehicleInstance();
    if ( pVehicle != nullptr ) {
        pVehicle->setShouldDraw( bVisible );
        pVehicle->setAlpha( 0.0f );
    }
}

void GameParticipantOffline::addToWorld() {
    // FUN_00807ab0
    if ( pCarState == nullptr || bAddedToWorld || bIsLocalPlayer ) {
        return;
    }

    {
        PhysicsLock lock;
        pCarState->getPhysicsVehicle()->setPostponedPause( 2 );
    }
    if ( pCarState->getAIController() == nullptr ) {
        AiRoadIt startHint;
        gpAI->getRace()->getCheckpointRoadLocation( 1, startHint );
        AiVehicle *pAI = gpAI->getAIPool()->addOpponent(
            pCarState, StartPositionMat, startHint, kOpponentMissionType );
        pCarState->setAIController( pAI );

        if ( pAI != nullptr ) {
            pAI->getDPC().BotID = BotID;
            pAI->getOpponentBehaviour().setOpponentIndex( OpponentIndex );
        }
    }
    if ( pCarState->getVehicleInstance() != nullptr ) {
        pCarState->getVehicleInstance()->setShouldDraw( true );
    }
    if ( pCarState->getDriver() != nullptr ) {
        pCarState->getDriver()->addInstanceReference();
    }
    if ( pCarState->getVehicleSound() != nullptr ) {
        pCarState->getVehicleSound()->Model.resume();
    }
    bAddedToWorld = true;
}

void GameParticipantOffline::removeFromWorld() {
    // FUN_008079b0
    if ( pCarState == nullptr || !bAddedToWorld || bIsLocalPlayer ) {
        return;
    }

    pCarState->getPhysicsVehicle()->setVirtualFlag( false );
    {
        PhysicsLock lock;
        pCarState->getPhysicsVehicle()->setPostponedPause( 1 );
    }
    AiVehicle *pAI = static_cast<AiVehicle *>( pCarState->getAIController() );
    if ( pAI != nullptr ) {
        gpAI->getAIPool()->removeVehicle( pAI, "Opponent Eliminated", false );
    }
    if ( pCarState->getVehicleInstance() != nullptr ) {
        pCarState->getVehicleInstance()->setShouldDraw( false );
    }
    if ( pCarState->getDriver() != nullptr ) {
        pCarState->getDriver()->removeInstanceReference();
    }
    if ( pCarState->getVehicleSound() != nullptr ) {
        pCarState->getVehicleSound()->Model.pause();
    }
    bAddedToWorld = false;
}

void GameParticipantOffline::installDriver() {
    // FUN_00807d90 (inlined)
    const DBEntryBot *pBot = GetBotDBEntry( BotID );
    const DBEntryPNJ *pPNJ =
        ( pBot != nullptr )
            ? static_cast<const DBEntryPNJ *>( pBot->PNJ.pAsRawPointer )
            : nullptr;

    int32_t clotheSet = 0;
    VehicleBaseResources *pResource = pCarState->getResource();
    if ( pResource != nullptr ) {
        DBEntryCarPhysicsData *pCarData =
            gpDatabase->getCarDBEntry( pResource->CarID );
        if ( pCarData != nullptr && gpDatabase->isBike( pCarData ) ) {
            clotheSet = kBikeClotheSet;
        }
    }

    AvatarConfiguration config;
    if ( pPNJ == nullptr ||
         !gpAvatar->applyPNJConfiguration( config, *pPNJ ) ) {
        config = gpAvatar->getDefaultConfiguration( 0 );
    }
    if ( pPNJName[0] != '\0' ) {
        OTDU_UNIMPLEMENTED; // TODO: AVATAR (FUN_008f61d0 PNJ configuration
                            // by name; never set offline)
    }

    EntityAvatar *pAvatar = gpScenaric->getAvatarManager().createAvatar(
        &config, clotheSet, true, ~0ull, false, kOpponentAvatarType );
    if ( pAvatar != nullptr &&
         !gpScenaric->placeDriver( pCarState, pAvatar->getDriver() ) ) {
        gpScenaric->getAvatarManager().releaseAvatar( pAvatar );
    }
}

bool GameParticipantOffline::instantiate( const int32_t missionType ) {
    // FUN_00807d90
    if ( bIsLocalPlayer ) {
        if ( pCarState == nullptr ) {
            pCarState = gpScenaric->getLocalPlayer().getCarState();
            PhysicsVehicle *pVehicle = ( pCarState != nullptr )
                                           ? pCarState->getPhysicsVehicle()
                                           : nullptr;
            if ( pVehicle == nullptr || !pVehicle->isActive() ||
                 bPhysicsInstanciated ) {
                pCarState = nullptr;
            } else {
                bPhysicsInstanciated = true;
                if ( gpScenaric->isTrainingLevel() && gAIConfig.Unknown0xE4 ) {
                    OTDU_UNIMPLEMENTED; // TODO: GSAI (FUN_0091f070 training
                                        // AI driver)
                } else if ( gAIConfig.bAIReplacePlayer ) {
                    OTDU_UNIMPLEMENTED; // TODO: GSAI (AddOpponent on the
                                        // player car, IAREPLACEPLAYER)
                }
            }
        } else {
            bPhysicsInstanciated = true;
        }
    } else {
        if ( pVehicleRsc == nullptr ) {
            VehicleBaseResources *pResource =
                gpCarState->grabFreeResourceEntry();
            if ( !pResource->load( CarDBID, 0, 0, 0, ~0ull, 1,
                                   eVehicleResourceType::VRT_Traffic ) ) {
                gpCarState->releaseResourceEntry( pResource );
                pResource = nullptr;
            }
            pVehicleRsc = pResource;
            if ( pVehicleRsc == nullptr ) {
                return false;
            }
        }

        if ( pCarState == nullptr && pVehicleRsc->isLoaded() ) {
            VehicleSpawnParams params;
            params.bAddPhysicsPaused = true;
            params.PhantomMode = 1;
            pCarState = gpCarState->installVehicle( pVehicleRsc, params,
                                                    VIT_Opponent, -1 );
            if ( pCarState == nullptr ) {
                return false;
            }
            SetOpponentVisible( pCarState, false );
            ApplyOpponentPaint( pCarState, CarPaintIndex );
        }

        if ( pCarState != nullptr && !bPhysicsInstanciated &&
             pCarState->getPhysicsVehicle()->isActive() ) {
            PhysicsVehicle *pVehicle = pCarState->getPhysicsVehicle();
            pVehicle->resetSimulationState();
            pVehicle->setDrivingMode( PhysicsVehicle::DM_Parked );
            SetOpponentVisible( pCarState, true );
            ApplyOpponentPaint( pCarState, CarPaintIndex );

            installDriver();

            {
                PhysicsLock lock;
                pVehicle->resetToTransform( StartPositionMat, true );
            }
            pCarState->getWorldMatrix() = StartPositionMat;

            AiRoadIt startHint;
            gpAI->getRace()->getCheckpointRoadLocation( 1, startHint );
            AiVehicle *pAI = gpAI->getAIPool()->addOpponent(
                pCarState, StartPositionMat, startHint, missionType );
            pCarState->setAIController( pAI );
            if ( pAI != nullptr ) {
                pAI->getDPC().BotID = BotID;
                pAI->getOpponentBehaviour().setOpponentIndex( OpponentIndex );
                pAI->startFade( false );
            } else if ( pCarState->getVehicleInstance() != nullptr ) {
                pCarState->getVehicleInstance()->setAlpha( 1.0f );
            }
            bAddedToWorld = true;
            bPhysicsInstanciated = true;
        }
    }

    if ( bPhysicsInstanciated ) {
        bActive = true;
        if ( !bAddedToWorld ) {
            addToWorld();
        }
        return true;
    }
    bActive = false;
    return false;
}

bool GameParticipantOffline::release() {
    // FUN_00807c00
    BotID = -1;
    OpponentIndex = -1;
    if ( bIsLocalPlayer ) {
        if ( gAIConfig.bAIReplacePlayer ) {
            OTDU_UNIMPLEMENTED; // TODO: GSAI (give the player car back,
                                // phys +0x550 / +0x334 / +0xd34)
        }
        pCarState = nullptr;
        pVehicleRsc = nullptr;
        bPhysicsInstanciated = false;
        return true;
    }

    if ( pCarState != nullptr ) {
        AiVehicle *pAI =
            static_cast<AiVehicle *>( pCarState->getAIController() );
        if ( pAI != nullptr ) {
            gpAI->getAIPool()->removeVehicle( pAI, "RemoveOpponent", false );
        }
        pCarState->setAIController( nullptr );
        EntityDriver *pDriver = pCarState->getDriver();
        if ( pDriver != nullptr ) {
            pCarState->unloadDriver();
            gpScenaric->getAvatarManager().releaseAvatar( pDriver );
        }
        gpCarState->releaseCarState( pCarState, false );
        bPhysicsInstanciated = false;
        pCarState = nullptr;
        bActive = false;
    }
    if ( pVehicleRsc != nullptr ) {
        pVehicleRsc->unload();
        gpCarState->releaseResourceEntry( pVehicleRsc );
        pVehicleRsc = nullptr;
    }
    return pVehicleRsc == nullptr;
}

GameSessionOffline::GameSessionOffline( GSAI *pOwnerAI )
    : GameSession(),
      pMissionInfo( nullptr ),
      pAI( pOwnerAI ),
      pListener( nullptr ),
      capacity( kMaxNumParticipants ),
      numParticipants( 0 ),
      numPlayers( 0 ),
      numOpponents( 0 ),
      participants() {
    // FUN_00807820
}

void GameSessionOffline::setListener( GameMechanism *pNewListener ) {
    // FUN_008075d0
    pListener = pNewListener;
}

bool GameSessionOffline::sendRaceMessage( const AiRaceMessage &message ) {
    // FUN_00807870
    pAI->broadcastRaceMessage( message );
    if ( message.Type != 0 || message.ID != kRaceMessageStartPositions ) {
        return true;
    }

    for ( int32_t i = 0; i < getCapacity(); i++ ) {
        GameParticipantOffline *pParticipant =
            static_cast<GameParticipantOffline *>( getParticipant( i ) );
        if ( pParticipant->isActive() &&
             pParticipant->getCarState() != nullptr ) {
            pParticipant->getCarState()->getBody()->WorldMatrix =
                pParticipant->StartPositionMat;
        }
    }
    return true;
}

bool GameSessionOffline::trySetLocalParticipantState( const int32_t state ) {
    // FUN_00807590
    GameParticipant *pLocal = getLocalParticipant();
    if ( pLocal->getState() != state ) {
        pLocal->setState( state );
    }
    return true;
}

void GameSessionOffline::setLocalParticipantState( const int32_t state ) {
    // FUN_00807580
    getLocalParticipant()->setState( state );
}

int32_t GameSessionOffline::countParticipantsInState( const int32_t state ) {
    // FUN_00807520
    int32_t count = 0;
    for ( int32_t i = 0; i < numParticipants; i++ ) {
        if ( participants[i].getState() == state ) {
            count++;
        }
    }
    return count;
}

GameParticipant *GameSessionOffline::getParticipant( const int32_t index ) {
    // FUN_00807500
    if ( index < 0 || index >= capacity ) {
        return nullptr;
    }
    return &participants[index];
}

GameParticipant *GameSessionOffline::findByCarID( const uint32_t carID ) {
    // FUN_008075f0
    for ( int32_t i = 0; i < getCapacity(); i++ ) {
        GameParticipant *pParticipant = getParticipant( i );
        if ( pParticipant != nullptr &&
             pParticipant->getCarState() != nullptr &&
             pParticipant->getCarState()->getID() == carID ) {
            return pParticipant;
        }
    }
    return nullptr;
}

int32_t GameSessionOffline::onEvent( const int32_t eventID ) {
    // FUN_008084b0
    switch ( eventID ) {
    case E_Unknown99:
        return 1;
    case E_Install:
        setup();
        computeStartGrid();
        instantiateAll();
        return numPlayers + numOpponents;
    case E_PollInstall:
        return instantiateAll() ? 1 : 0;
    case E_Release:
        while ( !releaseAll() ) {
        }
        return 0;
    case E_Unknown9A:
    case E_Unknown9D:
    default:
        return 0;
    }
}

void GameSessionOffline::setup() {
    // FUN_008076f0
    if ( pMissionInfo == nullptr ) {
        return;
    }

    numPlayers = 1;
    numOpponents = GetNumOpponents( pMissionInfo );
    numParticipants = GetNumOpponents( pMissionInfo ) + 1;
    for ( int32_t i = 0; i < numParticipants; i++ ) {
        GameParticipantOffline &participant = participants[i];
        participant.bIsLocalPlayer = i < numPlayers;
        participant.OpponentIndex = i;
        GetParticipantName( pMissionInfo, participant.getName(),
                            GameParticipantOffline::kNameLength, i );

        const MissionInfo::Participant &missionParticipant =
            GetMissionParticipant( pMissionInfo, i );
        participant.CarDBID = missionParticipant.CarDBID;
        participant.CarPaintIndex = missionParticipant.PaintIndex;
        participant.BotID = missionParticipant.BotID;
        if ( !participant.bIsLocalPlayer ) {
            participant.AILevel = kOpponentAILevel;
        }
    }
    participants[0].bMaster = true;
}

void GameSessionOffline::computeStartGrid() {
    // FUN_00807900
    const int32_t numSlots = GetNumOpponents( pMissionInfo ) + 1;
    for ( int32_t i = 0; i < numPlayers + numOpponents; i++ ) {
        GameParticipantOffline *pParticipant =
            static_cast<GameParticipantOffline *>( getParticipant( i ) );
        pParticipant->StartPositionMat =
            pMissionInfo->pOwner->computeGridMatrix( i, numSlots );
        if ( pParticipant->isActive() &&
             pParticipant->getCarState() != nullptr &&
             pParticipant->getCarState()->getAIController() != nullptr ) {
            static_cast<AiVehicle *>(
                pParticipant->getCarState()->getAIController() )
                ->teleport( pParticipant->StartPositionMat, true );
        }
    }
}

bool GameSessionOffline::instantiateAll() {
    // FUN_00808450
    bool bAllInstalled = true;
    for ( int32_t i = 0; i < numPlayers + numOpponents; i++ ) {
        GameParticipantOffline *pParticipant =
            static_cast<GameParticipantOffline *>( getParticipant( i ) );
        if ( !pParticipant->instantiate( kOpponentMissionType ) ) {
            bAllInstalled = false;
        }
    }
    return bAllInstalled;
}

bool GameSessionOffline::releaseAll() {
    // FUN_00807d30
    bool bAllReleased = true;
    for ( int32_t i = 0; i < numPlayers + numOpponents; i++ ) {
        GameParticipantOffline *pParticipant =
            static_cast<GameParticipantOffline *>( getParticipant( i ) );
        if ( !pParticipant->release() ) {
            bAllReleased = false;
        }
    }
    return bAllReleased;
}
