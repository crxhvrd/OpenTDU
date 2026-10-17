#pragma once

#include <cstdint>

#include "shared.h"

class Animation;

Eigen::Quaternionf BuildAimRotation( const Eigen::Vector3f &from,
                                     const Eigen::Vector3f &to,
                                     float damping );

struct AnimTargetParams {
    static constexpr uint32_t kNoTarget = 0xffffffffu;

    uint32_t TargetID = kNoTarget;
    Eigen::Vector4f Target = Eigen::Vector4f::Zero();
    Eigen::Vector4f NextTarget = Eigen::Vector4f::Zero();
    float TargetTransitionLength = 0.0f;
    float AngularSpeed = 0.0f;

    void setTarget( const Eigen::Vector3f &target, float transitionLength );
    void timeslice( Animation &animation, float deltaTime );
};

class AnimExpression {
public:
    virtual ~AnimExpression() = default;

    inline void changeDamping( float nextDamping, float transitionLength ) {
        // FUN_00678ce0
        NextDamping = nextDamping;
        DampingTransitionLength = transitionLength;
    }

    virtual void timeslice( Animation &animation, float deltaTime ) = 0;

public:
    uint32_t Flags = 0;
    AnimExpression *pNext = nullptr;
    float Damping = 0.0f;
    float NextDamping = 0.0f;
    float DampingTransitionLength = 0.0f;

protected:
    void advanceDamping( float deltaTime );
};

class AnimIK3NParams : public AnimExpression {
public:
    void timeslice( Animation &animation, float deltaTime ) override;

public:
    AnimTargetParams TargetParams;
    uint32_t NodesID[3] = { 0, 0, 0 };
};

class AnimTarget2NParams : public AnimExpression {
public:
    void timeslice( Animation &animation, float deltaTime ) override;

public:
    AnimTargetParams TargetParams;
    uint32_t NodesID[2] = { 0, 0 };
};

class AnimAlign1NParams : public AnimExpression {
public:
    void timeslice( Animation &animation, float deltaTime ) override;

public:
    uint32_t NodeID = 0;
    Eigen::Vector4f LocalAxis = Eigen::Vector4f::Zero();
    Eigen::Vector4f Target = Eigen::Vector4f::Zero();
};

class AnimLinkParams : public AnimExpression {
public:
    void timeslice( Animation &animation, float deltaTime ) override;

public:
    AnimTargetParams TargetParams;
    uint32_t NodeID = 0;
    Eigen::Vector4f LocalAxis = Eigen::Vector4f::Zero();
};

class AnimExpressionList : public AnimExpression {
public:
    void timeslice( Animation &animation, float deltaTime ) override;
    void append( AnimExpression *pExpression );
    void clear();
};
