#include "shared.h"
#include "camera_gme.h"

#include "config/gs_config.h"
#include "core/maths_helper.h"
#include "game/instant_challenge/gm_instant_challenge.h"
#include "game/scenaric/entity/entity_check_point.h"
#include "game/scenaric/gs_scenaric.h"
#include "render/camera/gs_camera.h"
#include "render/mng_shadow.h"
#include "world/gs_world.h"
#include "world/gs_world_streaming.h"
#include "world/mng_world.h"
#include "world/streaming_manager.h"

static float gGMEMaxDistance = 50.0f;     // DAT_00f7c7d4
static float gGMEDefaultDistance = 50.0f; // DAT_00f7c7d8
static float gGMENearPlane = 100.0f;      // DAT_00f7c7e0
static float gGMEFarPlane = 10000.0f;     // DAT_00f7c7e4

static constexpr float kGMEDefaultPitch = 0.09817477f;     // DAT_00f41c88
static constexpr float kGMENearPlaneDistanceRatio = 0.07f; // DAT_00fab338
static constexpr float kGMEMaxPitch = 1.0471976f;          // DAT_00f417dc
static constexpr float kGMEPitchSpeed = 0.5f;
static constexpr float kGMEAutoPitchSpeed = 0.15f;           // DAT_00f41af8
static constexpr float kGMEYawSpeed = 1.5707964f;            // DAT_00f7c7dc
static constexpr float kGMEPanAcceleration = 0.6f;           // DAT_00f7c7b8
static constexpr float kGMEPanMaxSpeed = 0.55f;              // DAT_00f7c7c0
static constexpr float kGMEPanDecelerationRatio = 5.0f;      // DAT_00f7c7c4
static constexpr float kGMEZoomAcceleration = 0.68f;         // DAT_00f7c7c8
static constexpr float kGMEZoomMaxSpeed = 0.7f;              // DAT_00f7c7cc
static constexpr float kGMEZoomDecelerationRatio = 5.0f;     // DAT_00f7c7d0
static constexpr float kGMEAutoZoomSpeedRatio = 1.5f;        // DAT_00f416b8
static constexpr float kGMERibbonHeightMin = 2.5f;           // DAT_00fab334
static constexpr float kGMERibbonHeightMax = 9.5f;           // DAT_00fab330
static constexpr float kGMEMaxStartDistanceSq = 28590408.0f; // DAT_00f41e34
static constexpr float kGMEMapCellSize = 768.0f;
static constexpr float kGMEIslandTargetCellRatioA = 5.5f; // DAT_00f41f44
static constexpr float kGMEIslandTargetCellRatioB = 2.0f; // DAT_00f416b0
static constexpr float kGMEIslandTargetZ = 26000.0f;
static constexpr float kGMEIslandPitch = 0.024543693f;   // DAT_00f41f40
static constexpr float kGMEIslandDistance = 105000.0f;   // DAT_00f41f3c
static constexpr float kGMEIslandFarPlane = 2200000.0f;  // DAT_00f41f38
static constexpr float kGMEIslandRibbonHeight = 500.0f;  // DAT_00f417f4
static constexpr float kGMEOverviewDistanceRatio = 0.7f; // DAT_00f41828
static constexpr float kGMEOverviewFarPlane = 95000.0f;  // DAT_00f41f4c
static constexpr float kGMEOverviewRibbonHeight = 35.0f; // DAT_00f41f48
static constexpr float kGMEEditNearPlane = 100.0f;       // DAT_00f41708
static constexpr float kGMEEditViewHeight = 560.0f;      // DAT_00f41f54
static constexpr float kGMEEditFarPlane = 15000.0f;      // DAT_00f41f50
static constexpr float kTwoPi =
    static_cast<float>( EIGEN_TWO_PI ); // DAT_00f416f8

static GMInstantChallenge *GetActiveEditor() {
    return static_cast<GMInstantChallenge *>( gpActiveGameMode );
}

static bool IsCameraInputAllowed() {
    return !gpScenaric->isInstantChallenge() ||
           GetActiveEditor()->isLocalPlayerRouteOwner();
}

static float IntegrateAxisSpeed( float speed,
                                 const float decreaseInput,
                                 const float increaseInput,
                                 const float acceleration,
                                 const float deceleration,
                                 const float maxSpeed ) {
    if ( decreaseInput > 0.0f ) {
        speed -=
            decreaseInput * ( ( speed <= 0.0f ) ? acceleration : deceleration );
    } else if ( increaseInput > 0.0f ) {
        speed +=
            increaseInput * ( ( speed < 0.0f ) ? deceleration : acceleration );
    } else if ( speed > 0.0f ) {
        speed = std::max( speed - deceleration, 0.0f );
    } else if ( speed < 0.0f ) {
        speed = std::min( speed + deceleration, 0.0f );
    }

    if ( speed < -maxSpeed ) {
        speed = -maxSpeed;
    }
    if ( maxSpeed < speed ) {
        speed = maxSpeed;
    }
    return speed;
}

CameraGME::CameraGME()
    : CameraGame(),
      direction( Eigen::Vector4f::Zero() ),
      zoomSpeed( 0.0f ),
      distance( gGMEDefaultDistance ),
      pitch( kGMEDefaultPitch ),
      yaw( 0.0f ),
      panSpeedX( 0.0f ),
      panSpeedZ( 0.0f ),
      field_0x198( 0.0f ),
      field_0x19c( 0.0f ),
      bAutoZoom( false ),
      bField0x1a1( false ),
      autoPitchTimer( 0.0f ),
      gmeMode( 0 ),
      pGameModeEditor( nullptr ) {
    // FUN_008dae50
}

CameraGME::~CameraGME() {}

void CameraGME::initialize() {
    // FUN_008dad40
    CameraGame::initialize();

    bField0x1a1 = false;
    if ( gpMngWorld != nullptr ) {
        gpMngWorld->setDisableStreamingOnCar( true );
    }
    if ( gpMngShadow != nullptr ) {
        gpMngShadow->setCastersEnabled( false );
    }
    distance = gGMEDefaultDistance;
    bAutoZoom = false;
}

bool CameraGME::tick( float deltaTime, bool param_1, bool param_2 ) {
    // FUN_008db4f0
    CameraGame::tick( deltaTime, param_1, param_2 );

    direction = Eigen::Vector4f( 0.0f, -1.0f, 0.0f, 0.0f );
    Matrix4x4f_t rotationX = MakeRotationMatX( pitch );
    Matrix4x4f_t rotationY = MakeRotationMatY( yaw );
    direction = TDUMat4x4MulVec( rotationX, direction );
    direction = TDUMat4x4MulVec( rotationY, direction );

    if ( gmeMode == 1 ) {
        updatePan( deltaTime );
    } else if ( gmeMode == 2 ) {
        gGMENearPlane = distance * kGMENearPlaneDistanceRatio;
        updatePan( deltaTime );
        updateZoom( deltaTime );

        float groundHeight = SampleHeightmapSurface( worldTarget, nullptr );
        worldTarget.y() = std::max( groundHeight, 0.0f );
        setTarget( worldTarget );

        if ( pGameModeEditor != nullptr ) {
            const float ribbonHeight =
                ( distance - gGMEDefaultDistance ) *
                    ( kGMERibbonHeightMax - kGMERibbonHeightMin ) /
                    ( gGMEMaxDistance - gGMEDefaultDistance ) +
                kGMERibbonHeightMin;
            pGameModeEditor->mngTracRoad.setHeight( std::clamp(
                ribbonHeight, kGMERibbonHeightMin, kGMERibbonHeightMax ) );
        }
    }

    worldPosition = worldTarget - direction * distance;
    setPosition( worldPosition );
    setTarget( worldTarget );
    updateMatrix( worldPosition.head<3>(), upVector.head<3>(),
                  worldTarget.head<3>() );
    setPosition( worldPosition );
    setTarget( worldTarget );

    nearPlane = gGMENearPlane;
    farPlane = gGMEFarPlane;
    gpCamera->setNearPlane( nearPlane );
    gpCamera->setFarPlane( farPlane );
    gpCamera->setEnableDepthAware( true );
    gpCamera->setInteriorNearPlane( interiorNearPlane );
    gpCamera->setInteriorFarPlane( interiorFarPlane );
    gpCamera->setEnableDepthAwareInterior( true );

    if ( gpMngCursor != nullptr && gpMngCursor->bLockWorldPosition ) {
        GetScreenCoords( gpMngCursor->screenPosition,
                         gpMngCursor->worldPosition );
        gpMngCursor->screenPosition.y() =
            1.0f - gpMngCursor->screenPosition.y();
        gpMngCursor->bLockWorldPosition = false;
    }

    return true;
}

void CameraGME::destroy() {
    // FUN_008db830
    StreamingManager *pStreaming =
        ( gpWorld != nullptr ) ? gpWorld->getStreamingManager() : nullptr;
    if ( pStreaming != nullptr ) {
        SectorManager &sectors = pStreaming->sectorManager;
        AreaGrid &areas = pStreaming->areaGrid;

        sectors.bDrawSector = false;
        sectors.bNeedStreamUpdate = true;
        areas.bNeedStreamUpdate = true;
        areas.bDrawArea = false;
        pStreaming->setActiveRenderLayers( 1 );
        sectors.bStreamSector = true;
        gpWorldStreaming->updateStreaming();
        areas.bStreamArea = true;
    }
    if ( gpMngWorld != nullptr ) {
        gpMngWorld->setDisableStreamingOnCar( false );
    }
    if ( gpMngShadow != nullptr ) {
        gpMngShadow->setCastersEnabled( false );
    }
}

void CameraGME::setMode( const int32_t mode ) {
    // FUN_008db8b0
    gmeMode = mode;

    StreamingManager *pStreaming = gpWorld->getStreamingManager();
    SectorManager &sectors = pStreaming->sectorManager;
    AreaGrid &areas = pStreaming->areaGrid;
    const Eigen::Vector4f topDownDirection =
        Eigen::Vector4f( 0.0f, -1.0f, -0.3f, 0.0f ).normalized();

    if ( mode == 0 ) {
        const float islandWidth =
            static_cast<float>( static_cast<int32_t>( sectors.numSectors ) ) *
            kGMEMapCellSize;
        worldTarget =
            Eigen::Vector4f( islandWidth * kGMEIslandTargetCellRatioA +
                                 islandWidth * kGMEIslandTargetCellRatioB,
                             0.0f, kGMEIslandTargetZ, 1.0f );
        setTarget( worldTarget );
        pitch = kGMEIslandPitch;
        yaw = 0.0f;
        distance = kGMEIslandDistance;

        sectors.bDrawSector = true;
        areas.bDrawArea = true;
        pStreaming->setActiveRenderLayers( 1 );
        sectors.bStreamSector = false;
        areas.bStreamArea = false;
        gGMENearPlane = kGMEEditNearPlane;
        gGMEFarPlane = kGMEIslandFarPlane;
        if ( pGameModeEditor != nullptr ) {
            pGameModeEditor->mngTracRoad.setHeight( kGMEIslandRibbonHeight );
        }
    } else if ( mode == 1 ) {
        distance = static_cast<float>(
                       static_cast<int32_t>( sectors.numSubSectors ) ) *
                   kGMEMapCellSize / std::tan( getFOV() * 0.5f ) *
                   kGMEOverviewDistanceRatio;
        direction = topDownDirection;

        sectors.bDrawSector = true;
        areas.bNeedStreamUpdate = true;
        areas.bDrawArea = false;
        pStreaming->setActiveRenderLayers( 1 );
        sectors.bStreamSector = false;
        areas.bStreamArea = true;
        gGMENearPlane = kGMEEditNearPlane;
        gGMEFarPlane = kGMEOverviewFarPlane;
        if ( pGameModeEditor != nullptr ) {
            pGameModeEditor->mngTracRoad.setHeight( kGMEOverviewRibbonHeight );
        }
    } else if ( mode == 2 ) {
        panSpeedX = 0.0f;
        panSpeedZ = 0.0f;
        field_0x198 = 0.0f;
        field_0x19c = 0.0f;
        zoomSpeed = 0.0f;
        gGMEMaxDistance = kGMEEditViewHeight / std::tan( getFOV() * 0.5f );
        distance = gGMEMaxDistance;
        direction = topDownDirection;

        sectors.bDrawSector = false;
        sectors.bNeedStreamUpdate = true;
        areas.bNeedStreamUpdate = true;
        areas.bDrawArea = false;
        pStreaming->setActiveRenderLayers( 0 );
        sectors.bStreamSector = true;
        gpWorldStreaming->updateStreaming();
        gGMEFarPlane = kGMEEditFarPlane;
        areas.bStreamArea = true;
    }
}

void CameraGME::updatePan( float deltaTime ) {
    // FUN_008daed0
    if ( gpMngCursor == nullptr || pGameModeEditor == nullptr ) {
        return;
    }
    if ( !IsCameraInputAllowed() ) {
        return;
    }

    const float acceleration = distance * kGMEPanAcceleration * deltaTime;
    const float deceleration = kGMEPanDecelerationRatio * acceleration;
    const float maxSpeed = distance * kGMEPanMaxSpeed;

    auto edgeInput = [&]( const bool bCursorEdge, const int32_t index ) {
        const float input = pGameModeEditor->bCursorAtEdge
                                ? 1.0f
                                : pGameModeEditor->cameraPanInputs[index];
        return ( bCursorEdge ? 1.0f : 0.0f ) * input;
    };

    panSpeedX =
        IntegrateAxisSpeed( panSpeedX, edgeInput( gpMngCursor->bAtLeftEdge, 0 ),
                            edgeInput( gpMngCursor->bAtRightEdge, 1 ),
                            acceleration, deceleration, maxSpeed );
    panSpeedZ = IntegrateAxisSpeed( panSpeedZ,
                                    edgeInput( gpMngCursor->bAtBottomEdge, 2 ),
                                    edgeInput( gpMngCursor->bAtTopEdge, 3 ),
                                    acceleration, deceleration, maxSpeed );

    const Eigen::Vector4f previousTarget = worldTarget;
    const Matrix4x4f_t rotationY = MakeRotationMatY( yaw );
    const Eigen::Vector4f right =
        TDUMat4x4MulVec( rotationY, Eigen::Vector4f( 1.0f, 0.0f, 0.0f, 0.0f ) );
    const Eigen::Vector4f forward =
        TDUMat4x4MulVec( rotationY, Eigen::Vector4f( 0.0f, 0.0f, 1.0f, 0.0f ) );
    worldTarget = worldTarget + right * ( panSpeedX * deltaTime ) +
                  forward * ( deltaTime * panSpeedZ );
    setTarget( worldTarget );

    if ( gpScenaric->isInstantChallenge() &&
         pGameModeEditor->getNumWaypoints() > 0 ) {
        EntityCheckPoint *pStart = pGameModeEditor->findStartCheckpoint();
        if ( pStart != nullptr ) {
            const Eigen::Vector4f startPosition =
                pStart->getModelMatrix().row( 3 ).transpose();
            Eigen::Vector4f previousOffset = startPosition - previousTarget;
            previousOffset.y() = 0.0f;
            Eigen::Vector4f offset = startPosition - worldTarget;
            offset.y() = 0.0f;
            const float distanceSq = offset.squaredNorm();
            if ( previousOffset.squaredNorm() < distanceSq &&
                 kGMEMaxStartDistanceSq < distanceSq ) {
                setTarget( previousTarget );
            }
        }
    }

    if ( !bAutoZoom ) {
        pitch += ( pGameModeEditor->cameraPitchInputs[1] -
                   pGameModeEditor->cameraPitchInputs[0] ) *
                 deltaTime * kGMEPitchSpeed;
    } else if ( autoPitchTimer <= 0.0f ) {
        autoPitchTimer = 0.0f;
        pitch -= deltaTime * kGMEAutoPitchSpeed;
    } else {
        autoPitchTimer -= deltaTime;
    }
    if ( pitch < kGMEDefaultPitch ) {
        pitch = kGMEDefaultPitch;
    } else if ( kGMEMaxPitch < pitch ) {
        pitch = kGMEMaxPitch;
    }

    if ( pGameModeEditor->cameraYawInputs[0] > 0.0f ) {
        yaw -= kGMEYawSpeed * deltaTime;
        if ( yaw < -kTwoPi ) {
            yaw += kTwoPi;
        }
    } else if ( pGameModeEditor->cameraYawInputs[1] > 0.0f ) {
        yaw += kGMEYawSpeed * deltaTime;
        if ( kTwoPi < yaw ) {
            yaw -= kTwoPi;
        }
    }
}

void CameraGME::updateZoom( float deltaTime ) {
    // FUN_008dab10
    if ( gpConfig->NbMaxPlayers < 1 || pGameModeEditor == nullptr ) {
        return;
    }
    if ( !IsCameraInputAllowed() ) {
        return;
    }

    const float acceleration = distance * kGMEZoomAcceleration * deltaTime;
    const float deceleration = kGMEZoomDecelerationRatio * acceleration;
    const float maxSpeed = distance * kGMEZoomMaxSpeed;

    const bool bZoomOut =
        pGameModeEditor->bKeyZoomOut || ( bAutoZoom && autoPitchTimer == 0.0f );
    const bool bZoomIn = pGameModeEditor->bKeyZoomIn && !bAutoZoom;
    zoomSpeed = IntegrateAxisSpeed( zoomSpeed, bZoomIn ? 1.0f : 0.0f,
                                    bZoomOut ? 1.0f : 0.0f, acceleration,
                                    deceleration, maxSpeed );
    if ( bAutoZoom ) {
        zoomSpeed *= kGMEAutoZoomSpeedRatio;
    }

    distance += zoomSpeed * deltaTime;
    if ( gGMEMaxDistance < distance ) {
        distance = gGMEMaxDistance;
        bAutoZoom = false;
    }
    if ( distance < gGMEDefaultDistance ) {
        distance = gGMEDefaultDistance;
    }
}
