#include "shared.h"
#include "ai_veh_nav_cinematic.h"

#include "ai/ai_vehicle.h"
#include "game/vehicle/car_state.h"
#include "physics/vehicle/physics_vehicle.h"

static constexpr float kAnchorEpsilon = 1e-5f;        // 0x3727c5ac
static constexpr float kMinAnchorDistance = 0.1f;     // DAT_00faa7f8
static constexpr float kFullScaleProgress = 1.0f;     // DAT_00faa7f4
static constexpr float kMaxSpeedScale = 1.5f;         // DAT_00faa7ec
static constexpr float kMinSpeedScale = 1.0f;         // DAT_00faa7f0
static constexpr float kMinDirectionSqLength = 1e-3f; // FLOAT_00f4167c
static constexpr float kAttachUnknown = 0.75f;        // DAT_00f7c300

// Nnever written by original pcode
static constexpr float kSpeedOffsetMax = 0.0f; // DAT_016a2c2c
static constexpr float kSpeedOffsetMin = 0.0f; // DAT_016a2c30

static bool IsNearlyEqual( const Eigen::Vector4f &a,
                           const Eigen::Vector4f &b ) {
    // FUN_00409350
    return ( ( a - b ).array().abs() < kAnchorEpsilon ).all();
}

AiVehNavCinematic::AiVehNavCinematic()
    : pOwner( nullptr ),
      anchor( Eigen::Vector4f::Zero() ),
      target( Eigen::Vector4f::Zero() ),
      speed( 0.0f ),
      holdTime( 0.0f ),
      unknown0x58( 0.0f ) {
    // FUN_0095b230
    reset( false );
}

void AiVehNavCinematic::attach( AiVehicle *pVehicle ) {
    // FUN_0095b200
    pOwner = pVehicle;
    reset( false );
    unknown0x58 = kAttachUnknown;
}

void AiVehNavCinematic::reset( const bool ) {
    // FUN_0095b050
    anchor = Eigen::Vector4f( 0.0f, 0.0f, 0.0f, 1.0f );
    target = Eigen::Vector4f( 0.0f, 0.0f, 1.0f, 1.0f );
    holdTime = -1.0f;
}

void AiVehNavCinematic::tick( const float, const float ) {
    // FUN_0095b020
    pOwner->getInfos().bHandbrake = holdTime < 0.0f;
}

void AiVehNavCinematic::detach() {
    // FUN_0095afe0
}

bool AiVehNavCinematic::getTarget( Eigen::Vector4f &outDirection,
                                   float &outSpeed ) const {
    // FUN_0095b250
    if ( IsNearlyEqual( anchor, Eigen::Vector4f::Zero() ) ) {
        outDirection = Eigen::Vector4f( 1.0f, 0.0f, 0.0f, 0.0f );
        outSpeed = 0.0f;
        return false;
    }

    const Matrix4x4f_t &world = pOwner->getCarState()->getWorldMatrix();
    const Eigen::Vector4f position = world.row( 3 ).transpose();
    const Eigen::Vector4f forward = world.row( 2 ).transpose();

    outDirection = target - position;
    outDirection.y() = 0.0f;
    outDirection.w() = 0.0f;
    outSpeed = speed;

    Eigen::Vector4f fromAnchor = position - anchor;
    fromAnchor.y() = 0.0f;
    fromAnchor.w() = 0.0f;
    if ( fromAnchor.norm() > kMinAnchorDistance ) {
        const float progress = fromAnchor.dot( position );
        if ( progress > kMinAnchorDistance ) {
            const float ratio =
                ( progress - kMinAnchorDistance ) *
                ( 1.0f / ( kFullScaleProgress - kMinAnchorDistance ) );
            const float offset = std::min(
                ( kSpeedOffsetMax - kSpeedOffsetMin ) * ratio + kSpeedOffsetMin,
                kSpeedOffsetMax );
            const float scale = std::min(
                ( kMaxSpeedScale - kMinSpeedScale ) * ratio + kMinSpeedScale,
                kMaxSpeedScale );
            outSpeed = speed * scale + offset;
        }
    }

    if ( outDirection.squaredNorm() < kMinDirectionSqLength ||
         outDirection.dot( forward ) < 0.0f ) {
        outDirection = forward;
    } else {
        outDirection.normalize();
    }

    AiVehicleInfo &infos = pOwner->getInfos();
    if ( outSpeed > 0.0f ) {
        infos.bHandbrake = false;
    } else {
        outSpeed = 0.0f;
        infos.bHandbrake = true;
    }
    return true;
}

void AiVehNavCinematic::getHeightAndTwist( float &outHeight,
                                           float &outTwist ) const {
    // FUN_00923420
    outTwist = 0.0f;
    PhysicsVehicle *pPhysics = pOwner->getCarState()->getPhysicsVehicle();
    if ( pPhysics == nullptr ) {
        outHeight = pOwner->getInfos().GroundMatrix( 3, 1 );
        return;
    }
    const Matrix4x4f_t chassis = pPhysics->getChassisWorldMatrix();
    outHeight = chassis( 3, 1 ) -
                chassis( 1, 1 ) * pPhysics->computeGroundToOriginOffset();
}
