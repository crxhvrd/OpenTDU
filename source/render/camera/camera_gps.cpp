#include "shared.h"
#include "camera_gps.h"

#include "config/gs_config.h"
#include "core/maths_helper.h"
#include "flash/implementation/flash_player.h"
#include "physics/gs_physics.h"
#include "render/hiearchy_node.h"
#include "system/ed_sys.h"
#include "world/gs_mng_resources.h"
#include "world/gs_world.h"
#include "world/streaming_manager.h"

#include "gs_camera.h"

bool gDisplayDemoZones = false; // DAT_010e7d20

static bool gbFreezeGPSTransition = false;    // DAT_016a2c84
static float gGPSMediumViewHeightBias = 0.0f; // DAT_016a2c88

static float gGPSSpaceTransitionDuration = 1.5f; // DAT_00fab2d4
static float gGPSZoomTransitionDuration = 1.25f; // DAT_00fab2d8
static float gGPSCloseZoomFactor = 0.1f;         // DAT_00fab2dc
static float gGPSMediumZoomFactor = 1.0f;        // DAT_00fab2e0
static float gGPSFarZoomFactor = 2.0f;           // DAT_00fab2e4
static float gGPSIslandZoomFactor = 1.0f;        // DAT_00fab2e8
static float gGPSSpaceToFixedSplit = 0.5f;       // DAT_00fab2ec
static float gGPSCloseViewScale = 1.0f;          // DAT_00fab2f0
static float gGPSCloseViewPitch = 25.0f;         // DAT_00fab2f4
static float gGPSCloseViewHeight = 85.0f;        // DAT_00fab2f8
static float gGPSMediumViewScale = 0.5f;         // DAT_00fab2fc
static float gGPSMediumViewPitch = 25.0f;        // DAT_00fab300
static float gGPSFarViewScale = 0.5f;            // DAT_00fab304
static float gGPSFarViewPitch = 25.0f;           // DAT_00fab308
static float gGPSFarViewHeight = 10000.0f;       // DAT_00fab30c

struct GPSIslandViewSetup {
    float Pitch;
    float Height;
    float OffsetZ;
    float OffsetX;
};

static GPSIslandViewSetup gGPSIslandView169 = { 15.0f, 25000.0f, -1000.0f,
                                                13000.0f }; // DAT_00fab310
static GPSIslandViewSetup gGPSIslandView43 = { 15.0f, 60000.0f, -3000.0f,
                                               13000.0f }; // DAT_00fab320

static constexpr float kGPSDefaultFOV = 0.7853982f;     // DAT_00f41cb0
static constexpr float kGPSDemoZonesPitch = 0.1f;       // FLOAT_00f41668
static constexpr float kGPSInteriorNearPlane = 0.1f;    // FLOAT_00f41668
static constexpr float kGPSInteriorFarPlane = 10000.0f; // DAT_00f416c4
static constexpr float kGPSSpaceViewAltitude = 2.0e7f;  // DAT_00f41e38
static constexpr float kGPSNearPlaneRatio = 0.01f;      // DAT_00f416d4
static constexpr float kGPSFarPlaneRatio = 4.0f;        // DAT_00f418d4
static constexpr float kGPSCellSize = 768.0f;           // DAT_00f416e4
static constexpr int32_t kGPSCellSizeInt = 0x300;
static constexpr float kGPSQuaternionEpsilon = 1e-6f; // DAT_00f41888

static constexpr const char *kGPSZoomNames[CameraGPS::GZL_Count] = {
    "Fixed View",  // DAT_00f145c8
    "Space View",  // DAT_00f145bc
    "Island View", // DAT_00f145b0
    "Far View",    // DAT_00f145a4
    "Medium View", // DAT_00f14598
    "Close View",  // DAT_00f1458c
};

CameraGPS::CameraGPS()
    : CameraGame(),
      mutex(),
      pRequestedEndCallback( nullptr ),
      pRequestedEndArg0( nullptr ),
      pRequestedEndArg1( nullptr ),
      requestedFromZoom( GZL_Medium ),
      requestedToZoom( GZL_Medium ),
      fixedPose(),
      focus( Eigen::Vector4f( 0.0f, 0.0f, 0.0f, 1.0f ) ),
      previousFocus( Eigen::Vector4f( 0.0f, 0.0f, 0.0f, 1.0f ) ),
      zoom( GZL_Medium ),
      previousZoom( GZL_Medium ),
      yawOffset( 0.0f ),
      pitchOffset( 0.0f ),
      bInTransition( false ),
      transitionProgress( 0.0f ),
      transitionBlend( 0.0f ),
      transitionElapsed( 0.0f ),
      transitionDuration( 1.0f ),
      earthRadius( 0.0f ),
      zoomDistanceToCenter{},
      spaceViewDistanceToCenter( 0.0f ),
      earthMatrix( Matrix4x4f_t::Identity() ),
      invEarthMatrix( Matrix4x4f_t::Identity() ),
      earthCenter( Eigen::Vector4f( 0.0f, 0.0f, 0.0f, 1.0f ) ),
      spaceViewAngle( 0.0f ),
      spaceViewAltitude( kGPSSpaceViewAltitude ),
      pEndCallback( nullptr ),
      pEndArg0( nullptr ),
      pEndArg1( nullptr ),
      pPoseCallback( nullptr ),
      pPoseCallbackUser( nullptr ) {
    // FUN_008dc760
    fixedPose.Position = Eigen::Vector4f( 0.0f, 0.0f, 0.0f, 1.0f );
    fixedPose.Target = Eigen::Vector4f( 0.0f, 0.0f, 0.0f, 1.0f );
    fixedPose.Up = Eigen::Vector4f( 0.0f, 1.0f, 0.0f, 0.0f );
    fixedPose.FOV = kGPSDefaultFOV;
    fixedPose.AspectRatio = 1.0f;
    fixedPose.NearPlane = 0.0f;
    fixedPose.FarPlane = 0.0f;

    reset( true );
}

CameraGPS::~CameraGPS() {}

void CameraGPS::initialize() {
    // FUN_008ddd80
    EarthResources &earth = gpMngResources->getEarthResources();
    HiearchyNode *pIslandNode = earth.getIslandNode();
    HiearchyNode *pEarthNode = earth.getEarthNode();
    if ( pIslandNode != nullptr && pEarthNode != nullptr ) {
        pIslandNode->updateAndPropagateTransformMatrices( false );

        earthMatrix = pEarthNode->TransformMatrix;
        invEarthMatrix = earthMatrix.inverse();
        earthCenter = earthMatrix.row( 3 ).transpose();
        earthRadius = Length3( earthCenter );

        static constexpr int32_t kRadiusZooms[4] = { GZL_Close, GZL_Medium,
                                                     GZL_Far, GZL_Island };
        const Eigen::Vector4f origin( 0.0f, 0.0f, 0.0f, 1.0f );
        for ( int32_t i = 0; i < 4; i++ ) {
            Pose pose;
            computeZoomPose( kRadiusZooms[i], origin, pose );
            zoomDistanceToCenter[i] =
                Length3( TDUMat4x4MulVec( invEarthMatrix, pose.Position ) );
        }
        spaceViewDistanceToCenter = spaceViewAltitude;
    }

    if ( requestedFromZoom != requestedToZoom ) {
        {
            ScopedMutexLock lock( &mutex );
            pEndArg0 = pRequestedEndArg0;
            pEndCallback = pRequestedEndCallback;
            pEndArg1 = pRequestedEndArg1;
        }
        startZoom( requestedFromZoom, requestedToZoom );

        pRequestedEndCallback = nullptr;
        pRequestedEndArg0 = nullptr;
        pRequestedEndArg1 = nullptr;
        requestedFromZoom = GZL_Medium;
        requestedToZoom = GZL_Medium;
    }
}

bool CameraGPS::tick( float deltaTime, bool /*param_1*/, bool /*param_2*/ ) {
    // FUN_008de200
    ScopedMutexLock lock( &mutex );

    Pose pose;
    if ( !bInTransition ) {
        computeZoomPose( zoom, focus, pose );
    } else {
        if ( !gbFreezeGPSTransition ) {
            updateTransition( deltaTime );
        }
        computeTransitionPose( pose );
    }

    if ( pPoseCallback != nullptr ) {
        pPoseCallback( pPoseCallbackUser, pose );
    }

    applyPose( pose );
    return true;
}

void CameraGPS::destroy() {
    // FUN_008dc0a0
    if ( bInTransition ) {
        endTransition();
    }
}

void CameraGPS::reset( bool bResetZoom ) {
    // FUN_008dc440
    if ( bResetZoom ) {
        previousZoom = GZL_Medium;
        zoom = GZL_Medium;
    }

    focus = Eigen::Vector4f( 0.0f, 0.0f, 0.0f, 1.0f );
    yawOffset = 0.0f;
    pitchOffset = 0.0f;

    const float aspectRatio = GetScreenAspectRatio();
    setPosition( Eigen::Vector4f( 0.0f, 0.0f, 0.0f, 1.0f ) );
    setTarget( Eigen::Vector4f( 1.0f, 0.0f, 0.0f, 1.0f ) );
    upVector = Eigen::Vector4f( 0.0f, 1.0f, 0.0f, 0.0f );
    setFOV( kGPSDefaultFOV );
    this->aspectRatio = aspectRatio;
    invAspectRatio = 1.0f / aspectRatio;
    interiorNearPlane = kGPSInteriorNearPlane;
    interiorFarPlane = kGPSInteriorFarPlane;
    updateMatrix( worldPosition.head<3>(), upVector.head<3>(),
                  worldTarget.head<3>() );

    earthMatrix = Matrix4x4f_t::Identity();
    invEarthMatrix = Matrix4x4f_t::Identity();
    earthCenter = Eigen::Vector4f( 0.0f, 0.0f, 0.0f, 1.0f );
    spaceViewAngle = 0.0f;
    spaceViewAltitude = kGPSSpaceViewAltitude;

    bInTransition = false;
    pEndCallback = nullptr;
    pPoseCallback = nullptr;
    transitionProgress = 0.0f;
    transitionBlend = 0.0f;
    transitionElapsed = 0.0f;
    transitionDuration = 1.0f;

    pRequestedEndCallback = nullptr;
    pRequestedEndArg0 = nullptr;
    pRequestedEndArg1 = nullptr;
    requestedFromZoom = GZL_Medium;
    requestedToZoom = GZL_Medium;
}

void CameraGPS::setZoom( const int32_t newZoom, const bool bAnimated ) {
    // FUN_008dd1a0
    startZoom( bAnimated ? zoom : newZoom, newZoom );
}

bool CameraGPS::zoomIn() {
    // FUN_008ddc00
    ScopedMutexLock lock( &mutex );
    if ( bInTransition || zoom < GZL_Space || zoom >= GZL_Close ) {
        return false;
    }

    startZoom( zoom, zoom + 1 );
    beginTransition( GetZoomTransitionDuration( previousZoom, zoom ) );
    return true;
}

bool CameraGPS::zoomOut() {
    // FUN_008ddcc0
    ScopedMutexLock lock( &mutex );
    if ( bInTransition || zoom <= GZL_Space || zoom > GZL_Close ) {
        return false;
    }

    startZoom( zoom, zoom - 1 );
    beginTransition( GetZoomTransitionDuration( previousZoom, zoom ) );
    return true;
}

void CameraGPS::requestZoom( const int32_t fromZoom,
                             const int32_t toZoom,
                             TransitionEndCallback_t pCallback,
                             void *pArg0,
                             void *pArg1 ) {
    // FUN_008dbf00
    requestedFromZoom = fromZoom;
    requestedToZoom = toZoom;
    pRequestedEndCallback = pCallback;
    pRequestedEndArg0 = pArg0;
    pRequestedEndArg1 = pArg1;
}

void CameraGPS::setPoseCallback( PoseCallback_t pCallback, void *pUser ) {
    // FUN_008dbca0
    ScopedMutexLock lock( &mutex );
    pPoseCallbackUser = pUser;
    pPoseCallback = pCallback;
}

void CameraGPS::setTransitionEndCallback( TransitionEndCallback_t pCallback,
                                          void *pArg0,
                                          void *pArg1 ) {
    // FUN_008dbcd0
    ScopedMutexLock lock( &mutex );
    pEndArg0 = pArg0;
    pEndCallback = pCallback;
    pEndArg1 = pArg1;
}

void CameraGPS::setFocus( const Eigen::Vector4f &position ) {
    // FUN_008dbec0
    ScopedMutexLock lock( &mutex );
    focus = position;
}

void CameraGPS::setSpaceViewAngle( const float angle ) {
    // FUN_00432650
    ScopedMutexLock lock( &mutex );
    spaceViewAngle = angle;
}

void CameraGPS::setFixedPose( const Eigen::Vector4f &position,
                              const Eigen::Vector4f &target,
                              const Eigen::Vector4f &up,
                              const float nearPlane,
                              const float farPlane,
                              const float fov,
                              const float aspectRatio ) {
    // FUN_008dbbe0
    ScopedMutexLock lock( &mutex );
    fixedPose.Position = position;
    fixedPose.Target = target;
    fixedPose.Up = up;
    fixedPose.NearPlane = nearPlane;
    fixedPose.FarPlane = farPlane;
    fixedPose.FOV = fov;
    fixedPose.AspectRatio = aspectRatio;
}

float CameraGPS::getZoomFactor() const {
    // FUN_008dbe00
    switch ( zoom ) {
    case GZL_Fixed:
    case GZL_Space:
        return 0.0f;
    case GZL_Island:
        return gGPSIslandZoomFactor;
    case GZL_Far:
        return gGPSFarZoomFactor;
    case GZL_Medium:
        return gGPSMediumZoomFactor;
    case GZL_Close:
        return gGPSCloseZoomFactor;
    default:
        return 1.0f;
    }
}

const char *CameraGPS::getZoomName() const {
    // FUN_008dbe60
    if ( zoom < GZL_Fixed || zoom >= GZL_Count ) {
        return "";
    }
    return kGPSZoomNames[zoom];
}

bool CameraGPS::isSpaceView() const {
    // FUN_008dc040
    if ( bInTransition ) {
        return zoom == GZL_Space || previousZoom == GZL_Space;
    }
    return zoom == GZL_Space;
}

float CameraGPS::getTransitionDuration() const {
    // FUN_008dc070
    return GetZoomTransitionDuration( previousZoom, zoom );
}

float CameraGPS::getDistanceToEarthCenter() {
    // FUN_008dc230
    Eigen::Vector4f position;
    {
        ScopedMutexLock lock( &mutex );
        position = worldPosition;
    }

    if ( zoom != GZL_Space &&
         !( previousZoom == GZL_Space && bInTransition ) ) {
        position.x() = 0.0f;
        position.z() = 0.0f;
    }

    const float distance =
        Length3( TDUMat4x4MulVec( invEarthMatrix, position ) );
    return ( distance > earthRadius ) ? distance : earthRadius;
}

float CameraGPS::getAltitude() {
    // FUN_008dc420
    const float altitude = getDistanceToEarthCenter() - earthRadius;
    return ( altitude > 0.0f ) ? altitude : 0.0f;
}

void CameraGPS::updateFlashZoom( FlashPlayer *pPlayer ) const {
    // FUN_008dc790
    switch ( zoom ) {
    case GZL_Space:
        pPlayer->setVariableValue( "/:WhatZoom", 5 );
        break;
    case GZL_Island:
        pPlayer->setVariableValue( "/:WhatZoom", 4 );
        break;
    case GZL_Far:
        pPlayer->setVariableValue( "/:WhatZoom", 3 );
        break;
    case GZL_Medium:
        pPlayer->setVariableValue( "/:WhatZoom", 2 );
        break;
    case GZL_Close:
        pPlayer->setVariableValue( "/:WhatZoom", 1 );
        break;
    default:
        break;
    }
}

float CameraGPS::GetZoomTransitionDuration( const int32_t fromZoom,
                                            const int32_t toZoom ) {
    // FUN_008dbdd0
    if ( fromZoom == GZL_Space || toZoom == GZL_Space ) {
        return gGPSSpaceTransitionDuration;
    }
    return gGPSZoomTransitionDuration;
}

void CameraGPS::computeZoomPose( const int32_t zoomLevel,
                                 const Eigen::Vector4f &focusPosition,
                                 Pose &outPose ) const {
    // FUN_008dc800
    StreamingManager *pStreaming = gpWorld->getStreamingManager();
    const int32_t numSectors =
        static_cast<int32_t>( pStreaming->sectorManager.numSectors );
    const int32_t numSubSectors =
        static_cast<int32_t>( pStreaming->sectorManager.numSubSectors );
    const float worldSizeX = static_cast<float>(
        static_cast<int32_t>( pStreaming->areaGrid.numAreas ) * numSectors *
        kGPSCellSizeInt );
    const float worldSizeZ = static_cast<float>(
        static_cast<int32_t>( pStreaming->areaGrid.numSubAreas ) *
        numSubSectors * kGPSCellSizeInt );
    const float subSectorSize =
        static_cast<float>( numSubSectors * kGPSCellSizeInt );

    outPose.Position = Eigen::Vector4f( 0.0f, 0.0f, 0.0f, 1.0f );
    outPose.Target = Eigen::Vector4f( 0.0f, 0.0f, 0.0f, 1.0f );
    outPose.Up = Eigen::Vector4f( 0.0f, 0.0f, -1.0f, 0.0f );
    outPose.FOV = kGPSDefaultFOV;
    outPose.AspectRatio = GetScreenAspectRatio();
    outPose.NearPlane = 0.0f;
    outPose.FarPlane = 0.0f;

    switch ( zoomLevel ) {
    case GZL_Fixed:
        outPose = fixedPose;
        break;
    case GZL_Space: {
        const Eigen::Vector4f orbitPosition(
            0.0f, spaceViewAltitude + fabsf( earthCenter.y() ), 0.0f, 1.0f );
        const Eigen::Vector4f rotatedPosition = TDUMat4x4MulVec(
            MakeRotationMatZ( spaceViewAngle * kDegToRad ), orbitPosition );
        outPose.Position = TDUMat4x4MulVec( earthMatrix, rotatedPosition );
        outPose.Target = earthCenter;
        outPose.Up = NormalizeVector4( TDUMat4x4MulVec(
            earthMatrix, Eigen::Vector4f( 0.0f, 0.0f, -1.0f, 0.0f ) ) );
        break;
    }
    case GZL_Island: {
        const GPSIslandViewSetup &setup =
            gpConfig->b169Ratio ? gGPSIslandView169 : gGPSIslandView43;
        const float halfWorldSizeZ = worldSizeZ * 0.5f;
        const float tanHalfFOV = tanf( outPose.FOV * 0.5f );

        outPose.Target =
            Eigen::Vector4f( worldSizeX * 0.5f + setup.OffsetX, 0.0f,
                             halfWorldSizeZ + setup.OffsetZ, 1.0f );
        const Eigen::Vector4f offset(
            0.0f, halfWorldSizeZ / tanHalfFOV + setup.Height, 0.0f, 0.0f );

        const Matrix4x4f_t pitch = MakeRotationMatX( setup.Pitch * kDegToRad );
        outPose.Position = TDUMat4x4MulVec(
            pitch, Eigen::Vector4f( outPose.Target + offset ) );
        outPose.Up = NormalizeVector4( TDUMat4x4MulVec( pitch, outPose.Up ) );
        break;
    }
    case GZL_Far:
    case GZL_Medium:
    case GZL_Close: {
        const float tanHalfFOV = tanf( outPose.FOV * 0.5f );
        outPose.Target = focusPosition;

        float height = 0.0f;
        float pitchInDegrees = 0.0f;
        if ( zoomLevel == GZL_Far ) {
            height =
                ( ( gGPSFarViewScale * subSectorSize ) / tanHalfFOV ) * 0.5f +
                gGPSFarViewHeight;
            pitchInDegrees = gGPSFarViewPitch;
        } else if ( zoomLevel == GZL_Medium ) {
            height = ( ( gGPSMediumViewScale * subSectorSize ) / tanHalfFOV ) *
                         0.5f +
                     gGPSMediumViewHeightBias;
            pitchInDegrees =
                gDisplayDemoZones ? kGPSDemoZonesPitch : gGPSMediumViewPitch;
        } else {
            height =
                ( ( gGPSCloseViewScale * kGPSCellSize ) / tanHalfFOV ) * 0.5f +
                gGPSCloseViewHeight;
            pitchInDegrees =
                gDisplayDemoZones ? kGPSDemoZonesPitch : gGPSCloseViewPitch;
        }

        outPose.Position =
            TDUMat4x4MulVec( MakeRotationMatX( pitchInDegrees * kDegToRad ),
                             Eigen::Vector4f( 0.0f, height, 0.0f, 0.0f ) ) +
            outPose.Target;
        outPose.Position.w() = 1.0f;
        break;
    }
    default:
        break;
    }

    if ( ( pitchOffset != 0.0f || yawOffset != 0.0f ) &&
         zoomLevel != GZL_Island && zoomLevel != GZL_Space ) {
        const float distance = ( outPose.Target - outPose.Position ).norm();

        Matrix4x4f_t rotation = MakeRotationMatX( pitchOffset );
        MatrixRotateY( rotation, yawOffset );

        const Eigen::Vector4f direction = TDUMat4x4MulVec(
            rotation, Eigen::Vector4f( 0.0f, -1.0f, 0.0f, 0.0f ) );
        outPose.Up = TDUMat4x4MulVec( rotation, outPose.Up );
        outPose.Position = outPose.Target - direction * distance;
    }

    if ( zoomLevel != GZL_Fixed ) {
        const float distance = ( outPose.Target - outPose.Position ).norm();
        outPose.NearPlane = distance * kGPSNearPlaneRatio;
        outPose.FarPlane = distance * kGPSFarPlaneRatio;
    }
}

void CameraGPS::applyPose( const Pose &pose ) {
    // FUN_008dc330
    setPosition( pose.Position );
    setTarget( pose.Target );
    upVector = pose.Up;
    setFOV( pose.FOV );
    aspectRatio = pose.AspectRatio;
    invAspectRatio = 1.0f / pose.AspectRatio;
    nearPlane = pose.NearPlane;
    farPlane = pose.FarPlane;

    CameraGame::tick( 0.0f, false, false );
}

void CameraGPS::startZoom( const int32_t fromZoom, const int32_t toZoom ) {
    // FUN_008dd0c0
    ScopedMutexLock lock( &mutex );
    if ( bInTransition ) {
        return;
    }

    previousZoom = fromZoom;
    zoom = toZoom;
    if ( fromZoom != toZoom ) {
        beginTransition( GetZoomTransitionDuration( fromZoom, toZoom ) );
    }

    Pose pose;
    computeZoomPose( fromZoom, focus, pose );
    applyPose( pose );
}

void CameraGPS::beginTransition( const float duration ) {
    // FUN_008dbd70
    previousFocus = focus;
    transitionProgress = 0.0f;
    transitionBlend = 0.0f;
    transitionElapsed = 0.0f;
    transitionDuration = duration;
    bInTransition = true;
}

void CameraGPS::endTransition() {
    // FUN_008dbd10
    ScopedMutexLock lock( &mutex );
    bInTransition = false;
    transitionProgress = 1.0f;
    transitionBlend = 1.0f;

    if ( pEndCallback != nullptr ) {
        pEndCallback( pEndArg0, pEndArg1 );
        pEndCallback = nullptr;
    }
}

void CameraGPS::updateTransition( float deltaTime ) {
    // FUN_008dc0c0
    if ( deltaTime <= 0.0f && previousZoom == GZL_Fixed &&
         !gpCamera->isPaused() ) {
        deltaTime = gPhysicsRefreshRate;
    }

    transitionElapsed += deltaTime;
    transitionProgress =
        Clamp( transitionElapsed / transitionDuration, 0.0f, 1.0f );

    if ( previousZoom == GZL_Fixed ) {
        const float p = powf( transitionProgress, 4.0f );
        transitionBlend =
            ( cosf( ( 1.0f - p ) * kPI + p * kTwoPI ) + 1.0f ) * 0.5f;
    } else if ( zoom == GZL_Fixed ) {
        transitionBlend = CosineEaseLerp( transitionProgress, 0.0f, 1.0f );
    } else {
        transitionBlend =
            Clamp( sinf( transitionProgress * kHalfPI ), 0.0f, 1.0f );
    }

    if ( transitionBlend >= 1.0f ) {
        endTransition();
    }
}

void CameraGPS::computeTransitionPose( Pose &outPose ) const {
    // FUN_008ddb50
    if ( previousZoom == GZL_Space ) {
        if ( zoom != GZL_Fixed ) {
            computeOrbitalTransitionPose( previousZoom, zoom, transitionBlend,
                                          outPose );
        } else if ( transitionProgress <= gGPSSpaceToFixedSplit ) {
            computeOrbitalTransitionPose(
                previousZoom, GZL_Island,
                transitionProgress / gGPSSpaceToFixedSplit, outPose );
        } else {
            computeLinearTransitionPose( GZL_Island, zoom, transitionBlend,
                                         outPose );
        }
    } else if ( zoom == GZL_Space ) {
        computeOrbitalTransitionPose( previousZoom, zoom, transitionBlend,
                                      outPose );
    } else {
        computeLinearTransitionPose( previousZoom, zoom, transitionBlend,
                                     outPose );
    }
}

void CameraGPS::computeLinearTransitionPose( const int32_t fromZoom,
                                             const int32_t toZoom,
                                             const float blend,
                                             Pose &outPose ) const {
    // FUN_008dd980
    Pose from;
    Pose to;
    computeZoomPose( fromZoom, previousFocus, from );
    computeZoomPose( toZoom, focus, to );

    const float invBlend = 1.0f - blend;
    outPose.FOV = invBlend * from.FOV + to.FOV * blend;
    outPose.AspectRatio = invBlend * from.AspectRatio + to.AspectRatio * blend;
    outPose.Position = from.Position * invBlend + to.Position * blend;
    outPose.Position.w() = 1.0f;
    outPose.Target = from.Target * invBlend + to.Target * blend;
    outPose.Target.w() = 1.0f;
    outPose.Up = NormalizeVector4( from.Up * invBlend + to.Up * blend );
    outPose.NearPlane = invBlend * from.NearPlane + to.NearPlane * blend;
    outPose.FarPlane = invBlend * from.FarPlane + to.FarPlane * blend;
}

void CameraGPS::computeOrbitalTransitionPose( const int32_t fromZoom,
                                              const int32_t toZoom,
                                              const float blend,
                                              Pose &outPose ) const {
    // FUN_008dd1d0
    Pose from;
    Pose to;
    computeZoomPose( fromZoom, previousFocus, from );
    computeZoomPose( toZoom, focus, to );

    Eigen::Vector4f fromLocal =
        TDUMat4x4MulVec( invEarthMatrix, from.Position );
    fromLocal.w() = 0.0f;
    Eigen::Vector4f toLocal = TDUMat4x4MulVec( invEarthMatrix, to.Position );
    toLocal.w() = 0.0f;

    const Eigen::Vector4f fromDirection = NormalizeVector4( fromLocal );
    const Eigen::Vector4f toDirection = NormalizeVector4( toLocal );

    // Longitude first...
    Eigen::Vector4f fromPlanar = fromDirection;
    fromPlanar.z() = 0.0f;
    fromPlanar = NormalizeVector4( fromPlanar );
    Eigen::Vector4f toPlanar = toDirection;
    toPlanar.z() = 0.0f;
    toPlanar = NormalizeVector4( toPlanar );

    const Eigen::Vector4f longitudeAxis =
        NormalizeVector4( Vector4DCross( fromPlanar, toPlanar ) );
    const float longitudeAngle = acosf( toPlanar.dot( fromPlanar ) );
    const float signedLongitude =
        ( longitudeAxis.z() < 0.0f ) ? -longitudeAngle : longitudeAngle;
    const Eigen::Vector4f rotatedDirection = TDUMat4x4MulVec(
        MakeRotationMatZ( signedLongitude * blend ), fromDirection );

    // ...then latitude
    const Eigen::Vector4f latitudeAxis =
        NormalizeVector4( Vector4DCross( rotatedDirection, toDirection ) );
    const float latitudeAngle = acosf( toDirection.dot( rotatedDirection ) );
    const Matrix4x4f_t latitudeRotation = BuildRotationMatrix(
        MakeAxisAngleQuaternion( latitudeAxis, -latitudeAngle * blend ) );
    const Eigen::Vector4f direction =
        TDUMat4x4MulVec( latitudeRotation, rotatedDirection );

    const float invBlend = 1.0f - blend;
    const float distanceToCenter =
        Length3( toLocal ) * blend + invBlend * Length3( fromLocal );

    Eigen::Vector4f localPosition = direction * distanceToCenter;
    localPosition.w() = 1.0f;
    outPose.Position = TDUMat4x4MulVec( earthMatrix, localPosition );

    outPose.Target = to.Target * blend + invBlend * from.Target;
    outPose.Target.w() = 1.0f;

    Eigen::Vector4f up = to.Up * blend + invBlend * from.Up;
    up.w() = 0.0f;
    outPose.Up = NormalizeVector4( up );

    outPose.FOV = to.FOV * blend + from.FOV * invBlend;
    outPose.AspectRatio = to.AspectRatio * blend + from.AspectRatio * invBlend;
    outPose.NearPlane = to.NearPlane * blend + from.NearPlane * invBlend;
    outPose.FarPlane = to.FarPlane * blend + from.FarPlane * invBlend;
}
