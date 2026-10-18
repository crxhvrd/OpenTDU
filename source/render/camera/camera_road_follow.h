#pragma once

#include "camera_game.h"

#include "ai/ai_road_iterator.h"
#include "core/cubic_bezier.h"
#include "core/random/bcpl.h"

enum class eRoadFollowTurn : int32_t {
    RFT_SmallestAngle = 0,
    RFT_LargestAngle = 1,
    RFT_Straight = 2,
    RFT_Random = 3
};

class CameraRoadFollow : public CameraGame {
public:
    CameraRoadFollow();
    virtual ~CameraRoadFollow();

    void initialize() override;
    bool tick( float deltaTime, bool param_1, bool param_2 ) override;
    void destroy() override;

private:
    void updateFromInput();
    void advance( const float deltaTime );
    void pickNextRoad();
    void setRoadPosition( const AiRoadIt &it );

private:
    bool bInTransition;
    AiRoadIt roadIt;
    float progress;
    int32_t transitionCurveType;
    CubicBezier transitionCurve;
    float speed;
    eRoadFollowTurn turnMode;
    bool bReverseRequested;
    float minDistance;
    float maxDistance;
    float maxSpeed;
    float minPitch;
    float maxPitch;
    float distance;
    float pitch;
    Eigen::Vector4f roadPosition;
    Eigen::Vector4f roadDirection;
    Eigen::Vector4f roadSide;
    RandomGen random;
    bool bStreamingDisabledBefore;
};
