#pragma once

#include "ai/ai_road_map.h"

// Position on a road curve
struct AiRoadIt {
    AiRoadMap::Road *pRoad = nullptr;
    int32_t IndEntity = -1;
    int32_t IndWay = -1;
    bool bWay = true;
    float T = 0.0f;

    inline AiRoadMap::Entity *getEntity() const {
        return pRoad->pDetail->pEntities[IndEntity];
    }

    inline int32_t getNumCurves() const { return getEntity()->getNumCurves(); }

    inline void flip() {
        bWay = !bWay;
        T = 1.0f - T;
    }

    inline void setWay( const bool bNewWay ) {
        if ( bWay != bNewWay ) {
            T = 1.0f - T;
        }
        bWay = bNewWay;
    }

    void setRoadEntry( AiRoadMap::Road *pOwnerRoad, const bool bNewWay );
    void setRoadExit( AiRoadMap::Road *pOwnerRoad, const bool bNewWay );
    bool isBefore( const AiRoadIt &other ) const;
    bool isSamePosition( const AiRoadIt &other, const float tolerance ) const;

    bool projectOnCurve( float &outT,
                         Eigen::Vector4f &outPosition,
                         Eigen::Vector4f &outTangent,
                         const Eigen::Vector4f &point ) const;
    Eigen::Vector4f computeDirection() const;
    Eigen::Vector4f computePosition( const int32_t lane ) const;
    Eigen::Vector4f computeWidthPosition( const float widthRatio ) const;
    Eigen::Vector4f computeOffsetPosition( const float lateral,
                                           const float height ) const;
    float computeAngleAtJunction( const AiRoadMap::Junction *pJunction ) const;
    Eigen::Vector4f computeSideDirection() const;
    Eigen::Vector4f computeStartDirection() const;
    bool computeFrame( Matrix4x4f_t &outFrame, const int32_t lane ) const;
    float computeSpeed( const float t ) const;
    float computeCurveLength( const float startT, const float endT ) const;

    bool isLastCurveInEntity() const;
    bool isFirstCurveInEntity() const;
    bool isAtRoadEnd() const;
    bool isAtRoadStart() const;
    bool stepCurve( const bool bBackward );
    bool advanceByDistance( float distance, const bool bEnsureShape );
};

// Waypoint (curve end point) of a road
struct AiRoadVertexIt {
    AiRoadMap::Road *pRoad = nullptr;
    AiRoadMap::RoadDetail *pDetail = nullptr;
    int32_t IndEntity = -1;
    int32_t IndVertex = -1;
    bool bWay = true;

    inline AiRoadMap::Entity *getEntity( const int32_t entityIndex ) const {
        return pDetail->pEntities[entityIndex];
    }

    inline int32_t getNumWaypoints( const int32_t entityIndex ) const {
        return getEntity( entityIndex )->getNumWaypoints();
    }

    inline const AiRoadMap::Vertex &getWaypoint() const {
        return getEntity( IndEntity )->getWaypoint( IndVertex );
    }

    void initAtRoadEnd( AiRoadMap::Road *pOwnerRoad, const bool bStart );

    bool advance( const bool bReverse );
    bool isLast() const;
    bool isFirst() const;
    void makeCurveIt( const bool bNext, AiRoadIt &outIt ) const;
    void computeHandle( const bool bReverse, Eigen::Vector4f &outHandle ) const;
    void computeTangent( Eigen::Vector4f &outTangent ) const;
    bool projectOnAdjacentCurves( const Eigen::Vector4f &point,
                                  Eigen::Vector4f &outPosition,
                                  Eigen::Vector4f &outTangent,
                                  AiRoadIt &outIt ) const;

private:
    bool isEntityEnd() const;
    bool isEntityStart() const;
};
