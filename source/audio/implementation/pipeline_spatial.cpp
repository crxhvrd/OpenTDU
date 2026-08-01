#include "shared.h"
#include "pipeline_spatial.h"

#include "ed_audio.h"

#include <cmath>

void SpatialPosition::set( const Eigen::Vector3f &position ) {
    // FUN_0040e040
    Position =
        Eigen::Vector4f( position.x(), position.y(), position.z(), 0.0f );

    Distance = position.norm();

    if ( Distance > 0.0f ) {
        InvDistance = 1.0f / Distance;
        Direction = Position * InvDistance;
        Direction.w() = 0.0f;
    } else {
        InvDistance = 0.0f;
        Direction = Eigen::Vector4f::Zero();
    }
}

float ClusterInfo::distanceTo( const SpatialSource &source ) const {
    // FUN_00614e50
    const float ratio = Placement.InvDistance * source.Placement.Distance;

    const float levelTerm =
        ( ratio > 0.0f )
            ? fabsf( log10f( ratio ) ) * SpatialClustering::kMetricScale
            : 0.0f;

    const float cosAngle = source.Placement.Direction.head<3>().dot(
        Placement.Direction.head<3>() );

    const float angleTerm =
        ( 1.0f - cosAngle ) * SpatialClustering::kMetricScale * 2.0f;

    return ( levelTerm + angleTerm ) * source.Weight;
}

void ClusterInfo::updateCentroid() {
    // FUN_00614f30
    Eigen::Vector3f weighted = Eigen::Vector3f::Zero();
    float weightSum = 0.0f;

    const SpatialSource *pMember = pFirstMember;
    for ( int32_t n = 0; n < MemberCount && pMember != nullptr; n++ ) {
        weighted += pMember->Placement.Position.head<3>() * pMember->Weight;
        weightSum += pMember->Weight;

        pMember = pMember->pNextMember;
    }

    TotalWeight = weightSum;

    if ( weightSum > 0.0f ) {
        Placement.set( weighted * ( 1.0f / weightSum ) );
    }
}

SpatialClustering::SpatialClustering()
    : Sources{},
      Clusters{},
      pSourceObjects{},
      pFlatMembers{},
      SourceCount( 0 ),
      ClusterCount( 0 ),
      MaxClusterCount( 0 ) {}

bool SpatialClustering::addSource(
    const Eigen::Vector3f &listenerRelativePosition,
    float weight,
    edAudioObject *pSourceModule ) {
    if ( SourceCount >= kMaxSources || pSourceModule == nullptr ) {
        return false;
    }

    const int32_t index = SourceCount;

    SpatialSource &source = Sources[index];
    source.Placement.set( listenerRelativePosition );
    source.Weight = weight;
    source.SourceIndex = index;
    source.pNextMember = nullptr;

    pSourceObjects[index] = pSourceModule;

    SourceCount++;

    return true;
}

void SpatialClustering::reset() {
    // FUN_0040c600
    SourceCount = 0;
    resetClusters();
}

void SpatialClustering::resetClusters() {
    ClusterCount = 0;

    for ( ClusterInfo &cluster : Clusters ) {
        cluster.pFirstMember = nullptr;
        cluster.MemberCount = 0;
    }
}

void SpatialClustering::build( float minSeparation ) {
    // FUN_0040c630
    if ( SourceCount == 0 ) {
        return;
    }

    bool bIsSeed[kMaxSources] = {};

    bIsSeed[0] = true;
    Clusters[0].Placement.set( Sources[0].Placement.Position.head<3>() );

    if ( MaxClusterCount < 1 ) {
        MaxClusterCount = kMaxClusters;
    }

    const int32_t clusterLimit = Min( MaxClusterCount, kMaxClusters );

    int32_t clusterCount = 1;
    while ( clusterCount < clusterLimit ) {
        int32_t bestSource = -1;
        float bestDistance = minSeparation;

        for ( int32_t i = 0; i < SourceCount; i++ ) {
            if ( bIsSeed[i] ) {
                continue;
            }

            float nearest = kInfiniteDistance;
            for ( int32_t c = 0; c < clusterCount; c++ ) {
                nearest = Min( nearest, Clusters[c].distanceTo( Sources[i] ) );
            }

            if ( bestDistance < nearest ) {
                bestSource = i;
                bestDistance = nearest;
            }
        }

        if ( bestSource < 0 ) {
            break;
        }

        bIsSeed[bestSource] = true;

        Clusters[clusterCount].pFirstMember = nullptr;
        Clusters[clusterCount].MemberCount = 0;
        Clusters[clusterCount].Placement.set(
            Sources[bestSource].Placement.Position.head<3>() );

        clusterCount++;
    }

    ClusterCount = clusterCount;

    if ( clusterCount < 2 ) {
        for ( int32_t i = 0; i < SourceCount; i++ ) {
            Sources[i].pNextMember = Clusters[0].pFirstMember;
            Clusters[0].pFirstMember = &Sources[i];
            Clusters[0].MemberCount++;
        }
    } else {
        for ( int32_t i = 0; i < SourceCount; i++ ) {
            int32_t bestCluster = -1;
            float bestDistance = kInfiniteDistance;

            for ( int32_t c = 0; c < ClusterCount; c++ ) {
                const float distance = Clusters[c].distanceTo( Sources[i] );
                if ( distance < bestDistance ) {
                    bestCluster = c;
                    bestDistance = distance;
                }
            }

            if ( bestCluster < 0 ) {
                bestCluster = 0;
            }

            Sources[i].pNextMember = Clusters[bestCluster].pFirstMember;
            Clusters[bestCluster].pFirstMember = &Sources[i];
            Clusters[bestCluster].MemberCount++;
        }
    }

    for ( int32_t c = 0; c < ClusterCount; c++ ) {
        Clusters[c].updateCentroid();
    }
}

void SpatialClustering::flattenMembers() {
    // FUN_0040c900
    int32_t writeCursor = 0;

    for ( int32_t c = 0; c < ClusterCount; c++ ) {
        ClusterInfo &cluster = Clusters[c];
        cluster.ppMembers = &pFlatMembers[writeCursor];

        SpatialSource *pMember = cluster.pFirstMember;
        for ( int32_t n = 0; n < cluster.MemberCount && pMember != nullptr;
              n++ ) {
            const int32_t index = pMember->SourceIndex;
            pFlatMembers[writeCursor++] = ( index >= 0 && index < kMaxSources )
                                              ? pSourceObjects[index]
                                              : nullptr;

            pMember = pMember->pNextMember;
        }
    }
}
