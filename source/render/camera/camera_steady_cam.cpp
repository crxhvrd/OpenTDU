#include "shared.h"
#include "camera_steady_cam.h"

#include "config/gs_config.h"
#include "core/maths_helper.h"
#include "flash/gs_flash.h"
#include "flash/movie_player.h"
#include "render/camera/gs_camera.h"
#include "world/mng_world.h"

static constexpr float kDefaultDistance = 4.0f;        // DAT_00f418d4
static constexpr float kDefaultSensitivity = 0.1f;     // FLOAT_00f41668
static constexpr float kDefaultMouseFov = 60.0f;       // FLOAT_00f41744
static constexpr float kDefaultAutoTiltFactor = 90.0f; // FLOAT_00f4180c

static constexpr float kAngleXFromMatrix = 0.63661975f; // DAT_00f41a30

static constexpr float kTargetDistance = 10.0f;

static constexpr float kMouseFovMin = 0.01f; // DAT_00f416d4
static constexpr float kMouseFovMax = 85.0f; // DAT_00f41f28

static constexpr float kFarPlane = 40000.0f; // DAT_00f41c78

static constexpr float kNearPlaneRefFov = 60.0f; // DAT_00fab404
static constexpr float kNearPlaneLowFov = 45.0f; // DAT_00fab400
static constexpr float kNearPlaneSpan = 0.9f;    // DAT_00f416a0
static constexpr float kNearPlaneBase = 0.1f;    // FLOAT_00f41668

static constexpr int32_t kMoviePauseBit = 0x400;

CameraSteadyCam::CameraSteadyCam()
    : CameraGame(),
      pBody( nullptr ),
      rootPosition( Eigen::Vector4f::Zero() ),
      rootTarget( Eigen::Vector4f::Zero() ),
      mousePort( 0xffffffff ),
      distance( kDefaultDistance ),
      angleX( 0.0f ),
      angleY( 0.0f ),
      rootAngleX( 0.0f ),
      rootAngleY( 0.0f ),
      sensitivity( kDefaultSensitivity ),
      stepRoll( 0 ),
      mouseFov( kDefaultMouseFov ),
      speedForward( kOne ),
      autoTilt( 0 ),
      autoTiltFactor( kDefaultAutoTiltFactor ),
      forceCarBehind( 0 ),
      modeFPS( 0 ),
      modeGravity( 0 ),
      followCar( 0 ),
      carIndex( 0 ),
      modePad( 0 ),
      modeCrouch( 0 ),
      modeRun( 0 ),
      bInvertMouse( gpConfig != nullptr && gpConfig->bInvertMouse != 0 ),
      bCollision( false ),
      bBlockPosition( false ) {
    // FUN_008ca3f0
}

CameraSteadyCam::~CameraSteadyCam() {}

void CameraSteadyCam::initialize() {
    // FUN_008ccbf0
    if ( gpMngWorld != nullptr ) {
        gpMngWorld->setDisableStreamingOnCar( true );
    }

    MoviePlayer *pMoviePlayer = gpFlash->getMoviePlayer( 0, 0 );
    if ( pMoviePlayer != nullptr ) {
        pMoviePlayer->IsMoviePaused |= kMoviePauseBit;
    }

    OTDU_UNIMPLEMENTED; // TODO: AI (culling config flag at +0x1c0)

    angleX = worldToCam.row( 1 )[2] * kAngleXFromMatrix;
    angleY = atan2f( worldToCam.row( 2 )[0], worldToCam.row( 2 )[2] );

    rootAngleX = 0.0f;
    rootAngleY = 0.0f;

    OTDU_UNIMPLEMENTED; // TODO: DEVICE (exclusive mouse capture)
}

bool CameraSteadyCam::tick( float deltaTime, bool param_1, bool param_2 ) {
    // FUN_008ca620
    if ( bBlockPosition && !param_2 ) {
        return true;
    }

    if ( !bBlockPosition ) {
        OTDU_UNIMPLEMENTED;
        return true;
    }

    Matrix4x4f_t rotation = MakeRotationMatX( rootAngleX + angleX );
    MatrixRotateY( rotation, rootAngleY + angleY );

    const Eigen::Vector4f forward = TDUMat4x4MulVec(
        rotation, Eigen::Vector4f( 0.0f, 0.0f, kTargetDistance, 0.0f ) );

    rootTarget = rootPosition + forward;
    rootTarget.w() = kOne;

    upVector = kWorldUpVector;

    setTarget( rootTarget );
    setPosition( rootPosition );

    if ( kMouseFovMax < mouseFov ) {
        mouseFov = kMouseFovMax;
    }
    if ( mouseFov < kMouseFovMin ) {
        mouseFov = kMouseFovMin;
    }
    setFOV( mouseFov * kDegToRad );

    updateMatrix( worldPosition.head<3>(), upVector.head<3>(),
                  worldTarget.head<3>() );

    float nearPlaneValue =
        ( ( mouseFov - kNearPlaneRefFov ) * kNearPlaneSpan ) /
            ( kNearPlaneLowFov - kNearPlaneRefFov ) +
        kNearPlaneBase;
    if ( kOne <= nearPlaneValue ) {
        nearPlaneValue = kOne;
    } else if ( nearPlaneValue <= kNearPlaneBase ) {
        nearPlaneValue = kNearPlaneBase;
    }

    nearPlane = nearPlaneValue;
    farPlane = kFarPlane;

    gpCamera->setFarPlane( kFarPlane );
    gpCamera->setNearPlane( nearPlaneValue );
    gpCamera->setEnableDepthAware( true );
    gpCamera->setInteriorFarPlane( interiorFarPlane );
    gpCamera->setInteriorNearPlane( interiorNearPlane );
    gpCamera->setEnableDepthAwareInterior( true );

    return true;
}

void CameraSteadyCam::destroy() {
    // FUN_008ccb90
    if ( gpMngWorld != nullptr ) {
        gpMngWorld->setDisableStreamingOnCar( false );
    }

    MoviePlayer *pMoviePlayer = gpFlash->getMoviePlayer( 0, 0 );
    if ( pMoviePlayer != nullptr ) {
        pMoviePlayer->IsMoviePaused &= ~kMoviePauseBit;
    }

    OTDU_UNIMPLEMENTED; // TODO: AI (culling config flag at +0x1c0)
    OTDU_UNIMPLEMENTED; // TODO: DEVICE (release the exclusive mouse capture)
}
