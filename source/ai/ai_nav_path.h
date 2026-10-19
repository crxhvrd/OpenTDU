#pragma once

#include "ai/ai_road_iterator.h"
#include "core/cubic_bezier.h"

class AiVehicle;

bool ComputeCircle( Eigen::Vector2f p1,
                    Eigen::Vector2f p2,
                    Eigen::Vector2f p3,
                    Eigen::Vector2f &outCenter,
                    float &outRadius );

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
        PT_End = 3, // Junction curve point without junction (opponent path)
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
        float WidthRatio = 0.0f; // -1024 on junction points
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
    virtual ~NavPath() = default;

    virtual void initialize( AiVehicle *pOwner );
    void attach( AiVehicle *pOwner );
    virtual void release();
    virtual void reset( const bool bFull );

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
    virtual void computeSpeedProfile( int32_t &outCount,
                                      SpeedSample *pOutSamples,
                                      const int32_t maxSamples ) const;
    bool computeTarget( Eigen::Vector4f &outDirection,
                        float &outSpeed,
                        float maxSpeed,
                        const float blend ) const;

    void updateProgress( const Matrix4x4f_t &groundMatrix );

protected:
    int32_t pointIdCounter;
    NavRing<Point> points;
    Point dummyPoint;
    float totalLength;
    bool bBatchAppend;
    AiVehicle *pVehicle;
    int32_t nearestIndex;
    float progress;
    NavRing<Span> spans;

protected:
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

// Opponent navigation path
class NavPathOpponent : public NavPath {
public:
    struct Bend {
        float Radius = 0.0f;
        float Angle = 0.0f;
        int32_t FirstIndex = 0;
        int32_t PeakIndex = 0;
        int32_t LastIndex = 0;
    };

public:
    inline bool isFull() const { return points.Count == points.Capacity; }
    inline int32_t getCapacity() const { return points.Capacity; }
    inline AiVehicle *getVehicle() const { return pVehicle; }
    inline void setBatchAppend( const bool bValue ) { bBatchAppend = bValue; }
    inline const NavRing<Bend> &getBends() const { return bends; }

public:
    NavPathOpponent();

    void initialize( AiVehicle *pOwner ) override;
    void release() override;
    void reset( const bool bFull ) override;

    void popBack();
    void pushRoadPoint( const AiRoadIt &it,
                        const float widthRatio,
                        float speedLimit,
                        const bool bForce );
    bool appendUTurn( const AiRoadIt &it,
                      const float widthFrom,
                      const float widthTo );
    bool appendRoadInterval( const AiRoadIt &from,
                             const AiRoadIt &to,
                             const float widthRatioFrom,
                             const float widthRatioTo,
                             const float speedLimit,
                             AiRoadIt *pOutEnd );
    bool appendRoadBezier( const AiRoadIt &itA,
                           const AiRoadIt &itB,
                           Eigen::Vector2f key0,
                           Eigen::Vector2f key1,
                           Eigen::Vector2f key2,
                           Eigen::Vector2f key3,
                           const Eigen::Vector4f &positionA,
                           const Eigen::Vector4f &positionB );
    bool appendJunctionCurve( const AiRoadMap::Junction *pJunction,
                              const Eigen::Vector4f &p0,
                              const Eigen::Vector4f &p1,
                              const Eigen::Vector4f &p2,
                              const Eigen::Vector4f &p3 );
    void rebuildBends() const;

    void computeSpeedProfile( int32_t &outCount,
                              SpeedSample *pOutSamples,
                              const int32_t maxSamples ) const override;

private:
    mutable NavRing<Bend> bends;
    int32_t field_0x108;
    int32_t field_0x10c;
};

float ComputeBezierBend( const CubicBezier &curve );
