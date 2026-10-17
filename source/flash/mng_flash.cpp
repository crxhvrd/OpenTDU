#include "shared.h"
#include "mng_flash.h"
#include "gs_flash.h"

#include "database/gs_database.h"
#include "game/gm_base.h"
#include "game/gm_scene.h"
#include "render/camera/gs_camera.h"
#include "tdu_instance.h"
#include "online/mng_chat.h"
#include "game/gs_tutorial.h"
#include "system/mng_options.h"
#include "game/scenaric/gs_scenaric.h"
#include "flash/mng_flash_counter.h"
#include "game/car_showcase/mng_showcase_car_control.h"
#include "player_data/driver.h"
#include "player_data/gs_playerdata.h"
#include "player_data/player_data_interface.h"
#include "player_data/saved_garage.h"
#include "game/freeride/gm_freeride.h"
#include "game/gm_world.h"
#include "game/vehicle/car_state.h"
#include "physics/vehicle/physics_vehicle.h"
#include "render/camera/camera_gps.h"
#include "render/gs_render.h"
#include "render/mng_cam_gps.h"
#include "render/render_scene.h"
#include "world/gs_world.h"
#include "flash/movie_player.h"
#include "flash/audio/flash_sounds.h"

static constexpr FlashMessage kGoMap = 0x616d6f67u;            // goma
static constexpr FlashMessage kGoFreeride = 0x72666f67u;       // gofr
static constexpr FlashMessage kGoAvatarShowcase = 0x76616f67u; // goav
static constexpr FlashMessage kGoGP = 0x70676f67u;      // gogp (GOGamePlay?)
static constexpr FlashMessage kGoEditor = 0x64656f67u;  // goed
static constexpr FlashMessage kGoHome = 0x6f686f67u;    // goho
static constexpr FlashMessage kGaugesOff = 0x666f6167u; // gaof
static constexpr FlashMessage kGaugesOn = 0x6e6f6167u;  // gaon
static constexpr FlashMessage kFadeOut = 0x756f6166u;   // faou
static constexpr FlashMessage kFadeIn = 0x6e696166u;    // fain
static constexpr FlashMessage kBCVI = 0x69766362u;      // bcvi
static constexpr FlashMessage kFTVI = 0x69767466u;      // ftvi
static constexpr FlashMessage kPCOptions = 0x6f746e65u; // ento (ENTer Options?)
static constexpr FlashMessage kCounters = 0x67756167u;  // gaug
static constexpr FlashMessage kPlaySound = 0x70646e73u; // sndp
static constexpr FlashMessage kMSKE = 0x656b736du;      // mske
static constexpr FlashMessage kHonkOn = 0x6b6e6f68u;    // honk
static constexpr FlashMessage kHonkOff = 0x666f6f68u;   // hoof
static constexpr FlashMessage kMoneyUp = 0x70756f6du;   // moup
static constexpr FlashMessage kSBDD = 0x64646273u;      // sbdd
static constexpr FlashMessage kSliders = 0x64696c73u;   // slid
static constexpr FlashMessage kStar = 0x72617473u;      // star
static constexpr FlashMessage kStao = 0x6f617473u;      // stao
static constexpr FlashMessage kStre = 0x65727473u;      // stre
static constexpr FlashMessage kSounds = 0x73646e73u;    // snds
static constexpr FlashMessage kWidget = 0x67646977u;    // widg

// TODO: Move to right cpp
static ShowCasePhysControl &GetShowcasePhysControl() {
    return gpActiveGMScene->getShowcaseCarControl().getPhysicsControl();
}

// TODO: Move to right cpp
static bool FUN_00914df0() {
    // FUN_00914df0
    OTDU_UNIMPLEMENTED;
    return false;
}

// TODO: Move to right cpp
static void PauseFrontEndMovie( const char *pMovieName ) {
    uint32_t levelIndex = 0;
    uint32_t movieIndex = 0;
    if ( !gpFlash->getFlashMovieIndex( &levelIndex, &movieIndex,
                                       pMovieName ) ) {
        return;
    }

    MoviePlayer *pMovie = gpFlash->getMoviePlayer( levelIndex, movieIndex );
    if ( pMovie != nullptr ) {
        pMovie->IsMoviePaused |= 2;
    }
}

// TODO: Move to right cpp
static void TransitionFromFreerideToMap( int32_t targetGameMode ) {
    // FUN_007183f0
    const NextIntroInfo &nextIntro = gpScenaric->getNextIntro();
    if ( gpTestDriveInstance->getActiveGameMode() == nextIntro.NextGameMode &&
         nextIntro.bIsActive ) {
        return;
    }

    if ( gpActiveGMScene != nullptr ) {
        if ( gpTestDriveInstance->getActiveGameMode() == GM_FreeRide &&
             static_cast<GMFreeride *>( gpActiveGMScene )
                 ->isNextIntroActive() ) {
            return;
        }
        gpActiveGMScene->parkLocalPlayerCar();
    }

    gpCamera->setUseBlur( true );
    PauseFrontEndMovie( "HUD" );
    PauseFrontEndMovie( "GAUGES" );

    Viewport *pBackViewport = gpRender->getRenderPass<RP_Back>().pViewport;
    pBackViewport->ClearColor.R = 0x22;
    pBackViewport->ClearColor.G = 0x20;
    pBackViewport->ClearColor.B = 0x32;
    pBackViewport->ClearColor.A = 0xff;

    Eigen::Vector4f focus =
        gpWorld->getPreviousTransform().row( 3 ).transpose();
    CarState *pCarState = gpScenaric->getLocalPlayer().getCarState();
    PhysicsVehicle *pPhysicsVehicle =
        ( pCarState != nullptr ) ? pCarState->getPhysicsVehicle() : nullptr;
    if ( pPhysicsVehicle != nullptr ) {
        focus = pPhysicsVehicle->getChassisWorldMatrix().row( 3 ).transpose();
        OTDU_UNIMPLEMENTED; // TODO: PHYSICS
    }

    CameraGPS *pCameraGPS = static_cast<CameraGPS *>(
        gpCamera->findCameraWithMode( 3, gpCamera->getBoundCarState() ) );
    pCameraGPS->setFocus( focus );

    if ( gpActiveGM != nullptr ) {
        gpActiveGM->setBlockPauseByStart( true );
    }

    CameraGame *pActiveCamera = gpCamera->getActiveCamera();
    const Eigen::Vector4f &zProjection = gpRender->getRenderPass<RP_Near>()
                                             .pScene->getFrustumWrite()
                                             .getZProjection();
    const Eigen::Vector3f target = pActiveCamera->getTarget();
    const Eigen::Vector3f up = pActiveCamera->getUp();
    pCameraGPS->setFixedPose(
        *pActiveCamera->getWorldPositionRW(),
        Eigen::Vector4f( target.x(), target.y(), target.z(), 1.0f ),
        Eigen::Vector4f( up.x(), up.y(), up.z(), 0.0f ), zProjection.x(),
        zProjection.y(), pActiveCamera->getFOV(),
        pActiveCamera->getAspectRatio() );

    int32_t zoom = pCameraGPS->getZoom();
    if ( zoom == CameraGPS::GZL_Fixed ) {
        zoom = pCameraGPS->getPreviousZoom();
    }
    if ( zoom == CameraGPS::GZL_Space ) {
        zoom = CameraGPS::GZL_Island;
    } else if ( zoom == CameraGPS::GZL_Fixed ) {
        zoom = CameraGPS::GZL_Medium;
    }
    if ( targetGameMode == GM_InstantChallenge ) {
        zoom = CameraGPS::GZL_Close;
    }
    pCameraGPS->startZoom( CameraGPS::GZL_Fixed, zoom );
    pCameraGPS->setTransitionEndCallback(
        &GMFreeride::OnGPSFlyUpEnd, nullptr,
        reinterpret_cast<void *>( static_cast<intptr_t>( targetGameMode ) ) );

    gpCamera->setCameraMode( 3, 0, 1.0f );
    if ( gpMngCamGPS != nullptr ) {
        gpMngCamGPS->setUpdateStreaming( true );
    }

    // A rider thrown off the bike is put back on it once the map closes.
    if ( pPhysicsVehicle != nullptr && pPhysicsVehicle->isBike() &&
         pPhysicsVehicle->hasBikeRider() ) {
        pPhysicsVehicle->setBackOnTrackForced( true );
        pCarState->getBody()->bHasBikeAvatar = false;
    }

    gpFlash->getFlashSound()->playFrontEndSound( "ent_map", 0, false );
    if ( gpMngChat != nullptr ) {
        gpMngChat->closeInputPopupIfOpen();
    }
}

// TODO: Move to GMMap
bool RequestMapGameMode( FlashPlayer *param_1 ) {
    // FUN_00718820
    param_1->getVariableAsString( "/:ContactType",
                                  gpFlash->getContactTypeRW() );

    if ( gpTestDriveInstance->getActiveGameMode() == GM_FreeRide ) {
        gpTutorial->cancelDisplayedMessages();
        TransitionFromFreerideToMap( 2 );
        return true;
    }

    if ( gpTestDriveInstance->getActiveGameMode() != GM_Map ) {
        GameMode *pGVar2 = gpActiveGameMode;
        pGVar2->requestExit();
        pGVar2->setTransitionFlags( 0u );
        gpTestDriveInstance->setNextGameMode( GM_Map );
    }

    return true;
}

// TODO: Move to right cpp
static bool FUN_00718730() {
    // FUN_00718730
    OTDU_UNIMPLEMENTED;
    return false;
}

MngFlash::MngFlash()
    : Manager(),
      bInitialized( false ),
      bWantAsyncInstall( false ),
      bNeedDatabaseInit( true ) {}

MngFlash::~MngFlash() { flashWidgets.clear(); }

bool MngFlash::initialize( TestDriveGameInstance * ) {
    // FUN_00718ee0
    gpFlash->initGameMode( gpActiveGameMode->getCurrentGameMode(), this,
                           false );
    return true;
}

void MngFlash::tick( float param_2, float param_3 ) {
    // FUN_00718370
    if ( !bInitialized ) {
        initializeWidgets();
        bInitialized = true;
    }

    for ( FlashWidget &widget : flashWidgets ) {
        if ( widget.pWidgetInstance != nullptr ) {
            widget.pWidgetInstance->tick( param_2, param_3 );
        }
    }
}

void MngFlash::terminate() {
    // FUN_0042b840
    gpFlash->FUN_0099be10();
}

void MngFlash::addWidget( const char pName[4], FlashEntity *pWidget ) {
    // FUN_00718320
    FlashWidget widget;
    // Store as little-endian to match FlashMessage::AsInteger used in kWidget
    // routing.
    widget.WidgetID = static_cast<uint32_t>( FlashMessage( pName ) );
    widget.pWidgetInstance = pWidget;

    flashWidgets.push_back( widget );
}

void MngFlash::removeWidget( FlashEntity *pWidget ) {
    // FUN_007182c0
    for ( auto it = flashWidgets.begin(); it != flashWidgets.end(); ) {
        if ( it->pWidgetInstance == pWidget ) {
            it = flashWidgets.erase( it );
        } else {
            ++it;
        }
    }
}

void MngFlash::removeWidgets() { flashWidgets.clear(); }

bool MngFlash::onMessage( FlashMessage *pMessage, FlashPlayer *pPlayer ) {
    // FUN_007bbd30
    OTDU_ASSERT( pMessage );

    for ( auto &widget : flashWidgets ) {
        if ( widget.pWidgetInstance != nullptr ) {
            if ( widget.pWidgetInstance->onMessage( pMessage, pPlayer ) ) {
                return true;
            }
        }
    }

    if ( gpMngChat != nullptr ) {
        bool bVar3 = gpMngChat->onMessage( pMessage );
        if ( bVar3 ) {
            return true;
        }
    }

    if ( gpTestDriveInstance == nullptr ) {
        return false;
    }

    switch ( *pMessage ) {
    case kGoGP:
    case kGoMap: {
        if ( gpActiveGameMode->getCurrentGameMode() == GM_FreeRide ||
             gpActiveGameMode->getCurrentGameMode() == GM_TestCar ) {
            bool bVar6 = FUN_00914df0();
            if ( bVar6 ) {
                return true;
            }
        }
        return RequestMapGameMode( pPlayer );
    }

    case kGoFreeride: {
        if ( gpTestDriveInstance->getActiveGameMode() == GM_FreeRide ) {
            return true;
        }
        gpScenaric->setICEnabled( false );
        if ( gpTestDriveInstance->getActiveGameMode() == GM_Map ) {
            gpActiveGameMode->setTransitionFlags(
                gpActiveGameMode->getTransitionFlags() & 0xffffffdf );
        }
        gpActiveGameMode->requestExit();
        gpTestDriveInstance->setNextGameMode( GM_FreeRide );
        return true;
    }

    case kGoAvatarShowcase: {
        if ( gpTestDriveInstance->getActiveGameMode() == GM_AvatarShowcase ) {
            return true;
        }
        gpActiveGameMode->requestExit();
        gpTestDriveInstance->setNextGameMode( GM_AvatarShowcase );
        return true;
    }

    case kGoEditor: {
        if ( gpActiveGameMode->getCurrentGameMode() == GM_FreeRide ||
             gpActiveGameMode->getCurrentGameMode() == GM_TestCar ) {
            bool bVar6 = FUN_00914df0();
            if ( bVar6 ) {
                return true;
            }
        }
        return FUN_00718730();
    }

    case kGoHome: {
        if ( gpTestDriveInstance->getActiveGameMode() == GM_Home ) {
            return true;
        }
        gpActiveGameMode->requestExit();
        gpTestDriveInstance->setNextGameMode( GM_Home );
        return true;
    }

    case kGaugesOff: {
        FlashPlayer *peVar5 = gpFlash->getFlashPlayer( "GAUGES" );
        if ( peVar5 == nullptr ) {
            return true;
        }
        peVar5->showFrame( "off_stop", "/", true );
        return true;
    }

    case kGaugesOn: {
        FlashPlayer *peVar5 = gpFlash->getFlashPlayer( "GAUGES" );
        if ( peVar5 == nullptr ) {
            return true;
        }
        peVar5->showFrame( "on_stop", "/", true );
        return true;
    }

    case kFadeIn: {
        FlashPlayer *peVar5 = gpFlash->getFlashPlayer( "GAUGES" );
        if ( peVar5 == nullptr ) {
            return true;
        }
        peVar5->showFrame( "on", "/", true );
        return true;
    }

    case kFadeOut: {
        FlashPlayer *peVar5 = gpFlash->getFlashPlayer( "GAUGES" );
        if ( peVar5 == nullptr ) {
            return true;
        }
        peVar5->showFrame( "off", "/", true );
        return true;
    }

    case kBCVI: {
        if ( gpActiveGMScene != nullptr ) {
            GetShowcasePhysControl().bBackView = true;
        }
        return true;
    }

    case kFTVI: {
        if ( gpActiveGMScene != nullptr ) {
            GetShowcasePhysControl().bBackView = false;
        }
        return true;
    }

    case kPCOptions: {
        if ( gpMngOptions == nullptr ) {
            OTDU_LOG_ERROR( "TestDrive Flash : Unknown command (but was almost "
                            "treated) -=%s=-.\n",
                            pMessage[0].pAsChar );
            return false;
        }
        return gpMngOptions->onMessage( pMessage, pPlayer );
    }

    case kCounters: {
        if ( gpMngFlashCounter == nullptr ) {
            return false;
        }
        FlashMessage *pNextMessage = pMessage + 1;
        gpMngFlashCounter->onMessage( pNextMessage, pPlayer );
        return true;
    }

    case kPlaySound: {
        FlashMessage *pNextMessage = pMessage + 1;
        return gpFlash->getFlashSound()->playFrontEndSound(
            pNextMessage->pAsChar, 0, false );
    }

    case kMSKE: {
        // NOP
        return true;
    }

    case kHonkOn: {
        if ( gpActiveGMScene != nullptr ) {
            GetShowcasePhysControl().bHonk = true;
        }
        return true;
    }

    case kHonkOff: {
        if ( gpActiveGMScene != nullptr ) {
            GetShowcasePhysControl().bHonk = false;
        }
        return true;
    }

    case kMoneyUp: {
        gpPlayerData->getLoadedSave()->getDriver()->startMoneyAnimation();
        return true;
    }

    case kSBDD: {
        if ( gpDatabase != nullptr && pPlayer != nullptr ) {
            gpDatabase->setFlashLocalization( 0, 0u, pPlayer );
        }

        if ( bNeedDatabaseInit ) {
            gpScenaric->getIntroTutorial().sendFlashVariables( pPlayer );
            OTDU_UNIMPLEMENTED_ONCE; // TODO: GSTutorial
            bNeedDatabaseInit = false;
        }
        return true;
    }

    case kSliders: {
        FlashMessage *pNextMessage = pMessage + 1;
        return FlashSlider::OnMessage( pNextMessage );
    }

    case kStar: {
        if ( gpActiveGMScene != nullptr ) {
            GetShowcasePhysControl().bRev = true;
        }
        return true;
    }

    case kStao: {
        if ( gpActiveGMScene != nullptr ) {
            GetShowcasePhysControl().bRev = false;
        }
        return true;
    }

    case kStre: {
        if ( gpTestDriveInstance->getActiveGameMode() != GM_FreeRide ) {
            gpPlayerData->getLoadedSave()->getGarage()->endRental();
        }
        return true;
    }

    case kSounds: {
        FlashMessage *pNextMessage = pMessage + 1;
        return gpFlash->getFlashSound()->stopGroup( pNextMessage->pAsChar,
                                                    0.0f );
    }

    case kWidget: {
        return dispatchWidgetMessage( pMessage + 1, pPlayer );
    }

    default:
        break;
    }

    OTDU_LOG_ERROR( "TestDrive Flash : Unknown command -=%s=-.\n",
                    pMessage[0].pAsChar );
    return false;
}

bool MngFlash::dispatchWidgetMessage( FlashMessage *pMessage,
                                      FlashPlayer *pPlayer ) {
    // FUN_007181c0
    const uint32_t widgetID = static_cast<uint32_t>( *pMessage );
    for ( auto &widget : flashWidgets ) {
        if ( widget.pWidgetInstance != nullptr &&
             widget.WidgetID == widgetID ) {
            return widget.pWidgetInstance->onMessage( pMessage + 1, pPlayer );
        }
    }
    OTDU_LOG_ERROR( "TestDrive Flash : Unknown widget command -=%s=-.\n",
                    pMessage->pAsChar );
    return false;
}
