#include "shared.h"
#include "mng_lobby.h"

#include "gm_lobby.h"

#include "tdu_instance.h"
#include "core/time.h"
#include "flash/gs_flash.h"
#include "flash/implementation/flash_player.h"
#include "flash/movie_player.h"
#include "game/scenaric/gs_scenaric.h"
#include "game/scenaric/spots.h"
#include "player_data/saved_garage.h"
#include "player_data/player_data_interface.h"
#include "player_data/gs_playerdata.h"
#include "game/vehicle/vehicle_base.h"
#include "game/vehicle/gs_car_state.h"
#include "game/vehicle/car_state.h"
#include "game/scenaric/local_player.h"
#include "game/scenaric/entity/entity_driver.h"

static constexpr FlashMessage kMsgIgnorePlayer = FlashMessage( "igpl" );
static constexpr FlashMessage kMsgConnectGameList = FlashMessage( "cogl" );
static constexpr FlashMessage kMsgCancelParams = FlashMessage( "capa" );
static constexpr FlashMessage kMsgChangeVehicle = FlashMessage( "case" );
static constexpr FlashMessage kMsgPlayerInfoIn = FlashMessage( "piin" );
static constexpr FlashMessage kMsgChangePlayer = FlashMessage( "chpl" );
static constexpr FlashMessage kMsgChangeSettings = FlashMessage( "chse" );
static constexpr FlashMessage kMsgGoClub = FlashMessage( "gocb" );
static constexpr FlashMessage kMsgFriendRequest = FlashMessage( "frre" );
static constexpr FlashMessage kMsgGoChallenge = FlashMessage( "goch" );
static constexpr FlashMessage kMsgGoSpot = FlashMessage( "gosp" );
static constexpr FlashMessage kMsgHostQuit = FlashMessage( "hoqu" );
static constexpr FlashMessage kMsgHostQuitClub = FlashMessage( "hscb" );
static constexpr FlashMessage kMsgHostGame = FlashMessage( "hoga" );
static constexpr FlashMessage kMsgHostLaunch = FlashMessage( "host" );
static constexpr FlashMessage kMsgQuickMatch = FlashMessage( "qkma" );
static constexpr FlashMessage kMsgPlayerQuit = FlashMessage( "plqu" );
static constexpr FlashMessage kMsgPlayerQuitClub = FlashMessage( "jscb" );
static constexpr FlashMessage kMsgPlayerInfoOut = FlashMessage( "piou" );
static constexpr FlashMessage kMsgPrivateChat = FlashMessage( "prca" );
static constexpr FlashMessage kMsgNoChat = FlashMessage( "neca" );
static constexpr FlashMessage kMsgQuickGame = FlashMessage( "qkga" );
static constexpr FlashMessage kMsgSavePrefs = FlashMessage( "sapr" );
static constexpr FlashMessage kMsgReady = FlashMessage( "redy" );
static constexpr FlashMessage kMsgRefresh = FlashMessage( "refr" );
static constexpr FlashMessage kMsgSelectGame = FlashMessage( "sega" );
static constexpr FlashMessage kMsgVgcd = FlashMessage( "vgcd" );
static constexpr FlashMessage kMsgKick = FlashMessage( "kick" );
static constexpr FlashMessage kMsgRentalReturnCreate = FlashMessage( "ircr" );
static constexpr FlashMessage kMsgRentalReturnJoin = FlashMessage( "ircj" );
static constexpr FlashMessage kMsgInPlayerList = FlashMessage( "inpl" );

// Handled by other listeners: MngLobby only swallows them.
static constexpr FlashMessage kIgnoredMessages[] = {
    FlashMessage( "caqk" ), FlashMessage( "bona" ), FlashMessage( "afbo" ),
    FlashMessage( "boin" ), FlashMessage( "boou" ), FlashMessage( "chop" ),
    FlashMessage( "chau" ), FlashMessage( "goic" ), FlashMessage( "falo" ),
    FlashMessage( "gogp" ), FlashMessage( "noic" ), FlashMessage( "newb" ),
    FlashMessage( "noch" ), FlashMessage( "inou" ), FlashMessage( "info" ),
    FlashMessage( "inic" ) };

static uint32_t GetTickCountMs() {
    return uint32_t( MonotonicToMs( GetTimeMonotonic() ) );
}

MngLobby::MngLobby()
    : MngFlash(),
      pOwner( nullptr ),
      pFlashPlayer( nullptr ),
      disconnectAtMs( 0u ),
      bInSession( false ),
      bShowPlayerInfo( false ),
      bVehicleListOpen( false ),
      bGamesListOpen( false ),
      bHeadLineSelected( false ),
      readyTimerMs( 0u ),
      readyAtMs( 0u ),
      bPendingConnect( false ),
      bJoinPending( false ),
      bJoinDone( false ),
      joinTick( 0u ),
      vehicleList() {
    // FUN_00753550
    OTDU_UNIMPLEMENTED; // TODO: GameData session parameters ('sett' / 'pref'
                        // lists items), games / player / vehicle lists
}

MngLobby::~MngLobby() {
    // FUN_0042d870
}

bool MngLobby::initialize( TestDriveGameInstance * ) {
    // FUN_0063f2b0
    return true;
}

void MngLobby::terminate() {
    // FUN_0063f2b0
}

void MngLobby::reset() {
    // FUN_007525b0
    readyTimerMs = 0u;
    readyAtMs = 0u;
    bJoinPending = false;
    bJoinDone = false;
    joinTick = 0u;
    bInSession = false;

    uint32_t levelIndex = 0u;
    uint32_t movieIndex = 0u;
    if ( gpFlash->getFlashMovieIndex( &levelIndex, &movieIndex, "CHALLENG" ) ) {
        MoviePlayer *pMovie = gpFlash->getMoviePlayer( int32_t( levelIndex ),
                                                       int32_t( movieIndex ) );
        if ( pMovie != nullptr ) {
            pMovie->pEntityInstance = this;
        }
    }

    resetSessionParams();
    if ( pOwner != nullptr && pOwner->isReturningToSession() ) {
        OTDU_UNIMPLEMENTED; // TODO: ONLINE (FUN_00751740, load the params
                            // from the GameSpy session)
    }

    pFlashPlayer = gpFlash->getFlashPlayer( "CHALLENG" );
    if ( pFlashPlayer == nullptr ) {
        return;
    }

    OTDU_UNIMPLEMENTED; // TODO: ONLINE (observe the P2P game data)
    OTDU_UNIMPLEMENTED; // TODO: "gali" / "para" / "pref" lists
    vehicleList.setPlayer( pFlashPlayer, "/vehicles/array_list" );
    addWidget( "case", &vehicleList );
    vehicleList.setOwner( this );
    pFlashPlayer->setVariableValue( "/:IsChangeVehicle", 0 );
    bVehicleListOpen = false;
}

void MngLobby::pause() {
    // FUN_00751ce0
    OTDU_UNIMPLEMENTED; // TODO: ONLINE (gGSOnline flags, P2P game data
                        // observer)
    removeWidget( &vehicleList );
    OTDU_UNIMPLEMENTED; // TODO: "para" / "pref" / "gali" lists
}

void MngLobby::tick( float deltaTime, float totalTime ) {
    // FUN_00754510
    MngFlash::tick( deltaTime, totalTime );

    if ( bJoinDone && joinTick + kJoinResultDelayMs < GetTickCountMs() ) {
        bJoinDone = false;
        joinTick = 0u;
    }

    if ( bGamesListOpen ) {
        OTDU_UNIMPLEMENTED_ONCE; // TODO: games list head line info panel
    }

    if ( bPendingConnect || bInSession ) {
        OTDU_UNIMPLEMENTED_ONCE; // TODO: ONLINE (session connect / ready
                                 // timers)
    }

    // Offline the "help/x_vehicle" hint is skipped (no local P2P player).
    OTDU_UNIMPLEMENTED_ONCE; // TODO: ONLINE (change vehicle hint)

    if ( disconnectAtMs != 0u ) {
        OTDU_UNIMPLEMENTED_ONCE; // TODO: ONLINE (disconnect popup timer)
    }

    if ( pOwner == nullptr || pFlashPlayer == nullptr ) {
        return;
    }

    // NOTE: gGSOnline +0xa730 (spectator voice chat) is always false offline.
    const bool bSpectatorChat = false;
    const int32_t isSpectator =
        pFlashPlayer->getVariableAsInt( "/:IsSpectator" );
    if ( !pOwner->isSpectatorMode() ) {
        if ( isSpectator == 1 && !bSpectatorChat ) {
            pFlashPlayer->setVariableValue( "/:IsSpectator", 0 );
        }
    } else if ( isSpectator == 0 && bSpectatorChat ) {
        pFlashPlayer->setVariableValue( "/:IsSpectator", 1 );
    }
}

bool MngLobby::onMessage( FlashMessage *pMessage, FlashPlayer *pPlayer ) {
    // FUN_007188a0
    if ( handleMessage( pMessage, pPlayer ) ) {
        return true;
    }
    return MngFlash::onMessage( pMessage, pPlayer );
}

bool MngLobby::handleMessage( FlashMessage *pMessage, FlashPlayer *pPlayer ) {
    // FUN_00754d70
    for ( const FlashMessage &ignored : kIgnoredMessages ) {
        if ( *pMessage == ignored ) {
            return false;
        }
    }

    if ( *pMessage == kMsgGoChallenge || *pMessage == kMsgGoSpot ) {
        if ( pOwner == nullptr ) {
            return false;
        }

        if ( !gpScenaric->isFromDriveIn() ) {
            LaunchSpotLevel( pOwner->getSpot(), false );
            return true;
        }

        pOwner->releasePreviewCamera();
        pOwner->snapStreamingToPlayerSpawn();
        OTDU_UNIMPLEMENTED; // TODO: EventParameters ("CUSTOMCHA", type 5)
        pOwner->requestExit();
        gpTestDriveInstance->setNextGameMode( GM_Mission );
        return true;
    }

    if ( *pMessage == kMsgGoClub ) {
        pOwner->quitLobby();
        return true;
    }

    if ( *pMessage == kMsgPlayerInfoOut ) {
        bShowPlayerInfo = false;
        hidePlayerInfo();
        return true;
    }

    if ( *pMessage == kMsgInPlayerList ) {
        OTDU_UNIMPLEMENTED; // TODO: ONLINE (host may change the challenge,
                            // ready timers)
        pFlashPlayer->setVariableValue( "/:IsChangeCha", 0 );
        OTDU_LOG_DEBUG( "MngLobby::Message inpl GET_CUR_P2P()->"
                        "GetNbActivePlayers == 0\n" );
        bInSession = true;
        bShowPlayerInfo = false;
        OTDU_LOG_DEBUG( "MngLobby::Message inpl\n" );
        showPlayers();
        return true;
    }

    if ( *pMessage == kMsgVgcd ) {
        return true;
    }

    if ( *pMessage == kMsgChangeVehicle ) {
        changeVehicle();
        return true;
    }

    if ( *pMessage == kMsgRentalReturnCreate ||
         *pMessage == kMsgRentalReturnJoin ) {
        OTDU_UNIMPLEMENTED; // TODO: session filter (rental car return check)
        return true;
    }

    if ( *pMessage == kMsgSavePrefs ) {
        OTDU_UNIMPLEMENTED; // TODO: GameData session parameters
        return true;
    }

    if ( *pMessage == kMsgConnectGameList || *pMessage == kMsgQuickMatch ||
         *pMessage == kMsgPrivateChat || *pMessage == kMsgNoChat ) {
        OTDU_UNIMPLEMENTED; // TODO: ONLINE
        return false;
    }

    if ( *pMessage == kMsgIgnorePlayer || *pMessage == kMsgCancelParams ||
         *pMessage == kMsgPlayerInfoIn || *pMessage == kMsgChangePlayer ||
         *pMessage == kMsgChangeSettings || *pMessage == kMsgFriendRequest ||
         *pMessage == kMsgHostQuit || *pMessage == kMsgHostQuitClub ||
         *pMessage == kMsgHostGame || *pMessage == kMsgHostLaunch ||
         *pMessage == kMsgPlayerQuit || *pMessage == kMsgPlayerQuitClub ||
         *pMessage == kMsgQuickGame || *pMessage == kMsgReady ||
         *pMessage == kMsgRefresh || *pMessage == kMsgSelectGame ||
         *pMessage == kMsgKick ) {
        if ( *pMessage == kMsgPlayerInfoIn ) {
            bShowPlayerInfo = true;
        }
        OTDU_UNIMPLEMENTED; // TODO: ONLINE
        return true;
    }

    return ( pOwner != nullptr ) ? pOwner->onMessage( pMessage, pPlayer )
                                 : false;
}

void MngLobby::resetSessionParams() {
    // FUN_00751ef0
    OTDU_UNIMPLEMENTED; // TODO: GameData session parameters (vehicle type,
                        // level range, players, collisions...)
}

void MngLobby::showPlayers() {
    // FUN_00755d20
    // Offline the session is always empty: every row is cleared.
    if ( pOwner == nullptr || pFlashPlayer == nullptr ) {
        return;
    }

    char path[64];
    for ( int32_t row = 1; row <= kNumLobbyRows; row++ ) {
        snprintf( path, sizeof( path ), "lobby/b%d", row );
        pFlashPlayer->showFrame( "empty", path, true );

        snprintf( path, sizeof( path ), "/lobby/b%d/lineplayer:name", row );
        pFlashPlayer->setVariableValueNonLinked( path, "" );
        snprintf( path, sizeof( path ), "/lobby/b%d/lineplayer:car", row );
        pFlashPlayer->setVariableValueNonLinked( path, "" );
        snprintf( path, sizeof( path ), "/lobby/b%d/lineplayer:upgrade", row );
        pFlashPlayer->setVariableValueNonLinked( path, "" );
        snprintf( path, sizeof( path ), "/lobby/b%d/lineplayer:points", row );
        pFlashPlayer->setVariableValueNonLinked( path, "" );
    }
}

void MngLobby::hidePlayerInfo() {
    // FUN_00755bc0
    if ( pOwner != nullptr ) {
        pOwner->restorePlayersViewTarget();
    }
    OTDU_UNIMPLEMENTED; // TODO: car specs panel observer
}

void MngLobby::changeVehicle() {
    // FUN_00754d70 (inlined)
    OTDU_UNIMPLEMENTED_ONCE; // TODO: ONLINE (vehicle change locked while the
                             // race is launched)
    if ( vehicleList.getListLength() == 0 ) {
        return;
    }

    Garage *pGarage = gpPlayerData->getLoadedSave()->getGarage();
    gpPlayerData->selectCurrentCar(
        &pGarage->pCars[vehicleList.getSelectedSlot()] );

    OTDU_UNIMPLEMENTED_ONCE; // TODO: ONLINE (local P2P player car, session
                             // car declaration)

    CarState *pLocalCar = gpScenaric->getLocalPlayer().getCarState();
    if ( pLocalCar != nullptr ) {
        EntityDriver *pDriver = pLocalCar->getDriver();
        if ( pDriver != nullptr ) {
            if ( pDriver->getInstanceRefCount() > 0 ) {
                pDriver->removeInstanceReference();
            }
            pLocalCar->unloadDriver();
            gpScenaric->getAvatarManager().releaseAvatar( pDriver );
        }

        VehicleBase *pVehicle = pLocalCar->getVehicleInstance();
        if ( pVehicle != nullptr && pVehicle->isInteriorLoaded() ) {
            pVehicle->unloadInterior();
        }

        gpScenaric->getLocalPlayer().setCarState( nullptr );
        VehicleBaseResources *pResource = pLocalCar->getResource();
        gpCarState->releaseCarState( pLocalCar, false );
        if ( pResource != nullptr ) {
            pResource->unload();
            gpCarState->releaseResourceEntry( pResource );
        }
    }

    if ( pOwner != nullptr ) {
        pOwner->updateLevelAndCarLocks();
    }
}
