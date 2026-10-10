#include "shared.h"
#include "ai_road_query.h"

#include "ai/ai_road_shape.h"
#include "ai/gs_ai.h"
#include "core/maths_helper.h"
#include "player_data/gs_playerdata.h"
#include "player_data/player_data_interface.h"
#include "player_data/saved_road_discovery.h"

static constexpr float kSnapBackOffDistance = 20.0f;    // DAT_00fab4d8
static constexpr float kSnapRoadEndMargin = 10.0f;      // FLOAT_00f4166c
static constexpr float kShoulderWidthRatio = 0.9f;      // DAT_00f416a0
static constexpr float kShoulderInnerMargin = 2.1f;     // DAT_00f42108
static constexpr float kSnapSearchRadius = 2000.0f;     // DAT_00fab954
static constexpr float kEntityStartHeight = 0.2f;       // DAT_00f41730
static constexpr float kFarDistanceSq = 10000000000.0f; // DAT_00f41b80
static constexpr int32_t kFirstLane = 0;
static constexpr int32_t kRoadCenterLane = -1;

static bool IsRoadMapAvailable() {
    return gpAI != nullptr && gpAI->isRoadMapLoaded();
}

static void SetTravelDirection( AiRoadIt &it, const bool bWay ) {
    if ( it.bWay != bWay ) {
        it.bWay = bWay;
        it.T = 1.0f - it.T;
    }
}

bool ProjectOnRoad( const Eigen::Vector4f &position,
                    AiRoadProfile **ppOutProfile,
                    float *pOutDistanceSqXZ,
                    const bool bBank,
                    AiRoadLocation *pOutLocation ) {
    // FUN_0078d720
    if ( !IsRoadMapAvailable() ) {
        return false;
    }

    Matrix4x4f_t localFrame;
    Matrix4x4f_t &outFrame =
        ( pOutLocation != nullptr ) ? pOutLocation->Frame : localFrame;

    Matrix4x4f_t frame = Matrix4x4f_t::Identity();
    frame.row( 3 ) = position.transpose();
    AiRoadMap::Road *pRoad =
        gpAI->getRoadMap().findNearestRoad( position, true );
    if ( pRoad == nullptr ) {
        return false;
    }

    AiRoadIt it;
    ProjectOnRoadShape( frame, it, &outFrame, bBank, pRoad->getShape() );
    if ( pOutLocation != nullptr ) {
        pOutLocation->It = it;
        pOutLocation->T = it.T;
    }
    if ( ppOutProfile != nullptr ) {
        *ppOutProfile = it.getEntity()->pProfile;
    }
    if ( pOutDistanceSqXZ != nullptr ) {
        Eigen::Vector4f delta = outFrame.row( 3 ).transpose() - position;
        delta.y() = 0.0f;
        *pOutDistanceSqXZ = delta.dot( delta );
    }
    return true;
}

bool ProjectFrameOnRoad( const Matrix4x4f_t &frame,
                         Matrix4x4f_t &outFrame,
                         AiRoadProfile **ppOutProfile,
                         const bool bBank ) {
    // FUN_0078d690
    if ( !IsRoadMapAvailable() ) {
        return false;
    }

    AiRoadMap::Road *pRoad =
        gpAI->getRoadMap().findNearestRoad( frame.row( 3 ).transpose(), true );
    AiRoadShape *pShape = ( pRoad != nullptr ) ? pRoad->getShape() : nullptr;
    if ( pShape == nullptr ) {
        return false;
    }

    AiRoadIt it;
    ProjectOnRoadShape( frame, it, &outFrame, bBank, pShape );
    *ppOutProfile = it.getEntity()->pProfile;
    return true;
}

void ComputeRoadApproachFrame( const Eigen::Vector4f &position,
                               Matrix4x4f_t &outFrame,
                               const float distance ) {
    // FUN_0078d8d0
    AiRoadLocation location;
    if ( !ProjectOnRoad( position, nullptr, nullptr, false, &location ) ) {
        outFrame = Matrix4x4f_t::Identity();
        outFrame.row( 3 ) = position.transpose();
        return;
    }

    location.It.advanceByDistance( -distance, true );
    location.It.computeFrame( outFrame, kRoadCenterLane );
}

bool ComputeRoadFrameFromIt( AiRoadIt &it,
                             const Eigen::Vector4f &position,
                             Matrix4x4f_t &outFrame,
                             const eRoadSnapMode mode,
                             const bool bShoulder ) {
    // FUN_0088deb0
    const AiRoadMap::Road *pRoad = it.pRoad;
    if ( pRoad->MinLeftLane == 0 ) {
        SetTravelDirection( it, true );
    } else if ( pRoad->MinRightLane == 0 ) {
        SetTravelDirection( it, false );
    } else {
        SetTravelDirection( it, true );
        const float wayDistance =
            ( position - it.computePosition( kFirstLane ) ).norm();
        SetTravelDirection( it, false );
        const float oppositeWayDistance =
            ( position - it.computePosition( kFirstLane ) ).norm();
        if ( wayDistance < oppositeWayDistance ) {
            SetTravelDirection( it, true );
        }
    }

    bool bBackedOff = true;
    if ( mode != eRoadSnapMode::RSM_ProjectOnly ) {
        bBackedOff = it.advanceByDistance( -kSnapBackOffDistance, true );
    }

    if ( mode == eRoadSnapMode::RSM_KeepAwayFromEnds ) {
        if ( ComputeRoadDistance( nullptr, false, it ) < kSnapRoadEndMargin ) {
            it.advanceByDistance( -kSnapRoadEndMargin, true );
        } else if ( ComputeRoadDistance( nullptr, true, it ) <
                    kSnapRoadEndMargin ) {
            it.advanceByDistance( kSnapRoadEndMargin, true );
        }
    }

    Eigen::Vector4f snappedPosition;
    if ( !bShoulder ) {
        snappedPosition = it.computePosition( kFirstLane );
    } else {
        const AiRoadProfile *pProfile = it.getEntity()->pProfile;
        float widthRatio =
            kShoulderWidthRatio - kShoulderInnerMargin / pProfile->InnerWidth;
        if ( !it.bWay ) {
            widthRatio = -widthRatio;
        }
        snappedPosition = it.computeWidthPosition( widthRatio );
    }

    Eigen::Vector4f direction = it.computeDirection();
    direction /= std::sqrt( direction.dot( direction ) );
    Eigen::Vector4f right( direction.z(), 0.0f, -direction.x(), 0.0f );
    right /= std::sqrt( right.dot( right ) );
    const Eigen::Vector4f up = Vector4DCross( direction, right );

    outFrame = Matrix4x4f_t::Identity();
    outFrame.row( 0 ) = Vector4DCross( up, direction ).transpose();
    outFrame.row( 1 ) = up.transpose();
    outFrame.row( 2 ) = direction.transpose();
    outFrame.row( 3 ) = snappedPosition.transpose();
    if ( mode == eRoadSnapMode::RSM_BackOff && !bBackedOff ) {
        outFrame.row( 3 ) -= ( direction * kSnapRoadEndMargin ).transpose();
    }
    return true;
}

bool FindNearestDiscoveredRoadEntity( const SavedRoadDiscovery *pDiscovery,
                                      const Eigen::Vector4f &position,
                                      AiRoadIt &outIt ) {
    // FUN_0088d740
    const AiRoadMap &roadMap = gpAI->getRoadMap();
    float nearestDistanceSq = kFarDistanceSq;
    for ( int32_t i = 0; i < roadMap.getNumRoads(); i++ ) {
        AiRoadMap::Road &road = roadMap.getRoads()[i];
        const AiRoadMap::RoadDetail *pDetail = road.pDetail;
        if ( pDetail == nullptr ) {
            continue;
        }

        for ( int32_t j = 0; j < pDetail->NumEntities; j++ ) {
            if ( pDiscovery->isRoadLocked( road.ID, j ) ||
                 !IsRoadAvailable( road.ID ) ) {
                continue;
            }

            const Eigen::Vector4f entityStart =
                pDetail->pEntities[j]->computeOffsetPosition(
                    0, 0.0f, 0.0f, kEntityStartHeight );
            const float distanceSq = ( entityStart - position ).squaredNorm();
            if ( distanceSq < nearestDistanceSq ) {
                nearestDistanceSq = distanceSq;
                outIt.pRoad = &road;
                outIt.IndEntity = j;
                outIt.IndWay = 0;
                outIt.bWay = true;
                outIt.T = 0.0f;
            }
        }
    }
    return nearestDistanceSq < kFarDistanceSq;
}

eRoadSnapResult SnapToRoadFrame( const Eigen::Vector4f &position,
                                 Matrix4x4f_t &outFrame,
                                 const eRoadSnapMode mode,
                                 const bool bShoulder,
                                 const bool bNearestDiscoverable ) {
    // FUN_009c0970
    AiRoadMap::Road *pRoad = nullptr;
    if ( IsRoadMapAvailable() ) {
        AiRoadMap &roadMap = gpAI->getRoadMap();
        pRoad = bNearestDiscoverable ? roadMap.findNearestRoad( position, true )
                                     : roadMap.findNearestRoadInRadius(
                                           position, kSnapSearchRadius );
    }
    if ( pRoad == nullptr ) {
        outFrame = Matrix4x4f_t::Identity();
        outFrame.row( 3 ) = position.transpose();
        return RSR_NoRoad;
    }

    Matrix4x4f_t frame = Matrix4x4f_t::Identity();
    frame.row( 3 ) = position.transpose();
    AiRoadIt it;
    ProjectOnRoadShape( frame, it, nullptr, false, pRoad->getShape() );
    ComputeRoadFrameFromIt( it, position, outFrame, mode, bShoulder );

    const SavedRoadDiscovery *pDiscovery =
        gpPlayerData->getLoadedSave()->getRoadDiscovery();
    if ( pDiscovery->isRoadLocked( it.pRoad->ID, it.IndEntity ) ||
         !IsRoadAvailable( it.pRoad->ID ) ) {
        return RSR_RoadLocked;
    }
    return RSR_RoadAvailable;
}
