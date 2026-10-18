#include "shared.h"
#include "tdu_instance.h"

#include "core/arg_parser.h"
#include "core/assert.h"
#include "core/event.h"
#include "core/locale.h"
#include "core/logger.h"
#include "core/mutex.h"
#include "core/thread.h"

#include "ai/gs_ai.h"
#include "audio/gs_ambiance.h"
#include "audio/gs_audio.h"
#include "audio/vehicle/gs_vehicle_sound.h"
#include "config/gs_config.h"
#include "database/gs_database.h"
#include "debug/gs_console.h"
#include "debug/gs_debug.h"
#include "debug/gs_profile.h"
#ifdef OTDU_USE_IMGUI
#include "debug/gs_imgui.h"
#include "debug/debug_entity.h"
#endif
#include "filesystem/gs_boot_datas.h"
#include "filesystem/gs_dirty_disk.h"
#include "filesystem/gs_file.h"
#include "flash/gs_flash.h"
#include "game/avatar/gs_avatar.h"
#include "physics/gs_cloth_system.h"
#include "game/cinematic/gs_cinematics.h"
#include "game/gs_intro_pool.h"
#include "game/gs_subtitles.h"
#include "game/gs_tutorial.h"
#include "game/scenaric/gs_scenaric.h"
#include "game/vehicle/gs_brand_car.h"
#include "game/vehicle/gs_car_colors.h"
#include "game/vehicle/gs_car_common.h"
#include "game/vehicle/gs_car_display.h"
#include "game/vehicle/gs_car_state.h"
#include "gs_cd_key.h"
#include "gs_pub.h"
#include "gs_timer.h"
#include "input/gs_device.h"
#include "movie/gs_movie.h"
#include "online/gs_online.h"
#include "physics/gs_physics.h"
#include "physics/gs_physicsfx.h"
#include "player_data/achievements_data.h"
#include "player_data/gs_playerdata.h"
#include "player_data/player_data_interface.h"
#include "player_data/user_profile.h"
#include "audio/radio/gs_radio_pc.h"
#include "render/camera/gs_camera.h"
#include "render/gs_noise.h"
#include "render/gs_render.h"
#include "render/mng_env_map.h"
#include "render/particles/gs_particles.h"
#include "render/postfx/gs_postfx.h"
#include "system/gs_keyboard_pc.h"
#include "system/gs_pc_options.h"
#include "system/gs_system.h"
#include "world/gs_aircraft.h"
#include "world/gs_boat.h"
#include "world/details/gs_details.h"
#include "world/gs_mng_resources.h"
#include "world/gs_pedestrians.h"
#include "world/gs_world.h"
#include "world/gs_world_streaming.h"
#include "world/streaming_manager.h"
#include "world/weather/gs_weather.h"

#include "player_data/save_game_manager.h"

#include "game/car_showcase/gm_car_showcase.h"
#include "game/avatar_showcase/gm_avatar_showcase.h"
#include "game/dressing/gm_dressing.h"
#include "game/video_bumper/gm_video_bumper.h"
#include "game/freeride/gm_freeride.h"
#include "game/sandbox/gm_sandbox.h"
#include "game/cinviewer/gm_cin_viewer.h"
#include "game/boat_tour/gm_boat_tour.h"
#include "game/jail/gm_jail.h"
#include "game/demo/gm_intro_demo.h"
#include "game/demo/gm_outro_demo.h"
#include "game/map/gm_map.h"
#include "game/login/gm_login.h"
#include "game/garage/gm_garage.h"
#include "game/aftermarket/gm_aftermarket.h"
#include "game/estates/gm_estates.h"
#include "game/goodies/gm_goodies.h"
#include "game/living/gm_living.h"
#include "game/intro/gm_intro.h"
#include "game/shop/gm_shop.h"
#include "game/test_car/gm_test_car.h"
#include "game/scene_interior.h"

#include "render/ed_render.h"

#include "asset_extractor/asset_extractor.h"

#include <filesystem>

const char **gpCmdLineArgs = nullptr;
int32_t gCmdLineArgCount = 0;
TestDriveGameInstance *gpTestDriveInstance = nullptr;

// TODO: Where does it belong?
bool gbLoadingInProgress = false;
bool gbRequestedExit = false;
static bool gbResetServicesOnLogin = false; // BOOL_016a2c1d
// TODO: Where does it belong?

bool DAT_010e723c = false;
bool DAT_010e723d = false;
bool DAT_010e7245 = false;

// TODO: Move those to the right cpp/h
static char gORBAddress[128];
uint64_t gAvailableMemory = 0; // DAT_01041a2c

static constexpr const char *kDefaultORBAddress = "orb.testdriveunlimited.com";

bool gBreakOnAssertFailure = false; // DAT_00f47931
bool gDisableAssert = false;
bool gDisableRunLock = false;

#if OTDU_DEVBUILD
static CmdLineArg CmdLineArgDbgBreakOnAssertFailure( "break_on_assert_failure",
                                                     []( const char *pArg ) {
                                                         gBreakOnAssertFailure =
                                                             true;
                                                     } );
static CmdLineArg CmdLineArgDisableAssert( "disable_assert",
                                           []( const char *pArg ) {
                                               gDisableAssert = true;
                                           } );

static CmdLineArg
    CmdLineArgLogTransient( "logtransient", []( const char *pArg ) {
        GlobalLogger::bLogTransientToFile = true;
    } );

static CmdLineArg CmdLineArgLogVerbose( "verbose", []( const char *pArg ) {
    GlobalLogger::bVerbose = true;
} );

static CmdLineArg CmdLineArgLogSilent( "silent", []( const char *pArg ) {
    GlobalLogger::bSilent = true;
} );

static CmdLineArg CmdLineArgNoRunLock( "norunlock", []( const char *pArg ) {
    gDisableRunLock = true;
} );
#endif

static CmdLineArg CmdLineArgORB( "orb", []( const char *pArg ) {
    // FUN_00406520
    strncpy( gORBAddress, pArg, 127 );
    gORBAddress[127] = '\0';

    OTDU_LOG_INFO( "ORB set to '%s'\n", gORBAddress );
} );

eGameMode gGameModeOverride = GM_Invalid;
static CmdLineArg CmdLineArgGameMode( "gm", []( const char *pArg ) {
    gGameModeOverride = (eGameMode)atoi( pArg );

    OTDU_LOG_INFO( "GameMode override (from command line) => '%s'\n",
                   kGameModeNames[gGameModeOverride] );
} );

// Loads a profile save at boot (the GMLogin 'Continue' path) so '-gm N' can
// start straight into a mode that needs a real playersave (e.g. GM_Garage).
static char gProfileOverride[64] = {};
static CmdLineArg CmdLineArgProfile( "profile", []( const char *pArg ) {
    strncpy( gProfileOverride, pArg, sizeof( gProfileOverride ) - 1 );
    gProfileOverride[sizeof( gProfileOverride ) - 1] = '\0';

    OTDU_LOG_INFO( "Profile override (from command line) => '%s'\n",
                   gProfileOverride );
} );

static void LoadProfileOverride() {
    const uint64_t profileHashcode =
        UserProfileRegister::HashProfileName( gProfileOverride );
    if ( gUserProfileRegister.getProfileIndex( profileHashcode ) < 0 ) {
        OTDU_LOG_WARN( "Profile '%s' does not exist; available profiles:\n",
                       gProfileOverride );
        for ( int32_t i = 0; i < gUserProfileRegister.getNumProfiles(); i++ ) {
            OTDU_LOG_WARN( "  '%s'\n",
                           gUserProfileRegister.getProfileNameByIndex( i ) );
        }
        return;
    }

    gpPlayerData->resetPlayerData( true, true );
    gpPlayerData->setSaveEnabled( true );
    gUserProfileRegister.initialize( profileHashcode );

    gbSaveAllowed = true;
    if ( !gpPlayerData->loadActiveProfileSave() ) {
        OTDU_LOG_WARN( "Profile '%s' has no readable save\n",
                       gProfileOverride );
        return;
    }

    AchievementsData *pAchievements =
        gpPlayerData->getLoadedSave()->getAchievements();
    if ( !pAchievements->isReady() ) {
        pAchievements->setReady( true );
        pAchievements->notify( ACF_Challenge | ACF_Road | ACF_Collection,
                               false );
    }

    if ( gpPlayerData->getLoadedSave()->getAvatar() != nullptr ) {
        TestDrive::RebakePlayerAvatar();
    }

    OTDU_LOG_INFO( "Loaded profile '%s'\n", gProfileOverride );
}

// Path to TestDriveUnlimited.exe, used for the in-process one-shot asset
// extraction (default: 'TestDriveUnlimited.exe' in the working directory).
static char gExecutablePath[OTDU_MAX_PATH] = "TestDriveUnlimited.exe";
static CmdLineArg CmdLineArgExecutable( "executable", []( const char *pArg ) {
    strncpy( gExecutablePath, pArg, OTDU_MAX_PATH - 1 );
    gExecutablePath[OTDU_MAX_PATH - 1] = '\0';
} );

static CmdLineArg CmdLineWorldViewer( "worldviewer", []( const char *pArg ) {
    gGameModeOverride = GM_Sandbox;
    gbEnableWorldViewer = true;
} );

static CmdLineArg CmdLineCarShowcase( "carshowcase", []( const char *pArg ) {
    gGameModeOverride = GM_CarShowCase;
} );

static CmdLineArg CmdLineCinViewer( "cinviewer", []( const char *pArg ) {
    gGameModeOverride = GM_CinViewer;
} );

static CmdLineArg CmdLineFreeRide( "freeride", []( const char *pArg ) {
    gGameModeOverride = GM_FreeRide;
} );

static CmdLineArg CmdLineLogin( "login", []( const char *pArg ) {
    gGameModeOverride = GM_Login;
} );

static CmdLineArg CmdLineBoatTour( "boattour", []( const char *pArg ) {
    gGameModeOverride = GM_BoatTour;
} );

static char gLogFilePath[OTDU_MAX_PATH] = "opentdu.log";
static CmdLineArg CmdLineArgLog( "log", []( const char *pArg ) {
    strncpy( gLogFilePath, pArg, OTDU_MAX_PATH - 1 );
    gLogFilePath[OTDU_MAX_PATH - 1] = '\0';
} );

static CmdLineArg CmdLineArgNoLog( "nolog", []( const char *pArg ) {
    gLogFilePath[0] = '\0';
} );

static void StartLogFile() {
    if ( !GlobalLogger::OpenLogFile( gLogFilePath ) ) {
        return;
    }

    std::string cmdLine;
    for ( int32_t argIdx = 1; argIdx < gCmdLineArgCount; argIdx++ ) {
        cmdLine += gpCmdLineArgs[argIdx];
        cmdLine += ' ';
    }

    OTDU_LOG_ALWAYS( "Command line: %s\n\n", cmdLine.c_str() );
}

// Runs the asset extractor in-process
// TODO: Add PSOs precompilation in the future
static bool OnFirstTimeRun() {
    if ( !std::filesystem::exists( gExecutablePath ) ) {
        OTDU_LOG_ERROR( "Cannot auto-extract assets: executable '%s' not found "
                        "(pass -executable <path> to override)\n",
                        gExecutablePath );
        return false;
    }

    char pWorkingDir[OTDU_MAX_PATH];
    TestDrive::GetWorkingDir( pWorkingDir, OTDU_MAX_PATH );

    OTDU_LOG_ALWAYS( "Running asset extraction from '%s'...\n",
                     gExecutablePath );

    AssetExtractor::ExtractionParams params;
    params.pExecutablePath = gExecutablePath;
    params.pOutputPath = pWorkingDir;
    params.bForceShaderExtraction = true;

    AssetExtractor::ExtractionResult result = AssetExtractor::Run( params );
    return result.bSuccess;
}

// TODO: Move to the right header
typedef enum eErrorMessage {
    EM_DX9NotSupported = 0,
    EM_SoundCardNotDetected = 1,
    EM_NotEnoughMemory = 2
} eErrorMessage;

constexpr const char *kErrorMessageDX9NotSupported[L_Count] = {
    "_no trad",
    "Incorrect version of DirectX!",
    "Version de DirectX incorrecte !",
    "�Versi�n incorrecta de DirectX!",
    "Falsche DirektX-Version!",
    "Versione errata di DirectX!",
    "_no trad",
    "_no trad",
    "_no trad",
    "_no trad",
    "_no trad" };

constexpr const char *kErrorMessageNotEnoughMemory[L_Count] = {
    "_no trad",
    "Not enough system memory!",
    "Pas assez de m�moire syst�me !",
    "�No bastante memoria de sistema!",
    "Nicht genug Systemged�chtnis!",
    "Non abbastanza memoria di sistema!",
    "_no trad",
    "_no trad",
    "_no trad",
    "_no trad",
    "_no trad" };

static void DisplayErrorMsgLocalized( eErrorMessage errorType ) {
    eLocale systemLocale = GetLocaleFromSystem();

    const char *pErrorMessage = nullptr;
    switch ( errorType ) {
    case EM_DX9NotSupported:
        pErrorMessage = kErrorMessageDX9NotSupported[errorType];
        break;
    case EM_NotEnoughMemory:
        pErrorMessage = kErrorMessageNotEnoughMemory[errorType];
        break;
    default:
        pErrorMessage = "_no trad";
        break;
    }

    if ( pErrorMessage != nullptr ) {
        TestDrive::DisplayMessageBox( pErrorMessage );
    }
}
// TODO: Move to the right header

static bool HasEnoughSysMemAvail() {
    gAvailableMemory = TestDrive::GetAvailableVirtualMemory();
    if ( gAvailableMemory < 484 ) {
        DisplayErrorMsgLocalized( EM_NotEnoughMemory );
        return false;
    }

    return true;
}

static TestDriveThread::ThreadReturnType_t
InstanciationWorker( void *in_param_1 ) {
    // FUN_009b4610
    TestDriveGameInstance::FileInstanciationWorkerInfos *param_1 =
        (TestDriveGameInstance::FileInstanciationWorkerInfos *)in_param_1;

    do {
        while ( true ) {
            param_1->Event.waitForEvent();

            // Cooperative shutdown: terminate() requests a stop and signals the
            // event, waking us here so we can leave the routine cleanly instead
            // of being force-killed.
            if ( param_1->pThread->isStopRequested() ) {
                return (TestDriveThread::ThreadReturnType_t)0;
            }

            param_1->pThread->setPriority( eThreadPriority::TP_Normal );

            if ( param_1->pCurrentFile == nullptr ) {
                break;
            }
            param_1->pCurrentFile->instanciate();
            param_1->bDone = true;
        }
        if ( param_1->pNextFile != nullptr ) {
            param_1->pNextFile->instanciate( param_1->pThread );
        }
        param_1->bDone = true;
    } while ( true );

    return (TestDriveThread::ThreadReturnType_t)0;
}

int32_t TestDrive::InitAndRun( const char **pCmdLineArgs,
                               const int32_t argCount ) {
    // FUN_009b4f60
    strncpy( gORBAddress, kDefaultORBAddress, sizeof( char ) * 26 );

    gpCmdLineArgs = (const char **)( pCmdLineArgs );
    gCmdLineArgCount = argCount;

    GetArgParserRegister().parseCmdLineArgs( gpCmdLineArgs, gCmdLineArgCount );
    if ( gLogFilePath[0] != '\0' ) {
        StartLogFile();
    }

    TestDriveGameInstance gameInstance( pCmdLineArgs, argCount );
    gSavegameManager.create();

    if ( gDisableRunLock || !IsAlreadyRunning() ) {
        bool bCheckSuccessful = HasEnoughSysMemAvail();

        if ( bCheckSuccessful ) {
            gameInstance.mainLoop();
            gameInstance.terminate();
            return 0;
        }
    }
    OTDU_LOG_INFO( "Game instance already running!\n" );

    return 1;
}

const char *TestDrive::GetVersion() { return "OpenTDU " OTDU_COMMIT_HASH; }

const char *TestDrive::GetVersionID() { return OTDU_COMMIT_HASH; }

bool TestDrive::IsBetaVersion() { return false; }

TestDriveGameInstance::TestDriveGameInstance( const char **argv,
                                              const int32_t argc )
    : argc( argc ),
      argv( argv ),
      bUseDVDVFS( false ),
      bNoAudio( false ),
      bNoSfx( false ),
      bProfile( false ),
      bMemCheck( false ),
      bFileTOC( false ),
      bSkip( false ),
      bPendingGameModeChange( false ),
      bRequestedExit( false ),
      wishedGameMode( GM_BootMenu ),
      memCheckSize( 0x2800 ),
      activeGameMode( GM_BootMenu ),
      previousGameMode( GM_Invalid ),
      mainLoopThread(),
      instanciationThread(),
      stackFileMutex( "StackInst" ),
      pUnkownResource( nullptr ) {
    // FUN_009b4c40
#if OTDU_64_BITS
    OTDUPointer::InitializePointerRegister();
#endif

    registeredServices.reserve( 64 );
    servicesInitOrder.reserve( 64 );

    instanciationThread.initialize(
        (TestDriveThread::StartRoutine_t)InstanciationWorker,
        &fileInstanciation, 0x10000, 0 );
    instanciationThread.setPriority( eThreadPriority::TP_Normal );
    instanciationThread.setWakeEvent( &fileInstanciation.Event );

    fileInstanciation.pThread = &instanciationThread;

    mainLoopInfos.CoreIndex = 0;
    gpTestDriveInstance = this;

#ifdef OTDU_USE_IMGUI
    memset( pServiceLookTextBuffer, 0, sizeof( pServiceLookTextBuffer ) );
#endif
}

TestDriveGameInstance::~TestDriveGameInstance() {
    bRequestedExit = true;

    while ( !servicesInitOrder.empty() ) {
        GameSystem *pIt = servicesInitOrder.back();
        pIt->terminate();
        if ( !pIt->isStaticallyAllocated() ) {
            delete pIt;
        }
        servicesInitOrder.pop_back();
    }
    registeredServices.clear();
}

void TestDriveGameInstance::terminate() {
    instanciationThread.terminate();
    stackFileMutex.terminate();
    mainLoopThread.terminate();
}

void TestDriveGameInstance::mainLoop() {
    // FUN_0097cde0
    bool bInitialized = initialize();
    if ( !bInitialized ) {
        TestDrive::DisplayMessageBox(
            "Fatal: failed to initialize engine instance\n" );
        return;
    }

    // Set localization for common flash
    gpFlash->setLocalizedStrings();

    // gpOnGSFileInit = &LAB_009a3360;
    if ( activeGameMode != eGameMode::GM_Login ) {
        EnableWorldSpotStreaming();
    }

    if ( gProfileOverride[0] != '\0' ) {
        LoadProfileOverride();
    }

    if ( gGameModeOverride != GM_Invalid ) {
        setNextGameMode( gGameModeOverride );
    }

    float deltaTime = 0.0f;

    GameMode *pGVar6 = nullptr;
    void *pGameModeAlloc = nullptr;

    while ( !bRequestedExit ) {
        switch ( activeGameMode ) {
        case GM_CarShowCase:
            pGameModeAlloc = TestDrive::AllocAligned(
                sizeof( GMCarShowcase ), alignof( GMCarShowcase ) );
            pGVar6 = new ( pGameModeAlloc ) GMCarShowcase();
            break;
        case GM_AvatarShowcase:
            pGameModeAlloc = TestDrive::AllocAligned(
                sizeof( GMAvatarShowcase ), alignof( GMAvatarShowcase ) );
            pGVar6 = new ( pGameModeAlloc ) GMAvatarShowcase();
            break;
        case GM_Dressing:
            pGameModeAlloc = TestDrive::AllocAligned( sizeof( GMDressing ),
                                                      alignof( GMDressing ) );
            pGVar6 = new ( pGameModeAlloc ) GMDressing();
            break;
        case GM_Goodies:
            pGameModeAlloc = TestDrive::AllocAligned( sizeof( GMGoodies ),
                                                      alignof( GMGoodies ) );
            pGVar6 = new ( pGameModeAlloc ) GMGoodies();
            break;
        case GM_AfterMarket:
            pGameModeAlloc = TestDrive::AllocAligned(
                sizeof( GMAfterMarket ), alignof( GMAfterMarket ) );
            pGVar6 = new ( pGameModeAlloc ) GMAfterMarket();
            break;
        case GM_Estates:
            pGameModeAlloc = TestDrive::AllocAligned( sizeof( GMEstates ),
                                                      alignof( GMEstates ) );
            pGVar6 = new ( pGameModeAlloc ) GMEstates();
            break;
        case GM_Login:
            if ( gbResetServicesOnLogin ) {
                pauseAndResetServices();
            }
            pGameModeAlloc = TestDrive::AllocAligned( sizeof( GMLogin ),
                                                      alignof( GMLogin ) );
            pGVar6 = new ( pGameModeAlloc ) GMLogin();
            break;
        case GM_VideoBumper:
            pGameModeAlloc = TestDrive::AllocAligned(
                sizeof( GMVideoBumper ), alignof( GMVideoBumper ) );
            pGVar6 = new ( pGameModeAlloc ) GMVideoBumper();
            break;
        case GM_FreeRide:
            pGameModeAlloc = TestDrive::AllocAligned( sizeof( GMFreeride ),
                                                      alignof( GMFreeride ) );
            pGVar6 = new ( pGameModeAlloc ) GMFreeride();
            break;
        case GM_Sandbox:
            pGameModeAlloc = TestDrive::AllocAligned( sizeof( GMSandbox ),
                                                      alignof( GMSandbox ) );
            pGVar6 = new ( pGameModeAlloc ) GMSandbox();
            break;
        case GM_CinViewer:
            pGameModeAlloc = TestDrive::AllocAligned( sizeof( GMCinViewer ),
                                                      alignof( GMCinViewer ) );
            pGVar6 = new ( pGameModeAlloc ) GMCinViewer();
            break;
        case GM_BoatTour:
            pGameModeAlloc = TestDrive::AllocAligned( sizeof( GMBoatTour ),
                                                      alignof( GMBoatTour ) );
            pGVar6 = new ( pGameModeAlloc ) GMBoatTour();
            break;
        case GM_Jail:
            pGameModeAlloc =
                TestDrive::AllocAligned( sizeof( GMJail ), alignof( GMJail ) );
            pGVar6 = new ( pGameModeAlloc ) GMJail();
            break;
        case GM_OutroDemo:
            pGameModeAlloc = TestDrive::AllocAligned( sizeof( GMOutroDemo ),
                                                      alignof( GMOutroDemo ) );
            pGVar6 = new ( pGameModeAlloc ) GMOutroDemo();
            break;
        case GM_IntroDemo:
            pGameModeAlloc = TestDrive::AllocAligned( sizeof( GMIntroDemo ),
                                                      alignof( GMIntroDemo ) );
            pGVar6 = new ( pGameModeAlloc ) GMIntroDemo();
            break;
        case GM_Map:
            pGameModeAlloc =
                TestDrive::AllocAligned( sizeof( GMMap ), alignof( GMMap ) );
            pGVar6 = new ( pGameModeAlloc ) GMMap();
            break;
        case GM_Garage:
            pGameModeAlloc = TestDrive::AllocAligned( sizeof( GMGarage ),
                                                      alignof( GMGarage ) );
            pGVar6 = new ( pGameModeAlloc ) GMGarage();
            break;
        case GM_Shop:
            pGameModeAlloc =
                TestDrive::AllocAligned( sizeof( GMShop ), alignof( GMShop ) );
            pGVar6 = new ( pGameModeAlloc ) GMShop();
            break;
        case GM_TestCar:
            pGameModeAlloc = TestDrive::AllocAligned( sizeof( GMTestCar ),
                                                      alignof( GMTestCar ) );
            pGVar6 = new ( pGameModeAlloc ) GMTestCar();
            break;
        case GM_Intro:
            pGameModeAlloc = TestDrive::AllocAligned( sizeof( GMIntro ),
                                                      alignof( GMIntro ) );
            pGVar6 = new ( pGameModeAlloc ) GMIntro();
            break;
        case GM_Living:
            pGameModeAlloc = TestDrive::AllocAligned( sizeof( GMLiving ),
                                                      alignof( GMLiving ) );
            pGVar6 = new ( pGameModeAlloc ) GMLiving();
            // TODO: REMOVE once GMIntro is implemented
            if ( BOOL_010e74f4 ) {
                EnableWorldSpotStreaming();
            }
            break;
        default:
            OTDU_FATAL_ERROR( "GAME MODE '%s' (%u) IS NOT IMPLEMENTED!\n",
                              kGameModeNames[activeGameMode], activeGameMode );
            break;
        }

        OTDU_LOG_INFO( "Switching to game mode '%s'...\n",
                       kGameModeNames[activeGameMode] );

        if ( activeGameMode != GM_VideoBumper ) {
            const bool bIsLogin = activeGameMode == GM_Login;
            const bool bIsIntro = activeGameMode == GM_Intro;

            pGVar6->mainLoop( this );

            pGVar6->~GameMode();
            TestDrive::FreeAligned( pGameModeAlloc );

            if ( bIsIntro ) {
                EnableWorldSpotStreaming();
            }

            if ( bIsLogin ) {
                gbResetServicesOnLogin = activeGameMode != GM_VideoBumper;
            }
        } else {
            GMVideoBumper::SetActiveBumper( "Atari.bik" );
            GMVideoBumper::SetNextGameMode( GM_VideoBumper );
            GMVideoBumper::SetSkippable();

            setNextGameMode(
                GM_VideoBumper ); // NOTE: Done twice in the original game (see
            // 0x0097c91e and 0x0097c96d). Not sure why
            GMVideoBumper::OnPlaybackComplete( "LOG_IN", "/popup", "on",
                                               false );

            pGVar6->setTransitionFlags( 0x0 );
            pGVar6->mainLoop( this );

            pGVar6->~GameMode();
            TestDrive::FreeAligned( pGameModeAlloc );
            GMVideoBumper::ClearActiveBumper();

            GMVideoBumper::SetActiveBumper( "Bumper_EDEN_TDU.bik" );
            GMVideoBumper::SetNextGameMode( GM_Login );
            GMVideoBumper::SetSkippable();

            GMVideoBumper::OnPlaybackComplete( "LOG_IN", "/popup", "on",
                                               false );

            pGameModeAlloc = TestDrive::AllocAligned(
                sizeof( GMVideoBumper ), alignof( GMVideoBumper ) );
            pGVar6 = new ( pGameModeAlloc ) GMVideoBumper();
            pGVar6->setTransitionFlags( 0x30 );
            pGVar6->mainLoop( this );

            pGVar6->~GameMode();
            TestDrive::FreeAligned( pGameModeAlloc );

            setNextGameMode( GM_Login );
        }

        if ( gpPlayerData != nullptr ) {
            gpPlayerData->requestSave( 3 );
        }
    }

    OTDU_LOG_DEBUG( "Exit requested\n" );
}

void TestDriveGameInstance::setGameMode( eGameMode newGameMode ) {
    wishedGameMode = newGameMode;
}

bool TestDriveGameInstance::initialize() {
    // FUN_0097c8f0
    OTDU_LOG_INFO( "Initializing OpenTDU version '%s'...\n",
                   TestDrive::GetVersion() );

    bool baseServicesInit = initializeBaseServices();
    if ( !baseServicesInit ) {
        return false;
    }

    if ( gpConfig->bEnableMainMenu && wishedGameMode != GM_AITrainer ) {
        gameInitStep = 2;
        setNextGameMode( GM_VideoBumper );

        GMVideoBumper::SetActiveBumper( "Atari.bik" );
        GMVideoBumper::SetNextGameMode( GM_VideoBumper );
        GMVideoBumper::SetSkippable();

        setNextGameMode(
            GM_VideoBumper ); // NOTE: Done twice in the original game (see
                              // 0x0097c91e and 0x0097c96d). Not sure why
        GMVideoBumper::OnPlaybackComplete( "LOG_IN", "/popup", "on", false );

        GMVideoBumper *pGVar3 = new ( TestDrive::AllocAligned(
            sizeof( GMVideoBumper ), alignof( GMVideoBumper ) ) )
            GMVideoBumper();
        pGVar3->setTransitionFlags( 0x0 );
        pGVar3->mainLoop( this );

        pGVar3->~GMVideoBumper();
        TestDrive::FreeAligned( pGVar3 );
        GMVideoBumper::ClearActiveBumper();

        GMVideoBumper::SetActiveBumper( "Bumper_EDEN_TDU.bik" );
        GMVideoBumper::SetNextGameMode( GM_Login );
        GMVideoBumper::SetSkippable();

        GMVideoBumper::OnPlaybackComplete( "LOG_IN", "/popup", "on", false );

        pGVar3 = new ( TestDrive::AllocAligned( sizeof( GMVideoBumper ),
                                                alignof( GMVideoBumper ) ) )
            GMVideoBumper();
        pGVar3->setTransitionFlags( 0x30 );
        pGVar3->mainLoop( this );

        pGVar3->~GMVideoBumper();
        TestDrive::FreeAligned( pGVar3 );

        setNextGameMode( GM_Login );
    } else {
        switchToWishedGameMode();
        int32_t iVar4 = gEdRender.flushDrawCommands( true );
        if ( iVar4 == 4 ) {
            return false;
        }
    }
    gameInitStep = 0;

    gpBootDatas->loadDataBanks();
    gpFile->flushPendingAsync();
    gpConfig->parseWeatherAndFXInis();

    bool gameServicesInit = initializeGameServices();
    if ( !gameServicesInit ) {
        return false;
    }

    resetServices();

    return true;
}

void TestDriveGameInstance::pauseAndResetServices() {
    // FUN_009b46c0
    for ( auto it = registeredServices.rbegin();
          it != registeredServices.rend(); ++it ) {
        ( *it )->pause();
    }

    resetServices();
}

void TestDriveGameInstance::resetServices() {
    // FUN_009b45d0
    for ( GameSystem *pService : registeredServices ) {
        pService->reset();
    }
}

// clang-format off
#define OTDU_INIT_SERVICE( condition )                                                        \
    do {                                                                                      \
        if ( !condition ) {                                                                   \
            std::string errorMsg = #condition " failed!\nCheck opentdu.log for more infos.\n" \
                "If you think this is a bug, please go to "                                   \
                "https://github.com/opentestdriveunlimited/OpenTestDriveUnlimited/issues"     \
                " and create a new issue (with your opentdu.log attached)";                   \
            TestDrive::DisplayMessageBox( errorMsg.c_str() );                                 \
            return false;                                                                     \
        }                                                                                     \
    } while ( 0 );
// clang-format on

bool TestDriveGameInstance::initializeBaseServices() {
    // FUN_009b4e40
    // NOTE: Initialization order does NOT match the original executable. This
    // is due to several things:
    // - Some GS relies on statically allocated allocators (that did not exist
    // in the original game)
    // - We are missing a few GS implementation
    // - Some GS were statically allocated in the original executable (we always
    // dynamically allocate GS
    //   to avoid undeterministic allocation order)
    OTDU_INIT_SERVICE( registerService<GSSystem>() );
    OTDU_INIT_SERVICE( registerService<GSFile>() );

// TODO: Might want to use a dummy file to track the state of shader
// extraction instead of checking folder...
#ifdef OTDU_VULKAN
    static constexpr const char *kOpenTDUAssetsStamp = "opentdu.txt";
#else
#error "Not Implemented!"
#endif
    bool bNeedToRunExtractor = !gpFile->fileExists( kOpenTDUAssetsStamp );
    if ( bNeedToRunExtractor ) {
        OTDU_LOG_ALWAYS( "Asset stamp '%s' not found; assets will be "
                         "(re)extracted for build '%s'\n",
                         kOpenTDUAssetsStamp, OTDU_COMMIT_HASH );
    } else {
        char *pStamp = nullptr;
        uint32_t stampLength = 0;
        bool bLoaded = gpFile->loadFile( kOpenTDUAssetsStamp, (void **)&pStamp,
                                         &stampLength );
        if ( bLoaded ) {
            bNeedToRunExtractor =
                ( strncmp( pStamp, OTDU_COMMIT_HASH,
                           strlen( OTDU_COMMIT_HASH ) ) != 0 );

            const uint32_t hashLength =
                static_cast<uint32_t>( strlen( OTDU_COMMIT_HASH ) );
            std::string stampOnDisk( pStamp, ( stampLength < hashLength )
                                                 ? stampLength
                                                 : hashLength );
            OTDU_LOG_ALWAYS(
                "Asset stamp: on disk '%s', this build '%s' => assets are %s\n",
                stampOnDisk.c_str(), OTDU_COMMIT_HASH,
                bNeedToRunExtractor ? "STALE (re-extracting)" : "up to date" );

            TestDrive::Free( pStamp );
        }
    }

    if ( bNeedToRunExtractor ) {
        // Assets were never extracted; attempt a one-shot in-process extraction
        // (shaders + packed .ini) so users don't have to run the standalone
        // opentdu_assetExtractor tool first.
        bool bExtracted = OnFirstTimeRun();
        if ( !bExtracted || !gpFile->fileExists( "EURO/Shaders" ) ) {
            char pWorkingDir[OTDU_MAX_PATH];
            TestDrive::GetWorkingDir( pWorkingDir, OTDU_MAX_PATH );

            std::string messageError =
                "Fatal: Could not locate shaders (folder '";
            messageError += pWorkingDir;
            messageError +=
                "/EURO/Shaders' does not exist) and automatic asset extraction "
                "failed.\n"
                "Make sure TestDriveUnlimited.exe is present in the working "
                "directory (or pass -executable <path>), or run "
                "opentdu_assetextractor manually before running this "
                "executable";

            TestDrive::DisplayMessageBox( messageError.c_str() );
            return false;
        }

        // TODO: IMPLEMENT NATIVE GSFILE Save func
        std::ofstream stampStream( kOpenTDUAssetsStamp,
                                   std::ios::trunc | std::ios::out );
        stampStream.write( OTDU_COMMIT_HASH, strlen( OTDU_COMMIT_HASH ) );
        stampStream.close();
    }

    OTDU_INIT_SERVICE( registerService<GSBootDatas>() );
    OTDU_INIT_SERVICE( registerService<GSConsole>() );
    OTDU_INIT_SERVICE( registerService<GSTimer>() );
    OTDU_INIT_SERVICE( registerService<GSConfig>() );
    OTDU_INIT_SERVICE( registerService<GSPlayerData>() );
    OTDU_INIT_SERVICE( registerService<GSWeather>() );
    OTDU_INIT_SERVICE( registerService<GSRender>( gpRender ) );
    OTDU_INIT_SERVICE( registerService<GSProfile>() );
    OTDU_INIT_SERVICE( registerService<GSDevice>( gpDevice ) );
    OTDU_INIT_SERVICE( registerService<GSDebug>() );
    OTDU_INIT_SERVICE( registerService<GSAudio>() );
    OTDU_INIT_SERVICE( registerService<GSFlash>( gpFlash ) );
    OTDU_INIT_SERVICE( registerService<GSMovie>() );
#ifdef OTDU_USE_IMGUI
    OTDU_INIT_SERVICE( registerService<GSImgui>() );
#endif

    gpConfig->parseIniFiles();

    if ( gGameModeOverride != GM_Invalid ) {
        gpConfig->bEnableMainMenu = false;
    }

    return true;
}

bool TestDriveGameInstance::initializeGameServices() {
    OTDU_INIT_SERVICE( registerService<GSIntroPool>( gpIntroPool ) );
    OTDU_INIT_SERVICE( registerService<GSWorld>( gpWorld ) );
    OTDU_INIT_SERVICE( registerService<GSNoise>() );
    OTDU_INIT_SERVICE( registerService<GSWorldStreaming>( gpWorldStreaming ) );
    OTDU_INIT_SERVICE( registerService<GSVehicleSound>() );
    OTDU_INIT_SERVICE( registerService<GSCarCommon>() );
    OTDU_INIT_SERVICE( registerService<GSCarColors>() );
    OTDU_INIT_SERVICE( registerService<GSPostFX>() );
    OTDU_INIT_SERVICE( registerService<GSAmbiance>() );
    OTDU_INIT_SERVICE( registerService<GSSubtitles>() );
    OTDU_INIT_SERVICE( registerService<GSCinematics>() );
    OTDU_INIT_SERVICE( registerService<GSPhysics>( gpPhysics ) );
    OTDU_INIT_SERVICE( registerService<GSCamera>( gpCamera ) );
    OTDU_INIT_SERVICE( registerService<GSCarDisplay>() );
    OTDU_INIT_SERVICE( registerService<GSCarState>() );
    reorderService( gpCarState, gpCarDisplay );
    OTDU_INIT_SERVICE( registerService<GSDatabase>( gpDatabase ) );
    OTDU_INIT_SERVICE( registerService<GSDetails>() );
    OTDU_INIT_SERVICE( registerService<GSPhysicsFX>() );
    OTDU_INIT_SERVICE( registerService<GSParticles>() );
    OTDU_INIT_SERVICE( registerService<GSClothSystem>() );
    OTDU_INIT_SERVICE( registerService<GSAvatar>() );
    OTDU_INIT_SERVICE( registerService<GSPedestrians>() );
    OTDU_INIT_SERVICE( registerService<GSPub>() );
    OTDU_INIT_SERVICE( registerService<GSScenaric>( gpScenaric ) );
    OTDU_INIT_SERVICE( registerService<GSAircraft>() );
    OTDU_INIT_SERVICE( registerService<GSBoat>() );
    OTDU_INIT_SERVICE( registerService<GSAI>( &gGSAI ) );
    OTDU_INIT_SERVICE( registerService<GSOnline>( gpOnline ) );
    OTDU_INIT_SERVICE( registerService<GSTutorial>() );
    OTDU_INIT_SERVICE( registerService<GSBrandCar>() );
    OTDU_INIT_SERVICE( registerService<GSMngResources>() );
    OTDU_INIT_SERVICE( registerService<GSKeyboardPC>( gpKeyboardPC ) );
    OTDU_INIT_SERVICE( registerService<GSDirtyDisk>() );
    OTDU_INIT_SERVICE( registerService<GSRadioPC>() );

    gpConfig->parseRadioIni();
    gpConfig->parseLeaderboardIni();

    OTDU_INIT_SERVICE( registerService<GSPCOptions>() );
    OTDU_INIT_SERVICE( registerService<GSCDKey>() );

    return true;
}

void TestDriveGameInstance::drawDebugMenu( float deltaTime ) {
    // TODO: MOVE TO GSDebug
    // Add available overlays draw calls prior to the main debug menu one.
    MngEnvMap::DrawEnvMapDebugOverlay();

    if ( gpImgui != nullptr && !gpImgui->hasFocus() ) {
        return;
    }

    if ( ImGui::Begin( "Debug Menu" ) ) {
        ImGui::Text( "%s", TestDrive::GetVersion() );
        ImGui::Spacing();
        if ( ImGui::BeginCombo( "Set GameMode:",
                                kGameModeNames[activeGameMode] ) ) {
            for ( int n = 0; n < eGameMode::GM_Count; n++ ) {
                const bool bSelected = ( activeGameMode == n );
                if ( ImGui::Selectable( kGameModeNames[n], bSelected ) ) {
                    eGameMode nextGameMode = (eGameMode)n;

                    if ( nextGameMode != activeGameMode ) {
                        gpActiveGameMode->requestExit();
                        setNextGameMode( nextGameMode );
                    }
                }

                if ( bSelected )
                    ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
        ImGui::Text( "Active GameMode: %s", kGameModeNames[activeGameMode] );
        ImGui::Text( "Previous GameMode: %s",
                     kGameModeNames[previousGameMode] );
        ImGui::Text( "Wished GameMode: %s", kGameModeNames[wishedGameMode] );

        if ( ImGui::CollapsingHeader( "Game Services" ) ) {
            ImGui::InputText( "Find by name", pServiceLookTextBuffer,
                              sizeof( pServiceLookTextBuffer ) );

            static bool gbDidServicesWarmup = false;
            if ( !gbDidServicesWarmup ) {
                // Single frame warmup to propagate service debug draw flag
                // (needs debug draw refactor to avoid this crap)
                for ( GameSystem *pService : registeredServices ) {
                    if ( !pService->hasDebugMenu() ) {
                        continue;
                    }

                    pService->drawDebug( deltaTime );
                }
                gbDidServicesWarmup = true;
            } else {
                for ( GameSystem *pService : registeredServices ) {
                    if ( !pService->hasDebugMenu() ) {
                        continue;
                    }

                    if ( *pServiceLookTextBuffer != '\0' ) {
                        std::string serviceNameLowerCase = pService->getName();
                        std::transform( serviceNameLowerCase.begin(),
                                        serviceNameLowerCase.end(),
                                        serviceNameLowerCase.begin(), tolower );

                        if ( serviceNameLowerCase.find(
                                 pServiceLookTextBuffer ) ==
                             std::string::npos ) {
                            continue;
                        }
                    }

                    if ( ImGui::CollapsingHeader( pService->getName() ) ) {
                        pService->drawDebug( deltaTime );
                    }
                }
            }
        }
    }
    ImGui::End();
}

template <typename TGS>
bool TestDriveGameInstance::registerService( TGS *pSystem ) {
    bool bStaticallyAllocated = pSystem != nullptr;
    if ( !bStaticallyAllocated ) {
        pSystem = new TGS();
    }
    pSystem->setStaticallyAllocated( bStaticallyAllocated );

    if ( !pSystem->initialize( this ) ) {
        if ( !pSystem->isStaticallyAllocated() ) {
            delete pSystem;
        }
        return false;
    }

    OTDU_LOG_INFO( "Succesfully registered service '%s'\n",
                   pSystem->getName() );

    registeredServices.push_back( pSystem );
    servicesInitOrder.push_back( pSystem );
    return true;
}

void TestDriveGameInstance::reorderService( GameSystem *pService,
                                            GameSystem *pTickBefore ) {
    if ( pService == nullptr || pTickBefore == nullptr ) {
        return;
    }

    auto it = std::find( registeredServices.begin(), registeredServices.end(),
                         pService );
    auto target = std::find( registeredServices.begin(),
                             registeredServices.end(), pTickBefore );
    if ( it == registeredServices.end() || target == registeredServices.end() ||
         it < target ) {
        return;
    }

    registeredServices.erase( it );
    target = std::find( registeredServices.begin(), registeredServices.end(),
                        pTickBefore );
    registeredServices.insert( target, pService );
}

void TestDriveGameInstance::setNextGameMode( eGameMode nextGameMode ) {
    if ( bPendingGameModeChange ) {
        OTDU_LOG_WARN(
            "Failed to submit GM change: GM change already in progress!\n" );
        return;
    }

    if ( activeGameMode != nextGameMode ) {
        previousGameMode = activeGameMode;
    }

    activeGameMode = nextGameMode;
}

void TestDriveGameInstance::switchToWishedGameMode() {
    if ( bPendingGameModeChange ) {
        OTDU_LOG_WARN(
            "Failed to submit GM change: GM change already in progress!\n" );
        return;
    }

    if ( activeGameMode != wishedGameMode ) {
        previousGameMode = activeGameMode;
    }

    activeGameMode = wishedGameMode;
}

void TestDriveGameInstance::flushPendingFileInstanciation( bool param_2 ) {
    ScopedMutexLock mutexLock( &stackFileMutex );

    size_t uVar2 = pendingStreamedResources.size();
    if ( 0 < pendingStreamedResources.size() ) {
        size_t iVar3 = 0;
        if ( param_2 ) {
            iVar3 = uVar2 - 1;
        }

        while ( iVar3 < uVar2 && uVar2 != 0 ) {
            updateFileInstanciation();
            ThreadYield();
            uVar2 = pendingStreamedResources.size();
        }

        bool bVar1 = fileInstanciation.bDone;
        while ( !bVar1 ) {
            ThreadYield();
            bVar1 = fileInstanciation.bDone;
        }
    }

    if ( !param_2 ) {
        bool bVar2 = fileInstanciation.bDone;
        while ( !bVar2 ) {
            ThreadYield();
            bVar2 = fileInstanciation.bDone;
        }
    }
}

void TestDriveGameInstance::tickServices( float param_2,
                                          float param_3,
                                          uint32_t param_4,
                                          bool param_5 ) {
    // FUN_009b4830
    if ( param_5 ) {
        // Never called.
        // Branch looks identical to other block, so this is most likely a debug
        // leftover...
        OTDU_UNIMPLEMENTED;
    } else {
        for ( GameSystem *pService : registeredServices ) {
            if ( pService->getIndex() == param_4 && !pService->isPaused() ) {
                pService->tick( param_3, param_2 );
                // FUN_009b4000() // TODO: Some weird memory check (most likely
                // debug leftover)
            }
        }
    }
}

void TestDriveGameInstance::drawServices( float deltaTime ) {
    // FUN_009b4780
    if ( DAT_010e7245 ) {
        OTDU_UNIMPLEMENTED;
    } else {
        for ( GameSystem *pService : registeredServices ) {
            if ( !pService->isPaused() ) {
                pService->draw( deltaTime );
                // FUN_009b4000() // TODO: Some weird memory check (most likely
                // debug leftover)
            }
        }
    }
}

void TestDriveGameInstance::tickGameMode( float param_1,
                                          float param_2,
                                          float deltaTime,
                                          float totalTime ) {
    // FUN_009b4ac0
    FUN_0097c8b0();
    tickServices( deltaTime, totalTime, 0xffffffff, DAT_010e723c );

    mainLoopInfos.DeltaTime = deltaTime;
    mainLoopInfos.bDone = false;
    mainLoopInfos.bDirtyManagers = false;
    mainLoopInfos.bDirtyServices = true;
    mainLoopInfos.Time = totalTime;

    tickServices( deltaTime, totalTime, mainLoopInfos.CoreIndex, DAT_010e723d );
    mainLoopInfos.bDone = true;

    FUN_009b4650();
    gpActiveGameMode->tick( deltaTime, totalTime );

    mainLoopInfos.DeltaTime = deltaTime;
    mainLoopInfos.bDone = false;
    mainLoopInfos.bDirtyManagers = true;
    mainLoopInfos.bDirtyServices = false;
    mainLoopInfos.Time = totalTime;

    gpActiveGameMode->tickManagers( deltaTime, totalTime,
                                    mainLoopInfos.CoreIndex, DAT_010e723d );
    mainLoopInfos.bDone = true;
    FUN_009b4650();
}

void TestDriveGameInstance::FUN_0097c8b0() {
    // FUN_0097c8b0
    gSavegameManager.refreshService();
}

void TestDriveGameInstance::FUN_009b4650() {
    // FUN_009b4650
    bool bVar1;
    int local_8;
    int local_4;

    bVar1 = true;
    do {
        local_4 = 0;
        local_8 = 0;
        do {
            if ( mainLoopInfos.bDone == true ) {
                local_4 = local_4 + 1;
            }
            local_8 = local_8 + 1;
        } while ( local_8 < 1 );

        if ( local_4 == 1 ) {
            bVar1 = false;
        }
    } while ( bVar1 );
}

void TestDriveGameInstance::updateFileInstanciation() {
    // FUN_009b4710
    bool bVar1 = fileInstanciation.bDone;
    if ( pendingStreamedResources.size() < 1 ) {
        if ( bVar1 ) {
            fileInstanciation.pCurrentFile = nullptr;
            fileInstanciation.bDone = false;
            fileInstanciation.Event.reset();
        }
    } else if ( bVar1 ) {
        fileInstanciation.pCurrentFile = pendingStreamedResources.front();
        fileInstanciation.bDone = false;
        fileInstanciation.Event.reset();

        pendingStreamedResources.pop_front();
        fileInstanciation.pCurrentFile->markAsInstanciated();
    }
}

bool TestDriveGameInstance::pushResourceToStream( StreamedResource *param_2 ) {
    // FUN_009b4490
    ScopedMutexLock lock( &stackFileMutex );
    if ( pendingStreamedResources.size() + 1 < 0x40 ) {
        pendingStreamedResources.push_back( param_2 );
        bPendingInstantiation = true;
        return true;
    }
    return false;
}

bool TestDriveGameInstance::popResourceFromStream( StreamedResource *param_2 ) {
    // FUN_009b4400
    ScopedMutexLock lock( &stackFileMutex );
    for ( auto it = pendingStreamedResources.begin();
          it != pendingStreamedResources.end(); ++it ) {
        if ( *it == param_2 ) {
            pendingStreamedResources.erase( it );
            param_2->markAsInstanciated();
            return true;
        }
    }
    return false;
}
