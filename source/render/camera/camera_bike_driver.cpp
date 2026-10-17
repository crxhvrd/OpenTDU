#include "shared.h"
#include "camera_bike_driver.h"

#include "ai/ai_road_query.h"
#include "core/maths_helper.h"
#include "game/vehicle/car_state.h"
#include "gs_camera.h"
#include "physics/vehicle/physics_vehicle.h"
#include "world/gs_world_streaming.h"

static constexpr float kTargetHeight = 0.5f;
static constexpr float kVelocityLookAhead = 2.0f; // DAT_00fab3fc
static constexpr float kCameraHeight = 5.0f;
static constexpr float kFovDistanceScale = 0.006666667f; // DAT_00f42134
static constexpr float kFovAtanScale = 7.5f;             // DAT_00fab3f4
static constexpr float kFovRangeScale = 0.25f;           // DAT_00fab3f8

static Eigen::Vector4f GetStreamingShift() {
    if ( gpWorldStreaming->isEnabled() ) {
        return gpWorldStreaming->getPosition();
    }
    return Eigen::Vector4f::Zero();
}

CameraBikeDriver::CameraBikeDriver()
    : CameraGame(),
      shiftedRoadPosition( Eigen::Vector4f::Zero() ),
      roadUpVector( kWorldUpVector ),
      bNeedsRoadPosition( false ) {
    // FUN_008ccd20
}

CameraBikeDriver::~CameraBikeDriver() {}

void CameraBikeDriver::initialize() {
    // FUN_008ccd00
    bNeedsRoadPosition = true;
}

bool CameraBikeDriver::tick( float deltaTime, bool param_1, bool param_2 ) {
    // FUN_008ccd40
    CarState *pCar = gpCamera->getBoundCarState();
    if ( pCar == nullptr ) {
        return param_1;
    }

    PhysicsVehicle *pPhysicsVehicle = pCar->getPhysicsVehicle();
    if ( pPhysicsVehicle == nullptr || !pCar->getBody()->bHasBikeAvatar ) {
        return param_1;
    }

    const Matrix4x4f_t &riderMatrix = pPhysicsVehicle->getBikeRiderMatrix();
    const Eigen::Vector4f shiftedTarget =
        riderMatrix.row( 3 ).transpose() +
        Eigen::Vector4f( 0.0f, kTargetHeight, 0.0f, 0.0f );

    if ( bNeedsRoadPosition ) {
        Eigen::Vector4f velocity = pPhysicsVehicle->getBikeRiderVelocity();
        velocity.y() = 0.0f;

        Eigen::Vector4f predictedTarget =
            shiftedTarget + GetStreamingShift() + velocity * kVelocityLookAhead;
        predictedTarget.w() = 1.0f;

        AiRoadLocation roadLocation;
        ProjectOnRoad( predictedTarget, nullptr, nullptr, false,
                       &roadLocation );

        shiftedRoadPosition =
            roadLocation.Frame.row( 3 ).transpose() - GetStreamingShift() +
            Eigen::Vector4f( 0.0f, kCameraHeight, 0.0f, 0.0f );
        shiftedRoadPosition.w() = 1.0f;
        roadUpVector = kWorldUpVector;
        bNeedsRoadPosition = false;
    }

    roadUpVector = kWorldUpVector;
    setPositionOffseted( shiftedRoadPosition );
    setTargetOffseted( shiftedTarget );
    upVector = roadUpVector;

    const Eigen::Vector4f toTarget = shiftedTarget - shiftedRoadPosition;
    const float distanceRatio =
        std::min( toTarget.norm() * kFovDistanceScale, kOne );
    const float fov = std::max(
        ( kHalfPI - atanf( kFovAtanScale * distanceRatio ) ) * kFovRangeScale,
        kDegToRad );
    setFOV( fov );

    updateMatrix( worldPosition.head<3>(), upVector.head<3>(),
                  worldTarget.head<3>() );

    return param_1;
}

void CameraBikeDriver::destroy() {
    // FUN_008cccf0
    // NOP
}
