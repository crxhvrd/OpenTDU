#pragma once

#include "ai/ai_road_iterator.h"

class AiVehicle;

template <typename T> struct NavRing {
    int32_t Capacity = 0;
    int32_t Count = 0;
    int32_t Front = -1; // Index before the oldest element
    int32_t Back = 0;   // Next write
    std::vector<T> Data;

    void allocate( const int32_t capacity ) {
        Capacity = capacity;
        Data.assign( static_cast<size_t>( capacity ), T() );
        clear();
    }

    void clear() {
        Count = 0;
        Front = Capacity - 1;
        Back = 0;
    }

    T &at( const int32_t index ) {
        int32_t slot = Front + 1 + index;
        if ( slot >= Capacity ) {
            slot -= Capacity;
        }
        return Data[slot];
    }

    const T &at( const int32_t index ) const {
        int32_t slot = Front + 1 + index;
        if ( slot >= Capacity ) {
            slot -= Capacity;
        }
        return Data[slot];
    }

    T &last() {
        int32_t slot = Back - 1;
        if ( slot < 0 ) {
            slot += Capacity;
        }
        return Data[slot];
    }

    const T &last() const {
        int32_t slot = Back - 1;
        if ( slot < 0 ) {
            slot += Capacity;
        }
        return Data[slot];
    }

    void pushBack( const T &value ) {
        Data[Back] = value;
        Back = ( Back + 1 ) % Capacity;
        Count++;
    }

    void popFront() {
        Front = ( Front + 1 ) % Capacity;
        Count--;
    }
};

// Traffic navigation path
class NavPath {
public:
    enum ePointType : int32_t {
        PT_None = 0,
        PT_Road = 1,
        PT_Junction = 2,
    };

    struct Point {
        int32_t Id = 0;
        Eigen::Vector4f Position = Eigen::Vector4f( 0.0f, 0.0f, 0.0f, 1.0f );
        float Len = 0.0f;       // From the previous point
        float Curvature = 0.0f; // Signed 1 / radius
        float field_0x28 = 0.0f;
        float Radius = -1.0f; // -1 when straight
        float CrestSpeedLimit = -1.0f;
        float RoadSpeedLimit = -1.0f;
        ePointType Type = PT_None;
        AiRoadIt It;
        float field_0x60 = 0.0f;
        const AiRoadMap::Junction *pJunction = nullptr;
        float JunctionLocalX = 0.0f;
        float JunctionLocalZ = 0.0f;
    };

    // Road interval (or junction crossing) sampled into points
    struct Span {
        int32_t FirstPointIndex = 0;
        int32_t LastPointIndex = 0;
        int32_t SampleFirst = 0;
        int32_t SampleLast = 0;
        ePointType Type = PT_None;
        AiRoadMap::Road *pRoad = nullptr;
        const AiRoadMap::Junction *pJunction = nullptr;
        // Bezier keys: (road fraction, width ratio) on a road, (x, z) on a
        // junction
        Eigen::Vector2f Keys[4] = {
            Eigen::Vector2f::Zero(), Eigen::Vector2f::Zero(),
            Eigen::Vector2f::Zero(), Eigen::Vector2f::Zero() };
        float SpeedLimit = -1.0f;
        float EndSpeedLimit = -1.0f;
    };

    struct SpeedSample {
        float Speed;
        float Distance;
    };

public:
    inline int32_t getNumPoints() const { return points.Count; }
    inline Point &getPoint( const int32_t index ) { return points.at( index ); }
    inline const Point &getPoint( const int32_t index ) const {
        return points.at( index );
    }
    inline float getProgress() const { return progress; }
    inline int32_t getNearestIndex() const { return nearestIndex; }
    inline float getTotalLength() const { return totalLength; }
    inline NavRing<Span> &getSpans() { return spans; }

public:
    float computePointSpeed( const Point &point ) const;
    NavPath();

    void initialize( AiVehicle *pOwner );
    void attach( AiVehicle *pOwner );
    void release();
    void reset( const bool bFull );

    Point &front();
    Point &back();
    const Point &front() const;
    const Point &back() const;

    Point &current();
    const Point &current() const;
    bool hasRoom() const;

    void push( Point &point, const bool bForce );
    float popFront( int32_t numPoints );
    void recomputeAllRadii();

    bool appendSpan( const AiRoadIt &from,
                     const AiRoadIt &to,
                     float widthRatioFrom,
                     float widthRatioTo,
                     AiRoadIt *pOutEnd,
                     const float speedLimit );
    bool appendJunctionSpan( const AiRoadIt &from,
                             const float widthRatioFrom,
                             const AiRoadIt &to,
                             const float widthRatioTo,
                             const float speedLimit );
    bool appendLaneChangeSpan( const AiRoadIt &from,
                               const AiRoadIt &to,
                               float widthRatioFrom,
                               float widthRatioTo,
                               const float speedLimit,
                               const float handleDistance );
    bool appendToRoadExit( const AiRoadIt &from,
                           AiRoadIt *pOutEnd,
                           const float widthRatio,
                           const float speedLimit );

    bool locate( const float distance,
                 const Point *&pOutA,
                 const Point *&pOutB,
                 float &outT ) const;
    Eigen::Vector4f computePositionAt( const float distance ) const;
    void computePositionAndTwistAt( const float distance,
                                    Eigen::Vector4f &outPosition,
                                    float &outTwist ) const;

    float computeLookAhead() const;
    float computeCurrentSpeedLimit() const;
    float computeCrestSpeedLimit( const int32_t index ) const;
    void computeSpeedProfile( int32_t &outCount,
                              SpeedSample *pOutSamples,
                              const int32_t maxSamples ) const;
    bool computeTarget( Eigen::Vector4f &outDirection,
                        float &outSpeed,
                        float maxSpeed,
                        const float blend ) const;

    void updateProgress( const Matrix4x4f_t &groundMatrix );

private:
    int32_t pointIdCounter;
    NavRing<Point> points;
    Point dummyPoint;
    float totalLength;
    bool bBatchAppend;
    AiVehicle *pVehicle;
    int32_t nearestIndex;
    float progress;
    NavRing<Span> spans;

private:
    float popFrontPoints( int32_t numPoints );
    bool generatePoints( const bool bKeepIds, const Span &span );
    int32_t getNextSpanFirstIndex() const;
    void onPointAdded();
    void recomputeRadius( const int32_t index );
    float computeCurvature( const int32_t index, const bool bSigned ) const;
    bool computeOffsetPosition( int32_t index,
                                const float distance,
                                Eigen::Vector4f &outPosition ) const;
    Eigen::Vector4f computeTangent( const int32_t index ) const;
};
