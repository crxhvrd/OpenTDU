#include "shared.h"
#include "mng_ingame_menu.h"

#include "gm_base.h"
#include "gm_world.h"
#include "mng_hud.h"
#include "gs_timer.h"

#include "config/gs_config.h"
#include "input/input_device.h"
#include "render/camera/gs_camera.h"
#include "flash/gs_flash.h"
#include "flash/mng_flash.h"
#include "flash/mng_flash_localize.h"
#include "flash/movie_player.h"
#include "flash/entities/flash_entity.h"
#include "flash/implementation/flash_player.h"
#include "player_data/gs_playerdata.h"
#include "flash/mng_fade.h"
#include "game/mng_rental.h"
#include "game/cinematic/gs_cinematics.h"
#include "game/cinematic/mng_cinematics.h"
#include "game/vehicle/car_desc.h"
#include "game/scenaric/gs_scenaric.h"
#include "game/scenaric/local_player.h"
#include "game/vehicle/car_state.h"
#include "database/gs_database.h"
#include "database/db_car_physics_data.h"
#include "physics/vehicle/physics_vehicle.h"

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

// Localized name of each driving help level
static constexpr uint64_t kDrivingHelpLevelHashcodes[] = {
    0x03326ae2, 0x0341ad22, 0x0350ef62, 0x03b7ee92 }; // DAT_00f9c440
constexpr int32_t kNumDrivingHelpLevels =
    int32_t( sizeof( kDrivingHelpLevelHashcodes ) /
             sizeof( kDrivingHelpLevelHashcodes[0] ) );

constexpr float kMenuNavigationTimeout = 5.0f; // DAT_00f41750

constexpr float kChronoBrakeTestSpeedMph = 60.0f;             // FLOAT_00f41744
constexpr float kMetersPerSecToMph = 2.2374146f;              // DAT_00f419f4
constexpr float kMphToKmh = 1.609f;                           // DAT_00f421c0
constexpr float kFeetToMeters = 0.3048f;                      // DAT_00f421c4
constexpr float kChronoArcadeBrakeDistanceScale = 1.6666666f; // DAT_00f421c8

constexpr uint64_t kChronoMsgIdle = 0x338fe7a;
constexpr uint64_t kChronoMsgStopFirst = 0x36b0712;
constexpr uint64_t kChronoMsgBrake = 0x3898b92;
constexpr uint64_t kChronoTimeHashcode = 0x38804f2;
constexpr uint64_t kChronoFeetHashcode = 0x3a68972;
constexpr uint64_t kChronoMetersHashcode = 0x3974732;
constexpr uint64_t kChronoMphHashcode = 0x3c50df2;
constexpr uint64_t kChronoKmhHashcode = 0x3b5cbb2;

static eInGameMenu FindMenuByName( const char *pMenuName ) {
    // FUN_006b66f0
    for ( int32_t i = IGM_None; i <= IGM_Chrono; ++i ) {
        if ( strcmp( pMenuName, kInGameMenuNames[i].pMenuName ) == 0 ) {
            return eInGameMenu( i );
        }
    }
    return IGM_None;
}

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

    FlashPlayer *pHudPlayer = nullptr;
    if ( gpActiveGameMode != nullptr && gpFlash != nullptr ) {
        const eGameMode gm = gpActiveGameMode->getCurrentGameMode();
        if ( gm == GM_FreeRide ) {
            pHudPlayer = gpFlash->getFlashPlayer( "HUD" );
        } else if ( gm == GM_Mission ) {
            pHudPlayer = gpFlash->getFlashPlayer( "HUD_CHA" );
        }
    }
    if ( pHudPlayer != nullptr && OpenedMenu == IGM_Chrono ) {
        updateChronoMenu();
    }

    if ( StateMenu == IGMS_Open && pHudPlayer != nullptr &&
         OpenedMenu == IGM_Players ) {
        OTDU_UNIMPLEMENTED_ONCE; // TODO: ONLINE/P2P (Players page population)
    }
}

static PhysicsVehicle *GetLocalPlayerPhysVehicle() {
    CarState *pCarState = ( gpScenaric != nullptr )
                              ? gpScenaric->getLocalPlayer().getCarState()
                              : nullptr;
    return ( pCarState != nullptr ) ? pCarState->getPhysicsVehicle() : nullptr;
}

bool MngInGameMenu::onMessage( FlashMessage *pMessage, FlashPlayer *pPlayer ) {
    // FUN_006b8750
    if ( pPlayer == nullptr ) {
        pPlayer = pFlashVariable;
    }

    switch ( *pMessage ) {
    case FlashMessage( "igop" ): { // the open animation started
        updateMenuTimeout();
        StateMenu = IGMS_Opening;
        if ( gpFlash != nullptr ) {
            gpFlash->setUseStick( false );
        }
        refreshDrivingHelpLabel();
        return true;
    }

    case FlashMessage( "igme" ): { // the open animation is over
        StateMenu = IGMS_Open;

        char pMenuName[32] = {};
        if ( pPlayer != nullptr ) {
            pPlayer->getVariableAsString( "/:WhatMenu", pMenuName );
        }
        OpenedMenu = FindMenuByName( pMenuName );

        updateMenuTimeout();
        CurrentPlayer = -1;

        if ( PhysicsVehicle *pVehicle = GetLocalPlayerPhysVehicle() ) {
            pVehicle->resetChrono();
        }
        return true;
    }

    case FlashMessage( "igcl" ): { // the close animation is over
        if ( gpFlash != nullptr ) {
            gpFlash->setUseStick( true );
        }
        OpenedMenu = IGM_None;
        StateMenu = IGMS_Closed;

        if ( PhysicsVehicle *pVehicle = GetLocalPlayerPhysVehicle() ) {
            pVehicle->resetChrono();
        }

        if ( bMustSaveOnClose ) {
            bMustSaveOnClose = false;
            if ( gpPlayerData != nullptr ) {
                gpPlayerData->requestSave( 1 );
            }
        }
        return true;
    }

    case FlashMessage( "chro" ): { // the chrono page restarts the speed run
        if ( PhysicsVehicle *pVehicle = GetLocalPlayerPhysVehicle() ) {
            pVehicle->armChrono();
            pVehicle->armBrakeChrono( kChronoBrakeTestSpeedMph );
            MaxSpeedImperial = -1.0f; // FLOAT_00f41664
        }
        return true;
    }

    case FlashMessage( "move" ): // navigating inside the already open page
    case FlashMessage( "nepl" ):
    case FlashMessage( "prpl" ): {
        const float now =
            ( gpTimer != nullptr ) ? gpTimer->SystemTotalTime : 0.0f;
        MenuCloseTimeLeft = now + kMenuNavigationTimeout;
        return true;
    }

    case FlashMessage( "nedm" ): // next driving-help level
        changeDrivingMode( true, false );
        return true;

    case FlashMessage( "prdm" ): // previous driving-help level
        changeDrivingMode( false, false );
        return true;

    case FlashMessage( "wind" ): // windows up/down from the radial menu
        toggleWindows( false );
        return true;

    case FlashMessage( "mupl" ): // consumed without any action in the original
    case FlashMessage( "vgcd" ):
        return true;

    case FlashMessage( "frre" ): // freeride player page / lock / isolate
    case FlashMessage( "lopl" ):
    case FlashMessage( "igpl" ):
    case FlashMessage( "isse" ):
    case FlashMessage( "nean" ):
    case FlashMessage( "poan" ):
    case FlashMessage( "quse" ):
        OTDU_UNIMPLEMENTED_ONCE; // TODO: ONLINE/P2P
        return true;

    default:
        return false;
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

    if ( bMapZoom && gpMngHud != nullptr ) {
        gpMngHud->cycleMinimapZoom();
    }

    if ( bMap && gm == GM_FreeRide && gpCamera != nullptr &&
         !gpCamera->isInteriorView() ) {
        if ( !canOpenMap() ) {
            if ( gpFlash != nullptr && gpFlash->getFlashSound() != nullptr ) {
                gpFlash->getFlashSound()->playFrontEndSound( "wrong", 0,
                                                             false );
            }
        } else if ( pFlashVariable != nullptr ) {
            RequestMapGameMode( pFlashVariable );
        }
    }
}

bool MngInGameMenu::canOpenMap() {
    // FUN_006b6820
    if ( gpScenaric == nullptr ) {
        return true;
    }
    return !gpScenaric->getIntroTutorial().isActive();
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
    if ( gpActiveGameMode != nullptr &&
         ( gpActiveGameMode->isMessageBoxVisible() ||
           gpActiveGameMode->hasRequestedExit() ) ) {
        return false;
    }

    if ( gpCinematics != nullptr &&
         gpCinematics->isCinematicPlaying( CT_Foreground, false, true ) ) {
        return false;
    }

    if ( gpMngFade != nullptr && ( gpMngFade->getTransitionFlags() & 1 ) ) {
        return false;
    }

    OTDU_UNIMPLEMENTED_ONCE; // TODO: REPLAY (DAT_00fa97dc == 5 blocks the menu)

    if ( gpMngRental != nullptr && gpMngRental->isRespawnPending() ) {
        return false;
    }
    if ( !bEnabled ) {
        return false;
    }

    CarState *pCarState = ( gpScenaric != nullptr )
                              ? gpScenaric->getLocalPlayer().getCarState()
                              : nullptr;
    return pCarState == nullptr || !pCarState->getPoliceState().isArrested();
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

void MngInGameMenu::resetWindowsDirection() {
    // FUN_006b6950
    const CarDesc *pCurrentCar = gpPlayerData->getCurrentCar();
    if ( pCurrentCar == nullptr ) {
        return;
    }

    DBEntryCarPhysicsData *pCarData =
        gpDatabase->getCarDBEntry( int32_t( pCurrentCar->getCarConfigID() ) );
    if ( pCarData == nullptr ) {
        return;
    }

    const uint64_t bodyHashcode =
        gpDatabase->getGameDB().Databases[1].getHashcodeForPointer(
            pCarData->CarBody.pAsRawPointer );
    WindowsDirection =
        ( bodyHashcode != 0x56760d && bodyHashcode != 0x57fcad ) ? 1 : -1;
}

void MngInGameMenu::changeDrivingMode( bool bNext, bool bPlayAnim ) {
    // FUN_006b7250
    if ( gpFlash != nullptr ) {
        gpFlash->setUseStick( false );
    }

    if ( pFlashVariable != nullptr &&
         pFlashVariable->spriteExists( "HUD/menu_ig" ) ) {
        OpenedMenu = IGM_Driving;
        pFlashVariable->setVariableValue(
            "/:WhatMenu", kInGameMenuNames[IGM_Driving].pMenuName );
        if ( bPlayAnim ) {
            pFlashVariable->showFrame( "open", "HUD/menu_ig", true );
            StateMenu = IGMS_Opening;
        }
    }

    updateMenuTimeout();

    if ( bPlayAnim ) {
        return;
    }

    if ( gpPlayerData != nullptr ) {
        int32_t level =
            gpPlayerData->getDrivingHelpLevel() + ( bNext ? 1 : -1 );
        if ( level >= kNumDrivingHelpLevels ) {
            level = 0;
        } else if ( level < 0 ) {
            level = kNumDrivingHelpLevels - 1;
        }
        gpPlayerData->setDrivingHelpLevel( level );
    }

    refreshDrivingHelpLabel();

    PhysicsVehicle *pVehicle = GetLocalPlayerPhysVehicle();
    if ( pVehicle != nullptr && gpPlayerData != nullptr ) {
        pVehicle->setDrivingHelpLevel( gpPlayerData->getDrivingHelpLevel() );
    }

    bMustSaveOnClose = true;
}

void MngInGameMenu::refreshDrivingHelpLabel() {
    if ( gpMngFlashLocalize == nullptr || pFlashVariable == nullptr ||
         gpPlayerData == nullptr ) {
        return;
    }

    const int32_t level = Clamp( gpPlayerData->getDrivingHelpLevel(), 0,
                                 kNumDrivingHelpLevels - 1 );
    gpMngFlashLocalize->setVarLocalized( pFlashVariable, "/:DVLevel",
                                         kDrivingHelpLevelHashcodes[level] );
}

static void SetChronoResults( FlashPlayer *pPlayer, const char *pValue ) {
    static constexpr const char *kChronoResultVars[] = {
        "/:cur_0_100_i",     "/:cur_0_100_m",    "/:cur_q_mile_i",
        "/:cur_q_mile_m",    "/:cur_100_0_i",    "/:cur_100_0_m",
        "/:cur_max_speed_i", "/:cur_max_speed_m" };
    for ( const char *pVarName : kChronoResultVars ) {
        pPlayer->setVariableValueNonLinked( pVarName, pValue );
    }
}

static void SetChronoDisplay( FlashPlayer *pPlayer,
                              const char *pTimer,
                              const char *pResults ) {
    pPlayer->setVariableValueNonLinked( "/:min_chrono", pTimer );
    pPlayer->setVariableValueNonLinked( "/:sec_chrono", pTimer );
    pPlayer->setVariableValueNonLinked( "/:cent_chrono", pTimer );
    SetChronoResults( pPlayer, pResults );
}

void MngInGameMenu::setChronoTime( float time ) {
    // FUN_006b6ac0
    const float seconds = floorf( time );
    const int32_t cents = int32_t( ( time - seconds ) * 100.0f );
    const int32_t secs = int32_t( seconds ) % 60;
    const int32_t mins = Clamp( int32_t( floorf( seconds / 60.0f ) ), 0, 99 );

    char pSeconds[8];
    char pCents[8];
    sprintf( pSeconds, "%02d", secs );
    sprintf( pCents, "%02d", cents );
    pFlashVariable->setVariableValue( "/:min_chrono", mins );
    pFlashVariable->setVariableValueNonLinked( "/:sec_chrono", pSeconds );
    pFlashVariable->setVariableValueNonLinked( "/:cent_chrono", pCents );
}

void MngInGameMenu::setChronoAccelerateMessage() {
    // FUN_006b7170
    char pSpeed[32];
    uint64_t speedUnitHashcode;
    if ( gpPlayerData->isUsingImperialUnits() ) {
        sprintf( pSpeed, "%d", 60 );
        gpMngFlashLocalize->setSysVar( "SPEED", pSpeed );
        speedUnitHashcode = 0x3c50df2;
    } else {
        sprintf( pSpeed, "%d", 96 );
        gpMngFlashLocalize->setSysVar( "SPEED", pSpeed );
        speedUnitHashcode = 0x3b5cbb2;
    }

    char pSpeedWithUnit[32];
    gpMngFlashLocalize->makeSentenceFromHashcode(
        pSpeedWithUnit, sizeof( pSpeedWithUnit ), speedUnitHashcode, nullptr );
    gpMngFlashLocalize->clearAllSysVar();

    gpMngFlashLocalize->setSysVar( "SPEED", pSpeedWithUnit );
    gpMngFlashLocalize->setVarLocalized( pFlashVariable, "/:chrono_msg",
                                         0x37a4952 );
    gpMngFlashLocalize->clearAllSysVar();
}

void MngInGameMenu::updateChronoMenu() {
    // FUN_006b7460
    CarState *pCarState = ( gpScenaric != nullptr )
                              ? gpScenaric->getLocalPlayer().getCarState()
                              : nullptr;
    if ( pCarState == nullptr ) {
        return;
    }
    PhysicsVehicle *pVehicle = pCarState->getPhysicsVehicle();
    if ( pVehicle == nullptr || pFlashVariable == nullptr ||
         gpMngFlashLocalize == nullptr ) {
        return;
    }

    const PhysicsVehicle::Chrono &chrono = pVehicle->getChrono();
    switch ( chrono.AccelState ) {
    case PhysicsVehicle::CAS_Off:
        if ( chrono.BrakeState != PhysicsVehicle::CBS_Off ) {
            return;
        }
        gpMngFlashLocalize->setVarLocalized( pFlashVariable, "/:chrono_msg",
                                             kChronoMsgIdle );
        SetChronoDisplay( pFlashVariable, "--", "--" );
        return;

    case PhysicsVehicle::CAS_StopFirst:
        gpMngFlashLocalize->setVarLocalized( pFlashVariable, "/:chrono_msg",
                                             kChronoMsgStopFirst );
        SetChronoDisplay( pFlashVariable, "---", "---" );
        pVehicle->armBrakeChrono( kChronoBrakeTestSpeedMph );
        updateMenuTimeout();
        return;

    case PhysicsVehicle::CAS_Ready:
        setChronoAccelerateMessage();
        pFlashVariable->setVariableValueNonLinked( "/:min_chrono", "0" );
        pFlashVariable->setVariableValueNonLinked( "/:sec_chrono", "00" );
        pFlashVariable->setVariableValueNonLinked( "/:cent_chrono", "00" );
        SetChronoResults( pFlashVariable, "---" );
        updateMenuTimeout();
        return;

    case PhysicsVehicle::CAS_Running:
        break;

    default:
        return;
    }

    const bool bImperial = gpPlayerData->isUsingImperialUnits();
    const bool bMetric = !bImperial;
    const float speedMph =
        pCarState->getBody()->SpeedInKmh * kMetersPerSecToMph;

    const bool bCompleted = ( chrono.T100kmh > 0.0f || bImperial ) &&
                            ( chrono.T60mph > 0.0f || bMetric ) &&
                            chrono.TQuarterMile > 0.0f &&
                            chrono.Brake100kmhDistance > 0.0f;
    if ( !bCompleted ) {
        if ( chrono.BrakeState != PhysicsVehicle::CBS_Off ) {
            Counter = chrono.Timer;
            MaxSpeedImperial = Max( speedMph, MaxSpeedImperial );
        }
        setChronoTime( Counter );
    }

    if ( chrono.BrakeState == PhysicsVehicle::CBS_WaitForSpeed ) {
        if ( MaxSpeedImperial <= kChronoBrakeTestSpeedMph ||
             speedMph < kChronoBrakeTestSpeedMph ) {
            setChronoAccelerateMessage();
        } else {
            gpMngFlashLocalize->setVarLocalized( pFlashVariable, "/:chrono_msg",
                                                 kChronoMsgBrake );
        }
    } else if ( chrono.BrakeState == PhysicsVehicle::CBS_Braking ) {
        gpMngFlashLocalize->setVarLocalized( pFlashVariable, "/:chrono_msg",
                                             kChronoMsgBrake );
    } else if ( chrono.BrakeState == PhysicsVehicle::CBS_Off ) {
        gpMngFlashLocalize->setVarLocalized( pFlashVariable, "/:chrono_msg",
                                             kChronoMsgIdle );
    }

    if ( chrono.T100kmh <= 0.0f ) {
        pFlashVariable->setVariableValueNonLinked( "/:cur_0_100_m", "---" );
    } else {
        gpMngFlashLocalize->setSysVar( "TIME", chrono.T60mph );
        gpMngFlashLocalize->setVarLocalized( pFlashVariable, "/:cur_0_100_m",
                                             kChronoTimeHashcode );
        gpMngFlashLocalize->clearAllSysVar();
    }
    if ( chrono.T60mph <= 0.0f ) {
        pFlashVariable->setVariableValueNonLinked( "/:cur_0_100_i", "---" );
    } else {
        gpMngFlashLocalize->setSysVar( "TIME", chrono.T60mph );
        gpMngFlashLocalize->setVarLocalized( pFlashVariable, "/:cur_0_100_i",
                                             kChronoTimeHashcode );
        gpMngFlashLocalize->clearAllSysVar();
    }
    if ( chrono.TQuarterMile <= 0.0f ) {
        pFlashVariable->setVariableValueNonLinked( "/:cur_q_mile_m", "---" );
        pFlashVariable->setVariableValueNonLinked( "/:cur_q_mile_i", "---" );
    } else {
        gpMngFlashLocalize->setSysVar( "TIME", chrono.TQuarterMile );
        gpMngFlashLocalize->setVarLocalized( pFlashVariable, "/:cur_q_mile_m",
                                             kChronoTimeHashcode );
        gpMngFlashLocalize->setVarLocalized( pFlashVariable, "/:cur_q_mile_i",
                                             kChronoTimeHashcode );
        gpMngFlashLocalize->clearAllSysVar();
    }

    const float brakeDistanceScale =
        gbSimuModeEnabled ? 1.0f : kChronoArcadeBrakeDistanceScale;
    if ( chrono.Brake60mphDistance > 0.0f ) {
        gpMngFlashLocalize->setSysVar(
            "DISTANCE", brakeDistanceScale * chrono.Brake60mphDistance );
        gpMngFlashLocalize->setVarLocalized( pFlashVariable, "/:cur_100_0_i",
                                             kChronoFeetHashcode );
        gpMngFlashLocalize->clearAllSysVar();
    }

    if ( chrono.Brake100kmhDistance > 0.0f ) {
        gpMngFlashLocalize->setSysVar( "DISTANCE", chrono.Brake60mphDistance *
                                                       kFeetToMeters *
                                                       brakeDistanceScale );
        gpMngFlashLocalize->setVarLocalized( pFlashVariable, "/:cur_100_0_m",
                                             kChronoMetersHashcode );
        gpMngFlashLocalize->clearAllSysVar();
    }

    gpMngFlashLocalize->setSysVar( "SPEED", MaxSpeedImperial );
    gpMngFlashLocalize->setVarLocalized( pFlashVariable, "/:cur_max_speed_i",
                                         kChronoMphHashcode );
    gpMngFlashLocalize->clearAllSysVar();
    gpMngFlashLocalize->setSysVar( "SPEED", MaxSpeedImperial * kMphToKmh );
    gpMngFlashLocalize->setVarLocalized( pFlashVariable, "/:cur_max_speed_m",
                                         kChronoKmhHashcode );
    gpMngFlashLocalize->clearAllSysVar();

    updateMenuTimeout();
}

void MngInGameMenu::updateMenuTimeout() {
    // FUN_006b6780
    if ( OpenedMenu == IGM_None ) {
        return;
    }
    const float now = ( gpTimer != nullptr ) ? gpTimer->SystemTotalTime : 0.0f;
    MenuCloseTimeLeft = now + kInGameMenuNames[OpenedMenu].Timeout;
}

void MngInGameMenu::reset() {
    // FUN_006b6a10
    bHasWindows = false;

    CarDesc *pCurrentCar =
        ( gpPlayerData != nullptr ) ? gpPlayerData->getCurrentCar() : nullptr;
    if ( pCurrentCar == nullptr || gpDatabase == nullptr ) {
        return;
    }
    DBEntryCarPhysicsData *pDB =
        gpDatabase->getCarDBEntry( pCurrentCar->getCarConfigID() );
    if ( pDB == nullptr ) {
        return;
    }
    const uint64_t windowsHash =
        gpDatabase->getGameDB().Databases[1].getHashcodeForPointer(
            pDB->WindowsType.pAsRawPointer );
    bHasWindows = ( windowsHash != kNoWindowsTypeHashcode );
}

void MngInGameMenu::pause() {
    // FUN_00432630
    pFlashVariable = nullptr;
}
