#include "shared.h"
#include "map_cursor.h"

#include "config/gs_config.h"
#include "flash/implementation/ed_flash.h"
#include "game/map/gm_map.h"
#include "game/map/mng_map_viewer.h"
#include "game/scenaric/mng_spots.h"
#include "game/scenaric/spots.h"
#include "input/gs_device.h"
#include "input/input_device.h"
#include "render/camera/camera_gps.h"
#include "render/camera/gs_camera.h"

static constexpr uint8_t kMapFilterFreeMode = 0x10;
static constexpr float kMapFocusDefault = 0.5f; // FLOAT_00f41674
static constexpr float kNoZoomCursor = -1.0f;   // FLOAT_00f41664
static constexpr float kCursorPadSpeedY = 0.5f; // DAT_00fab960
static constexpr float kCursorPadSpeedX = 0.5f; // DAT_00fab964
static constexpr int32_t kCameraModeGPS = 3;

static CameraGPS *FindGPSCamera() {
    return static_cast<CameraGPS *>( gpCamera->findCameraWithMode(
        kCameraModeGPS, gpCamera->getBoundCarState() ) );
}

static float ReadCursorAction( DeviceAction *pAction ) {
    if ( pAction == nullptr || pAction->NumAddedChannels == 0 ) {
        return 0.0f;
    }
    return pAction->FUN_00434bc0( 0.0f, 1.0f );
}

MapCursor::MapCursor()
    : cursorX( kMapFocusDefault ),
      cursorY( kMapFocusDefault ),
      zoomCursorX( kNoZoomCursor ),
      zoomCursorY( kNoZoomCursor ),
      zoomOutCursorPosition( Eigen::Vector4f::Zero() ) {
    // FUN_00741650
}

void MapCursor::update( FlashPlayer *pPlayer, const float deltaTime ) {
    // FUN_00741d30
    if ( pPlayer == nullptr || !gpMngSpots->areMapRectsLatched() ) {
        return;
    }
    const FlashRECT &rect = gpMngSpots->getFreeModeRect();
    CameraGPS *pCameraGPS = FindGPSCamera();

    PlayerConfiguration *pPlayerConfig = gpConfig->pPlayerConfig;
    const bool bMenuPadPressed =
        ( pPlayerConfig->pMenuLeft->Flags & 1 ) != 0 ||
        ( pPlayerConfig->pMenuRight->Flags & 1 ) != 0 ||
        ( pPlayerConfig->pMenuUp->Flags & 1 ) != 0 ||
        ( pPlayerConfig->pMenuDown->Flags & 1 ) != 0;

    float inputLeft = 0.0f;
    float inputRight = 0.0f;
    float inputUp = 0.0f;
    float inputDown = 0.0f;
    if ( pPlayerConfig != nullptr ) {
        inputLeft = ReadCursorAction( pPlayerConfig->pMapCursorLeft );
        inputRight = ReadCursorAction( pPlayerConfig->pMapCursorRight );
        inputUp = ReadCursorAction( pPlayerConfig->pMapCursorUp );
        inputDown = ReadCursorAction( pPlayerConfig->pMapCursorDown );
    }

    const bool bMouseMoved = gpDevice->getMouseState().FUN_00980000();
    const Eigen::Vector2f mouse( gEdFlash.Input.MouseCoordinates[0],
                                 gEdFlash.Input.MouseCoordinates[1] );
    const bool bMouseInRect =
        rect.X <= mouse.x() && mouse.x() <= rect.Width + rect.X &&
        rect.Y <= mouse.y() && mouse.y() <= rect.Height + rect.Y;

    if ( gMapActiveFilterMode != kMapFilterFreeMode && !bMenuPadPressed ) {
        if ( inputDown == 0.0f && inputUp == 0.0f && inputLeft == 0.0f &&
             inputRight == 0.0f ) {
            if ( bMouseMoved && bMouseInRect ) {
                MapSpotRecord *pHoveredRecord = nullptr;
                const bool bHovered = gpMngSpots->findRecordUnderCursor(
                    &mouse, &pHoveredRecord );
                const InputChannel *pButton =
                    gpDevice->getMouseState().pChannelButton2;
                if ( !bHovered && ( pButton == nullptr ||
                                    ( pButton->Flags & 0x10 ) == 0 ) ) {
                    enterFreeMode();
                } else if ( pHoveredRecord != nullptr &&
                            gpMngSpots->getSelectedRecord() !=
                                pHoveredRecord ) {
                    if ( gMapActiveFilterMode != kMapFilterFreeMode ) {
                        enterFreeMode();
                    }
                    leaveFreeMode( pHoveredRecord );
                }
            }
        } else {
            enterFreeMode();
        }
    }

    if ( pCameraGPS == nullptr ) {
        return;
    }

    if ( gMapActiveFilterMode == kMapFilterFreeMode &&
         !pCameraGPS->isInTransition() &&
         ( gpMapView == nullptr || !gpMapView->isMoving() ) ) {
        if ( gpConfig->NbMaxPlayers < 1 ) {
            return;
        }

        if ( inputDown > 0.0f ) {
            cursorY = kCursorPadSpeedY * inputDown * deltaTime + cursorY;
        }
        if ( inputUp > 0.0f ) {
            cursorY = cursorY - kCursorPadSpeedY * inputUp * deltaTime;
        }
        if ( inputLeft > 0.0f ) {
            cursorX = cursorX - kCursorPadSpeedX * inputLeft * deltaTime;
        }
        if ( inputRight > 0.0f ) {
            cursorX = kCursorPadSpeedX * inputRight * deltaTime + cursorX;
        }
        if ( bMouseInRect ) {
            cursorX = mouse.x();
            cursorY = mouse.y();
        }

        float panX = 0.0f;
        if ( rect.X < cursorX ) {
            if ( rect.Width + rect.X <= cursorX ) {
                panX = inputRight;
            }
        } else {
            panX = 0.0f - inputLeft;
        }
        float panZ = 0.0f;
        if ( rect.Y < cursorY ) {
            if ( rect.Height + rect.Y <= cursorY ) {
                panZ = inputDown;
            }
        } else {
            panZ = 0.0f - inputUp;
        }
        if ( gpMapView != nullptr ) {
            gpMapView->InputX = panX;
            gpMapView->InputZ = panZ;
        }

        cursorX = std::max( cursorX, rect.X );
        cursorY = std::max( cursorY, rect.Y );
        cursorX = std::min( cursorX, rect.Width + rect.X );
        cursorY = std::min( cursorY, rect.Height + rect.Y );
        const float cursor[2] = { cursorX, cursorY };
        SetMapClipPosition( "/curseur", cursor, nullptr, nullptr );

        const Eigen::Vector2f cursor2D( cursorX, cursorY );
        MapSpotRecord *pHoveredRecord = nullptr;
        if ( gpMngSpots->findRecordUnderCursor( &cursor2D, &pHoveredRecord ) ) {
            leaveFreeMode( pHoveredRecord );
        }
        return;
    }

    if ( !pCameraGPS->isInTransition() ) {
        return;
    }

    if ( zoomCursorX > 0.0f || zoomCursorY > 0.0f ) {
        const float progress = pCameraGPS->getTransitionProgress();
        const float remaining = 1.0f - progress;
        const float focus = progress * kMapFocusDefault;
        cursorX = remaining * zoomCursorX + focus;
        cursorY = zoomCursorY * remaining + focus;
    } else if ( zoomOutCursorPosition.x() == 0.0f &&
                zoomOutCursorPosition.y() == 0.0f &&
                zoomOutCursorPosition.z() == 0.0f ) {
        cursorX = kMapFocusDefault;
        cursorY = kMapFocusDefault;
    } else {
        float x = 0.0f;
        float y = 0.0f;
        if ( ProjectToMapScreen( zoomOutCursorPosition, pCameraGPS, x, y ) ) {
            cursorX = x;
            cursorY = y;
        }
    }
    const float cursor[2] = { cursorX, cursorY };
    SetMapClipPosition( "/curseur", cursor, nullptr, nullptr );
}

void MapCursor::zoomIn( FlashPlayer *pPlayer ) {
    // FUN_00741a50
    CameraGPS *pCameraGPS = FindGPSCamera();

    Eigen::Vector4f cursorPosition = Eigen::Vector4f::Zero();
    if ( gMapActiveFilterMode == kMapFilterFreeMode ) {
        ScreenToWorldGround( Eigen::Vector2f( cursorX, 1.0f - cursorY ),
                             cursorPosition );
    }
    gpMapView->bEnabled = true;

    if ( pCameraGPS == nullptr ) {
        return;
    }

    if ( pCameraGPS->zoomIn() ) {
        if ( gMapActiveFilterMode == kMapFilterFreeMode ) {
            if ( gpMngSpots->isInsideMapRect( cursorPosition ) ) {
                gpMngMapViewer->setFocus( cursorPosition );
                pCameraGPS->setFocus( cursorPosition );
                gpMngSpots->setFocus( cursorPosition );
                zoomCursorX = cursorX;
                zoomCursorY = cursorY;
            } else {
                zoomCursorX = kNoZoomCursor;
                zoomCursorY = kNoZoomCursor;
            }
            zoomOutCursorPosition = cursorPosition;
        } else {
            const MapSpotRecord *pSelectedRecord =
                gpMngSpots->getSelectedRecord();
            if ( pSelectedRecord != nullptr ) {
                gpMngMapViewer->setFocus( pSelectedRecord->Position );
                pCameraGPS->setFocus( pSelectedRecord->Position );
                gpMngSpots->setFocus( pSelectedRecord->Position );
                zoomOutCursorPosition = pSelectedRecord->Position;
            }
        }
    }

    pCameraGPS->updateFlashZoom( pPlayer );
}

void MapCursor::zoomOut( FlashPlayer *pPlayer ) {
    // FUN_007419c0
    CameraGPS *pCameraGPS = FindGPSCamera();
    if ( pCameraGPS == nullptr ) {
        return;
    }

    if ( pCameraGPS->zoomOut() ) {
        if ( gMapActiveFilterMode == kMapFilterFreeMode ) {
            ScreenToWorldGround( Eigen::Vector2f( cursorX, 1.0f - cursorY ),
                                 zoomOutCursorPosition );
        }
        zoomCursorX = kNoZoomCursor;
        zoomCursorY = kNoZoomCursor;
    }

    pCameraGPS->updateFlashZoom( pPlayer );
}

void MapCursor::enterFreeMode() {
    // FUN_00741c20
    if ( gMapActiveFilterMode != kMapFilterFreeMode ) {
        gMinimapFilterMode = gMapActiveFilterMode;
    }
    gMapActiveFilterMode = kMapFilterFreeMode;

    MapSpotRecord *pSelectedRecord = gpMngSpots->getSelectedRecord();
    if ( pSelectedRecord == nullptr ||
         !gpMngSpots->isCursorOverRecord(
             pSelectedRecord, Eigen::Vector2f( cursorX, cursorY ) ) ) {
        if ( pSelectedRecord == nullptr ) {
            cursorX = kMapFocusDefault;
            cursorY = kMapFocusDefault;
        } else {
            ProjectToMapScreen( pSelectedRecord->Position, nullptr, cursorX,
                                cursorY );
        }
    }
    gpMngSpots->refreshCursorMode();
}

void MapCursor::leaveFreeMode( MapSpotRecord *pRecord ) {
    // FUN_00741ce0
    gpMngSpots->setWasFreeMode( gMapActiveFilterMode == kMapFilterFreeMode );
    gMapActiveFilterMode = gMinimapFilterMode;
    gpMngSpots->setSelectedRecord( pRecord );
    gpMngSpots->refreshSelectionCart();
    gpMngSpots->refreshCursorMode();
}
