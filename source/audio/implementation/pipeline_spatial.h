#pragma once

#include "ed_audio_object.h"

struct SpatialPosition {
    Eigen::Vector4f Position;
    Eigen::Vector4f Direction;
    float Distance;
    float InvDistance;

    void set( const Eigen::Vector3f &position );
};

// One positional sound
struct SpatialSource {
    SpatialPosition Placement;
    float Weight;
    int32_t SourceIndex;
    SpatialSource *pNextMember;
};

// Clustering state
struct ClusterInfo {
    SpatialSource *pFirstMember;
    int32_t MemberCount;
    SpatialPosition Placement;
    float TotalWeight;
    edAudioObject **ppMembers;

    void updateCentroid();
    float distanceTo( const SpatialSource &source ) const;
};

// Groups the active positional sounds (into at most 16 clusters)
class SpatialClustering {
public:
    static constexpr int32_t kMaxSources = 256;
    static constexpr int32_t kMaxClusters = 16;
    static constexpr float kMetricScale = 0.5f;         // FLOAT_00f41674
    static constexpr float kInfiniteDistance = 1.0e10f; // DAT_00f41b80

public:
    SpatialClustering();

    bool addSource( const Eigen::Vector3f &listenerRelativePosition,
                    float weight,
                    edAudioObject *pSourceModule );
    void reset();
    void resetClusters();
    void build( float minSeparation );
    void flattenMembers();

public:
    SpatialSource Sources[kMaxSources];
    ClusterInfo Clusters[kMaxClusters];
    edAudioObject *pSourceObjects[kMaxSources];
    edAudioObject *pFlatMembers[kMaxSources];

    int32_t SourceCount;
    int32_t ClusterCount;
    int32_t MaxClusterCount;
};
