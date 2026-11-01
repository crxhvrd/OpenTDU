#include "shared.h"
#include "mng_map_viewer.h"

#include "config/gs_config.h"
#include "flash/gs_flash.h"
#include "flash/implementation/ed_flash.h"
#include "flash/implementation/flash_player.h"
#include "flash/mng_fade.h"
#include "flash/movie_player.h"
#include "game/gm_scene.h"
#include "game/instant_challenge/mng_gme.h"
#include "game/lobby/gm_lobby.h"
#include "game/scenaric/entity/entity_cinematic.h"
#include "game/scenaric/game_intro_tutorial.h"
#include "game/scenaric/gs_scenaric.h"
#include "game/scenaric/mng_spots.h"
#include "game/scenaric/spots.h"
#include "physics/gs_physics.h"
#include "tdu_instance.h"
#include "input/gs_device.h"
#include "input/input_device.h"
#include "render/camera/camera_gps.h"
#include "render/camera/gs_camera.h"
#include "render/mng_cam_gps.h"
#include "world/gs_world.h"
#include "world/mng_electrical_network.h"
#include "world/mng_sun_flare.h"
#include "world/mng_tall_grass.h"
#include "world/streaming_manager.h"
#include "world/gs_world_streaming.h"

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

static constexpr float kFadeDuration = 0.5f;         // FLOAT_00f41674
static constexpr float kCameraTransitionTime = 1.0f; // FLOAT_00f4165c
static constexpr uint32_t kMapFilterLobbyIntra = 0x1ffdea;
static constexpr uint32_t kMapFilterLobbyInter = 0x1ff9ea;
static constexpr uint32_t kMapFilterLobby = 0x1ff5ea;
static constexpr uint8_t kSpotTypeChallenge = 7;

static constexpr FlashMessage kMsgMapOn = FlashMessage( "maon" );
static constexpr FlashMessage kMsgMapOff = FlashMessage( "maof" );
static constexpr FlashMessage kMsgStatsOn = FlashMessage( "ston" );
static constexpr FlashMessage kMsgStatsOff = FlashMessage( "stof" );
static constexpr FlashMessage kMsgZoomIn = FlashMessage( "zoin" );
static constexpr FlashMessage kMsgZoomOut = FlashMessage( "zoou" );
static constexpr FlashMessage kMsgChooseChallenge = FlashMessage( "chch" );
static constexpr FlashMessage kMsgBrowseChallengeM = FlashMessage( "cbhm" );
static constexpr FlashMessage kMsgBrowseChallengeS = FlashMessage( "cbhs" );

static bool gbMapViewSmoothMove = false; // DAT_016a6aa0
static constexpr uint8_t kMapFilterModeFreeMode = 0x10;

static bool IsMapCursorCaptured() {
    return gpMngCursor != nullptr && gpMngCursor->isCursorOff();
}

static Eigen::Vector4f GetSpotPosition( const SpotMap *pSpot ) {
    return Eigen::Vector4f( pSpot->X, pSpot->Y, pSpot->Z, 1.0f );
}

static void SetSpotStreamActive( const bool bActive ) {
    SpotStream &spotStream = gpWorld->getStreamingManager()->spotStream;
    spotStream.bStream = bActive;
    spotStream.bDraw = bActive;
}

static CameraGPS *FindGPSCamera() {
    return static_cast<CameraGPS *>( gpCamera->findCameraWithMode(
        kGPSCameraMode, gpCamera->getBoundCarState() ) );
}

static FlashPlayer *GetMovieHudFlashPlayer() {
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

    FlashPlayer *pHudPlayer = GetMovieHudFlashPlayer();
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

        FlashPlayer *pHudPlayer = GetMovieHudFlashPlayer();
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
      pSelectedSpot( nullptr ),
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
    restoreSpotFilter();
}

void MngMapViewer::restoreSpotFilter() {
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

void MngMapViewer::setSpot( const SpotMap *pNewSpot, FlashPlayer *pPlayer ) {
    // FUN_006bd6e0
    pMapPlayer = pPlayer;
    pSpot = pNewSpot;
    focusPosition = ( pNewSpot != nullptr )
                        ? GetSpotPosition( pNewSpot )
                        : Eigen::Vector4f( 0.0f, 0.0f, 0.0f, 1.0f );
    gpMngSpots->setFocus( focusPosition );
    mapView.pFocus = &focusPosition;
    gpMngSpots->setIconsEnabled( false );
    gpMngSpots->setPadNavigationEnabled( false );

    CameraGPS *pCameraGPS = FindGPSCamera();
    if ( pCameraGPS != nullptr ) {
        const auto isOverviewZoom = [pCameraGPS]() {
            return pCameraGPS->getZoom() == CameraGPS::GZL_Space ||
                   pCameraGPS->getZoom() == CameraGPS::GZL_Fixed;
        };
        if ( isOverviewZoom() ) {
            pCameraGPS->startZoom( pCameraGPS->getPreviousZoom(),
                                   pCameraGPS->getPreviousZoom() );
        }
        if ( isOverviewZoom() ) {
            pCameraGPS->startZoom( CameraGPS::GZL_Island,
                                   CameraGPS::GZL_Island );
        }
        pCameraGPS->updateFlashZoom( pPlayer );
    }
    gpMngSpots->setFlashPlayer();
}

bool MngMapViewer::onFlashMessage( const FlashMessage *pMessage ) {
    // FUN_006bd120
    if ( pMapPlayer == nullptr ) {
        return false;
    }

    switch ( *pMessage ) {
    case kMsgMapOn:
        state = MS_FadingIn;
        gpMngFade->startTransition(
            ( gpMngFade->getTransitionFlags() & ~0xcu ) | 1u, kFadeDuration,
            &MngMapViewer::OnOpenFadedOut, nullptr, this ); // LAB_006bd110
        gpMngSpots->setShowCursor( false );
        return true;
    case kMsgMapOff:
        if ( state == MS_Active ) {
            state = MS_Leaving;
            gpMngMapViewer->setFocus( GetSpotPosition( pSpot ) );
            gpMngCamGPS->setForceStreaming( true );
            gpMngSpots->setPadNavigationEnabled( false );
            gpMngSpots->closeInfoCart();
            gpMngSpots->setInfoCartRequested( false );
            SetSpotStreamActive( true );
        }
        return true;
    case kMsgStatsOff:
        if ( gpMapView != nullptr ) {
            gpMapView->bEnabled = false;
        }
        gpMngSpots->setPadNavigationEnabled( false );
        bPlayerListOpen = true;
        return true;
    case kMsgStatsOn:
        if ( gpMapView != nullptr ) {
            gpMapView->bEnabled = true;
        }
        gpMngSpots->setPadNavigationEnabled( true );
        bPlayerListOpen = false;
        return true;
    case kMsgZoomIn:
    case kMsgZoomOut:
        return true;
    case kMsgChooseChallenge: {
        const SpotMap *pSelected = gpMngSpots->getSelectedSpot();
        if ( pSelected != nullptr ) {
            pSelectedSpot = pSelected;
            pSpot = pSelected;
        }
        return true;
    }
    case kMsgBrowseChallengeM:
    case kMsgBrowseChallengeS: {
        const SpotMap *pSelected = gpMngSpots->getSelectedSpot();
        if ( pSelected != nullptr ) {
            pSelectedSpot = pSelected;
        }
        return true;
    }
    default:
        return false;
    }
}

void MngMapViewer::OnOpenFadedOut( void *pData, void * /*pSource*/ ) {
    // LAB_006bd110
    static_cast<MngMapViewer *>( pData )->open();
}

void MngMapViewer::open() {
    // FUN_006bce80
    state = MS_WaitingForGPSCamera;
    if ( !bRestoreFilter ) {
        savedFilter = gMapActiveFilterMode;
        savedSpotMask = static_cast<int32_t>( gMapChallengeFilterMask );
        bRestoreFilter = true;
    }
    gpMngSpots->setWasFreeMode( gMapActiveFilterMode ==
                                kMapFilterModeFreeMode );
    gMapActiveFilterMode = 1;
    gpMngSpots->refreshDensityMap();
    gpMngSpots->requestResort();

    char gameType[76] = {};
    pMapPlayer->getVariableAsString( "/:GameType", gameType );
    if ( strcmp( gameType, "host_intra" ) == 0 ||
         gpScenaric->getClubChallengeKind() == 1 ) {
        gMapChallengeFilterMask = kMapFilterLobbyIntra;
    } else if ( strcmp( gameType, "host_inter" ) == 0 ||
                gpScenaric->getClubChallengeKind() == 2 ) {
        gMapChallengeFilterMask = kMapFilterLobbyInter;
    } else if ( gpTestDriveInstance->getActiveGameMode() == GM_Lobby ) {
        gMapChallengeFilterMask = kMapFilterLobby;
    }

    if ( gpMngSunFlare != nullptr ) {
        gpMngSunFlare->setDraw( false );
    }
    if ( gpMngTallGrass != nullptr ) {
        gbGrassEnabled = false;
    }
    if ( gpMngElectricalNetwork != nullptr ) {
        gpMngElectricalNetwork->setActivated( false );
    }

    CameraGPS *pCameraGPS = FindGPSCamera();
    if ( pCameraGPS != nullptr ) {
        pCameraGPS->setFocus( ( pSpot != nullptr )
                                  ? GetSpotPosition( pSpot )
                                  : Eigen::Vector4f( 0.0f, 0.0f, 0.0f, 1.0f ) );
        if ( pCameraGPS->getZoom() == CameraGPS::GZL_Space ) {
            pCameraGPS->startZoom( CameraGPS::GZL_Island,
                                   CameraGPS::GZL_Island );
        } else if ( pCameraGPS->getZoom() == CameraGPS::GZL_Fixed ) {
            pCameraGPS->startZoom( CameraGPS::GZL_Medium,
                                   CameraGPS::GZL_Medium );
        }
    }

    pPreviousCamera = gpCamera->getActiveCamera();
    gpCamera->requestCameraMode( kGPSCameraMode, 0, kCameraTransitionTime );
    EntityCinematic *pCinematicLoop = gpActiveGMScene->getCinematicLoop();
    if ( pCinematicLoop != nullptr ) {
        pCinematicLoop->stop( true );
    }

    gpMngMapViewer->setFocus( GetSpotPosition( pSpot ) );
    gpMngCamGPS->setForceStreaming( false );
    gpMngCamGPS->setForceDraw( false );
    gpMngSpots->setInfoCartRequested( true );
    SetSpotStreamActive( false );
    gpMngFade->startTransition( ( gpMngFade->getTransitionFlags() & ~0x8u ) |
                                    5u,
                                kFadeDuration, &MngMapViewer::OnOpenFadedIn );
}

void MngMapViewer::OnOpenFadedIn( void * /*pData*/, void * /*pSource*/ ) {
    // LAB_006bcdc0
    gpMngSpots->setShowCursor( true );
    gpMngSpots->refreshCursorMode();
    gpMngSpots->setShowCursor( true );
}

void MngMapViewer::leave() {
    // FUN_006bd4e0
    const Eigen::Vector4f spotPosition = GetSpotPosition( pSpot );
    if ( !gpWorld->isStreamedAround( &spotPosition, true, false ) ) {
        return;
    }

    state = MS_FadingOut;
    gpMngFade->startTransition(
        ( gpMngFade->getTransitionFlags() & ~0xcu ) | 1u, kFadeDuration,
        &MngMapViewer::OnLeaveFadedOut, nullptr, this ); // FUN_006bd4d0
    gpMngSpots->setShowCursor( false );
    gpMngSpots->setPadNavigationEnabled( false );
    if ( gpMngCamGPS != nullptr ) {
        gpMngCamGPS->setUpdateStreaming( false );
    }

    StreamingManager *pStreaming = gpWorld->getStreamingManager();
    SectorManager &sectors = pStreaming->sectorManager;
    AreaGrid &areas = pStreaming->areaGrid;
    sectors.bNeedStreamUpdate = true;
    sectors.bDrawSector = false;
    areas.bNeedStreamUpdate = true;
    areas.bDrawArea = false;
    pStreaming->setActiveRenderLayers( 1 );
    sectors.bStreamSector = true;
    gpWorldStreaming->updateStreaming();
    areas.bStreamArea = true;
}

void MngMapViewer::OnLeaveFadedOut( void *pData, void * /*pSource*/ ) {
    // FUN_006bd4d0
    static_cast<MngMapViewer *>( pData )->close();
}

void MngMapViewer::close() {
    // FUN_006bd350
    switch ( gpTestDriveInstance->getActiveGameMode() ) {
    case GM_Lobby:
        pMapPlayer->showFrame( "open", "title", true );
        pMapPlayer->showFrame( "lobby", "/", true );
        break;
    case GM_Club:
        pMapPlayer->showFrame( "map_back", "ss_menus", false );
        break;
    default:
        break;
    }

    restoreSpotFilter();

    if ( gpMngTallGrass != nullptr ) {
        gbGrassEnabled = true;
    }
    if ( gpMngSunFlare != nullptr ) {
        gpMngSunFlare->setDraw( true );
    }
    if ( gpMngElectricalNetwork != nullptr ) {
        gpMngElectricalNetwork->setActivated( true );
    }
    gpMngSpots->setIconsEnabled( false );
    gpMngSpots->setPadNavigationEnabled( false );

    if ( pPreviousCamera != nullptr ) {
        gpCamera->setNextCamera( pPreviousCamera, 0, kCameraTransitionTime );
    }

    EntityCinematic *pCinematicLoop = gpActiveGMScene->getCinematicLoop();
    if ( pCinematicLoop != nullptr ) {
        pCinematicLoop->launchSpotCine( const_cast<SpotMap *>( pSpot ), 0.0f );
    }

    state = MS_Idle;
    if ( pSpot->Type == kSpotTypeChallenge ) {
        if ( gpTestDriveInstance->getActiveGameMode() == GM_Lobby ) {
            static_cast<GMLobby *>( gpActiveGameMode )->onMapSpotChanged();
        } else {
            OTDU_UNIMPLEMENTED; // TODO: ONLINE (GMClub vtable slot 18)
        }
    }

    PhysicsLock physLock;
    gpPhysics->refreshHeightmapCollision();
}
