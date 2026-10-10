#include "shared.h"
#include "mng_map_viewer.h"

#include "config/gs_config.h"
#include "flash/gs_flash.h"
#include "flash/implementation/ed_flash.h"
#include "flash/implementation/flash_player.h"
#include "flash/movie_player.h"
#include "game/scenaric/game_intro_tutorial.h"
#include "game/scenaric/gs_scenaric.h"
#include "game/scenaric/mng_spots.h"
#include "input/gs_device.h"
#include "input/input_device.h"
#include "render/camera/camera_gps.h"
#include "render/camera/gs_camera.h"
#include "render/mng_cam_gps.h"
#include "world/gs_world.h"
#include "world/streaming_manager.h"

MngMapViewer *gpMngMapViewer = nullptr; // DAT_010e801c
MapView *gpMapView = nullptr;           // DAT_010e8020

static constexpr int32_t kGPSCameraMode = 3;

static constexpr float kPanMinSpeed = 500.0f;            // DAT_00f417f4
static constexpr float kPanMaxSpeed = 2000.0f;           // DAT_00f419a8
static constexpr float kPanInputDeadZone = 0.3f;         // DAT_00f416bc
static constexpr float kPanInputRangeScale = 1.4285715f; // DAT_00f41b18
static constexpr float kPanEdgeSize = 0.05f;             // DAT_00fabf54
static constexpr float kPanDragDivisor = 2.0f;
static constexpr float kPanInertiaDamping = 0.9f;          // DAT_00f416a0
static constexpr float kPanInertiaEpsilon = 0.01f;         // DAT_00f416d4
static constexpr float kPanViewHeightScale = 2.0f;         // DAT_00f416b0
static constexpr float kPanMediumMinX = -616.0f;           // DAT_00f41b20
static constexpr float kPanCloseMin = 384.0f;              // DAT_00f416e8
static constexpr float kPanMediumMaxX = 68200.0f;          // DAT_00f41b30
static constexpr float kPanCloseMaxX = 67200.0f;           // DAT_00f41b1c
static constexpr float kPanMaxZ = 54912.0f;                // DAT_00f41b28
static constexpr float kPanDefaultMaxXSectorScale = 10.5f; // DAT_00f41b2c
static constexpr float kPanDefaultMaxZSectorScale = 8.5f;  // DAT_00f41b24

static bool gbMapViewSmoothMove = false; // DAT_016a6aa0
static constexpr uint8_t kMapFilterModeFreeMode = 0x10;

static bool IsMapCursorCaptured() {
    OTDU_UNIMPLEMENTED_ONCE; // TODO: MngCursor (DAT_010e7cf4)
    return false;
}

static CameraGPS *FindGPSCamera() {
    return static_cast<CameraGPS *>( gpCamera->findCameraWithMode(
        kGPSCameraMode, gpCamera->getBoundCarState() ) );
}

static FlashPlayer *GetHudFlashPlayer() {
    MoviePlayer *pMoviePlayer =
        ( gpFlash != nullptr ) ? gpFlash->getMoviePlayer( 1, 0 ) : nullptr;
    if ( pMoviePlayer == nullptr ||
         pMoviePlayer->MovieBank.getState() !=
             BankFlash::eBankFlashInit::BFI_Loaded ) {
        return nullptr;
    }
    return pMoviePlayer->MovieBank.getFlashPlayer();
}

static float ScaleScrollInput( const float value,
                               const float minSpeed,
                               const float maxSpeed ) {
    if ( value < kPanInputDeadZone ) {
        return 0.0f;
    }
    const float clampedValue = ( value < 1.0f ) ? value : 1.0f;
    return ( maxSpeed - minSpeed ) * ( clampedValue - kPanInputDeadZone ) *
               kPanInputRangeScale +
           minSpeed;
}

static float ReadScrollAction( DeviceAction *pAction ) {
    if ( pAction == nullptr || pAction->NumAddedChannels == 0 ) {
        return 0.0f;
    }
    return pAction->FUN_00434bc0( 0.0f, 1.0f );
}

static void ClampToBounds( Eigen::Vector4f &position,
                           const Eigen::Vector4f &bounds ) {
    if ( bounds.x() > position.x() ) {
        position.x() = bounds.x();
    }
    if ( bounds.y() > position.z() ) {
        position.z() = bounds.y();
    }
    if ( position.x() > bounds.z() ) {
        position.x() = bounds.z();
    }
    if ( position.z() > bounds.w() ) {
        position.z() = bounds.w();
    }
}

MapView::MapView()
    : MinSpeed( kPanMinSpeed ),
      MaxSpeed( kPanMaxSpeed ),
      bEnabled( true ),
      bFocusMoved( false ),
      bEdgeScrollActive( false ),
      pFocus( nullptr ),
      MediumBounds( kPanMediumMinX, kPanCloseMin, kPanMediumMaxX, kPanMaxZ ),
      CloseBounds( kPanCloseMin, kPanCloseMin, kPanCloseMaxX, kPanMaxZ ),
      InputX( 0.0f ),
      InputZ( 0.0f ),
      MoveStart( Eigen::Vector4f::Zero() ),
      MoveTarget( Eigen::Vector4f::Zero() ),
      MoveVelocity( Eigen::Vector4f::Zero() ),
      MoveTime( -1.0f ),
      MoveDuration( 0.0f ),
      DragAnchor( Eigen::Vector4f::Zero() ),
      DragVelocityX( 0.0f ),
      DragVelocityZ( 0.0f ) {
    // FUN_006bc2b0
    const SectorManager &sectorManager =
        gpWorld->getStreamingManager()->sectorManager;
    const float sectorsX = static_cast<float>(
        static_cast<int32_t>( sectorManager.numSectors * 0x300 ) );
    const float sectorsZ = static_cast<float>(
        static_cast<int32_t>( sectorManager.numSubSectors * 0x300 ) );
    DefaultBounds = Eigen::Vector4f( sectorsX * 0.5f, sectorsZ * 0.5f,
                                     sectorsX * kPanDefaultMaxXSectorScale,
                                     sectorsZ * kPanDefaultMaxZSectorScale );

    gpMapView = this;
}

void MapView::update( const float deltaTime ) {
    // FUN_006bc740
    if ( isMoving() ) {
        updateMove( deltaTime );
        return;
    }

    if ( !bEnabled ) {
        return;
    }

    CameraGPS *pCameraGPS = FindGPSCamera();
    float offsetX = 0.0f;
    float offsetZ = 0.0f;
    bool bDragged = false;

    if ( gpFlash->FUN_0099b1e0( 3, 1 ) && !IsMapCursorCaptured() ) {
        const Eigen::Vector4f cursor( gEdFlash.Input.MouseCoordinates[0],
                                      gEdFlash.Input.MouseCoordinates[1], 1.0f,
                                      1.0f );
        const InputChannel *pButton = gpDevice->getMouseState().pChannelButton2;
        if ( pButton != nullptr && ( pButton->Flags & 0x80 ) != 0 ) {
            screenToWorld( cursor, DragAnchor );
            DragVelocityZ = 0.0f;
            DragVelocityX = 0.0f;
        }

        const int32_t zoom = pCameraGPS->getZoom();
        if ( zoom != CameraGPS::GZL_Island && zoom != CameraGPS::GZL_Space ) {
            if ( pButton != nullptr && ( pButton->Flags & 0x10 ) != 0 ) {
                Eigen::Vector4f cursorPosition;
                screenToWorld( cursor, cursorPosition );
                const Eigen::Vector4f delta =
                    ( cursorPosition - DragAnchor ) / kPanDragDivisor;
                pFocus->x() -= delta.x();
                pFocus->z() += delta.z();
                screenToWorld( cursor, DragAnchor );
                bDragged = true;

                DragVelocityX =
                    delta.x() / ( pCameraGPS->getZoomFactor() * deltaTime );
                DragVelocityZ =
                    delta.z() / ( pCameraGPS->getZoomFactor() * deltaTime );
            } else if ( DragVelocityX > 0.0f ||
                        fabsf( DragVelocityZ ) > 0.0f ) {
                if ( !IsMapCursorCaptured() ) {
                    offsetX = 0.0f - DragVelocityX;
                    offsetZ = DragVelocityZ;
                }
                DragVelocityX = DragVelocityX * kPanInertiaDamping + 0.0f;
                DragVelocityZ = DragVelocityZ * kPanInertiaDamping + 0.0f;
                if ( fabsf( DragVelocityX ) < kPanInertiaEpsilon &&
                     fabsf( DragVelocityZ ) < kPanInertiaEpsilon ) {
                    DragVelocityZ = 0.0f;
                    DragVelocityX = 0.0f;
                }
            }

            if ( DragVelocityX == 0.0f && DragVelocityZ == 0.0f && !bDragged ) {
                computeEdgeScroll( offsetX, offsetZ, MinSpeed, MaxSpeed );
            }
        }
    }

    const float speedRange = MaxSpeed - MinSpeed;
    const float moveX = InputX * speedRange * kPanInputRangeScale + offsetX;
    const float moveZ = InputZ * speedRange * kPanInputRangeScale + offsetZ;
    InputZ = 0.0f;
    InputX = 0.0f;

    if ( moveX != 0.0f || moveZ != 0.0f || bDragged ) {
        pFocus->x() =
            pCameraGPS->getZoomFactor() * moveX * deltaTime + pFocus->x();
        pFocus->z() =
            pCameraGPS->getZoomFactor() * moveZ * deltaTime + pFocus->z();

        switch ( pCameraGPS->getZoom() ) {
        case CameraGPS::GZL_Medium:
            ClampToBounds( *pFocus, MediumBounds );
            break;
        case CameraGPS::GZL_Close:
            ClampToBounds( *pFocus, CloseBounds );
            break;
        default:
            ClampToBounds( *pFocus, DefaultBounds );
            break;
        }
    }

    if ( !pCameraGPS->isInTransition() ) {
        return;
    }

    const int32_t zoom = pCameraGPS->getZoom();
    const int32_t previousZoom = pCameraGPS->getPreviousZoom();
    const float blend = pCameraGPS->getTransitionBlend();
    Eigen::Vector4f bounds;
    if ( zoom == CameraGPS::GZL_Close ) {
        bounds = CloseBounds;
    } else if ( zoom == CameraGPS::GZL_Medium ) {
        if ( previousZoom == CameraGPS::GZL_Close ) {
            bounds = CloseBounds * ( 1.0f - blend ) + MediumBounds * blend;
        } else {
            bounds = MediumBounds;
        }
    } else {
        return;
    }
    ClampToBounds( *pFocus, bounds );
    bFocusMoved = true;
}

void MapView::startMove( const Eigen::Vector4f &target, const float duration ) {
    // FUN_006bc460
    if ( pFocus == nullptr ) {
        return;
    }

    MoveTime = 0.0f;
    MoveDuration = duration;
    MoveStart = *pFocus;
    MoveTarget = target;
    MoveVelocity = ( target - MoveStart ) / duration;

    FlashPlayer *pHudPlayer = GetHudFlashPlayer();
    if ( pHudPlayer != nullptr ) {
        pHudPlayer->setVariableValue( "/:IsMapQuit", 0 );
    }
}

void MapView::updateMove( const float deltaTime ) {
    // FUN_006bc580
    MoveTime += deltaTime;
    float t = MoveTime / MoveDuration;
    if ( t < 0.0f ) {
        t = 0.0f;
    } else if ( t > 1.0f ) {
        t = 1.0f;
    }

    if ( !gbMapViewSmoothMove ) {
        *pFocus += MoveVelocity * deltaTime;
    } else {
        const float smoothT = t * t * 3.0f - t * t * t * 2.0f;
        t = ( 1.0f - smoothT ) * 0.0f + smoothT;
        *pFocus = MoveStart * ( 1.0f - t ) + MoveTarget * t;
    }

    if ( t >= 1.0f ) {
        MoveTime = -1.0f;
        *pFocus = MoveTarget;

        FlashPlayer *pHudPlayer = GetHudFlashPlayer();
        if ( pHudPlayer != nullptr &&
             !gpScenaric->getIntroTutorial().isActive() ) {
            pHudPlayer->setVariableValue( "/:IsMapQuit", 1 );
        }
    }
    bFocusMoved = true;
}

void MapView::computeEdgeScroll( float &outX,
                                 float &outZ,
                                 const float minSpeed,
                                 const float maxSpeed ) {
    // FUN_006bbcf0
    float left = 0.0f;
    float right = 0.0f;
    float up = 0.0f;
    float down = 0.0f;
    PlayerConfiguration *pPlayerConfig = gpConfig->pPlayerConfig;
    if ( pPlayerConfig != nullptr ) {
        left = ReadScrollAction( pPlayerConfig->pMapLeft );
        right = ReadScrollAction( pPlayerConfig->pMapRight );
        up = ReadScrollAction( pPlayerConfig->pMapUp );
        down = ReadScrollAction( pPlayerConfig->pMapDown );
    }

    outX = ScaleScrollInput( right, minSpeed, maxSpeed ) -
           ScaleScrollInput( left, minSpeed, maxSpeed );
    outZ = ScaleScrollInput( down, minSpeed, maxSpeed ) -
           ScaleScrollInput( up, minSpeed, maxSpeed );

    if ( IsMapCursorCaptured() ) {
        return;
    }

    const float mouseX = gEdFlash.Input.MouseCoordinates[0];
    const float mouseY = gEdFlash.Input.MouseCoordinates[1];
    const float speedRange = maxSpeed - minSpeed;

    float edgeX = 0.0f;
    bool bOutsideEdgeX = false;
    if ( mouseX >= 0.0f && kPanEdgeSize >= mouseX ) {
        edgeX = 0.0f - ( ( kPanEdgeSize - mouseX ) / kPanEdgeSize * speedRange +
                         minSpeed );
    } else if ( mouseX >= 1.0f - kPanEdgeSize && 1.0f >= mouseX ) {
        edgeX =
            ( mouseX - ( 1.0f - kPanEdgeSize ) ) / kPanEdgeSize * speedRange +
            minSpeed;
    } else {
        bOutsideEdgeX = true;
    }

    float edgeZ = 0.0f;
    bool bOutsideEdgeZ = false;
    if ( mouseY >= 0.0f && kPanEdgeSize >= mouseY ) {
        edgeZ = 0.0f - ( ( kPanEdgeSize - mouseY ) / kPanEdgeSize * speedRange +
                         minSpeed );
    } else if ( mouseY >= 1.0f - kPanEdgeSize && 1.0f >= mouseY ) {
        edgeZ =
            ( mouseY - ( 1.0f - kPanEdgeSize ) ) / kPanEdgeSize * speedRange +
            minSpeed;
    } else {
        bOutsideEdgeZ = true;
    }

    if ( bOutsideEdgeX && bOutsideEdgeZ ) {
        bEdgeScrollActive = true;
    }
    if ( bEdgeScrollActive ) {
        outX += edgeX;
        outZ += edgeZ;
    }
}

void MapView::screenToWorld( const Eigen::Vector4f &cursor,
                             Eigen::Vector4f &outPosition ) const {
    // FUN_006bc040
    CameraGame *pCamera = gpCamera->getDefaultCamera();
    const Matrix4x4f_t &camToWorld = pCamera->getCamToWorld();
    const Eigen::Vector4f right = camToWorld.row( 0 ).transpose();
    const Eigen::Vector4f up = camToWorld.row( 1 ).transpose();
    const Eigen::Vector3f eye3 = pCamera->getWorldPosition();
    const Eigen::Vector3f target3 = pCamera->getTarget();
    const Eigen::Vector4f eye( eye3.x(), eye3.y(), eye3.z(), 1.0f );
    const Eigen::Vector4f target( target3.x(), target3.y(), target3.z(), 1.0f );

    const float viewHeight = pCamera->getTanHalfFov() *
                             ( target - eye ).norm() * kPanViewHeightScale;
    const float viewWidth = pCamera->getAspectRatio() * viewHeight;
    const float cursorX = cursor.x() - 0.5f;
    const float cursorY = cursor.y() - 0.5f;

    const Eigen::Vector4f screenOffset =
        right * viewWidth * cursorX + up * viewHeight * cursorY;
    const Eigen::Vector4f pointOnScreen = target + screenOffset;
    Eigen::Vector4f direction = pointOnScreen - eye;
    direction /= direction.norm();
    const float rayLength = fabsf( eye.y() / direction.y() );

    outPosition = *pFocus + direction * rayLength;
    outPosition += screenOffset;
}

MngMapViewer::MngMapViewer()
    : Manager(),
      pSpot( nullptr ),
      mapView(),
      bRestoreFilter( false ),
      savedFilter( 0 ),
      savedSpotMask( 0 ),
      focusPosition( Eigen::Vector4f::Zero() ),
      bPendingFocusMove( false ),
      pendingFocusTarget( Eigen::Vector4f::Zero() ),
      bSyncSpotsFocus( true ),
      bPlayerListOpen( false ),
      state( MS_Idle ),
      pMapPlayer( nullptr ),
      pPreviousCamera( nullptr ) {
    // FUN_006bc400
    mapView.pFocus = &focusPosition;
    gpMngMapViewer = this;
}

MngMapViewer::~MngMapViewer() {
    // FUN_009bfae0 (inlined)
    gpMngMapViewer = nullptr;
    gpMapView = nullptr;
}

void MngMapViewer::tick( float deltaTime, float /*totalTime*/ ) {
    // FUN_006bd5f0
    CameraGame *pActiveCamera = gpCamera->getActiveCamera();
    const bool bGPSCameraActive =
        pActiveCamera != nullptr && pActiveCamera->getType() == kGPSCameraMode;

    if ( state == MS_WaitingForGPSCamera && bGPSCameraActive ) {
        state = MS_Active;
        gpMngSpots->setIconsEnabled( true );
        gpMngSpots->setPadNavigationEnabled( true );
        gpMngSpots->setShowCursor( false );
    }

    if ( state == MS_Active ) {
        if ( gpMngCamGPS != nullptr ) {
            gpMngCamGPS->setUpdateStreaming( true );
        }

        mapView.update( deltaTime );

        if ( bGPSCameraActive ) {
            CameraGPS *pCameraGPS = static_cast<CameraGPS *>( pActiveCamera );
            pCameraGPS->setFocus( focusPosition );
            pCameraGPS->tick( 0.0f, false, true );
        }

        if ( bSyncSpotsFocus ) {
            gpMngSpots->setFocus( focusPosition );
        }
    }

    if ( state == MS_Leaving ) {
        leave();
    }
}

void MngMapViewer::pause() {
    // FUN_006bbcd0
    SpotStream &spotStream = gpWorld->getStreamingManager()->spotStream;
    spotStream.bDraw = true;
    spotStream.bStream = true;
}

void MngMapViewer::terminate() {
    // FUN_006bce20
    if ( bRestoreFilter ) {
        gpMngSpots->setWasFreeMode( gMapActiveFilterMode ==
                                    kMapFilterModeFreeMode );
        gMapActiveFilterMode = savedFilter;
        gpMngSpots->refreshDensityMap();
        gpMngSpots->requestResort();
        gMapChallengeFilterMask = static_cast<uint32_t>( savedSpotMask );
        bRestoreFilter = false;
    }
}

void MngMapViewer::leave() {
    // FUN_006bd4e0
    OTDU_UNIMPLEMENTED;
}
