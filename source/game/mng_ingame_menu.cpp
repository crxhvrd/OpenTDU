#include "shared.h"
#include "mng_ingame_menu.h"

#include "gm_base.h"
#include "gm_world.h"
#include "mng_hud.h"
#include "gs_timer.h"

#include "config/gs_config.h"
#include "input/input_device.h"
#include "flash/gs_flash.h"
#include "flash/movie_player.h"
#include "flash/implementation/flash_player.h"
#include "game/cinematic/mng_cinematics.h"
#include "game/scenaric/gs_scenaric.h"
#include "game/scenaric/local_player.h"
#include "game/vehicle/car_state.h"
#include "database/gs_database.h"
#include "database/db_car_physics_data.h"

MngInGameMenu *gpMngInGameMenu = nullptr; // DAT_010e803c

struct InGameMenuEntry {
    const char *pMenuName;
    float Timeout; // seconds the page stays open before auto-closing
};

// DAT_00f03fa0
static constexpr InGameMenuEntry kInGameMenuNames[] = {
    {"",        0.0f}, // IGM_None
    {"players", 5.0f}, // IGM_Players
    {"radio",   5.0f}, // IGM_Radio
    {"driving", 5.0f}, // IGM_Driving
    {"windows", 5.0f}, // IGM_Windows
    {"chrono",  5.0f}, // IGM_Chrono
};

constexpr uint64_t kNoWindowsTypeHashcode = 0x4a1475b;

MngInGameMenu::MngInGameMenu() : Manager() {
    // FUN_006b6860
    CurrentPlayer = -1;
    CurrentAskingLockPlayer = -1;
    CurrentAskingToIsolate = -1;
    gpMngInGameMenu = this;
    pFlashVariable = nullptr;
    StateMenu = IGMS_Closed;
    OpenedMenu = IGM_None;
    MenuCloseTimeLeft = 0.0f;
    TimeoutAskingLockPlayer = 0.0f;
    LockStateMenu = IGMS_Closed;
    TimeoutAskingIsolate = 0.0f;
    TimeoutAskingToIsolate = 0.0f;
    WindowsDirection = 0;
    for ( int32_t i = 0; i < 16; ++i ) {
        pbAskingToLockWith[i] = false;
        pTimeoutAskingToLockWith[i] = 0.0f;
        pbAcceptedToIsolateWith[i] = false;
    }
    bDrivingModeEnabled = true;
    bEnabled = true;
    bMustSaveOnClose = false;
    bHasWindows = false;
}

MngInGameMenu::~MngInGameMenu() { gpMngInGameMenu = nullptr; }

void MngInGameMenu::tick( float, float ) {
    // FUN_006b7e90
    pFlashVariable =
        ( gpMngHud != nullptr ) ? gpMngHud->getHudPlayer() : nullptr;

    updateInput();

    if ( LockStateMenu == IGMS_Closing ) {
        LockStateMenu = IGMS_Closed;
    }

    const float now = ( gpTimer != nullptr ) ? gpTimer->SystemTotalTime : 0.0f;
    if ( OpenedMenu != IGM_None && now > MenuCloseTimeLeft &&
         StateMenu != IGMS_Opening && StateMenu != IGMS_Closed ) {
        close();
    }

    if ( OpenedMenu == IGM_Players || OpenedMenu == IGM_Chrono ) {
        OTDU_UNIMPLEMENTED; // TODO: ONLINE/P2P + Players/Chrono page population
    }
}

void MngInGameMenu::close() {
    // FUN_006b6ee0
    if ( pFlashVariable == nullptr || OpenedMenu == IGM_None ) {
        return;
    }
    if ( !pFlashVariable->spriteExists( "/HUD/menu_ig" ) ) {
        return;
    }

    pFlashVariable->showFrame( "close", "/HUD/menu_ig", true );
    CurrentPlayer = -1;
    OpenedMenu = IGM_None;

    MoviePlayer *pMovie =
        ( gpFlash != nullptr ) ? gpFlash->getMoviePlayer( 1, 0 ) : nullptr;
    StateMenu = ( pMovie != nullptr && pMovie->IsMoviePaused == 0 )
                    ? IGMS_Closing
                    : IGMS_Closed;
}

void MngInGameMenu::updateInput() {
    // FUN_006b7b90
    if ( StateMenu != IGMS_Closed || LockStateMenu != IGMS_Closed ||
         !canOpenMenu() ) {
        return;
    }

    PlayerConfiguration *pCfg =
        ( gpConfig != nullptr ) ? gpConfig->pPlayerConfig : nullptr;
    if ( pCfg == nullptr || gpConfig->NbMaxPlayers < 1 ) {
        return;
    }

    const bool bDpadLeft = IsActionPressed( pCfg->pInGameMenuLeft ) &&
                           !IsActionDown( pCfg->pInGameMenuUp ) &&
                           !IsActionDown( pCfg->pInGameMenuDown );

    const bool bWindows = IsActionPressed( pCfg->pWindows );
    const bool bMapZoom = IsActionPressed( pCfg->pMapZoom );
    const bool bMap = IsActionPressed( pCfg->pMap );
    const bool bRadio = IsActionPressed( pCfg->pRadioOpenClose );
    const bool bDrivingHelp = IsActionPressed( pCfg->pDrivingHelp );
    const bool bPlayers = IsActionPressed( pCfg->pFreeridePlayers );
    const bool bChrono = IsActionPressed( pCfg->pChronopack );

    // Ignore hotkeys while a P2P lock/isolate prompt is pending.
    if ( CurrentAskingLockPlayer != -1 || CurrentAskingToIsolate != -1 ) {
        return;
    }

    if ( bDpadLeft ) {
        changeMenu( IGM_Radio );
    }
    if ( bHasWindows && bWindows ) {
        toggleWindows( true );
    }
    if ( bRadio ) {
        changeMenu( IGM_Radio );
    }

    const eGameMode gm = ( gpActiveGameMode != nullptr )
                             ? gpActiveGameMode->getCurrentGameMode()
                             : GM_FreeRide;
    if ( bDrivingHelp && gm != GM_Mission ) {
        changeMenu( IGM_Driving );
    }
    if ( bPlayers ) {
        changeMenu( IGM_Players );
    }
    if ( bChrono ) {
        changeMenu( IGM_Chrono );
    }

    OTDU_UNIMPLEMENTED;

    (void)bMapZoom;
    (void)bMap;
}

bool MngInGameMenu::canOpenMenu() {
    // FUN_006b6de0
    if ( pFlashVariable == nullptr ) {
        return false;
    }

    MoviePlayer *pMovie =
        ( gpFlash != nullptr ) ? gpFlash->getMoviePlayer( 1, 0 ) : nullptr;
    if ( pMovie == nullptr || pMovie->IsMoviePaused != 0 ||
         pMovie->Flags != 0 ) {
        return false;
    }

    if ( gpActiveGM != nullptr && gpActiveGM->isPaused() ) {
        return false;
    }
    if ( gpActiveGameMode != nullptr && gpActiveGameMode->hasRequestedExit() ) {
        return false;
    }

    if ( gpMngCinematics != nullptr &&
         gpMngCinematics->getActiveCinematic() != nullptr ) {
        return false;
    }

    OTDU_UNIMPLEMENTED;

    return bEnabled;
}

bool MngInGameMenu::changeMenu( eInGameMenu menu ) {
    // FUN_006b6f60
    if ( !canOpenMenu() ) {
        return false;
    }
    if ( pFlashVariable != nullptr &&
         pFlashVariable->spriteExists( "HUD/menu_ig" ) ) {
        if ( OpenedMenu == IGM_None ) {
            OpenedMenu = menu;
            pFlashVariable->setVariableValue(
                "/:WhatMenu", kInGameMenuNames[menu].pMenuName );
            pFlashVariable->showFrame( "open", "HUD/menu_ig", true );
            StateMenu = IGMS_Opening;
            return true;
        }
        if ( OpenedMenu == menu ) {
            updateMenuTimeout();
        }
    }
    return true;
}

void MngInGameMenu::toggleWindows( bool bPlayAnim ) {
    // FUN_006b6bb0
    FlashPlayer *pPlayer = pFlashVariable;

    WindowsDirection = ( WindowsDirection != 1 ) ? 1 : -1;

    if ( pPlayer == nullptr ) {
        return;
    }

    MoviePlayer *pMovie =
        ( gpFlash != nullptr ) ? gpFlash->getMoviePlayer( 1, 0 ) : nullptr;
    if ( pMovie != nullptr && pMovie->IsMoviePaused == 0 ) {
        OpenedMenu = IGM_Windows;
        pPlayer->setVariableValue( "/:WhatMenu",
                                   kInGameMenuNames[IGM_Windows].pMenuName );
        const char *pWay = ( WindowsDirection == 1 ) ? "up" : "down";
        pPlayer->setVariableValue( "/:WindowsWay", pWay );
        if ( bPlayAnim ) {
            pPlayer->showFrame( "open", "HUD/menu_ig", true );
        }
    }
}

void MngInGameMenu::updateMenuTimeout() {
    // FUN_006b6780
    if ( OpenedMenu == IGM_None ) {
        return;
    }
    const float now = ( gpTimer != nullptr ) ? gpTimer->SystemTotalTime : 0.0f;
    MenuCloseTimeLeft = now + kInGameMenuNames[OpenedMenu].Timeout;
}

void MngInGameMenu::refreshHasWindows() {
    bHasWindows = false;
    if ( gpScenaric == nullptr || gpDatabase == nullptr ) {
        return;
    }
    CarState *pCar = gpScenaric->getLocalPlayer().getCarState();
    if ( pCar == nullptr ) {
        return;
    }
    DBEntryCarPhysicsData *pDB = gpDatabase->getCarDBEntry( pCar->getCarID() );
    if ( pDB == nullptr ) {
        return;
    }
    const uint64_t windowsHash =
        gpDatabase->getGameDB().Databases[1].getHashcodeForPointer(
            pDB->WindowsType.pAsRawPointer );
    bHasWindows = ( windowsHash != kNoWindowsTypeHashcode );
}

void MngInGameMenu::reset() {
    // FUN_006b6a10
    refreshHasWindows();
    OTDU_UNIMPLEMENTED; // TODO: PLAYERDATA (garage-selected car override)
}

void MngInGameMenu::pause() {
    // FUN_00432630
    pFlashVariable = nullptr;
}
