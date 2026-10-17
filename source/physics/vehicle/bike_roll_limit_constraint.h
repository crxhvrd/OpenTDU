#pragma once

#include <Jolt/Jolt.h>

#include <Jolt/Physics/Constraints/ConstraintPart/AngleConstraintPart.h>
#include <Jolt/Physics/Constraints/TwoBodyConstraint.h>

class BikeRollLimitConstraintSettings final
    : public JPH::TwoBodyConstraintSettings {
public:
    JPH::TwoBodyConstraint *Create( JPH::Body &inBody1,
                                    JPH::Body &inBody2 ) const override;

    float mRollLimit = 0.01f;
};

class BikeRollLimitConstraint final : public JPH::TwoBodyConstraint {
public:
    BikeRollLimitConstraint(
        JPH::Body &inWorld,
        JPH::Body &inChassis,
        const BikeRollLimitConstraintSettings &inSettings );

    JPH::EConstraintSubType GetSubType() const override {
        return JPH::EConstraintSubType::User1;
    }
    void NotifyShapeChanged( const JPH::BodyID &, JPH::Vec3Arg ) override {}
    void SetupVelocityConstraint( float inDeltaTime ) override;
    void ResetWarmStart() override;
    void WarmStartVelocityConstraint( float inWarmStartImpulseRatio ) override;
    bool SolveVelocityConstraint( float inDeltaTime ) override;
    bool SolvePositionConstraint( float inDeltaTime,
                                  float inBaumgarte ) override;
#ifdef JPH_DEBUG_RENDERER
    void DrawConstraint( JPH::DebugRenderer * ) const override {}
#endif
    JPH::Ref<JPH::ConstraintSettings> GetConstraintSettings() const override;
    JPH::Mat44 GetConstraintToBody1Matrix() const override {
        return JPH::Mat44::sIdentity();
    }
    JPH::Mat44 GetConstraintToBody2Matrix() const override {
        return JPH::Mat44::sIdentity();
    }

private:
    bool computeRoll( JPH::Vec3 &outAxis, float &outRoll ) const;
    float angleToLimit( float inRoll ) const;
    void calculateLimitProperties( float inDeltaTime );

    float rollLimit;
    JPH::Vec3 axis = JPH::Vec3::sAxisZ();
    float roll = 0.0f;
    JPH::AngleConstraintPart limitPart;
};
