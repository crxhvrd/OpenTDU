#pragma once

#include "ai/ai_nav_path.h"
#include "ai/ai_path_optimiser.h"
#include "ai/ai_road_iterator.h"

class AiVehicle;
class AiVehNavOpponent;
class OptPath;

// Racing line element
struct OptPathElem {
    enum eType : int32_t {
        ET_None = 0,
        ET_Road = 1,
        ET_Junction = 2,
        ET_Empty = 3, // Only in the situation copy of an empty line
    };

    enum eMarker : int32_t {
        EM_None = 0,
        EM_BeforeUTurn = 1,
        EM_AfterUTurn = 2,
        EM_End = 3, // Last element of the controller route
        EM_JunctionUTurn = 4,
    };

    int32_t Id = 0;
    Eigen::Vector4f Position = Eigen::Vector4f::Zero();
    float Len = 0.0f;
    float Curvature = 0.0f;
    float field_0x28 = 0.0f;
    float Radius = -1.0f;
    float CrestSpeedLimit = -1.0f;
    eType Type = ET_None;
    Eigen::Vector4f OptPosition = Eigen::Vector4f::Zero(); // Zero when unset
    AiRoadIt It;
    float FrameX = 0.0f; // Lateral axis (unit, XZ)
    float FrameZ = 0.0f;
    float BasePosX = 0.0f;
    float BasePosZ = 0.0f;
    float LateralLimits[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    const AiRoadMap::Junction *pJunction = nullptr;
    float WidthRatio = 0.0f;
    float OptWidthRatio = -1024.0f; // -1024 when unset
    float JunctionLocalX = 0.0f;
    float JunctionLocalZ = 0.0f;
    int32_t RouteIndex = -1;
    float TurningSpeed = 0.0f;
    eMarker Marker = EM_None;
};

// Subset of the opponent navigation "situation"
struct OptPathSituation {
    bool bLineMismatch = false;       // nav+0x4d0 (FUN_00953800)
    bool bBike = false;               // nav+0x4d1 (FUN_00955640)
    AiRoadIt RouteFrom;               // nav+0x4d4
    AiRoadIt RoadIt;                  // nav+0x524
    AiRoadIt LastValidRoadIt;         // nav+0x560
    float LastPointWidthRatio = 0.0f; // nav+0x598 (FUN_00954780)
    float LastWidthRatio = 0.0f;      // nav+0x59c (FUN_00953930)
    float LineDistance = 0.0f;        // nav+0x5a8
    float CurSpeed = 0.0f;            // nav+0x5ac
    uint32_t LocationFlags = 0;       // nav+0x5c8 (& 3: 1 road, 2 junction)
    uint32_t LocationID = 0;          // nav+0x5cc
    Eigen::Vector4f Position =
        Eigen::Vector4f( 0.0f, 0.0f, 0.0f, 1.0f ); // nav+0x610
    NavPath::Point LastPathPoint;                  // nav+0x620 (FUN_00954780)
    OptPathElem Last;                              // nav+0x690 (FUN_00953930)
    AiVehicle *pVehicle = nullptr;                 // nav+0x754
    OptPath *pOptPath = nullptr;                   // nav+0x758
    NavPathOpponent *pPath = nullptr;              // nav+0x75c

    bool isBikeRiderDown() const;
    inline bool isOnRoad() const { return ( LocationFlags & 3 ) == 1; }
    inline bool isInJunction() const { return ( LocationFlags & 3 ) == 2; }

    void refreshLast();
    void refreshLastPathPoint();
    void checkLineMismatch();
};

// Pair of racing line elements and the inner keys of the curve between them
// (road fraction, width ratio)
struct OptSpanKeys {
    int32_t IndexA = 0;
    int32_t IndexB = 0;
    Eigen::Vector2f KeyA = Eigen::Vector2f::Zero();
    Eigen::Vector2f KeyB = Eigen::Vector2f::Zero();
    Eigen::Vector2f Handle1 = Eigen::Vector2f( -1.0f, -10.0f );
    Eigen::Vector2f Handle2 = Eigen::Vector2f( -1.0f, -10.0f );
};

// Same as OptSpanKeys in world space
struct OptSpanHandles {
    int32_t IndexA = 0;
    int32_t IndexB = 0;
    Eigen::Vector4f PositionA = Eigen::Vector4f::Zero();
    Eigen::Vector4f PositionB = Eigen::Vector4f::Zero();
    Eigen::Vector4f Handle1 = Eigen::Vector4f::Zero();
    Eigen::Vector4f Handle2 = Eigen::Vector4f::Zero();
};

// Racing line
class OptPath {
public:
    static constexpr int32_t kCapacity = 0x20;
    static constexpr float kUnsetWidthRatio = -1024.0f; // DAT_00f417f8

public:
    inline int32_t getCount() const { return elems.Count; }
    inline int32_t getCapacity() const { return elems.Capacity; }
    inline bool isFull() const { return elems.Count == elems.Capacity; }
    inline OptPathElem &at( const int32_t index ) { return elems.at( index ); }
    inline const OptPathElem &at( const int32_t index ) const {
        return elems.at( index );
    }
    inline int32_t getCurrentIndex() const { return currentIndex; }
    inline float getTotalLength() const { return totalLength; }
    inline int32_t getLastSentId() const { return lastSentId; }
    inline void setLastSentId( const int32_t id ) { lastSentId = id; }
    inline int32_t getResampleCursor() const { return resampleCursor; }
    inline void setResampleCursor( const int32_t index ) {
        resampleCursor = index;
    }
    inline void setField0x128( const int32_t value ) { field_0x128 = value; }
    inline void setBatchAppend( const bool bValue ) { bBatchAppend = bValue; }
    inline AiVehicle *getVehicle() const { return pVehicle; }
    inline OptPathSituation *getSituation() const { return pSituation; }

    static const Eigen::Vector4f &
    GetEffectivePosition( const OptPathElem &elem );

public:
    OptPath();

    void attach( AiVehNavOpponent *pNav,
                 AiVehicle *pVehicle,
                 OptPathSituation *pSituation );
    void release();
    void reset( const bool bFull );

    OptPathElem &front();
    OptPathElem &back();
    const OptPathElem &front() const;
    const OptPathElem &back() const;

    float popFront( int32_t numElems );
    void popBack();
    void push( OptPathElem &elem, const bool bForce, const int32_t routeIndex );
    void seed( const AiRoadIt &pos,
               const int32_t routeIndex,
               const float widthRatio,
               const bool bForce );
    void pushUTurn( const AiRoadIt &it,
                    const int32_t routeIndexA,
                    const int32_t routeIndexB,
                    const float widthRatioA,
                    const float widthRatioB );
    bool appendRoadStretch( const AiRoadIt &from,
                            const AiRoadIt &to,
                            const int32_t routeIndex,
                            const float widthRatioFrom,
                            const float widthRatioTo );
    void onRoutePopped();
    void clearOptimisation( const int32_t first, const int32_t last );
    void blendLateral( const int32_t first,
                       const int32_t last,
                       const float widthFirst,
                       const float widthLast );
    void updateCurrentIndex( float *pOutLineDistance );
    void recomputeAllRadii();
    int32_t findLastOnSameRoad( const int32_t start,
                                const float maxDistance,
                                float *pOutDistance ) const;
    void setLateralTarget( const int32_t first,
                           const int32_t last,
                           const float widthRatio );
    void driftBackToLine();

    void computeSpanKeys( OptSpanKeys &keys ) const;
    void computeSpanHandles( OptSpanHandles &handles ) const;

private:
    int32_t idCounter;
    NavRing<OptPathElem> elems;
    OptPathElem dummyElem;
    int32_t field_0x100;
    int32_t field_0x104;
    float totalLength;
    bool bBatchAppend;
    int32_t lastSentId;
    int32_t resampleCursor;
    int32_t field_0x128;
    AiVehNavOpponent *pNav;
    AiVehicle *pVehicle;
    OptPathSituation *pSituation;
    int32_t currentIndex;

private:
    void pushRaw( OptPathElem &elem, const bool bForce );
    void onElemAdded();
    void recomputeRadius( const int32_t index );
    float computeCurvature( const int32_t index, const bool bSigned ) const;
    bool computeOffsetPosition( int32_t index,
                                const float distance,
                                Eigen::Vector4f &outPosition ) const;
    bool isCarBeforeRoadElem( const int32_t index ) const;
    bool isCarInsideUTurn( const int32_t index ) const;
    bool isCarBeforeElemXZ( const int32_t index ) const;
    void computeLineDistance( float *pOutDistance ) const;
    bool getNeighbourKey( const OptSpanKeys &keys,
                          Eigen::Vector2f &outKey,
                          const bool bNext ) const;
    Eigen::Vector2f getNeighbourPositionXZ( const OptSpanHandles &handles,
                                            const bool bNext ) const;
    void updateElemFrame( const int32_t index );
    void updateLateralLimits( const int32_t index, const bool bWiden );
    void applyDriftOffset( const int32_t index,
                           const float distance,
                           const bool bLeft );
    float computeDriftOffset( const int32_t step,
                              const float distanceStep,
                              const bool bLeft ) const;
};

// Fills the racing line from the controller route and resamples it into the
// navigation path
class OptPathBuilder {
public:
    OptPathBuilder();

    void attach( AiVehNavOpponent *pNav,
                 AiVehicle *pVehicle,
                 OptPath *pLine,
                 NavPathOpponent *pPath,
                 OptPathSituation *pSituation );
    void detach();
    void reset( const bool bFull = false );

    void fillOnly();
    void optimise();
    void resample();
    void extendAlongRoadSide();
    void computeUTurnWidthRatios( const AiRoadIt &it,
                                  float &outWidthRatioFrom,
                                  float &outWidthRatioTo,
                                  float *pOutWidth ) const;

private:
    AiPathOptimiserSlot localSlot;
    int32_t solverSlot;
    int32_t optimiserIteration;
    OptPath *pLine;
    NavPathOpponent *pPath;
    AiVehNavOpponent *pNav;
    AiVehicle *pVehicle;
    float halfWidth;
    OptPathSituation *pSituation;

private:
    void pullOptimiserResult();
    void submitToOptimiser();
    void buildSlot( AiPathOptimiserSlot &slot ) const;
    void applyResult( const AiPathOptimiserSlot &slot, const int32_t start );
    void extendSimple();
    void extendFull();
    bool extendAlongRoad( const AiRoadIt &from,
                          const AiRoadIt &to,
                          const int32_t routeIndex,
                          const float widthRatio );
    bool finishRoadAndCross( const AiRoadIt &last,
                             const AiRoadIt &nextFrom,
                             const int32_t routeIndex );
    bool appendJunction( const AiRoadIt &last,
                         const Eigen::Vector4f &startPosition,
                         const AiRoadIt &next,
                         const int32_t routeIndex,
                         const float widthRatio );
    float computeWidthRatio( Eigen::Vector4f *pOutPosition,
                             const AiRoadIt &it );
    bool isCarInJunctionOf( const AiRoadIt &from ) const;
    float computeDefaultWidthRatio( const AiRoadIt &it ) const;
    float computeRoadSideWidthRatio( const AiRoadIt &it ) const;
    void pickNextRoadEntry( AiRoadIt &outEntry, float &outLength );
    void appendTurnAround( const AiRoadIt &last, const int32_t routeIndex );
};

bool ComputeOwnSideCentreRatio( const AiRoadIt &it, float &outRatio );
bool ComputeWidthRatioAt( const AiRoadIt &it,
                          const Eigen::Vector4f &position,
                          float &outRatio );
bool IsBeforeInWay( AiRoadIt a, AiRoadIt b, const bool bWay );
