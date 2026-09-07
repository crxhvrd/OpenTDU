#include "shared.h"
#include "mng_photo_mode.h"

#include "config/gs_config.h"
#include "filesystem/gs_file.h"
#include "flash/entities/flash_entity.h"
#include "flash/flash_callbacks_register.h"
#include "flash/gs_flash.h"
#include "flash/implementation/flash_player.h"
#include "flash/implementation/flash_sprite.h"
#include "flash/modal_message_box.h"
#include "game/mng_hud.h"
#include "game/scenaric/gs_scenaric.h"
#include "game/scenaric/entity/entity_cinematic.h"
#include "game/scenaric/entity/entity_driver.h"
#include "world/mng_sector_name.h"
#include "game/vehicle/car_state.h"
#include "gs_timer.h"
#include "core/maths_helper.h"
#include "physics/vehicle/physics_vehicle.h"
#include "render/camera/camera_showcase.h"
#include "render/camera/gs_camera.h"
#include "render/draw_list.h"
#include "render/gs_render.h"
#include "render/postfx/gs_postfx.h"

static constexpr uint64_t kSavePopupHashcode = 0x37ba8e2ull;
static constexpr float kSavePopupDuration = 4.0f;
static constexpr const char *kDefaultCaption = "PHOTO"; // DAT_00f3ce38
static constexpr const char *kShotClipName = "lurephoto";
static constexpr const char *kPhotoContext = "Photo";
static constexpr const wchar_t *kPhotoContextDisplay = L"Photos";
static constexpr int32_t kPhotoCameraMode =
    6; // Camera showcase ID (TODO: QUANTIFY/ENUM TYPE THIS MAGIC)
static constexpr float kPhotoCameraMaxDistance = 50.0f;
static constexpr float kPhotoCameraTransitionTime = 1.0f;
static constexpr float kShowcaseMinDistance = 1.0f;     // FLOAT_00f4165c
static constexpr float kShowcaseBoundsMargin = 0.18f;   // DAT_00f41ba0
static constexpr float kShowcaseDefaultDistance = 4.0f; // DAT_00f418d4
static constexpr uint32_t kShowcaseCollisionFlags = 0x547;
static constexpr float kFallbackBackOffset = -0.1f;  // FLOAT_00f4188c
static constexpr float kFallbackHeight = 1.0f;       // DAT_00fabf38
static constexpr float kPhotoCameraNearPlane = 0.1f; // FLOAT_00f41668

MngPhotoMode *gpMngPhotoMode = nullptr; // DAT_010e7fec

void SetupShowcaseCameraOnCar( Matrix4x4f_t &carTransform,
                               const bool bEnableRoll,
                               const bool bEnableTranslation,
                               const float maxDistance,
                               const bool bForcePlacement,
                               CameraGame *pTransitionCamera,
                               const float transitionTime,
                               const bool bFrameOnBounds,
                               CarState *pCarState ) {
    // FUN_006cdd90
    if ( pCarState == nullptr || gpCamera == nullptr ) {
        return;
    }

    PhysicsVehicle *pPhysicsVehicle = pCarState->getPhysicsVehicle();
    if ( pPhysicsVehicle != nullptr && pPhysicsVehicle->isActive() ) {
        carTransform = pPhysicsVehicle->getChassisWorldMatrix();
    }

    Matrix4x4f_t boundingBox;
    pCarState->retrieveBoundingBox( boundingBox );

    const float boundingRadius =
        TDUMat4x4MulVec( boundingBox,
                         Eigen::Vector4f( 0.5f, 0.5f, 0.5f, 0.0f ) )
            .norm();

    Eigen::Vector4f *pCarPosition =
        reinterpret_cast<Eigen::Vector4f *>( carTransform.row( 3 ).data() );

    const float distanceToBoxCenter =
        ( *pCarPosition - boundingBox.row( 3 ).transpose() ).norm();

    CameraShowcase *pCamera = (CameraShowcase *)gpCamera->findCameraWithMode(
        kPhotoCameraMode, gpCamera->getBoundCarState() );
    CameraGame *pActiveCamera = gpCamera->getActiveCamera();
    if ( pCamera == nullptr || pActiveCamera == nullptr ) {
        return;
    }

    pCamera->setFOVAndAspectRatio( pActiveCamera->getAspectRatio(),
                                   pActiveCamera->getFOV() );
    pCamera->lookAt( pCarPosition );
    pCamera->setOffset( Eigen::Vector4f::Zero() );

    float minDistance = kShowcaseMinDistance;
    if ( bFrameOnBounds ) {
        minDistance =
            distanceToBoxCenter + boundingRadius + kShowcaseBoundsMargin;
    }
    pCamera->setDistanceRange( ( minDistance <= maxDistance ) ? minDistance
                                                              : maxDistance,
                               maxDistance );
    pCamera->setCollisionFlags( kShowcaseCollisionFlags );
    pCamera->setTargetCar( pCarState, pPhysicsVehicle );

    CameraGame *pOutgoingCamera = gpCamera->findCameraWithMode(
        gpCamera->findFirstVisibleTransitionMode(),
        gpCamera->getBoundCarState() );

    bool bPlaced = false;
    if ( pOutgoingCamera == nullptr || bForcePlacement ) {
        bPlaced =
            pCamera->placeAt( *pActiveCamera->getWorldPositionRW(), true );
    } else {
        pOutgoingCamera->tick( 0.0f, false, true );
        bPlaced =
            pCamera->placeAt( *pOutgoingCamera->getWorldPositionRW(), true );

        if ( pCamera->getMinDistance() > kShowcaseDefaultDistance ) {
            pCamera->setDistance( pCamera->getMinDistance() );
        } else {
            pCamera->setDistance(
                ( kShowcaseDefaultDistance > pCamera->getMaxDistance() )
                    ? pCamera->getMaxDistance()
                    : kShowcaseDefaultDistance );
        }
    }

    if ( !bPlaced ) {
        Eigen::Vector4f forward = carTransform.row( 2 ).transpose();
        forward.normalize();

        const Eigen::Vector4f fallbackOffset(
            forward.x() * kFallbackBackOffset, kFallbackHeight,
            forward.z() * kFallbackBackOffset, 0.0f );
        pCamera->placeAt( *pCarPosition + fallbackOffset, true );
    }

    pCamera->setAllowRoll( bEnableRoll );
    pCamera->setCanMoveCamera( bEnableTranslation );
    pCamera->setFarPlane( gpRender->getDefaultFarPlane() );
    pCamera->setNearPlane( kPhotoCameraNearPlane );

    gpCamera->setCameraMode( kPhotoCameraMode, pTransitionCamera,
                             transitionTime );
}

MngPhotoMode::MngPhotoMode()
    : Manager(),
      PhotoFilters(),
      bPhotoModeActive( false ),
      carTransform( Matrix4x4f_t::Identity() ),
      MngScrenshots(),
      pShotMaterial( nullptr ),
      pFlashPlayer( nullptr ),
      bWaitingSaveResult( false ),
      bFXWasOn( true ) {
    // FUN_006cd300
    gpMngPhotoMode = this;
}

MngPhotoMode::~MngPhotoMode() {
    // FUN_006cdaf0
    gpMngPhotoMode = nullptr;
}

bool MngPhotoMode::initialize( TestDriveGameInstance * ) {
    // FUN_006cdad0
    MngScrenshots.create( gpRender->getWidth(), gpRender->getHeight(),
                          VF_X8R8G8B8 );
    return true;
}

void MngPhotoMode::tick( float, float ) {
    // FUN_006ce510
    if ( bFXWasOn ) {
        // Wait until every queued screenshot file has been flushed to disk
        // before dismissing the "saving..." message box.
        if ( !gpFile->hasPendingFileToSave() ) {
            if ( MngScrenshots.getSaveState() != SWS_Unknown ) {
                bFXWasOn = false;
                gpFlashMessageBox->FUN_0099fbe0();
            }
        }
    }

    if ( bPhotoModeActive ) {
        PhotoFilters.tick( gpTimer->SystemDeltaTime );
    }
}

void MngPhotoMode::setShowcaseCameraInput( const bool bEnabled ) {
    if ( gpCamera == nullptr ) {
        return;
    }

    CameraShowcase *pCamera = (CameraShowcase *)gpCamera->findCameraWithMode(
        kPhotoCameraMode, gpCamera->getBoundCarState() );
    if ( pCamera != nullptr ) {
        pCamera->setInputEnabled( bEnabled );
    }
}

void MngPhotoMode::enterPhotoMode() {
    if ( !bPhotoModeActive ) {
        if ( gpCamera != nullptr ) {
            gpCamera->setUseBlur( true );
        }
        bPhotoModeActive = true;

        PhotoFilters.start();

        OTDU_UNIMPLEMENTED_ONCE; // TODO: ONLINE (freeride manager)
    }

    EntityCinematic::ShowSpots( false );
    if ( gpMngSectorName != nullptr ) {
        gpMngSectorName->bEnabled = false;
    }

    SetupShowcaseCameraOnCar( carTransform, true, true, kPhotoCameraMaxDistance,
                              false, nullptr, kPhotoCameraTransitionTime, false,
                              ( gpScenaric != nullptr )
                                  ? gpScenaric->getLocalPlayer().getCarState()
                                  : nullptr );

    PostFXStep *pGlowPause =
        ( gpPostFX != nullptr )
            ? gpPostFX->getEffect<ePostFXStep::PFXS_GlowPause>()
            : nullptr;

    bGlowPauseWasEnabled =
        ( pGlowPause != nullptr ) && pGlowPause->getInput()->isEnabled();

    if ( gpPostFX != nullptr && !gpPostFX->isFXLocked() &&
         pGlowPause != nullptr ) {
        pGlowPause->getInput()->setEnabled( false );
    }
}

void MngPhotoMode::leavePhotoMode() {
    if ( gpPostFX != nullptr && !gpPostFX->isFXLocked() ) {
        const bool bRestored =
            ( gpRender != nullptr && gpRender->isHDREnabled() )
                ? false
                : bGlowPauseWasEnabled;

        PostFXStep *pGlowPause =
            gpPostFX->getEffect<ePostFXStep::PFXS_GlowPause>();
        if ( pGlowPause != nullptr ) {
            pGlowPause->getInput()->setEnabled( bRestored );
        }
    }

    if ( gpCamera != nullptr ) {
        gpCamera->setUseBlur( false );
    }
    bPhotoModeActive = false;

    OTDU_UNIMPLEMENTED_ONCE; // TODO: ONLINE (freeride manager)
    EntityCinematic::ShowSpots( true );
    if ( gpMngSectorName != nullptr ) {
        gpMngSectorName->bEnabled = true;
    }

    if ( gpCamera != nullptr ) {
        gpCamera->restoreCamera();
    }

    CarState *pCarState = ( gpScenaric != nullptr )
                              ? gpScenaric->getLocalPlayer().getCarState()
                              : nullptr;
    EntityDriver *pDriver =
        ( pCarState != nullptr ) ? pCarState->getDriver() : nullptr;
    if ( pDriver != nullptr && pDriver->getInstanceRefCount() < 1 ) {
        pDriver->setInstanceRefCount( pDriver->getInstanceRefCount() + 1 );
    }

    PhotoFilters.stop();
}

void MngPhotoMode::takeShot() {
    CarState *pCaptionCarState =
        ( gpScenaric != nullptr ) ? gpScenaric->getLocalPlayer().getCarState()
                                  : nullptr;
    if ( pCaptionCarState == nullptr || gpMngSectorName == nullptr ) {
        snprintf( caption, sizeof( caption ), "%s", kDefaultCaption );
    } else {
        const Eigen::Vector4f carPosition =
            pCaptionCarState->getBody()->WorldMatrix.row( 3 ).transpose();

        const char *pSectorName = gpMngSectorName->getSectorName( carPosition );
        if ( pSectorName == nullptr ) {
            pSectorName = " ";
        }

        strncpy( caption, pSectorName, kMaxCaptionLength - 1 );
        caption[kMaxCaptionLength - 1] = '\0';

        for ( char *pIt = caption; *pIt != '\0'; pIt++ ) {
            if ( *pIt == '\'' ) {
                *pIt = ' ';
            }
        }
    }

    if ( pFlashPlayer != nullptr ) {
        pFlashPlayer->setVariableValueWritableLinked( "/:TxtInput", caption );
    }

    setShowcaseCameraInput( false );

    pShotMaterial = MngScrenshots.grabAndGetMaterial();

    if ( gpFlash != nullptr ) {
        gpFlash->getMouse()->field12_0x30 = false;
    }
}

void MngPhotoMode::saveShot() {
    // FUN_006ce100
    // TODO: Move to separate shared header
    time_t rawTime = time( nullptr );
    tm localTime = {};
#if defined( OTDU_WIN32 )
    localtime_s( &localTime, &rawTime );
#else
    localtime_r( &rawTime, &localTime );
#endif

    char fileName[64];
    snprintf( fileName, sizeof( fileName ), "%02d%02d%02d%02d%02d%02d.jpg",
              localTime.tm_year + 1900, localTime.tm_mon + 1, localTime.tm_mday,
              localTime.tm_hour, localTime.tm_min, localTime.tm_sec );

    if ( gpFlashMessageBox != nullptr ) {
        gpFlashMessageBox->display( kSavePopupHashcode, kSavePopupDuration, 0,
                                    &MngPhotoMode::OnSavePopupClosed, this,
                                    true );
    }

    gFlashCallbacksRegister.removeAllSynonyms(
        &MngPhotoMode::ShotDrawCallback );

    pShotMaterial = nullptr;
    bFXWasOn = true;

    // Metadata format "(N:<caption>)(ID:<car>)
    // (FMT:<widescreen>)".
    char metaInfos[512] = {};
    CarState *pCarState =
        ( gpCamera != nullptr ) ? gpCamera->getBoundCarState() : nullptr;
    if ( pCarState != nullptr ) {
        snprintf( metaInfos, sizeof( metaInfos ), "(N:%s)(ID:%d)(FMT:%d)",
                  caption, pCarState->getCarID(),
                  ( gpConfig != nullptr ) ? gpConfig->b169Ratio : 0 );
    }

    MngScrenshots.save( fileName, kPhotoContext, kPhotoContextDisplay,
                        metaInfos );
}

void MngPhotoMode::showLiveView() {
    setShowcaseCameraInput( true );

    if ( gpCamera != nullptr ) {
        gpCamera->restoreCamera();
    }

    gFlashCallbacksRegister.add( kShotClipName, &MngPhotoMode::ShotDrawCallback,
                                 this );
}

void MngPhotoMode::cancelShot() {
    if ( gpCamera != nullptr ) {
        gpCamera->findAndSetCameraMode( kPhotoCameraMode, nullptr, 1.0f );
    }

    gFlashCallbacksRegister.removeAllSynonyms(
        &MngPhotoMode::ShotDrawCallback );
    pShotMaterial = nullptr;
}

void MngPhotoMode::OnSavePopupClosed( void *pUser ) {
    // FUN_006cdc10
    MngPhotoMode *pManager = static_cast<MngPhotoMode *>( pUser );
    if ( pManager->pFlashPlayer != nullptr ) {
        pManager->pFlashPlayer->showFrame( "photo_mode_in", "/menus", true );
    }
}

void MngPhotoMode::ShotDrawCallback( FlashRECT * /*pViewport*/,
                                     float /*depth*/,
                                     DrawList *pDrawList,
                                     void *pUser,
                                     FlashResource *pResource,
                                     const char * /*pClipName*/ ) {
    // FUN_006cdc70
    MngPhotoMode *pManager = static_cast<MngPhotoMode *>( pUser );
    if ( pManager->pFlashPlayer == nullptr ) {
        return;
    }

    FlashClip clip;
    if ( !pManager->pFlashPlayer->getClipPlayer( "/menus/frame/frame/lurephoto",
                                                 &clip ) &&
         !pManager->pFlashPlayer->getClipPlayer(
             "/menus/frame_bm/frame/lurephoto", &clip ) ) {
        return;
    }

    FlashRECT rect;
    clip.getPropertyViewport( eFlashProperty::FP_Viewport, rect );

    if ( pManager->pShotMaterial == nullptr ) {
        return;
    }

    pDrawList->setActiveMaterial( pManager->pShotMaterial );
    pDrawList->setDiffuse( 0, 0xffffffffu );

    DrawGaugeQuad( 0.0f, 0.0f, 1.0f, 1.0f, rect.X, rect.Y, rect.Width + rect.X,
                   rect.Height + rect.Y, pResource );
}

bool MngPhotoMode::onMessage( FlashMessage *pMessage ) {
    // FUN_006ce940
    if ( PhotoFilters.onMessage( pMessage ) ) {
        return true;
    }

    switch ( *pMessage ) {
    case FlashMessage( "phin" ): // photo mode in
        enterPhotoMode();
        return true;

    case FlashMessage( "phou" ): // photo mode out
        leavePhotoMode();
        return true;

    case FlashMessage( "phcl" ): // photo click
        takeShot();
        return true;

    case FlashMessage( "phsa" ): // photo save
        saveShot();
        return true;

    case FlashMessage( "phsh" ): // photo show
        showLiveView();
        return true;

    case FlashMessage( "phca" ): // photo mode cancel
        cancelShot();
        return true;

    case FlashMessage( "himo" ): // hide mouse (actually does the opposite)
        if ( gpFlash != nullptr ) {
            gpFlash->getMouse()->field12_0x30 = true;
        }
        return true;

    default:
        break;
    }

    return false;
}

void MngPhotoMode::terminate() {
    // FUN_00431f00
    MngScrenshots.destroy();
}
