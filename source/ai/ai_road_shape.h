#pragma once

#include "ai/ai_road_buckets.h"
#include "ai/ai_road_iterator.h"

struct AiRoadShape {
    struct CurveCalc {
        int32_t IndEntity;
        int32_t IndCurve;
        float Length;
        CurveCalc *pNext;
    };

    uint32_t LastUse = 0;
    AiRoadMap::Road *pRoad = nullptr;
    CurveCalc *pCurveList = nullptr;
    int32_t NumVertex = 0;
    int32_t NumWaypoints = 0;
    int32_t NumCurves = 0;
    float TotalLength = 0.0f;
    // Objects overlapping the road phantoms, sorted by (entity, curve)
    AiRoadBuckets::PhantomNode *pFirstPhantom = nullptr;
    AiRoadBuckets::PhantomNode *pLastPhantom = nullptr;
    int32_t NumPhantoms = 0;

    void init( AiRoadMap::Road *pOwnerRoad );
    void clear();
    float computeRoadFraction( const AiRoadIt &it ) const;
    float computeUniformRoadFraction( const AiRoadIt &it ) const;
    void computeIt( const float fraction, AiRoadIt &outIt ) const;
    void computeUniformIt( float fraction, AiRoadIt &outIt ) const;
    void addPhantom( GameObject *pObject,
                     const int32_t entityIndex,
                     const int32_t curveIndex,
                     const int32_t roadID );
    void removePhantom( const GameObject *pObject,
                        const int32_t entityIndex,
                        const int32_t curveIndex );
    void computeLateralPosition( const float fraction,
                                 const float lateralRatio,
                                 Eigen::Vector4f &outPosition ) const;

private:
    void buildCurveList();
};

extern uint32_t gRoadShapeUseCounter;
extern bool gbUniformRoadFraction;

float ComputeRoadDistance( const AiRoadShape *pShape,
                           const bool bCovered,
                           const AiRoadIt &it );

void ProjectOnRoadShape( const Matrix4x4f_t &frame,
                         AiRoadIt &io,
                         Matrix4x4f_t *pOutFrame,
                         const bool bBank,
                         const AiRoadShape *pShape );
