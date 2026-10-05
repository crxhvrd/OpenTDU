#include "shared.h"
#include "gs_pc_options.h"

#include "audio/gs_audio.h"
#include "config/gs_config.h"
#include "flash/gs_flash.h"
#include "flash/flash_mouse.h"
#include "flash/mng_fade.h"
#include "flash/mng_flash_counter.h"
#include "flash/modal_message_box.h"
#include "flash/implementation/flash_player.h"
#include "flash/implementation/flash_resource.h"
#include "flash/implementation/flash_sprite.h"
#include "game/gm_base.h"
#include "game/login/gm_login.h"
#include "gs_timer.h"
#include "input/input_device.h"
#include "physics/vehicle/physics_vehicle.h"
#include "player_data/achievements_data.h"
#include "player_data/player_save.h"
#include "player_data/gs_playerdata.h"
#include "player_data/save_game_manager.h"
#include "player_data/user_profile.h"
#include "tdu_instance.h"
#include "world/weather/gs_weather.h"

GSPCOptions *gpPCOptions = nullptr; // DAT_01416130

// Hashcodes of the three CONFIGPC main menu confirmations.
static constexpr uint64_t kClearCurrentGameHashcode = 0x3acf5fa;
static constexpr uint64_t kReturnToWindowsHashcode = 0x39db3ba;
static constexpr uint64_t kChangeProfileHashcode = 0x35475ba;

static constexpr TimeOfDay kWeatherResumeInterval = { 0.0f, 30.0f, 0.0f };

static constexpr const char *kNotificationClips[] = {
    "/achievement", "/reward",   "/levelup",    "/pop_news",
    "/subtitle",    "/tutorial", "/jail_popup",
};

GSPCOptions::GSPCOptions()
    : GameSystem(),
      FlashEntity(),
      PopupCallback(),
      mngOptions(),
      keymapList(),
      pFlashPlayer( nullptr ),
      nickname{},
      email{},
      password{},
      bProfileScreenOpened( false ),
      profileScreenMode( 0 ),
      field297_0x188( false ),
      profileConnected{},
      profileNames{},
      pInputActions( nullptr ),
      bCanOpenMainMenu( true ),
      bMainMenuOpened( false ),
      bOptionsRootHidden( false ),
      bMoviesNeedReenable( false ),
      bSavedMouseEnabled( false ),
      bWasGameAlreadyPaused( false ),
      bCloseRequested( false ),
      field23_0x35( false ),
      bPendingPopupAnswer( false ),
      savedAudioMixState( 0u ),
      pendingMenuAction( MA_None ),
      exitCountdown( -1 ),
      pendingPopupAnswer( ePopupAnswer::PA_Unknown ),
      pendingPopupUserData( 0 ),
      activeProfileHashcode( 0ull ) {
    // FUN_007c4f10
    gpPCOptions = this;
}

GSPCOptions::~GSPCOptions() {
    // FUN_0042b380
    gpPCOptions = nullptr;
}

bool GSPCOptions::initialize( TestDriveGameInstance * ) {
    // FUN_007c4990
    reset();

    OTDU_UNIMPLEMENTED_ONCE; // TODO: FRONTEND (the two globals FUN_007c4990
                             // publishes are not ported)

    return true;
}

void GSPCOptions::tick( float totalTime, float deltaTime ) {
    // FUN_007c5b60
    mngOptions.tick( totalTime, deltaTime );

    if ( pFlashPlayer == nullptr ) {
        pFlashPlayer = gpFlash->getFlashPlayer( "CONFIGPC" );
        if ( pFlashPlayer == nullptr ) {
            return;
        }
        gpFlash->bindPlayerEntity( this, "CONFIGPC" );

        pInputActions =
            ( gpConfig != nullptr ) ? gpConfig->pPlayerConfig : nullptr;

        keymapList.bind( pFlashPlayer,
                         &gpPlayerData->getLocalPlayerData().InputBindings,
                         pInputActions );

        mngOptions.bindFlashPlayer( pFlashPlayer, 0 );
        mngOptions.refreshOptionsScreen();
    }

    if ( bPendingPopupAnswer ) {
        handlePendingPopupAnswer();
        return;
    }

    if ( bMoviesNeedReenable ) {
        const int32_t numMovies = gpFlash->getNumMovies( 1 );
        for ( int32_t movieIndex = 0; movieIndex < numMovies; movieIndex++ ) {
            MoviePlayer *pMovie = gpFlash->getMoviePlayer( 1, movieIndex );
            if ( pMovie != nullptr ) {
                pMovie->Flags &= ~1u;
            }
        }
        bMoviesNeedReenable = false;
    }

    const bool bMenuQuitPressed = ( pInputActions != nullptr ) &&
                                  IsActionPressed( pInputActions->pMenuQuit );

    if ( bMenuQuitPressed ) {
        bool bHandled = false;

        if ( gpFlash->FUN_0099b240( "CONFIGPC", 0 ) ) {
            if ( !bMainMenuOpened || bCloseRequested || field23_0x35 ) {
                const bool bPopupDisplayed =
                    ( gpActiveGameMode != nullptr &&
                      gpActiveGameMode->isMessageBoxVisible() ) ||
                    ( gpFlashMessageBox != nullptr &&
                      gpFlashMessageBox->getDisplayState() !=
                          ModalMessageBox::MMBDS_Closed );

                const bool bLeavingGameMode =
                    ( gpActiveGameMode != nullptr &&
                      gpActiveGameMode->hasRequestedExit() ) ||
                    pendingMenuAction == MA_QuitToDesktop;

                const bool bFading =
                    ( gpMngFade != nullptr ) &&
                    ( gpMngFade->getTransitionFlags() & 1 ) != 0;

                if ( bCanOpenMainMenu && !bPopupDisplayed &&
                     !bLeavingGameMode && !bFading ) {
                    openMainMenu( true );
                    bHandled = true;
                }
            } else if ( !bOptionsRootHidden ) {
                pendingMenuAction = MA_QuitToDesktop;
                onMainMenuSelection( MMO_Quit );
                bHandled = true;
            }
        }

        if ( !bHandled ) {
            gpFlash->getFlashSound()->playFrontEndSound( "wrong", 0, false );
        }
    }

    if ( bMainMenuOpened ) {
        keymapList.tick( gpTimer->SystemDeltaTime, gpTimer->SystemTotalTime );
    }

    if ( gSavegameManager.isDwordDiffFromPendingSaveCount() ) {
        return;
    }

    if ( bCloseRequested ) {
        bCloseRequested = false;
        closeOptionsMenu();
    }

    if ( exitCountdown > 0 ) {
        exitCountdown--;
    }

    if ( exitCountdown == 0 &&
         !gSavegameManager.isDwordDiffFromPendingSaveCount() &&
         ( gpFlashMessageBox == nullptr ||
           !gpFlashMessageBox->FUN_00403100() ) ) {
        exitCountdown = -1;
        if ( pendingMenuAction == MA_QuitToDesktop ) {
            TestDrive::RequestGameExit();
            return;
        }

        GMLogin::ChangeProfile( activeProfileHashcode );
    }
}

void GSPCOptions::terminate() {
    // NOP
}

void GSPCOptions::pause() {
    // FUN_007c4830
    mngOptions.terminate();
    exitCountdown = -1;
    pFlashPlayer = nullptr;
}

void GSPCOptions::reset() {
    // FUN_007c4860
    field23_0x35 = false;
    mngOptions.initialize( nullptr );
}

bool GSPCOptions::onMessage( FlashMessage *pMessage, FlashPlayer *pPlayer ) {
    // FUN_007c5de0
    if ( mngOptions.onMessage( pMessage, pPlayer ) ) {
        return true;
    }

    const char *pTag = pMessage->pAsChar;
    const uint32_t tag = ( (uint32_t)(uint8_t)pTag[0] << 24 ) |
                         ( (uint32_t)(uint8_t)pTag[1] << 16 ) |
                         ( (uint32_t)(uint8_t)pTag[2] << 8 ) |
                         (uint32_t)(uint8_t)pTag[3];

    switch ( tag ) {
    case 0x6d636c73: // "mcls" — the main menu finished closing
        closeMainMenu();
        return true;

    case 0x6d656e75: { // "menu" — a main menu entry was validated
        const eMainMenuOption choice = eMainMenuOption(
            pFlashPlayer->getVariableAsInt( "/options_root/cursor:choice" ) );
        onMainMenuChoice( choice );
        return true;
    }

    case 0x6f707469: // "opti" — enter the options page
        hideOptionsRoot();
        mngOptions.refreshOptionsScreen();
        return true;

    case 0x7265636f: // "reco"
        pendingMenuAction = MA_Resume;
        bCloseRequested = true;
        field23_0x35 = true;
        return true;

    case 0x71756974: // "quit"
        bMainMenuOpened = true;
        pendingMenuAction = MA_QuitToDesktop;
        onMainMenuSelection( MMO_Quit );
        return false;

    case 0x6e67616d: // "ngam" — new game
        onMainMenuSelection( MMO_Continue );
        gbSimuModeEnabled = false;
        return true;

    case 0x73696d75: // "simu" — hardcore mode
        pendingMenuAction = MA_Resume;
        onMainMenuSelection( MMO_HardcoreMode );
        gbSimuModeEnabled = true;
        return true;

    case 0x736c6964: // "slid"
        return FlashSlider::OnMessage( pMessage + 1 );

    case 0x736e6470: // "sndp"
        if ( gpMngOptions != nullptr &&
             strcmp( pMessage[1].pAsChar, "wrong" ) == 0 &&
             gpMngOptions->onVideoPageInvalidAction() ) {
            return true;
        }
        return gpFlash->getFlashSound()->playFrontEndSound( pMessage[1].pAsChar,
                                                            0, false );

    case 0x67617567: // "gaug"
        if ( gpMngFlashCounter == nullptr ) {
            return false;
        }
        gpMngFlashCounter->onMessage( pMessage + 1, pPlayer );
        return true;

    case 0x68696f75: // "hiou"
    case 0x636b6b79: // "ckky"
    case 0x6869696e: // "hiin"
        if ( gpGMLogin != nullptr && gpActiveGameMode != nullptr &&
             gpActiveGameMode->getCurrentGameMode() == GM_Login ) {
            return gpGMLogin->onMessage( pMessage, pPlayer );
        }
        return false;

    case 0x706f706e: // "popn" — the online profile form was opened
        refreshOnlineProfileScreen();
        return true;

    case 0x70726f66: // "prof" — open the profile screen
        hideOptionsRoot();
        showProfileSelection( 0 );
        return true;

    case 0x6d6f706e: // "mopn" — the main menu asks which entries are selectable
        updateMainMenuVisibility();
        return true;

    case 0x70636c73: // "pcls" — the profile screen was closed
        hideProfileSelection();
        if ( !field23_0x35 ) {
            showOptionsRoot();
        }
        return true;

    case 0x6f706f75: // "opou" — leaving the options page, back to the main menu
        if ( gpPlayerData->getLoadedSave()->getDriver()->isValid() ) {
            gpPlayerData->requestSave( 1 );
        } else {
            OTDU_UNIMPLEMENTED_ONCE; // TODO: PLAYERSAVE (FUN_0097bf40)
        }

        showOptionsRoot();
        return true;

    case 0x7073656c: // "psel" — a profile line was highlighted
        selectProfile();
        return false;

    case 0x7064656c:             // "pdel" — delete the selected profile
    case 0x706e6577:             // "pnew"
    case 0x7063616e:             // "pcan"
    case 0x736e6473:             // "snds"
        OTDU_UNIMPLEMENTED_ONCE; // TODO: PLAYERSAVE
        return false;

    case 0x77696467: // "widg"
        if ( pMessage[1] == FlashMessage( "kmap" ) ) {
            return keymapList.onMessage( pMessage + 2, pPlayer );
        }
        return false;

    default:
        return false;
    }
}

void GSPCOptions::openMainMenu( bool bInGameOverlay ) {
    // FUN_007c52f0
    pFlashPlayer->setVariableValue( "/:IsWrongProfile", uint32_t( 0u ) );

    AchievementsData *pAchievements =
        gpPlayerData->getLoadedSave()->getAchievements();
    const bool bSimuModeLocked = pAchievements->isRewardLocked(
        AchievementsData::kSimuModeRewardHashcode );
    pFlashPlayer->setVariableValue( "/:IsSimuModeAvailable",
                                    int32_t( !bSimuModeLocked ) );

    bMoviesNeedReenable = false;

    const int32_t numMovies = gpFlash->getNumMovies( 1 );
    for ( int32_t movieIndex = 0; movieIndex < numMovies; movieIndex++ ) {
        MoviePlayer *pMovie = gpFlash->getMoviePlayer( 1, movieIndex );
        if ( pMovie != nullptr ) {
            pMovie->Flags |= 1u;
        }
    }

    if ( bInGameOverlay ) {
        for ( int32_t movieIndex = 0; movieIndex < numMovies; movieIndex++ ) {
            MoviePlayer *pMovie = gpFlash->getMoviePlayer( 1, movieIndex );
            if ( pMovie != nullptr ) {
                pMovie->field3_0xac8 |= 1;
            }
        }

        for ( int32_t movieIndex = 0; movieIndex < numMovies; movieIndex++ ) {
            MoviePlayer *pMovie = gpFlash->getMoviePlayer( 1, movieIndex );
            if ( pMovie == nullptr ) {
                continue;
            }
            pMovie->field5_0xad0 |= 1;

            FlashPlayer *pMoviePlayer = pMovie->MovieBank.getFlashPlayer();
            if ( pMoviePlayer != nullptr &&
                 pMoviePlayer->getBoundFlashResource() != nullptr ) {
                pMoviePlayer->getBoundFlashResource()->setDrawMode(
                    ( pMovie->field5_0xad0 != 0 ) ? 2 : 0 );
            }
        }
    }

    if ( gpFlash->getMouse() != nullptr ) {
        bSavedMouseEnabled = gpFlash->getMouse()->field12_0x30;
        gpFlash->getMouse()->field12_0x30 = false;
    }

    if ( gpActiveGameMode != nullptr ) {
        gpActiveGameMode->setsMessageBoxVisible( true );
    }

    bWasGameAlreadyPaused =
        ( gpTimer != nullptr ) && ( gpTimer->GameSpeed == 0.0f );

    bool bFreezeGame = true;
    if ( gpActiveGameMode != nullptr &&
         gpActiveGameMode->getCurrentGameMode() == GM_Mission ) {
        OTDU_UNIMPLEMENTED_ONCE; // TODO: GameLogic
    } else if ( gpActiveGameMode != nullptr &&
                gpActiveGameMode->getCurrentGameMode() == GM_FreeRide ) {
        OTDU_UNIMPLEMENTED_ONCE; // TODO: ONLINE
    }

    if ( !bWasGameAlreadyPaused ) {
        if ( bFreezeGame ) {
            if ( gpTimer != nullptr ) {
                if ( gpTimer->PreviousGameSpeed != gpTimer->GameSpeed &&
                     gpTimer->GameSpeed != 0.0f ) {
                    gpTimer->PreviousGameSpeed = gpTimer->GameSpeed;
                }
                gpTimer->GameSpeed = 0.0f;
            }
            if ( gpWeather != nullptr ) {
                gpWeather->setUpdateTimerEnabled( false );
            }
        }

        if ( gpAudio != nullptr ) {
            savedAudioMixState = gpAudio->getMixState();
            gpAudio->changeMixState( gpAudio->getMixFlags( "ClientMenuSFX" ) );
        }
    }

    bMainMenuOpened = true;
    bCloseRequested = false;

    showOptionsRoot();
    pFlashPlayer->showFrame( "open", "/options_root", true );
    bOptionsRootHidden = false;

    setNotificationClipsVisible( false );
    pendingMenuAction = MA_None;
}

void GSPCOptions::closeMainMenu() {
    // FUN_007c50b0
    bMainMenuOpened = false;
    field23_0x35 = false;

    const int32_t numMovies = gpFlash->getNumMovies( 1 );
    for ( int32_t movieIndex = 0; movieIndex < numMovies; movieIndex++ ) {
        MoviePlayer *pMovie = gpFlash->getMoviePlayer( 1, movieIndex );
        if ( pMovie == nullptr ) {
            continue;
        }
        pMovie->field5_0xad0 &= ~1;

        FlashPlayer *pMoviePlayer = pMovie->MovieBank.getFlashPlayer();
        if ( pMoviePlayer != nullptr &&
             pMoviePlayer->getBoundFlashResource() != nullptr ) {
            pMoviePlayer->getBoundFlashResource()->setDrawMode(
                ( pMovie->field5_0xad0 != 0 ) ? 2 : 0 );
        }
    }

    for ( int32_t movieIndex = 0; movieIndex < numMovies; movieIndex++ ) {
        MoviePlayer *pMovie = gpFlash->getMoviePlayer( 1, movieIndex );
        if ( pMovie != nullptr ) {
            pMovie->field3_0xac8 &= ~1;
        }
    }

    hideOptionsRoot();
    setNotificationClipsVisible( true );

    if ( gpActiveGameMode != nullptr ) {
        gpActiveGameMode->setsMessageBoxVisible( false );
    }

    if ( gpTestDriveInstance != nullptr &&
         gpTestDriveInstance->getActiveGameMode() != GM_Login ) {
        if ( pendingMenuAction == MA_QuitToDesktop ) {
            OTDU_UNIMPLEMENTED_ONCE; // TODO: PLAYERDATA
            exitCountdown = 10;
            pendingMenuAction = MA_QuitToDesktop;
            return;
        }
        if ( pendingMenuAction == MA_NewGame ) {
            GMLogin::RequestNewGame();
            switchToLoginGameMode();

            OTDU_UNIMPLEMENTED_ONCE;
            return;
        }
        if ( pendingMenuAction == MA_ChangeProfile ) {
            gpPlayerData->requestSave( 1 );
            exitCountdown = 10;
            pendingMenuAction = MA_ChangeProfile;
            return;
        }
    }

    if ( !bWasGameAlreadyPaused ) {
        bool bRestoreGameSpeed = true;
        if ( gpActiveGameMode != nullptr &&
             gpActiveGameMode->getCurrentGameMode() == GM_Mission ) {
            OTDU_UNIMPLEMENTED_ONCE; // TODO: GameLogic
        } else if ( gpActiveGameMode != nullptr &&
                    gpActiveGameMode->getCurrentGameMode() == GM_FreeRide ) {
            OTDU_UNIMPLEMENTED_ONCE; // TODO: ONLINE
        }

        if ( bRestoreGameSpeed ) {
            if ( gpTimer != nullptr ) {
                gpTimer->GameSpeed = gpTimer->PreviousGameSpeed;
            }
            if ( gpWeather != nullptr ) {
                gpWeather->setUpdateTimer( true, kWeatherResumeInterval );
            }
        }

        if ( gpAudio != nullptr ) {
            gpAudio->changeMixState( savedAudioMixState );
        }
    }

    if ( gpFlash->getMouse() != nullptr && bSavedMouseEnabled ) {
        gpFlash->getMouse()->field12_0x30 = true;
    }

    bMoviesNeedReenable = true;
}

void GSPCOptions::onMainMenuChoice( eMainMenuOption choice ) {
    // FUN_007c59d0
    switch ( choice ) {
    case MMO_Continue:
        pendingMenuAction = MA_Resume;
        field23_0x35 = true;
        bCloseRequested = true;
        break;

    case MMO_QuitToDesktop:
        onMainMenuSelection( MMO_Continue );
        break;

    case MMO_ChangeProfile:
        hideOptionsRoot();
        showProfileSelection( 0 );
        break;

    case MMO_Options:
        hideOptionsRoot();
        mngOptions.refreshOptionsScreen();
        break;

    case MMO_Quit:
        bMainMenuOpened = true;
        pendingMenuAction = MA_QuitToDesktop;
        onMainMenuSelection( MMO_Quit );
        break;

    case MMO_HardcoreMode:
        pendingMenuAction = MA_Resume;
        onMainMenuSelection( MMO_HardcoreMode );
        break;

    default:
        break;
    }
}

void GSPCOptions::onMainMenuSelection( eMainMenuOption selection ) {
    // FUN_007c5650
    switch ( selection ) {
    case MMO_Continue:
        if ( gpPlayerData->getSaveLoadState() != 1 &&
             gpTestDriveInstance->getActiveGameMode() == GM_Login ) {
            onClose( ePopupAnswer::PA_Accept, nullptr, 0 );
            return;
        }

        TestDrive::DisplayModalPrompt( true, kClearCurrentGameHashcode, 0,
                                       nullptr, this, nullptr, 0,
                                       "_Clear current game ? Sure ?", true,
                                       false, MPT_Warning, gpFlashMessageBox );
        return;

    case MMO_QuitToDesktop:
        TestDrive::DisplayModalPrompt( true, kReturnToWindowsHashcode, 0,
                                       nullptr, this, nullptr, 1,
                                       "_Return to Windows ?", true, false,
                                       MPT_Warning, gpFlashMessageBox );
        return;

    case MMO_ChangeProfile:
        if ( gpTestDriveInstance->getActiveGameMode() == GM_Login ||
             gUserProfileRegister.getActiveProfileHashcode() ==
                 activeProfileHashcode ) {
            onClose( ePopupAnswer::PA_Accept, nullptr, 2 );
            return;
        }

        TestDrive::DisplayModalPrompt(
            true, kChangeProfileHashcode, 0, nullptr, this, nullptr, 2,
            "_Change profile ?", true, false, MPT_Warning, gpFlashMessageBox );
        return;

    case MMO_HardcoreMode:
        gpFlash->enablePlayer( "CONFIGPC", ModalMessageBox::MMBDS_Closed, false,
                               0 );
        if ( FlashPlayer *pGeneral = gpFlash->getFlashPlayer( "GENERAL" ) ) {
            pGeneral->showFrame( "open", "/simu_avert", true );
        }
        return;

    case MMO_Quit:
        settlePoliceFine();
        OTDU_UNIMPLEMENTED_ONCE; // TODO: FRONTEND

        onMainMenuSelection( ( pendingMenuAction == MA_QuitToDesktop )
                                 ? MMO_QuitToDesktop
                                 : MMO_ChangeProfile );
        return;

    default:
        return;
    }
}

void GSPCOptions::onClose( ePopupAnswer answer, void *, int32_t param_3 ) {
    // FUN_007c47b0
    bPendingPopupAnswer = true;
    pendingPopupAnswer = answer;
    pendingPopupUserData = param_3;
}

void GSPCOptions::handlePendingPopupAnswer() {
    // FUN_007c5a90
    const int32_t userData = pendingPopupUserData;
    const bool bAccepted = ( pendingPopupAnswer == ePopupAnswer::PA_Accept );
    bPendingPopupAnswer = false;

    switch ( userData ) {
    case 0: // "_Clear current game ? Sure ?"
        if ( bAccepted ) {
            field23_0x35 = true;
            bCloseRequested = true;
            pendingMenuAction = MA_NewGame;
        }
        break;

    case 1: // return to Windows
        if ( bAccepted ) {
            settlePoliceFine();
            field23_0x35 = true;
            bCloseRequested = true;
            pendingMenuAction = MA_QuitToDesktop;
        }
        break;

    case 2: // change profile
        if ( bAccepted ) {
            settlePoliceFine();
            beginProfileChange();
        }
        break;

    case 4: // police fine confirmation
        if ( !bAccepted ) {
            pendingMenuAction = MA_None;
            return;
        }
        onMainMenuSelection( ( pendingMenuAction == MA_QuitToDesktop )
                                 ? MMO_QuitToDesktop
                                 : MMO_ChangeProfile );
        break;

    case 5:
        gpFlash->enablePlayer( "CONFIGPC", ModalMessageBox::MMBDS_Closed, true,
                               0 );
        pendingMenuAction = MA_Resume;
        field23_0x35 = true;
        bCloseRequested = true;
        break;

    default:
        break;
    }
}

void GSPCOptions::hideProfileSelection() {
    // FUN_007c3560
    FlashClip clip;
    if ( pFlashPlayer->getClipPlayer( "/profile", &clip ) &&
         clip.pSprite != nullptr ) {
        clip.setProperty( eFlashProperty::FP_Visible, false );
    }
    bProfileScreenOpened = false;
}

void GSPCOptions::updateMainMenuVisibility() {
    // FUN_007c4fe0
    const int32_t playerDataState = gpPlayerData->getSaveLoadState();

    const eGameMode activeGameMode = gpTestDriveInstance->getActiveGameMode();
    const int32_t bHasGameInProgress =
        int32_t( playerDataState == 1 || activeGameMode != GM_Login );
    const int32_t bNotIntro = int32_t( activeGameMode != GM_Intro );

    pFlashPlayer->setVariableValue( "/options_root:isReco",
                                    bHasGameInProgress );
    pFlashPlayer->setVariableValue( "/options_root:isSimu",
                                    bHasGameInProgress );
    pFlashPlayer->setVariableValue( "/options_root:isNgam", bNotIntro );
    pFlashPlayer->setVariableValue( "/options_root:isProf", bNotIntro );
    pFlashPlayer->setVariableValue( "/options_root:isOpti",
                                    int32_t( playerDataState != 0 ) );
    pFlashPlayer->setVariableValue( "/options_root:isQuit", int32_t( 1 ) );
}

void GSPCOptions::refreshOnlineProfileScreen() {
    // FUN_007c3e80
    const int32_t numProfiles = gUserProfileRegister.getNumProfiles();

    pFlashPlayer->setVariableValue( "/:nickname", nickname );
    pFlashPlayer->setVariableValue( "/:email", email );
    pFlashPlayer->setVariableValue( "/:password", password );

    gatherProfileEntries();
    refreshProfileList();

    if ( numProfiles == 0 || profileScreenMode == 2 ) {
        openNewProfilePopup();
    }
}

void GSPCOptions::gatherProfileEntries() {
    // FUN_0089b6f0
    for ( int32_t i = 0; i < kMaxProfileEntries; i++ ) {
        profileNames[i][0] = '\0';
        profileConnected[i] = false;
    }

    const int32_t numProfiles = gUserProfileRegister.getNumProfiles();
    for ( int32_t i = 0; i < numProfiles && i < kMaxProfileEntries; i++ ) {
        const char *pProfileName =
            gUserProfileRegister.getProfileNameByIndex( i );
        if ( pProfileName == nullptr ) {
            continue;
        }

        strncpy( profileNames[i], pProfileName, kMaxProfileNameLength );
        profileNames[i][kMaxProfileNameLength - 1] = '\0';

        LocalPlayerData profilePlayerData = {};
        if ( gpPlayerData->readProfileOptions(
                 UserProfileRegister::HashProfileName( pProfileName ),
                 profilePlayerData ) ) {
            strncpy( profileNames[i], profilePlayerData.Nickname,
                     kMaxProfileNameLength );
            profileNames[i][kMaxProfileNameLength - 1] = '\0';
            profileConnected[i] = profilePlayerData.bOnlineAccount;
        }
    }
}

int32_t GSPCOptions::refreshProfileList() {
    // FUN_007c3790
    int32_t numProfiles = gUserProfileRegister.getNumProfiles();
    pFlashPlayer->setVariableValue( "/:NoProfile",
                                    int32_t( numProfiles == 0 ) );

    if ( numProfiles < 0 ) {
        numProfiles = 0;
    } else if ( numProfiles > kMaxDisplayedProfiles ) {
        numProfiles = kMaxDisplayedProfiles;
    }
    pFlashPlayer->setVariableValue( "/profile:nb_profiles", numProfiles );

    int32_t selectedProfile = 0;
    for ( int32_t i = 0; i < numProfiles; i++ ) {
        char pVariableName[128];

        sprintf( pVariableName, "/profile:connected_profile%d", i + 1 );
        pFlashPlayer->setVariableValue( pVariableName,
                                        int32_t( profileConnected[i] ) );

        const char *pProfileName =
            gUserProfileRegister.getProfileNameByIndex( i );

        sprintf( pVariableName, "/profile/profile%d:name", i + 1 );
        pFlashPlayer->setVariableValueNonLinked( pVariableName,
                                                 profileNames[i] );

        if ( pProfileName != nullptr &&
             UserProfileRegister::HashProfileName( pProfileName ) ==
                 gUserProfileRegister.getActiveProfileHashcode() ) {
            selectedProfile = i;
        }
    }

    pFlashPlayer->setVariableValue( "/profile:cur_profile",
                                    selectedProfile + 1 );

    pFlashPlayer->setVariableValueNonLinked(
        "/:CurrentProfile",
        ( numProfiles == 0 ) ? "" : profileNames[selectedProfile] );

    return numProfiles;
}

void GSPCOptions::openNewProfilePopup() {
    // FUN_007c35f0
    if ( profileScreenMode == 2 ) {
        const LocalPlayerData &localPlayerData =
            gpPlayerData->getLocalPlayerData();

        strncpy( nickname, localPlayerData.Nickname,
                 sizeof( localPlayerData.Nickname ) );
        nickname[sizeof( localPlayerData.Nickname )] = '\0';
        strncpy( email, localPlayerData.Email,
                 sizeof( localPlayerData.Email ) );
        email[sizeof( localPlayerData.Email )] = '\0';
        strncpy( password, localPlayerData.Password,
                 sizeof( localPlayerData.Password ) );
        password[sizeof( localPlayerData.Password )] = '\0';
    }

    pFlashPlayer->showFrame( "open", "/profile/new_profile", true );
}

void GSPCOptions::showOptionsRoot() {
    // FUN_007c48d0
    FlashClip clip;
    if ( pFlashPlayer->getClipPlayer( "/options_root", &clip ) &&
         clip.pSprite != nullptr ) {
        clip.setProperty( eFlashProperty::FP_Visible, true );
    }
    bOptionsRootHidden = false;
}

void GSPCOptions::hideOptionsRoot() {
    // FUN_007c4930
    FlashClip clip;
    if ( pFlashPlayer->getClipPlayer( "/options_root", &clip ) &&
         clip.pSprite != nullptr ) {
        clip.setProperty( eFlashProperty::FP_Visible, false );
    }
    bOptionsRootHidden = true;
}

void GSPCOptions::showProfileSelection( const int32_t mode ) {
    // FUN_007c3a60
    FlashClip clip;
    if ( pFlashPlayer->getClipPlayer( "/profile", &clip ) &&
         clip.pSprite != nullptr ) {
        clip.setProperty( eFlashProperty::FP_Visible, true );
    }

    pFlashPlayer->showFrame( "profiles_in", "/", true );

    bProfileScreenOpened = true;
    profileScreenMode = mode;
    field297_0x188 = false;

    pFlashPlayer->setVariableValue( "/:ForceSelection",
                                    uint32_t( mode != 0 ? 1u : 0u ) );
}

void GSPCOptions::closeOptionsMenu() {
    // FUN_007c4a10
    field23_0x35 = true;
    pFlashPlayer->showFrame( "close", "/options_root", true );

    if ( bProfileScreenOpened ) {
        pFlashPlayer->showFrame( "close", "/profile", true );
        bProfileScreenOpened = false;
    }

    if ( bSavedMouseEnabled && gpFlash->getMouse() != nullptr ) {
        gpFlash->getMouse()->field12_0x30 = true;
    }
}

void GSPCOptions::setNotificationClipsVisible( bool bVisible ) {
    // FUN_007c4c80
    uint32_t validGM = 0u;
    uint32_t movieIndex = 0u;
    if ( !gpFlash->getFlashMovieIndex( &validGM, &movieIndex, "GENERAL" ) ) {
        return;
    }

    MoviePlayer *pMovie =
        gpFlash->getMoviePlayer( int32_t( validGM ), int32_t( movieIndex ) );
    if ( pMovie == nullptr || !pMovie->MovieBank.isLoaded() ) {
        return;
    }

    FlashPlayer *pGeneral = pMovie->MovieBank.getFlashPlayer();
    if ( pGeneral == nullptr ) {
        return;
    }

    for ( const char *pClipName : kNotificationClips ) {
        FlashClip clip;
        if ( pGeneral->getClipPlayer( pClipName, &clip ) &&
             clip.pSprite != nullptr ) {
            clip.setProperty( eFlashProperty::FP_Visible, bVisible );
        }
    }
}

void GSPCOptions::selectProfile() {
    // FUN_007c36f0
    const int32_t selectedLine =
        Clamp( pFlashPlayer->getVariableAsInt( "/profile:cur_line" ) - 1, 0,
               kMaxDisplayedProfiles - 1 );

    const char *pProfileName =
        gUserProfileRegister.getProfileNameByIndex( selectedLine );
    const uint64_t profileHashcode =
        ( pProfileName != nullptr )
            ? UserProfileRegister::HashProfileName( pProfileName )
            : 0ull;
    const uint64_t activeHashcode =
        gUserProfileRegister.getActiveProfileHashcode();

    if ( profileHashcode == activeHashcode && profileScreenMode == 0 ) {
        field297_0x188 = false;
        activeProfileHashcode = activeHashcode;
        return;
    }

    activeProfileHashcode = profileHashcode;
    field297_0x188 = true;
}

void GSPCOptions::beginProfileChange() {
    // FUN_007c4a70
    pendingMenuAction = MA_ChangeProfile;

    static constexpr const char *kClipsHiddenOnProfileChange[] = {
        "/help", "/profile", "/menus", "/menus/ss_menus/ssm",
        "/menus/ss_menus/ssm2" };

    for ( const char *pClipName : kClipsHiddenOnProfileChange ) {
        FlashClip clip;
        if ( pFlashPlayer->getClipPlayer( pClipName, &clip ) &&
             clip.pSprite != nullptr ) {
            clip.setProperty( eFlashProperty::FP_Visible, false );
        }
    }

    pFlashPlayer->showFrame( "default", "/profile", false );
    hideOptionsRoot();

    OTDU_UNIMPLEMENTED_ONCE; // TODO: ONLINE

    field23_0x35 = true;
    bCloseRequested = true;
}

void GSPCOptions::switchToLoginGameMode() {
    if ( gpActiveGameMode != nullptr ) {
        gpActiveGameMode->requestExit();
    }

    gpTestDriveInstance->setNextGameMode( GM_Login );
    gpTestDriveInstance->setGMChangeInProgressFlag();
}

void GSPCOptions::settlePoliceFine() {
    // FUN_007c5570
    OTDU_UNIMPLEMENTED_ONCE; // TODO: AI/POLICE (infringement settlement)
}
