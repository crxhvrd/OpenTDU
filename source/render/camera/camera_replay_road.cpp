#include "shared.h"
#include "camera_replay_road.h"

#include "ai/ai_road_query.h"
#include "camera_vehicle.h"
#include "core/maths_helper.h"
#include "game/vehicle/car_state.h"
#include "gs_camera.h"
#include "physics/vehicle/physics_vehicle.h"
#include "world/gs_world_streaming.h"
#include "world/streaming_manager.h"

int32_t gRPLRatioCamOutCamOnCar = 0;                          // DAT_0147a2e0
int32_t gRPLRatioCamCarOutInGame = 0;                         // DAT_0147a2e4
float gRPLMinTimeToSwap = 0.0f;                               // DAT_0147a2ec
float gRPLMaxTimeToSwap = 0.0f;                               // DAT_0147a2f0
float gRPLSwapLength = 0.0f;                                  // DAT_0147a2e8
float gRPLMinHeight = 0.0f;                                   // DAT_0147a2f4
float gRPLMaxHeight = 0.0f;                                   // DAT_0147a2f8
float gRPLMaxHeightModeLarge = 0.0f;                          // DAT_0147a2fc
float gRPLMaxWidthFromCenterOfRoad = 0.0f;                    // DAT_0147a300
float gRPLSpeedRatio = 0.0f;                                  // DAT_0147a304
Eigen::Vector4f gRPLCarWheelOffset = Eigen::Vector4f::Zero(); // DAT_0147a310
Eigen::Vector4f gRPLCarOffsetPos = Eigen::Vector4f::Zero();   // DAT_0147a320
Eigen::Vector4f gRPLCarOffsetTRG = Eigen::Vector4f::Zero();   // DAT_0147a330
float gRPLFOVNormalMin = 0.0f;
float gRPLFOVNormalMax = 0.0f;
float gRPLFOVZoomK1 = 0.0f;
float gRPLFOVZoomK2 = 0.0f;
float gRPLFOVZoomMin = 0.0f;
float gRPLFOVZoomMax = 0.0f;
float gRPLFOVLargeMin = 0.0f;
float gRPLFOVLargeMax = 0.0f;
float gRPLFOVCar = 0.0f;
float gRPLDOFCamNormalMin = 0.0f;
float gRPLDOFCamNormalMax = 0.0f;
float gRPLDOFCamZoomMin = 0.0f;
float gRPLDOFCamZoomMax = 0.0f;
float gRPLDOFCamLargeMin = 0.0f;
float gRPLDOFCamLargeMax = 0.0f;
float gRPLDOFCamFixedMin = 0.0f;
float gRPLDOFCamFixedMax = 0.0f;
Eigen::Vector4f gRPLSpringMinPos = Eigen::Vector4f::Zero();     // DAT_0147a390
Eigen::Vector4f gRPLSpringMaxPos = Eigen::Vector4f::Zero();     // DAT_0147a3a0
Eigen::Vector4f gRPLSpringRate = Eigen::Vector4f::Zero();       // DAT_0147a3b0
Eigen::Vector4f gRPLSpringDamping = Eigen::Vector4f::Zero();    // DAT_0147a3c0
Eigen::Vector4f gRPLSpringForceScale = Eigen::Vector4f::Zero(); // DAT_0147a3d0
Eigen::Vector4f gRPLSpringForceDeadZone =
    Eigen::Vector4f::Zero(); // DAT_0147a3e0
Eigen::Vector4f gRPLSpringForceClamping =
    Eigen::Vector4f::Zero();                 // DAT_0147a3f0
float gRPLSpringTimescale = 0.0f;            // DAT_0147a400
float gRPLHelicoMaxAccelLongitudinal = 0.0f; // DAT_0147a424
float gRPLHelicoMaxAccelLateral = 0.0f;      // DAT_0147a428
float gRPLHelicoMaxSpeedLongitudinal = 0.0f; // DAT_0147a42c
float gRPLHelicoMaxSpeedLateral = 0.0f;
float gRPLDampingMotion = 0.0f;                          // DAT_0147a438
float gRPLHelicoHeight = 0.0f;                           // DAT_0147a420
float gRPLHelicoFOV = 0.0f;                              // DAT_0147a434
Eigen::Vector4f gRPLHelicoAim = Eigen::Vector4f::Zero(); // DAT_0147a410

static float gDOFFocale = 10.0f; // DAT_00fab3e8

static constexpr float kRoadSideDistanceRatio = 0.9f;    // DAT_00f416a0
static constexpr float kRandWidthScale = 3.051804e-05f;  // DAT_00f42140
static constexpr float kMinWidthFromCenterOfRoad = 5.0f; // DAT_00f41750
static constexpr float kFollowSideOffset = 20.0f;
static constexpr float kFollowBackOffset = 10.0f;
static constexpr float kFollowRoadOffset = 10.0f;
static constexpr float kMinDirectionSpeed = 1.0f;     // FLOAT_00f4165c
static constexpr float kFollowHeightRate = 5.000001f; // _DAT_00f4213c
static constexpr float kHelicoYawRate = 1.0e-05f;     // _DAT_00fab3f0
static constexpr float kHelicoBlendRate = 0.4999995f; // DAT_00f42138
static constexpr float kHelicoAimKeep = 0.995f;       // _DAT_00fab3ec
static constexpr float kHelicoAimRate = 100.0f;       // DAT_00f41708
static constexpr float kHelicoFOVRandomScale = 0.75f; // DAT_00f416a4
static constexpr float kHelicoFOVRandomBias = 0.25f;  // DAT_00f416dc
static constexpr float kDefaultNearPlane = 1.0f;      // FLOAT_00f4165c
static constexpr float kDefaultFarPlane = 40000.0f;   // DAT_00f41c78
static constexpr float kCarNearPlane = 0.1f;          // FLOAT_00f41668
static constexpr float kCarFarPlane = 1000.0f;        // DAT_00f41718
static constexpr float kDOFFixedDistance = 20.0f;     // DAT_00f03ee0
static constexpr float kDOFFocaleRate = 19.999998f;   // _DAT_00f41bec

// Matches the Windows CRT rand() range (RAND_MAX = 0x7fff).
static inline int32_t Rand15() {
    return static_cast<int32_t>( rand() & 0x7fff );
}

static inline Eigen::Vector4f GetShiftWorldPosition() {
    if ( gpWorldStreaming->isEnabled() ) {
        return gpWorldStreaming->getPosition();
    }
    return Eigen::Vector4f::Zero();
}

static inline float
BlendClamped( const float from, const float to, const float ratio ) {
    const float value = ( to - from ) * ratio + from;
    const float hi = Max( from, to );
    const float lo = Min( from, to );
    return Max( Min( value, hi ), lo );
}

static inline Eigen::Vector4f
GetVelocityDirection( PhysicsVehicle *pVehicle,
                      const Matrix4x4f_t &carMatrix ) {
    Eigen::Vector4f direction = pVehicle->getChassisVelocity();
    if ( direction.norm() < kMinDirectionSpeed ) {
        return carMatrix.row( 2 ).transpose();
    }
    return direction.normalized();
}

static inline float ApplySpringForceLimits( float force,
                                            const float deadZone,
                                            const float clamping ) {
    if ( std::abs( force ) < deadZone ) {
        force = 0.0f;
    }
    if ( clamping < std::abs( force ) ) {
        force = clamping * ( force <= 0.0f ? kMinusOne : kOne );
    }
    return force;
}

CameraReplayRoad::CameraReplayRoad()
    : CameraGame(),
      shot( eReplayRoadShot::RRS_Zoom ),
      carShot( eReplayRoadCarShot::RRCS_Wheel ),
      bSwapRequested( false ),
      bResetShot( false ),
      shotPosition( Eigen::Vector4f::Zero() ),
      shotTarget( Eigen::Vector4f::Zero() ),
      shotVelocity( Eigen::Vector4f::Zero() ),
      shotSpeed( 0.0f ),
      roadSide( 0.0f ),
      bDepthOfField( false ),
      springPosition( Eigen::Vector4f::Zero() ),
      springVelocity( Eigen::Vector4f::Zero() ),
      springUnused( Eigen::Vector4f::Zero() ),
      helicoYaw( 0.0f ),
      helicoForward( Eigen::Vector4f::Zero() ),
      helicoSide( Eigen::Vector4f::Zero() ),
      helicoFOVTarget( 0.0f ),
      helicoFOV( 0.0f ),
      helicoFOVTimer( 0.0f ),
      helicoOrbitAngle( 0.0f ),
      helicoOrbitAngleTarget( 0.0f ),
      helicoOrbitRadius( 0.0f ),
      helicoOrbitRadiusTarget( 0.0f ),
      helicoOrbitTimer( 0.0f ),
      helicoAimAngle( 0.0f ),
      helicoAimAngleTarget( 0.0f ),
      helicoAimRadius( 0.0f ),
      helicoAimRadiusTarget( 0.0f ),
      helicoAimTimer( 0.0f ) {
    // FUN_008ce430
}

CameraReplayRoad::~CameraReplayRoad() {}

void CameraReplayRoad::initialize() {
    // FUN_008ce390
    shot = eReplayRoadShot::RRS_Zoom;
    carShot = eReplayRoadCarShot::RRCS_Wheel;
    springPosition = Eigen::Vector4f::Zero();
    springVelocity = Eigen::Vector4f::Zero();
    springUnused = Eigen::Vector4f::Zero();
}

bool CameraReplayRoad::tick( float deltaTime, bool param_1, bool param_2 ) {
    // FUN_008ce470
    CarState *pTarget = gpCamera->getBoundCarState();
    if ( pTarget == nullptr ) {
        return false;
    }
    PhysicsVehicle *pVehicle = pTarget->getPhysicsVehicle();
    if ( pVehicle == nullptr ) {
        return false;
    }

    const Matrix4x4f_t carMatrix = pVehicle->getChassisShiftedMatrix();
    const Eigen::Vector4f carPosition = carMatrix.row( 3 ).transpose();
    const float carDistance =
        ( carPosition - worldPositionWithUnknownOffset ).norm();

    switch ( shot ) {
    case eReplayRoadShot::RRS_Fixed: {
        const Eigen::Vector4f viewDirection =
            worldTargetWithUnknownOffset - worldPositionWithUnknownOffset;
        const float facing = ( carPosition - worldPositionWithUnknownOffset )
                                 .dot( viewDirection );
        bSwapRequested = ( facing < 0.0f || gRPLSwapLength < carDistance );
        break;
    }
    case eReplayRoadShot::RRS_Helico:
    case eReplayRoadShot::RRS_Car:
        bSwapRequested = false;
        break;
    default:
        bSwapRequested = ( gRPLSwapLength < carDistance );
        break;
    }

    const bool bShotReset = bResetShot;
    if ( bResetShot ) {
        resetShot( carMatrix, pVehicle );
    }

    Eigen::Vector4f position = shotPosition;
    bResetShot = false;

    switch ( shot ) {
    case eReplayRoadShot::RRS_Follow:
        updateFollowShot( deltaTime, carDistance );
        position = shotPosition;
        break;
    case eReplayRoadShot::RRS_Helico:
        updateHelicoShot( carMatrix, deltaTime );
        position = shotPosition;
        break;
    case eReplayRoadShot::RRS_Car:
        if ( isCarShotSpringed() ) {
            updateCarSpring( pVehicle, deltaTime );
            position = TDUMat4x4MulVec(
                carMatrix, Eigen::Vector4f( shotPosition + springPosition ) );
        }
        break;
    default:
        break;
    }
    setPositionOffseted( position );

    Eigen::Vector4f target = computeTarget( carMatrix, deltaTime );
    if ( ( target - position ).norm() == 0.0f ) {
        target.x() = kOne;
    }
    setTargetOffseted( target );
    upVector = kWorldUpVector;

    float shotNearPlane = kDefaultNearPlane;
    float shotFarPlane = kDefaultFarPlane;
    const float fov = computeFOV( carMatrix, carDistance, deltaTime,
                                  shotNearPlane, shotFarPlane );
    setFOV( fov * kDegToRad );

    nearPlane = shotNearPlane;
    farPlane = shotFarPlane;
    gpCamera->setNearPlane( shotNearPlane );
    gpCamera->setFarPlane( shotFarPlane );
    gpCamera->setEnableDepthAware( true );
    gpCamera->setInteriorFarPlane( interiorFarPlane );
    gpCamera->setInteriorNearPlane( interiorNearPlane );
    gpCamera->setEnableDepthAwareInterior( true );

    updateMatrix( worldPosition.head<3>(), kWorldUpVector.head<3>(),
                  worldTarget.head<3>() );

    if ( bDepthOfField ) {
        updateDepthOfField( deltaTime );
    }

    return bShotReset;
}

void CameraReplayRoad::destroy() {
    // FUN_008ce380
    bDepthOfField = false;
}

void CameraReplayRoad::resetShot( const Matrix4x4f_t &carMatrix,
                                  PhysicsVehicle *pVehicle ) {
    shotTarget = carMatrix.row( 3 ).transpose();
    setVelocity( DAT_00fac350 );

    switch ( shot ) {
    case eReplayRoadShot::RRS_Follow:
        resetFollowShot( carMatrix, pVehicle );
        break;
    case eReplayRoadShot::RRS_Helico:
        resetHelicoShot( carMatrix, pVehicle );
        break;
    case eReplayRoadShot::RRS_Car:
        resetCarShot( pVehicle );
        break;
    default:
        resetRoadSideShot( carMatrix );
        break;
    }
}

void CameraReplayRoad::resetFollowShot( const Matrix4x4f_t &carMatrix,
                                        PhysicsVehicle *pVehicle ) {
    const float side = ( kHalf < CameraRandom01() ) ? kMinusOne : kOne;
    shotPosition = carMatrix.row( 3 ).transpose() +
                   carMatrix.row( 2 ).transpose() * kFollowBackOffset +
                   carMatrix.row( 0 ).transpose() * side * kFollowSideOffset;

    const Eigen::Vector4f shift = GetShiftWorldPosition();
    AiRoadLocation location;
    ProjectOnRoad( shotPosition + shift, nullptr, nullptr, false, &location );

    const Eigen::Vector4f roadPosition =
        location.Frame.row( 3 ).transpose() - shift;
    const Eigen::Vector4f roadRight = location.Frame.row( 0 ).transpose();
    roadSide = ( 0.0f < ( shotPosition - roadPosition ).dot( roadRight ) )
                   ? kOne
                   : kMinusOne;
    shotPosition = roadPosition + roadRight * roadSide * kFollowRoadOffset;

    const float speed = pVehicle->getChassisVelocity().norm();
    const Eigen::Vector4f direction =
        GetVelocityDirection( pVehicle, carMatrix );
    shotVelocity =
        direction * CameraRandomGaussian( speed, speed * gRPLSpeedRatio );
    shotSpeed = shotVelocity.norm();
}

void CameraReplayRoad::resetHelicoShot( const Matrix4x4f_t &carMatrix,
                                        PhysicsVehicle *pVehicle ) {
    helicoYaw = std::atan2( carMatrix( 2, 0 ), carMatrix( 2, 2 ) );
    helicoFOVTimer = CameraRandomGaussian( 3.0f, 1.0f );
    helicoOrbitTimer = 0.0f;
    helicoFOVTarget = gRPLHelicoFOV;
    helicoFOV = helicoFOVTarget;

    const float aimRange = gRPLHelicoAim.y() - gRPLHelicoAim.x();
    helicoOrbitAngleTarget = CameraRandom01() * kTwoPI;
    helicoOrbitAngle = helicoOrbitAngleTarget;
    helicoOrbitRadiusTarget = CameraRandom01() * aimRange + gRPLHelicoAim.x();
    helicoOrbitRadius = helicoOrbitRadiusTarget;
    helicoAimAngle = CameraRandom01() * kTwoPI;
    helicoAimRadius = CameraRandom01() * aimRange + gRPLHelicoAim.x();

    const Eigen::Vector4f orbit(
        std::cos( helicoOrbitAngle ) * helicoOrbitRadius, 0.0f,
        std::sin( helicoOrbitAngle ) * helicoOrbitRadius, 0.0f );
    shotPosition = carMatrix.row( 3 ).transpose() +
                   TDUMat4x4MulVec( carMatrix, orbit ) +
                   Eigen::Vector4f( 0.0f, gRPLHelicoHeight, 0.0f, 0.0f );
    shotPosition.w() = kOne;

    const float speed = pVehicle->getChassisVelocity().norm();
    const Eigen::Vector4f direction =
        GetVelocityDirection( pVehicle, carMatrix );
    shotVelocity =
        direction * CameraRandomGaussian( speed, speed * gRPLSpeedRatio );
}

void CameraReplayRoad::resetCarShot( PhysicsVehicle *pVehicle ) {
    const float wheelSideScale = pVehicle->isBike() ? kTwo : kOne;

    switch ( carShot ) {
    case eReplayRoadCarShot::RRCS_Wheel: {
        const int32_t wheelIndex =
            Rand15() % static_cast<int32_t>( pVehicle->getNumWheels() );
        const PhysicsWheelState &wheel = pVehicle->getWheelState( wheelIndex );

        const PhysicsVehicle::WheelReadout readout =
            pVehicle->getWheelReadout( wheelIndex );
        shotTarget = Eigen::Vector4f( readout.LocalPosition.x(),
                                      readout.LocalPosition.y(),
                                      readout.LocalPosition.z(), kOne );
        shotTarget.y() += gRPLCarWheelOffset.w();

        float x = gRPLCarWheelOffset.x() * wheelSideScale;
        float z = gRPLCarWheelOffset.z();
        if ( wheel.SideFlag == 1 ) {
            x = 0.0f - x;
        }
        if ( wheel.AxleIndex == 1 ) {
            z = 0.0f - gRPLCarWheelOffset.z();
        }
        if ( 50 < Rand15() % 100 ) {
            z = 0.0f - z;
        }

        shotPosition =
            Eigen::Vector4f( x, gRPLCarWheelOffset.w() + gRPLCarWheelOffset.y(),
                             z, 0.0f ) +
            shotTarget;
        break;
    }
    case eReplayRoadCarShot::RRCS_Offset: {
        float x = gRPLCarOffsetPos.x();
        float z = gRPLCarOffsetPos.z();
        if ( 50 < Rand15() % 100 ) {
            x = 0.0f - x;
        }
        if ( 50 < Rand15() % 100 ) {
            z = 0.0f - z;
        }
        shotPosition = Eigen::Vector4f( x, gRPLCarOffsetPos.y(), z, kOne );
        shotTarget = gRPLCarOffsetTRG;
        break;
    }
    case eReplayRoadCarShot::RRCS_Fixed:
        shotPosition = Eigen::Vector4f( 2.0f, 1.0f, 0.0f, 1.0f );
        shotTarget = Eigen::Vector4f( 0.0f, 0.0f, 0.0f, 1.0f );
        break;
    default:
        break;
    }
}

void CameraReplayRoad::resetRoadSideShot( const Matrix4x4f_t &carMatrix ) {
    Eigen::Vector4f forward = carMatrix.row( 2 ).transpose();
    forward.y() = 0.0f;

    const Eigen::Vector4f shift = GetShiftWorldPosition();
    shotPosition = carMatrix.row( 3 ).transpose() +
                   forward * ( gRPLSwapLength * kRoadSideDistanceRatio ) +
                   shift;

    const float heightRatio = kOne - std::cos( CameraRandom01() * kHalfPI );
    float height = BlendClamped( gRPLMinHeight, gRPLMaxHeight, heightRatio );
    if ( shot == eReplayRoadShot::RRS_Large ) {
        height =
            BlendClamped( gRPLMaxHeight, gRPLMaxHeightModeLarge, heightRatio );
    }

    AiRoadLocation location;
    ProjectOnRoad( shotPosition, nullptr, nullptr, false, &location );
    shotPosition = location.Frame.row( 3 ).transpose();

    const float widthRatio =
        static_cast<float>( Rand15() & 0xffff ) * kRandWidthScale - kOne;
    float width = 0.0f;
    if ( 0.0f < widthRatio ) {
        width = BlendClamped( kMinWidthFromCenterOfRoad,
                              gRPLMaxWidthFromCenterOfRoad, widthRatio );
    } else {
        width =
            BlendClamped( -kMinWidthFromCenterOfRoad,
                          0.0f - gRPLMaxWidthFromCenterOfRoad, -widthRatio );
    }

    shotPosition += location.Frame.row( 0 ).transpose() * width;
    shotPosition.y() += height;
    shotPosition -= shift;
}

void CameraReplayRoad::updateFollowShot( const float deltaTime,
                                         const float carDistance ) {
    const Eigen::Vector4f shift = GetShiftWorldPosition();
    AiRoadLocation location;
    ProjectOnRoad( shotPosition + shift, nullptr, nullptr, false, &location );

    const Eigen::Vector4f roadForward = location.Frame.row( 2 ).transpose();
    const float speed =
        ( 0.0f < shotVelocity.dot( roadForward ) ) ? shotSpeed : -shotSpeed;
    shotVelocity = roadForward * speed;
    shotPosition += shotVelocity * deltaTime;

    const float groundHeight =
        SampleHeightmapSurface( shotPosition + shift, nullptr );
    const float height = BlendClamped( gRPLMaxHeight, gRPLMaxHeightModeLarge,
                                       carDistance / gRPLSwapLength );
    shotPosition.y() += ( ( height + groundHeight ) - shotPosition.y() ) *
                        deltaTime * kFollowHeightRate;
}

void CameraReplayRoad::updateHelicoShot( const Matrix4x4f_t &carMatrix,
                                         const float deltaTime ) {
    helicoForward = Eigen::Vector4f( std::sin( helicoYaw ), 0.0f,
                                     std::cos( helicoYaw ), 0.0f );
    helicoSide = Vector4DCross( kWorldUpVector, helicoForward ).normalized();

    const Eigen::Vector4f orbit(
        std::cos( helicoOrbitAngle ) * helicoOrbitRadius, 0.0f,
        std::sin( helicoOrbitAngle ) * helicoOrbitRadius, 0.0f );
    Eigen::Vector4f aimPoint =
        carMatrix.row( 3 ).transpose() + TDUMat4x4MulVec( carMatrix, orbit );
    const float aimHeight = carMatrix( 3, 1 ) + gRPLHelicoHeight;
    aimPoint.y() = aimHeight;

    Eigen::Vector4f delta = aimPoint - shotPosition;
    delta.y() = 0.0f;
    const float longitudinal =
        Min( delta.dot( helicoForward ), gRPLHelicoMaxAccelLongitudinal );
    const float lateral =
        Min( delta.dot( helicoSide ), gRPLHelicoMaxAccelLateral );

    Eigen::Vector4f acceleration = helicoForward * longitudinal;
    acceleration.y() = aimHeight - shotPosition.y();
    acceleration -= shotVelocity * gRPLDampingMotion;

    helicoYaw += shotVelocity.norm() * kHelicoYawRate * lateral;
    if ( kTwoPI < helicoYaw ) {
        helicoYaw -= kTwoPI;
    }
    if ( helicoYaw < 0.0f ) {
        helicoYaw += kTwoPI;
    }

    shotVelocity += acceleration * deltaTime;
    if ( gRPLHelicoMaxSpeedLongitudinal < shotVelocity.norm() ) {
        shotVelocity =
            shotVelocity.normalized() * gRPLHelicoMaxSpeedLongitudinal;
    }
    shotPosition += shotVelocity * deltaTime;
}

void CameraReplayRoad::updateCarSpring( PhysicsVehicle *pVehicle,
                                        const float deltaTime ) {
    const Eigen::Vector4f accelerationG( pVehicle->getLateralG(),
                                         pVehicle->getVerticalG(),
                                         pVehicle->getLongitudinalG(), 0.0f );
    Eigen::Vector4f force = gRPLSpringForceScale.cwiseProduct( accelerationG );
    force.w() = 0.0f;
    for ( int32_t i = 0; i < 3; i++ ) {
        force[i] = ApplySpringForceLimits( force[i], gRPLSpringForceDeadZone[i],
                                           gRPLSpringForceClamping[i] );
    }

    Eigen::Vector4f acceleration =
        force * gRPLSpringTimescale -
        gRPLSpringDamping.cwiseProduct( springVelocity ) * gRPLSpringTimescale -
        gRPLSpringRate.cwiseProduct( springPosition ) * gRPLSpringTimescale;

    springVelocity.head<3>() += acceleration.head<3>() * deltaTime;
    springPosition.head<3>() += springVelocity.head<3>() * deltaTime;

    for ( int32_t i = 0; i < 3; i++ ) {
        if ( springPosition[i] < gRPLSpringMinPos[i] ) {
            springVelocity[i] = 0.0f;
            springPosition[i] = gRPLSpringMinPos[i];
        }
        if ( gRPLSpringMaxPos[i] < springPosition[i] ) {
            springVelocity[i] = 0.0f;
            springPosition[i] = gRPLSpringMaxPos[i];
        }
    }
}

Eigen::Vector4f CameraReplayRoad::computeTarget( const Matrix4x4f_t &carMatrix,
                                                 const float deltaTime ) {
    const Eigen::Vector4f carPosition = carMatrix.row( 3 ).transpose();
    switch ( shot ) {
    case eReplayRoadShot::RRS_Zoom:
    case eReplayRoadShot::RRS_Large:
    case eReplayRoadShot::RRS_Follow:
        return carPosition;
    case eReplayRoadShot::RRS_Helico: {
        if ( helicoAimTimer < 0.0f ) {
            const float aimMin = gRPLHelicoAim.x() * gRPLHelicoAim.z();
            const float aimMax = gRPLHelicoAim.y() * gRPLHelicoAim.z();
            helicoAimAngleTarget = CameraRandom01() * kTwoPI;
            helicoAimRadiusTarget =
                CameraRandom01() * ( aimMax - aimMin ) + aimMin;
            helicoAimTimer = CameraRandomGaussian( 1.0f, 0.5f );
        }

        const float blend = ( kOne - kHelicoAimKeep ) * deltaTime;
        helicoAimAngle +=
            ( helicoAimAngleTarget - helicoAimAngle ) * blend * kHelicoAimRate;
        helicoAimRadius += ( helicoAimRadiusTarget - helicoAimRadius ) * blend *
                           kHelicoAimRate;
        helicoAimTimer -= deltaTime;
        shotTarget = carPosition;
        return carPosition;
    }
    case eReplayRoadShot::RRS_Car:
        if ( isCarShotSpringed() ) {
            return TDUMat4x4MulVec(
                carMatrix, Eigen::Vector4f( shotTarget + springPosition ) );
        }
        return shotTarget;
    default:
        return shotTarget;
    }
}

float CameraReplayRoad::computeFOV( const Matrix4x4f_t &carMatrix,
                                    const float carDistance,
                                    const float deltaTime,
                                    float &outNearPlane,
                                    float &outFarPlane ) {
    const float distanceRatio = carDistance / gRPLSwapLength;
    float fov =
        BlendClamped( gRPLFOVNormalMin, gRPLFOVNormalMax, distanceRatio );

    switch ( shot ) {
    case eReplayRoadShot::RRS_Zoom: {
        const float distance =
            ( carMatrix.row( 3 ).transpose() - shotPosition ).norm();
        const float zoom =
            ( kHalfPI - std::atan( distance * gRPLFOVZoomK2 ) ) * gRPLFOVZoomK1;
        fov = Min( zoom, gRPLFOVZoomMax );
        if ( fov < gRPLFOVZoomMin ) {
            fov = gRPLFOVZoomMin;
        }
        break;
    }
    case eReplayRoadShot::RRS_Large:
        fov = BlendClamped( gRPLFOVLargeMin, gRPLFOVLargeMax, distanceRatio );
        break;
    case eReplayRoadShot::RRS_Helico: {
        if ( helicoOrbitTimer < 0.0f ) {
            helicoOrbitAngleTarget = CameraRandom01() * kTwoPI;
            helicoOrbitRadiusTarget =
                CameraRandom01() * ( gRPLHelicoAim.y() - gRPLHelicoAim.x() ) +
                gRPLHelicoAim.x();
            helicoOrbitTimer = CameraRandomGaussian( 10.0f, 1.0f );
        }
        if ( helicoFOVTimer < 0.0f ) {
            helicoFOVTarget = ( CameraRandom01() * kHelicoFOVRandomScale +
                                kHelicoFOVRandomBias ) *
                              gRPLHelicoFOV;
            helicoFOVTimer = CameraRandomGaussian( 4.0f, 1.0f );
        }

        helicoOrbitTimer -= deltaTime;
        helicoFOVTimer -= deltaTime;
        helicoOrbitAngle += ( helicoOrbitAngleTarget - helicoOrbitAngle ) *
                            deltaTime * kHelicoBlendRate;
        helicoFOV +=
            ( helicoFOVTarget - helicoFOV ) * deltaTime * kHelicoBlendRate;
        helicoOrbitRadius += ( helicoOrbitRadiusTarget - helicoOrbitRadius ) *
                             deltaTime * kHelicoBlendRate;
        fov = helicoFOV;
        break;
    }
    case eReplayRoadShot::RRS_Car:
        outNearPlane = kCarNearPlane;
        outFarPlane = kCarFarPlane;
        if ( isCarShotSpringed() ) {
            fov = gRPLFOVCar;
        }
        break;
    default:
        break;
    }

    return fov;
}

void CameraReplayRoad::updateDepthOfField( const float deltaTime ) {
    float distance =
        ( worldTargetWithUnknownOffset - worldPositionWithUnknownOffset )
            .norm();

    float focaleRange = 0.0f;
    float focaleStart = 0.0f;
    switch ( shot ) {
    case eReplayRoadShot::RRS_Zoom:
        focaleRange = gRPLDOFCamZoomMax - gRPLDOFCamZoomMin;
        focaleStart = gRPLDOFCamZoomMin;
        break;
    case eReplayRoadShot::RRS_Follow:
        focaleRange = gRPLDOFCamNormalMax - gRPLDOFCamNormalMin;
        focaleStart = gRPLDOFCamNormalMin;
        break;
    case eReplayRoadShot::RRS_Fixed:
        distance = kDOFFixedDistance;
        focaleRange = gRPLDOFCamFixedMax - gRPLDOFCamFixedMin;
        focaleStart = gRPLDOFCamFixedMin;
        break;
    default:
        focaleRange = gRPLDOFCamNormalMin;
        focaleStart = gRPLDOFCamNormalMax - gRPLDOFCamNormalMin;
        break;
    }

    gDOFFocale += ( distance - gDOFFocale ) * deltaTime * kDOFFocaleRate;

    float *pFocaleInfos = getFocaleInfosRW();
    pFocaleInfos[0] = gDOFFocale;
    pFocaleInfos[1] = focaleRange;
    pFocaleInfos[2] = focaleStart;
}
