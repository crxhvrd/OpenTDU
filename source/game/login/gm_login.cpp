#include "shared.h"
#include "gm_login.h"

#include "game/game_cheats.h"
#include "game/living/gm_living.h"

#include "database/gs_database.h"
#include "filesystem/gs_dirty_disk.h"
#include "filesystem/gs_file.h"
#include "flash/entities/flash_panel.h"
#include "flash/gs_flash.h"
#include "flash/implementation/flash_resource.h"
#include "flash/implementation/flash_sprite.h"
#include "flash/modal_message_box.h"
#include "core/time.h"
#include "game/gs_tutorial.h"
#include "game/scenaric/gs_scenaric.h"
#include "gs_cd_key.h"
#include "gs_timer.h"
#include "input/ed_dev.h"
#include "input/input_device.h"
#include "online/ed_net_gamespy_manager.h"
#include "online/gs_online.h"
#include "online/p2p_game_service.h"
#include "physics/vehicle/physics_vehicle.h"
#include "game/vehicle/gs_car_display.h"
#include "game/gs_intro_pool.h"
#include "player_data/achievements_data.h"
#include "player_data/driver.h"
#include "player_data/player_save.h"
#include "player_data/gs_playerdata.h"
#include "player_data/opentdu_settings.h"
#include "player_data/save_game_manager.h"
#include "player_data/user_profile.h"
#include "render/ed_render.h"
#include "render/gs_render.h"
#include "system/ed_sys.h"
#include "system/gs_pc_options.h"
#include "game/scenaric/game_intro_tutorial.h"

#include "world/gs_world.h"
#include "world/streaming_manager.h"

#include "tdu_instance.h"

static void ApplySavedVideoSettings() {
    // FUN_0074dca0
    static constexpr uint32_t kFallbackWidth = 800u;
    static constexpr uint32_t kFallbackHeight = 600u;

    do {
        gEdSys.pollEvents();
        gEdRender.flushDrawCommands( false );
    } while ( !gEdSys.isWindowActivated() );
    gEdRender.flushDrawCommands( false );

    LocalPlayerData &localPlayerData = gpPlayerData->getLocalPlayerData();

    if ( gpRender != nullptr && gpRender->isInitialized() &&
         gEdRender.pRenderDevice != nullptr ) {
        GPUAdapterDesc adapterInfos = {};
        if ( gEdRender.pRenderDevice->retrieveAdapterInfos( 0u,
                                                            &adapterInfos ) ) {
            const bool bSameAdapter =
                localPlayerData.GPUDeviceId == adapterInfos.DeviceID &&
                localPlayerData.GPUSubSysId == adapterInfos.SubSysId &&
                localPlayerData.GPUDriverRevision == adapterInfos.Revision &&
                localPlayerData.GPUVendorId == adapterInfos.VendorId;

            localPlayerData.GPUDeviceId = adapterInfos.DeviceID;
            localPlayerData.GPUSubSysId = adapterInfos.SubSysId;
            localPlayerData.GPUDriverRevision = adapterInfos.Revision;
            localPlayerData.GPUVendorId = adapterInfos.VendorId;

            if ( bSameAdapter ) {
                if ( localPlayerData.RenderWidth == gWindowWidth &&
                     localPlayerData.RenderHeight == gWindowHeight &&
                     localPlayerData.RefreshRate ==
                         uint32_t( gPickedRefreshRate ) &&
                     localPlayerData.AntiAliasing == gpRender->getAAMethod() &&
                     localPlayerData.bHDREnabled == gpRender->isHDREnabled() ) {
                    return;
                }

                OTDU_LOG_DEBUG(
                    "Restoring saved video mode %ux%u @ %uHz (AA %d, HDR "
                    "%d)\n",
                    localPlayerData.RenderWidth, localPlayerData.RenderHeight,
                    localPlayerData.RefreshRate,
                    int32_t( localPlayerData.AntiAliasing ),
                    int32_t( localPlayerData.bHDREnabled ) );

                if ( gpRender->resize( localPlayerData.RenderWidth,
                                       localPlayerData.RenderHeight,
                                       localPlayerData.RefreshRate,
                                       localPlayerData.AntiAliasing,
                                       localPlayerData.bHDREnabled ) ) {
                    return;
                }
            }
        }
    }

    OTDU_LOG_WARN( "Saved video mode could not be restored; falling back to "
                   "the mode the game came up in\n" );

    if ( gpRender == nullptr ) {
        return;
    }

    if ( !gpRender->resize( gBootDisplayMode.Width, gBootDisplayMode.Height,
                            gBootDisplayMode.RefreshRate,
                            eAntiAliasingMethod::AAM_Disabled, false ) &&
         !gpRender->resize( kFallbackWidth, kFallbackHeight,
                            gBootDisplayMode.RefreshRate,
                            eAntiAliasingMethod::AAM_Disabled, false ) ) {
        OTDU_LOG_ERROR( "Could not bring the renderer up in any display "
                        "mode; giving up\n" );
        gpTestDriveInstance->requestExit();
        return;
    }

    gpRender->setLODQuality( 1u );

    localPlayerData.LODQuality = gpRender->getLODQuality();
    localPlayerData.bHDREnabled = gpRender->isHDREnabled();
    localPlayerData.AntiAliasing = gpRender->getAAMethod();
    localPlayerData.RefreshRate = uint32_t( gPickedRefreshRate );
    localPlayerData.RenderHeight = gWindowHeight;
    localPlayerData.RenderWidth = gWindowWidth;
}

static constexpr const char *kAtariCopyright = "(C) Atari Inc"; // DAT_00f99784
static constexpr const char *kConnectingToServerMessage =
    "#Connecting to TestDrive server..."; // DAT_00f364bc

// Hashcode of  '_Connection 90s'
static constexpr uint64_t kConnection90sHashcode = 0x33f400a;
// Hashcode of 'connecting to server'
static constexpr uint64_t kConnectingHashcode = 0x52f0d4;

// Makes GMLogin skip its boot animation and go straight back out to the game
// mode the player came from
static bool BOOL_010e7d18 = false; // DAT_010e7d18

GMLogin *gpGMLogin = nullptr; // DAT_010e7d14

void GMLogin::RequestNewGame() { BOOL_010e7d18 = true; }

static uint32_t GetTickCountMs() {
    return uint32_t( MonotonicToMs( GetTimeMonotonic() ) );
}

GMLogin::GMLogin()
    : GameMode(),
      fade(),
      flashLocalize(),
      flashLogin( this ),
      flashCounter(),
      pPanelOnline( new( TestDrive::Alloc( sizeof( FlashPanelOnline ) ) )
                        FlashPanelOnline() ),
      pGeneralFlashPlayer( nullptr ),
      pConfigPCFlashPlayer( nullptr ),
      pLoginFlashPlayer( nullptr ),
      menuActionCountdown( -1 ),
      idleTime( 0.0f ),
      saveWaitStartTicks( 0u ),
      waitExitStartTicks( 0u ),
      state( GLS_LegalDisplay ) {
    // FUN_0074e140
    registerManager( &fade );
    registerManager( &flashLocalize );
    registerManager( &flashLogin );
    registerManager( &flashCounter );

    OTDU_ASSERT( gpGMLogin == nullptr );
    gpGMLogin = this;

    transitionFlags = 2;
    gpScenaric->getIntroTutorial().reset();

    gpPlayerData->setSaveEnabled( false );
    gpPCOptions->setMainMenuAllowed( false );

    gbScenarioTutorialEnabled = true;
}

GMLogin::~GMLogin() {
    // FUN_0074e080

    if ( pPanelOnline != nullptr ) {
        pPanelOnline->~FlashPanelOnline();
        TestDrive::Free( pPanelOnline );
        pPanelOnline = nullptr;
    }

    gpPCOptions->setMainMenuAllowed( true );

    gpGMLogin = nullptr;
    gpMngFade = nullptr;
}

void GMLogin::reset() {
    // FUN_0074ecc0
    gpTestDriveInstance->clearGMChangeInProgressFlag();

    pGeneralFlashPlayer = gpFlash->getFlashPlayer( "GENERAL" );
    pConfigPCFlashPlayer = gpFlash->getFlashPlayer( "CONFIGPC" );
    pLoginFlashPlayer = gpFlash->getFlashPlayer( "LOG_IN" );

    // Original game returns version only (i.e. for 'MC 1.66 A' => '1.66 A')
    pGeneralFlashPlayer->setVariableValue( "/:BetaVersion",
                                           TestDrive::GetVersionID() );

    FlashClip local_8;
    bool bVar3 = pGeneralFlashPlayer->getClipPlayer( "/version", &local_8 );
    if ( bVar3 ) {
        ScopedMutexLock lock = gpFlash->scopedLock();
        gpPreviousActiveFlashResource = gpActiveFlashResource;
        ( local_8.pSprite )->getDisplayList()->getFlagsRW() =
            ( local_8.pSprite )->getDisplayList()->getFlagsRW() & 0xfffffffe;
    }

    pPanelOnline->pMovie = pConfigPCFlashPlayer;
    gpPlayerData->subscribe( pPanelOnline );

    pLoginFlashPlayer->setVariableValue( "/:IsEsrbIcon", 0u );
    const char *pcVar7 = gpDatabase->getStringByHashcode( 0, 0x3c227c2 );
    pLoginFlashPlayer->setVariableValue( "/:m_TextLegal_01", pcVar7 );
    pLoginFlashPlayer->setVariableValue( "/:Version", "OpenTDU" );
#if 0
    pLoginFlashPlayer->setVariableValue( "/:CopyrightText", kAtariCopyright );
#else
    pLoginFlashPlayer->setVariableValue( "/:CopyrightText",
                                         TestDrive::GetVersionID() );
#endif
    pConfigPCFlashPlayer->goToFrameAndPlay( 1, "/", false );
    pcVar7 = "/:IsBoot";

    pConfigPCFlashPlayer->setVariableValue( "/:IsBoot", 1 );
    if ( !BOOL_010e7d18 ) {
        pcVar7 = "/";
        pLoginFlashPlayer->goToFrameAndPlay( 0, "/", true );
    } else {
        pLoginFlashPlayer->getBoundFlashResource()->setUnknownDword( 2 );
    }

    state = GLS_Signing;
    idleTime = gpTimer->SystemTotalTime;

    gpOnline->resetLoginState();

    AchievementsData *pAchievements =
        gpPlayerData->getLoadedSave()->getAchievements();
    if ( pAchievements->isReady() ) {
        pAchievements->setReady( false );
        pAchievements->notify( ACF_Challenge | ACF_Road | ACF_Collection,
                               false );
    }

    gpScenaric->clearObjectives();

    gpTestDriveInstance->clearGMChangeInProgressFlag();

    gpOnline->clearFieldA713();
    OTDU_UNIMPLEMENTED_ONCE; // TODO: ONLINE (CP2PFreeRideMgr +0x154ac = 1)

    gpFlash->setLocalizedStrings();

    OTDU_UNIMPLEMENTED_ONCE; // TODO: FRONTEND

    gSavegameManager.toggleProfileChange();

    gpScenaric->setCurrentSpotIndex( -1 );
    gbEnteringLivingHub = true;

    gbSimuModeEnabled = false;

    saveWaitStartTicks = 0u;
}

bool GMLogin::onMessage( FlashMessage *pMessage, FlashPlayer *pPlayer ) {
    // FUN_0074dea0
    const char *pTag = pMessage->pAsChar;
    const uint32_t tag = ( (uint32_t)(uint8_t)pTag[0] << 24 ) |
                         ( (uint32_t)(uint8_t)pTag[1] << 16 ) |
                         ( (uint32_t)(uint8_t)pTag[2] << 8 ) |
                         (uint32_t)(uint8_t)pTag[3];

    switch ( tag ) {
    case 0x68696f75: // "hiou" — reveal the legal/boot overlay
        pLoginFlashPlayer->showFrame( "show", "/stuff_to_hide", true );
        return true;

    case 0x6869696e: // "hiin" — hide it again
        pLoginFlashPlayer->showFrame( "hide", "/stuff_to_hide", true );
        return true;

    case 0x636b6b79: { // "ckky" — validate the typed CD key
        if ( state != GLS_SelectPad ) {
            return false;
        }

        char inputKey01[32] = {};
        char inputKey02[32] = {};
        char inputKey03[32] = {};
        char inputKey04[32] = {};
        char cdKey[135] = {};

        pPlayer->getVariableAsString( "/:input_key_01", inputKey01 );
        pPlayer->getVariableAsString( "/:input_key_02", inputKey02 );
        pPlayer->getVariableAsString( "/:input_key_03", inputKey03 );
        pPlayer->getVariableAsString( "/:input_key_04", inputKey04 );

        snprintf( cdKey, sizeof( cdKey ), "%s-%s-%s-%s", inputKey01, inputKey02,
                  inputKey03, inputKey04 );

        if ( !gpCDKey->isValidKey( cdKey ) ) {
            pPlayer->showFrame( "invalid", "/cd_key", true );
            return true;
        }

        gpCDKey->updateGameKey( cdKey );
        pPlayer->showFrame( "close", "/cd_key", true );
        state = GLS_WaitStartAnim;
        return true;
    }

    case 0x6c676c73: // "lgls" — the login movie finished its sequence
        if ( state == GLS_LoginToServer || state == GLS_ConnectLSP ) {
            state = GLS_WaitFileMap;
        }
        return true;

    case 0x7369696e: // "siin" — the player asked to sign in
        gpPCOptions->postPendingPopupAnswer( ePopupAnswer::PA_Accept, 5 );
        return true;

    default:
        return false;
    }
}

void GMLogin::onClose( ePopupAnswer answer, void *param_2, int32_t param_3 ) {
    // FUN_0074e390
    if ( param_3 == 0 ) {
        gpPCOptions->hideOptionsRoot();
        gpPCOptions->showProfileSelection( 2 );
        return;
    }

    if ( param_3 == 3 ) {
        saveWaitStartTicks = 0u;
        state = GLS_WaitForLoad;
        return;
    }

    if ( param_3 != 1 ) {
        if ( param_3 != 2 ) {
            return;
        }

        if ( answer == ePopupAnswer::PA_Accept ) {
            SDL_OpenURL( gpOnline->getProxyHostInterface().getUpdateURL() );
            TestDrive::RequestGameExit();
            return;
        }
    }

    gpPCOptions->hideOptionsRoot();
    gpPCOptions->showProfileSelection( 1 );
}

bool GMLogin::isMenuClipVisible() {
    // FUN_0074da10
    if ( gpFlashMessageBox->getNumOpenMsgBox() != 0 ||
         gpFlashMessageBox->getUnknownFlag() ) {
        return false;
    }

    static constexpr const char *kMenuClips[] = { "/options_root", "/profile" };
    for ( const char *pClipPath : kMenuClips ) {
        FlashClip clip;
        if ( !pConfigPCFlashPlayer->getClipPlayer( pClipPath, &clip ) ) {
            continue;
        }

        uint32_t flags = 0u;
        {
            ScopedMutexLock lock = gpFlash->scopedLock();
            flags = clip.pSprite->getDisplayList()->getFlagsRW();
            gpPreviousActiveFlashResource = gpActiveFlashResource;
        }

        if ( ( flags & 1 ) != 0 ) {
            return true;
        }
    }

    return false;
}

bool GMLogin::HasInputActivity() {
    // FUN_0074dbb0
    const int32_t numDevices = int32_t(
        gEdDev.MaxNumMouse + gEdDev.MaxNumKeyboard + gEdDev.MaxNumJoystick );
    for ( int32_t deviceIdx = 0; deviceIdx < numDevices; deviceIdx++ ) {
        Device *pDevice = gEdDev.ppDevices[deviceIdx];
        if ( pDevice != nullptr && pDevice->hasChannelBeenPressed() ) {
            return true;
        }
    }

    Device *pMouse = gEdDev.getDevice( eDeviceType::DT_Mouse, 0 );
    if ( pMouse == nullptr ) {
        return false;
    }

    static constexpr const char *kMouseAxis[] = { "LEFT", "RIGHT", "UP",
                                                  "DOWN" };
    for ( const char *pAxis : kMouseAxis ) {
        if ( pMouse->getChannelValue( pAxis ) != 0.0f ) {
            return true;
        }
    }

    return false;
}

void GMLogin::tick( float deltaTime, float totalTime ) {
    // FUN_0074f030

    switch ( state ) {
    case GLS_Signing:
        gpFlash->getMouse()->field12_0x30 = true;
        state = GLS_ConnectLSP;
        return;

    case GLS_ConnectLSP:
        if ( ( ~( transitionFlags >> 3 ) & 1 ) == 0 ) {
            return;
        }

        if ( BOOL_010e7d18 ) {
            startNewGame();

            gpPlayerData->setSaveEnabled( true );
            BOOL_010e7d18 = false;
            state = GLS_LeaveGameMode;

            pLoginFlashPlayer->stopClip( "/" );
            return;
        }

        state = GLS_LoginToServer;
        return;

    case GLS_LoginToServer:
        // Idle: waits for the login movie to report "lgls".
        return;

    case GLS_WaitFileMap:
        if ( gpFile->isUsingFilemap() && !gpFile->isFilemapActive() ) {
            gpDirtyDisk->onDataLoadFailure();
            return;
        }

        state = GLS_LegalDisplay;
        return;

    case GLS_LegalDisplay:
        if ( gpCDKey->hasValidGameKey() ) {
            state = GLS_WaitStartAnim;
            return;
        }

        pConfigPCFlashPlayer->showFrame( "open", "/cd_key", true );
        gpFlash->getMouse()->field12_0x30 = false;
        state = GLS_SelectPad;
        return;

    case GLS_SelectPad: {
        if ( !gpPCOptions->isMainMenuOpened() &&
             gpPCOptions->getPendingMenuAction() == MA_QuitToDesktop ) {
            pConfigPCFlashPlayer->showFrame( "close_quit", "/cd_key", true );
            gpFlash->getMouse()->field12_0x30 = true;
            menuActionCountdown = 0x32;
            state = GLS_LeaveApplication;
            return;
        }

        for ( int32_t keyIdx = 1; keyIdx <= 4; keyIdx++ ) {
            char varName[60] = {};
            char varValue[32] = {};

            snprintf( varName, sizeof( varName ), "/:input_key_0%d", keyIdx );
            pConfigPCFlashPlayer->getVariableAsString( varName, varValue );

            for ( char &c : varValue ) {
                c = char( toupper( uint8_t( c ) ) );
            }

            pConfigPCFlashPlayer->setVariableValueNonLinked( varName,
                                                             varValue );
        }
        return;
    }

    case GLS_WaitStartAnim: {
        gpPlayerData->resetPlayerData( true, true );
        gpPlayerData->setSaveEnabled( true );

        int32_t numProfiles = gUserProfileRegister.getNumProfiles();
        if ( numProfiles == 0 ) {
            // NOTE: the original signs the player up online here and only
            // registers 'default' when an orphan savegame was found; the port
            // has no sign-up flow so it always creates the local profile.
            const uint64_t defaultHashcode =
                UserProfileRegister::HashProfileName( "default" );
            if ( gUserProfileRegister.registerProfile( "default" ) ) {
                gUserProfileRegister.initialize( defaultHashcode );
                gUserProfileRegister.updateProfileList( defaultHashcode );
            }

            numProfiles = gUserProfileRegister.getNumProfiles();
        }

        if ( numProfiles == 0 ) {
            gpPCOptions->setMainMenuAllowed( true );
            gpPCOptions->openMainMenu( false );
            gpPCOptions->hideOptionsRoot();
            gpPCOptions->showProfileSelection( 0 );
            state = GLS_MainMenu;
            return;
        }

        gUserProfileRegister.initialize(
            gUserProfileRegister.getProfileListHashcode() );
        if ( gUserProfileRegister.getProfileIndex(
                 gUserProfileRegister.getActiveProfileHashcode() ) < 0 ) {
            gUserProfileRegister.initialize(
                UserProfileRegister::HashProfileName(
                    gUserProfileRegister.getProfileNameByIndex( 0 ) ) );
        }

        gbSaveAllowed = true;
        gpPlayerData->loadActiveProfileSave();

        OpenTDUSettings::Load();
        ApplySavedVideoSettings();

        AchievementsData *pAchievements =
            gpPlayerData->getLoadedSave()->getAchievements();
        if ( !pAchievements->isReady() ) {
            pAchievements->setReady( true );
            if ( pAchievements->getField516_0x204() ) {
                pAchievements->setCurrentGameMode(
                    gpTestDriveInstance->getActiveGameMode() );
            }

            pAchievements->notify( ACF_Challenge | ACF_Road | ACF_Collection,
                                   false );
        }

        LocalPlayerData &localPlayerData = gpPlayerData->getLocalPlayerData();
        if ( localPlayerData.Nickname[0] == '\0' ) {
            strncpy( localPlayerData.Nickname,
                     gUserProfileRegister.getActiveProfileName().c_str(),
                     sizeof( localPlayerData.Nickname ) - 1 );
        }

        pConfigPCFlashPlayer->setVariableValueNonLinked(
            "/:CurrentProfile", localPlayerData.Nickname );
        gpOnline->setUsername( localPlayerData.Nickname );

        if ( localPlayerData.bOnlineAccount ) {
            state = GLS_WaitSaveLoaded;
            saveWaitStartTicks = 0u;
            return;
        }

        gpEdNetGamespyManager->resetConnect();
        if ( gpPlayerData->isOnline() ) {
            gpPlayerData->setOnline( false );
            gpPlayerData->notify( 0u );
        }

        gpPCOptions->openMainMenu( false );
        state = GLS_MainMenu;
        return;
    }

    case GLS_WaitSaveLoaded:
        if ( !gpFlashMessageBox->FUN_00403100() ) {
            TestDrive::DisplayModalPrompt( false, kConnection90sHashcode, 0,
                                           nullptr, this, nullptr, 3,
                                           "_Connection 90s", true, false,
                                           MPT_Warning, gpFlashMessageBox );

            saveWaitStartTicks = GetTickCountMs();
            return;
        }

        if ( saveWaitStartTicks != 0u &&
             ( saveWaitStartTicks + 5000u ) < GetTickCountMs() &&
             gpFlashMessageBox->FUN_00403100() ) {
            saveWaitStartTicks = 0u;

            gpFlashMessageBox->forceClose();

            state = GLS_WaitForLoad;
        }
        return;

    case GLS_WaitForLoad: {
        if ( gpFlashMessageBox->getInputState() !=
             ModalMessageBox::eInputState::MMBIS_None ) {
            return;
        }

        LocalPlayerData &localPlayerData = gpPlayerData->getLocalPlayerData();
        Driver *pDriver = gpPlayerData->getLoadedSave()->getDriver();
        // FUN_0088fed0
        strncpy( pDriver->Nickname, localPlayerData.Nickname,
                 sizeof( pDriver->Nickname ) - 1 );
        pDriver->notify( 0u );
        OTDU_UNIMPLEMENTED_ONCE; // TODO: ONLINE (copy the nickname to the
                                 // local P2PPlayer +0x20ec)

        gpEdNetGamespyManager->setNick( localPlayerData.Nickname );
        gpEdNetGamespyManager->setEmail( localPlayerData.Email );
        gpEdNetGamespyManager->setPassword( localPlayerData.Password );
        gpEdNetGamespyManager->setCDKey( gpCDKey->getGameKey() );
        if ( localPlayerData.bOffline ) {
            gpEdNetGamespyManager->resetConnectNewAccount();
        } else {
            gpEdNetGamespyManager->resetConnect();
        }

        gpFlashMessageBox->display( kConnectingHashcode, 10.0f, 0xb, nullptr,
                                    nullptr, false );
        state = GLS_DeletingBadSaves;
        return;
    }

    case GLS_DeletingBadSaves: {
        LocalPlayerData &localPlayerData = gpPlayerData->getLocalPlayerData();
        if ( localPlayerData.bOffline ) {
            const int32_t result =
                gpEdNetGamespyManager->pollConnectCreateAccount();
            if ( result == edNetGamespyManager::kPollCreateFailed ) {
                onConnectionFailed();
                state = GLS_MainMenu;
                return;
            }
            if ( result != edNetGamespyManager::kPollCreateOK ) {
                return;
            }
            localPlayerData.bOffline = false;
        } else {
            const int32_t result = gpEdNetGamespyManager->pollConnect();
            if ( result == edNetGamespyManager::kPollConnectFailed ) {
                onConnectionFailed();
                state = GLS_MainMenu;
                return;
            }
            if ( result != edNetGamespyManager::kPollConnectOK ) {
                return;
            }
        }

        gpOnline->startProxy();
        state = GLS_WaitForSave;
        return;
    }

    case GLS_WaitForSave:
        if ( gpCP2PGameService->getProxy().getResult() ==
             edNetProxy::R_Failed ) {
            onProxyFailed();
            state = GLS_MainMenu;
            return;
        }

        if ( !gpOnline->getServices().getDiscovery().isDone() ) {
            return;
        }

        state = GLS_NewContinueQuestion;
        connectToGameServer();
        return;

    case GLS_NewContinueQuestion:
        if ( !gpOnline->isLoginAnswered() ) {
            return;
        }

        if ( !gpOnline->isLoggedIn() ) {
            onLoginFailed( false );
            state = GLS_MainMenu;
            return;
        }

        if ( !gpPlayerData->isOnline() ) {
            gpPlayerData->setOnline( true );
            gpPlayerData->notify( 0u );
        }
        gpFlashMessageBox->close();
        gpPCOptions->setMainMenuAllowed( true );
        gpPCOptions->openMainMenu( false );

        gpOnline->getServices().requestNetConfig( "netconfig.xml" );
        OTDU_UNIMPLEMENTED_ONCE; // TODO: ONLINE (stats summary upload
                                 // FUN_0087cc40, Savegame::r_check
                                 // FUN_00871ac0 + FUN_008983f0 summary;
                                 // neither blocks login)

        waitExitStartTicks = GetTickCountMs();
        state = GLS_WaitExit;
        return;

    case GLS_WaitExit:
        if ( ( waitExitStartTicks + 3000u ) < GetTickCountMs() ) {
            state = GLS_MainMenu;
            return;
        }

        OTDU_UNIMPLEMENTED_ONCE; // TODO: ONLINE (SavegameCheck result +0xc
                                 // and SavegameSave vtbl+8, SecuROM payload)
        return;

    case GLS_MainMenu: {
        const float systemTotalTime = gpTimer->SystemTotalTime;

        if ( HasInputActivity() || systemTotalTime < idleTime ||
             !isMenuClipVisible() || !gpFlash->FUN_0099b240( "CONFIGPC", 0 ) ) {
            idleTime = systemTotalTime;
        }

        const float demoTimer = gpScenaric->getDemoTimer();
        if ( demoTimer > 0.0f && demoTimer <= ( systemTotalTime - idleTime ) ) {
            idleTime = systemTotalTime;

            gpScenaric->startDemoMode();

            gpPCOptions->setCloseRequested();

            menuActionCountdown = 2;
            state = GLS_LeaveGameMode;
            return;
        }

        const eMenuAction menuAction =
            gpPCOptions->isMainMenuOpened()
                ? MA_None
                : gpPCOptions->getPendingMenuAction();

        state = GLS_LeaveGameMode;
        switch ( menuAction ) {
        case MA_Resume:
            resumeSavedGame();
            return;

        case MA_NewGame:
            startNewGame();
            return;

        case MA_QuitToDesktop:
            menuActionCountdown = 0x14;

            if ( gpPlayerData->getLoadedSave()->getDriver()->isValid() ) {
                gpPlayerData->requestSave( 1 );
            }

            state = GLS_LeaveApplication;
            return;

        case MA_ChangeProfile:
            ChangeProfile( gpPCOptions->getActiveProfileHashcode() );
            return;

        default:
            state = GLS_MainMenu;
            return;
        }
    }

    case GLS_LeaveGameMode:
        if ( menuActionCountdown > 0 ) {
            menuActionCountdown--;
        }

        if ( menuActionCountdown == 0 ) {
            requestExit();
        }
        return;

    case GLS_LeaveApplication:
        if ( menuActionCountdown > 0 ) {
            menuActionCountdown--;
        }

        if ( menuActionCountdown == 0 &&
             !gSavegameManager.isDwordDiffFromPendingSaveCount() ) {
            TestDrive::RequestGameExit();
        }
        return;

    default:
        return;
    }
}

void GMLogin::resumeSavedGame() {
    // FUN_0074e420
    Driver *pDriver = gpPlayerData->getLoadedSave()->getDriver();

    gbScenarioTutorialEnabled =
        ( pDriver->TutorialState == kTutorialStatePending ) ||
        ( pDriver->TutorialState == kTutorialStateRunning );

    IPlayerData *pLoadedSave = gpPlayerData->getLoadedSave();
    if ( pLoadedSave != nullptr && pLoadedSave->getAvatar() != nullptr ) {
        TestDrive::RebakePlayerAvatar();
    }

    if ( pDriver->TutorialState == kTutorialStateIntro ) {
        if ( gpCarDisplay != nullptr ) {
            gpCarDisplay->reset();
        }
    } else if ( BOOL_010e74f4 ) {
        EnableWorldSpotStreaming();
    }

    if ( pDriver->bCheatDetected ) {
        gpOnline->notifyCheatDetected( pDriver );
    }

    gpTestDriveInstance->setNextGameMode( GM_Living );

    menuActionCountdown = 2;
}

void GMLogin::startNewGame() {
    // FUN_0074ea80
    Driver *pDriver = gpPlayerData->getLoadedSave()->getDriver();

    gpOnline->notifyCheatDetected( pDriver );

    gpOnline->resetForNewGame();

    if ( gpScenaric != nullptr ) {
        gpScenaric->getIntroTutorial().reset();
    }

    gpPlayerData->prepareActiveProfile();

    gpPlayerData->newGame( false );

    if ( gbNewGameSkipIntro ) {
        gpTestDriveInstance->setNextGameMode( GM_FreeRide );
    }

    gbScenarioTutorialEnabled = false;

    if ( !BOOL_010e74f4 && gpCarDisplay != nullptr ) {
        gpCarDisplay->reset();
    }

    LocalPlayerData &localPlayerData = gpPlayerData->getLocalPlayerData();
    if ( localPlayerData.Nickname[0] != '\0' ) {
        pConfigPCFlashPlayer->setVariableValueNonLinked(
            "/:CurrentProfile", localPlayerData.Nickname );
    }

    gpFile->deleteProfileSaveFolder(
        gUserProfileRegister.getActiveProfileHashcode(),
        eContextSource::CS_Savegame );

    // OTDU_ADDITION: OpenTDU.ini lives in the folder deleted above
    OpenTDUSettings::Save();

    AchievementsData *pAchievements =
        gpPlayerData->getLoadedSave()->getAchievements();
    if ( !pAchievements->isReady() ) {
        pAchievements->setReady( true );
        if ( pAchievements->getField516_0x204() ) {
            pAchievements->setCurrentGameMode(
                gpTestDriveInstance->getActiveGameMode() );
        }

        pAchievements->notify( ACF_Challenge | ACF_Road | ACF_Collection,
                               false );
    }

    gpPlayerData->requestSave( 2 );

    menuActionCountdown = 2;
}

void GMLogin::ChangeProfile( const uint64_t profileHashcode ) {
    // FUN_0074e300
    gpPlayerData->setSaveEnabled( false );
    gSaveStreamRevision = 0;

    gUserProfileRegister.initialize( profileHashcode );
    gUserProfileRegister.updateProfileList( profileHashcode );

    if ( gpActiveGameMode != nullptr ) {
        gpActiveGameMode->requestExit();
    }

    gbScenarioTutorialEnabled = true;

    gpTestDriveInstance->setNextGameMode( GM_Login );
    gpTestDriveInstance->setGMChangeInProgressFlag();

    gpOnline->resetForProfileChange();
}

void GMLogin::onConnectionFailed() {
    // FUN_0074e660
    if ( gpPlayerData->isOnline() ) {
        gpPlayerData->setOnline( false );
        gpPlayerData->notify( 0u );
    }
    gpFlashMessageBox->close();

    LocalPlayerData &localPlayerData = gpPlayerData->getLocalPlayerData();
    auto prompt = [this]( const uint64_t hashcode, const char *pMessage,
                          const int32_t userData, const int32_t dbIndex ) {
        TestDrive::DisplayModalPrompt( false, hashcode, dbIndex, nullptr, this,
                                       nullptr, userData, pMessage, true, false,
                                       MPT_Warning, gpFlashMessageBox );
    };

    switch ( int32_t( gpEdNetGamespyManager->getLastError() ) ) {
    case 2:
        prompt( 0x371739a, "_Gamespy service temporarily unavailable!!", 1, 0 );
        break;
    case 3:
        prompt( 0x362315a, "_Gamespy service unavailable!!", 1, 0 );
        break;
    case 4:
        prompt( 0x3bc383a, "_Gamespy Friends service unavailable!!", 1, 0 );
        break;
    case 5:
        prompt( 0x3346a9a, "_Login timeout!!", 1, 0 );
        break;
    case 6:
    case 0x14:
    case 0x16:
        prompt( 0x380b5da, "_Invalid Nick!!", 0, 0 );
        localPlayerData.bOffline = true;
        break;
    case 7:
        prompt( 0x39f3a5a, "_Invalid Email!!", 0, 0 );
        localPlayerData.bOffline = true;
        break;
    case 8:
    case 0x17:
        prompt( 0x38ff81a, "_Invalid Password!!", 0, 0 );
        localPlayerData.bOffline = true;
        break;
    case 9:
        prompt( 0x3ae7c9a, "_Invalid Profile!!", 0, 0 );
        localPlayerData.bOffline = true;
        break;
    case 0xc:
        prompt( 0x335f13a, "_Nick already in use!!", 0, 0 );
        localPlayerData.bOffline = true;
        break;
    case 10:
    case 0x15:
        prompt( 0x343acda, "_Login failed!!", 1, 0 );
        break;
    case 0x18:
        prompt( 0x5b0b0c,
                "_Someone is already logged on with this account. If you "
                "have just been disconnected, please wait a minute and try "
                "again.",
                1, 0xb );
        break;
    case 0x1f:
        prompt( 0x352ef1a, "_Invalid CD Key!!", 1, 0 );
        OTDU_UNIMPLEMENTED_ONCE; // TODO: ONLINE (DAT_00fa9884 = 2 when set:
                                 // invalidate the cached CD key state)
        break;
    default:
        prompt( 0x345337a, "_Connection failed.", 1, 0 );
        break;
    }

    gpPCOptions->setMainMenuAllowed( true );
    gpPCOptions->openMainMenu( false );
}

void GMLogin::onProxyFailed() {
    // FUN_0074e570
    if ( gpPlayerData->isOnline() ) {
        gpPlayerData->setOnline( false );
        gpPlayerData->notify( 0u );
    }
    gpFlashMessageBox->close();

    if ( gpOnline->getProxyHostInterface().getLastError() ==
         edNetProxyProtocol::IE_Rejected ) {
        TestDrive::DisplayModalPrompt( true, 0x36b802a, 0, nullptr, this,
                                       nullptr, 2, "_Download update", true,
                                       false, MPT_Warning, gpFlashMessageBox );
    } else {
        TestDrive::DisplayModalPrompt( false, 0x345337a, 0, nullptr, this,
                                       nullptr, 1, "_Connection failed.", true,
                                       false, MPT_Warning, gpFlashMessageBox );
    }

    gpPCOptions->setMainMenuAllowed( true );
    gpPCOptions->openMainMenu( false );
}

void GMLogin::onLoginFailed( const bool bSuccess ) {
    // FUN_0074e9a0
    gpPlayerData->setOnline( bSuccess );
    gpPlayerData->notify( 0u );
    gpFlashMessageBox->close();

    if ( !bSuccess ) {
        TestDrive::DisplayModalPrompt( false, 0x345337a, 0, nullptr, this,
                                       nullptr, 1, "_Connection failed.", true,
                                       false, MPT_Warning, gpFlashMessageBox );
    }

    gpPCOptions->setMainMenuAllowed( true );
    gpPCOptions->openMainMenu( false );
}

void GMLogin::connectToGameServer() {
    // FUN_0074ea40
    const OnlineServices &services = gpOnline->getServices();
    if ( services.getDiscovery().isSuccess() &&
         services.UserDatabase.IP != 0u ) {
        gpOnline->connectGameServer( 0 );
        return;
    }

    onLoginFailed( false );
    state = GLS_MainMenu;
}

void GMLogin::pause() {
    // FUN_0074ec00
    gpPlayerData->unsubscribe( pPanelOnline );

    pConfigPCFlashPlayer->setVariableValue( "/:IsBoot", 0 );
    pConfigPCFlashPlayer->getBoundFlashResource()->setClearBeforeDraw( false );

    pGeneralFlashPlayer = nullptr;
    pConfigPCFlashPlayer = nullptr;

    AchievementsData *pAchievements =
        gpPlayerData->getLoadedSave()->getAchievements();
    if ( !pAchievements->getField516_0x204() ) {
        pAchievements->setField516_0x204( true );
        if ( pAchievements->isReady() ) {
            pAchievements->setCurrentGameMode(
                gpTestDriveInstance->getActiveGameMode() );
        }

        pAchievements->notify( ACF_Challenge | ACF_Road | ACF_Collection,
                               false );
    }

    const int32_t numMovies = gpFlash->getNumMovies( 1 );
    for ( int32_t movieIndex = 0; movieIndex < numMovies; movieIndex++ ) {
        MoviePlayer *pMovie = gpFlash->getMoviePlayer( 1, movieIndex );
        if ( pMovie != nullptr ) {
            pMovie->Flags = pMovie->Flags & 0xfffffffd;
        }
    }
}
