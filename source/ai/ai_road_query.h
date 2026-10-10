#pragma once

#include "ai/ai_road_iterator.h"

class SavedRoadDiscovery;

struct AiRoadLocation {
    AiRoadIt It;
    float T = -1.0f;
    Matrix4x4f_t Frame = Matrix4x4f_t::Identity();
};

enum class eRoadSnapMode : int32_t {
    RSM_ProjectOnly = 0,
    // Back off 20 m and keep 10 m away from the road ends
    RSM_KeepAwayFromEnds = 1,
    // Back off 20 m (10 m straight back if the road is too short)
    RSM_BackOff = 2,
};

enum eRoadSnapResult : int32_t {
    RSR_NoRoad = 0,
    RSR_RoadLocked = 1,
    RSR_RoadAvailable = 2,
};

bool ProjectOnRoad( const Eigen::Vector4f &position,
                    AiRoadProfile **ppOutProfile,
                    float *pOutDistanceSqXZ,
                    const bool bBank,
                    AiRoadLocation *pOutLocation );
bool ProjectFrameOnRoad( const Matrix4x4f_t &frame,
                         Matrix4x4f_t &outFrame,
                         AiRoadProfile **ppOutProfile,
                         const bool bBank );
void ComputeRoadApproachFrame( const Eigen::Vector4f &position,
                               Matrix4x4f_t &outFrame,
                               const float distance );
bool ComputeRoadFrameFromIt( AiRoadIt &it,
                             const Eigen::Vector4f &position,
                             Matrix4x4f_t &outFrame,
                             const eRoadSnapMode mode,
                             const bool bShoulder );
bool FindNearestDiscoveredRoadEntity( const SavedRoadDiscovery *pDiscovery,
                                      const Eigen::Vector4f &position,
                                      AiRoadIt &outIt );
eRoadSnapResult SnapToRoadFrame( const Eigen::Vector4f &position,
                                 Matrix4x4f_t &outFrame,
                                 const eRoadSnapMode mode,
                                 const bool bShoulder,
                                 const bool bNearestDiscoverable );
