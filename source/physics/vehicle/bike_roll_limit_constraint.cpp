#include "shared.h"
#include "bike_roll_limit_constraint.h"

#include <Jolt/Physics/Body/Body.h>

#include <cfloat>

JPH::TwoBodyConstraint *
BikeRollLimitConstraintSettings::Create( JPH::Body &inBody1,
                                         JPH::Body &inBody2 ) const {
    return new BikeRollLimitConstraint( inBody1, inBody2, *this );
}

BikeRollLimitConstraint::BikeRollLimitConstraint(
    JPH::Body &inWorld,
    JPH::Body &inChassis,
    const BikeRollLimitConstraintSettings &inSettings )
    : JPH::TwoBodyConstraint( inWorld, inChassis, inSettings ),
      rollLimit( inSettings.mRollLimit ) {}

bool BikeRollLimitConstraint::computeRoll( JPH::Vec3 &outAxis,
                                           float &outRoll ) const {
    const JPH::Quat rotation = mBody2->GetRotation();
    const JPH::Vec3 forward = rotation * JPH::Vec3::sAxisZ();
    const JPH::Vec3 up = rotation * JPH::Vec3::sAxisY();

    const JPH::Vec3 worldUp = JPH::Vec3::sAxisY();
    const JPH::Vec3 zeroRollUp = worldUp - forward * forward.Dot( worldUp );
    const float zeroRollUpLength = zeroRollUp.Length();
    if ( zeroRollUpLength < 1.0e-3f ) {
        return false;
    }

    const JPH::Vec3 reference = zeroRollUp / zeroRollUpLength;
    outAxis = forward;
    outRoll =
        std::atan2( reference.Cross( up ).Dot( forward ), reference.Dot( up ) );
    return true;
}

float BikeRollLimitConstraint::angleToLimit( float inRoll ) const {
    if ( inRoll > rollLimit ) {
        return inRoll - rollLimit;
    }
    if ( inRoll < -rollLimit ) {
        return inRoll + rollLimit;
    }
    return 0.0f;
}

void BikeRollLimitConstraint::calculateLimitProperties( float inDeltaTime ) {
    if ( computeRoll( axis, roll ) && angleToLimit( roll ) != 0.0f ) {
        limitPart.CalculateConstraintProperties( *mBody1, *mBody2, axis );
    } else {
        limitPart.Deactivate();
    }
    (void)inDeltaTime;
}

void BikeRollLimitConstraint::SetupVelocityConstraint( float inDeltaTime ) {
    calculateLimitProperties( inDeltaTime );
}

void BikeRollLimitConstraint::ResetWarmStart() { limitPart.Deactivate(); }

void BikeRollLimitConstraint::WarmStartVelocityConstraint(
    float inWarmStartImpulseRatio ) {
    limitPart.WarmStart( *mBody1, *mBody2, inWarmStartImpulseRatio );
}

bool BikeRollLimitConstraint::SolveVelocityConstraint( float inDeltaTime ) {
    (void)inDeltaTime;
    if ( !limitPart.IsActive() ) {
        return false;
    }

    // Only push the chassis back towards the allowed range.
    const float minLambda = ( roll > 0.0f ) ? -FLT_MAX : 0.0f;
    const float maxLambda = ( roll > 0.0f ) ? 0.0f : FLT_MAX;
    return limitPart.SolveVelocityConstraint( *mBody1, *mBody2, axis, minLambda,
                                              maxLambda );
}

bool BikeRollLimitConstraint::SolvePositionConstraint( float inDeltaTime,
                                                       float inBaumgarte ) {
    calculateLimitProperties( inDeltaTime );
    if ( !limitPart.IsActive() ) {
        return false;
    }
    return limitPart.SolvePositionConstraint(
        *mBody1, *mBody2, angleToLimit( roll ), inBaumgarte );
}

JPH::Ref<JPH::ConstraintSettings>
BikeRollLimitConstraint::GetConstraintSettings() const {
    BikeRollLimitConstraintSettings *pSettings =
        new BikeRollLimitConstraintSettings();
    ToConstraintSettings( *pSettings );
    pSettings->mRollLimit = rollLimit;
    return pSettings;
}
