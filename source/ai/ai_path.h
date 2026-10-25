#pragma once

#include "ai/ai_road_iterator.h"

struct AiCompressedRoadIt {
    // 0 - road way
    // 1 - set when the position is not at a road extremity
    // 2..15 - road ID
    uint32_t RoadInfos = 0;
    float Curve = 0.0f;

    inline void reset() {
        RoadInfos = 0;
        Curve = 0.0f;
    }

    inline uint32_t getRoadID() const { return ( RoadInfos >> 2 ) & 0x3fffu; }
    inline bool getWay() const { return ( RoadInfos & 1u ) != 0; }

    void pack( const AiRoadIt &it );
    void unpack( AiRoadIt &outIt ) const;
};

// One road traversed by a path (From/To in travel order)
struct AiRouteElem {
    AiCompressedRoadIt From;
    AiCompressedRoadIt To;
    AiRouteElem *pNext = nullptr;
    AiRouteElem *pPrevious = nullptr;
    float BoundsMinX = 0.0f;
    float BoundsMinZ = 0.0f;
    float BoundsMaxX = 0.0f;
    float BoundsMaxZ = 0.0f;

    inline void reset() {
        From.reset();
        To.reset();
        pNext = nullptr;
        pPrevious = nullptr;
    }
};

struct AiPathNode {
    static constexpr int32_t kMaxChildren = 16;

    static constexpr int32_t kStartWaypoint = -1;
    static constexpr int32_t kGoalWaypoint = -2;

    int32_t F;
    int32_t G;
    int32_t H;
    AiPathNode *pParent;
    AiPathNode *pNext;
    AiPathNode *pStack;
    int32_t Waypoint;
    // Slot of the edge in the parent junction neighbor list
    int32_t IndexNeighbor;
    int32_t Side;
    int32_t NumChildren;
    AiPathNode *pChildren[kMaxChildren];
};

// Node and route element pools shared by every path
class AiPathPool {
public:
    AiPathPool();
    ~AiPathPool();

    void initialize( const int32_t numNodes, const int32_t numRouteElems );

    AiPathNode *allocNode();
    void freeNode( AiPathNode *pNode );
    AiRouteElem *allocRouteElem();
    void freeRouteElem( AiRouteElem *pElem );

private:
    template <typename T> struct FreeList {
        int32_t Max = 0;
        int32_t Num = 0;
        // Index before the front element
        int32_t First = 0;
        int32_t Last = 0;
        std::vector<T *> Elements;

        void initialize( const int32_t capacity ) {
            Max = capacity;
            Num = 0;
            First = capacity - 1;
            Last = 0;
            Elements.assign( capacity, nullptr );
        }

        T *pop() {
            if ( Num == 0 ) {
                return nullptr;
            }
            First = ( First + 1 ) % Max;
            Num--;
            return Elements[First];
        }

        void push( T *pElement ) {
            if ( Num >= Max ) {
                return;
            }
            Elements[Last] = pElement;
            Num++;
            Last = ( Last + 1 ) % Max;
        }
    };

private:
    bool bInitialized;
    std::vector<AiPathNode> nodes;
    FreeList<AiPathNode> freeNodes;
    std::vector<AiRouteElem> routeElems;
    FreeList<AiRouteElem> freeRouteElems;
};

// Path end point as seen by the junction graph
struct AiPathEndpoint {
    AiRoadIt It;
    float field_0x14 = 0.0f;
    Eigen::Vector4f Position = Eigen::Vector4f( 0.0f, 0.0f, 0.0f, 1.0f );
    int32_t Side = 0;
    int32_t WaypointA = 0;
    int32_t WaypointB = 0;
    // 1 when the link uses an extra slot after the junction neighbors
    int32_t NumExtraA = 0;
    int32_t NumExtraB = 0;
    // Slot of the endpoint road in junction A/B
    int32_t IndexA = 0;
    int32_t IndexB = 0;
    // Endpoint -> junction
    int32_t CostToA = 0;
    int32_t CostToB = 0;
    // Junction -> endpoint
    int32_t CostFromA = 0;
    int32_t CostFromB = 0;

    void buildLinks( const bool bIgnoreOneWay,
                     const bool bReversePenalty,
                     const bool bAllowReverseStart );
    void buildFloodLinks( const bool bBehind, const bool bAhead );
};

struct AiPathGraph {
    AiPathEndpoint Start;
    AiPathEndpoint End;
    bool bIgnoreOneWay = true;
    bool bReversePenalty = true;
    bool bAllowReverseStart = true;
    bool bIgnoreRoadLocks = false;

    void setStart( const AiRoadIt &it, const int32_t side );
    void setEnd( const AiRoadIt &it );

    int32_t getNumStartNodes() const;
    void initStartNode( AiPathNode *pNode );
    int32_t getNumNeighbors( const AiPathNode *pNode ) const;
    int32_t getNeighbor( const AiPathNode *pNode, const int32_t index ) const;
    int32_t computeHeuristic( const int32_t waypoint ) const;
    int32_t computeEdgeCost( const AiPathNode *pNode,
                             const int32_t index,
                             const int32_t toWaypoint,
                             int32_t *pOutSide ) const;
    int32_t computePropagationCost( const AiPathNode *pParent,
                                    const int32_t childWaypoint,
                                    int32_t *pIoIndex,
                                    int32_t *pOutSide ) const;

private:
    void buildEndpoints();
    void buildSameRoadEndpoints();
    bool computePseudoEdgeCost( const int32_t fromWaypoint,
                                const int32_t toWaypoint,
                                int32_t *pOutIndex,
                                int32_t *pOutCost ) const;
    bool computeRoadCost( const AiRoadMap::Road *pRoad,
                          const AiRoadMap::Junction *pFrom,
                          const AiRoadMap::Junction *pTo,
                          const Eigen::Vector4f &toPosition,
                          int32_t *pOutCost ) const;
    int32_t computeTurnCost( const int32_t waypoint,
                             const int32_t parentWaypoint,
                             const int32_t inSlot,
                             const int32_t outSlot ) const;
    bool computeSideTransition( const int32_t toWaypoint,
                                const int32_t fromWaypoint,
                                const int32_t sideIn,
                                const int32_t slot,
                                int32_t *pOutSide ) const;
    AiRoadMap::Road *getEdgeRoad( const int32_t fromWaypoint,
                                  const int32_t slot ) const;
};

// A* over the junction graph
class AiPathSolver {
public:
    enum eStatus : int32_t {
        S_Failed = 1,
        S_OutOfMemory = 2,
        S_Success = 3,
    };

public:
    inline AiPathGraph &getGraph() { return graph; }
    inline AiPathNode *getResult() const { return pResult; }

public:
    AiPathSolver();

    void initialize( AiPathPool *pNodePool );
    void reset();
    int32_t solve();

protected:
    void resetLists();
    void insertSorted( AiPathNode *pNode );
    void pushUnique( AiPathNode *pNode );
    bool propagate( AiPathNode *pNode );

protected:
    AiPathGraph graph;
    AiPathPool *pPool;
    AiPathNode *pOpen;
    AiPathNode *pClosed;
    AiPathNode *pStack;
    AiPathNode *pResult;
};

// Floods the junction graph from a road location until a junction is at
// least the given distance away
class AiPathFloodSolver : public AiPathSolver {
public:
    inline void setDirections( const bool bBehind, const bool bAhead ) {
        bFloodBehind = bBehind;
        bFloodAhead = bAhead;
    }

public:
    AiPathFloodSolver();

    void setStart( const AiRoadIt &it );
    void setRadius( const int32_t newRadius );
    int32_t solve();

private:
    int32_t radius;
    bool bFloodBehind;
    bool bFloodAhead;

private:
    void initStartNode( AiPathNode *pNode );
    bool isGoal( const AiPathNode *pNode ) const;
    int32_t computeHeuristic( const int32_t waypoint ) const;
};

class AiPath {
public:
    inline int32_t getStatus() const { return Status; }
    inline int32_t getTotalDistance() const { return TotalDistance; }
    inline AiRouteElem *getHead() const { return pHead; }
    inline AiRouteElem *getTail() const { return pTail; }
    inline int32_t getNumElems() const { return NumElems; }
    inline const AiRoadIt &getStart() const { return Start; }
    inline const AiRoadIt &getEnd() const { return End; }
    inline int32_t getEndSide() const { return EndSide; }
    inline const Matrix4x4f_t &getStartFrame() const { return StartFrame; }
    inline const Matrix4x4f_t &getEndFrame() const { return EndFrame; }
    inline void setIgnoreOneWay( const bool bIgnore ) {
        bIgnoreOneWay = bIgnore;
    }
    inline void setIgnoreRoadLocks( const bool bIgnore ) {
        bIgnoreRoadLocks = bIgnore;
    }

public:
    AiPath();

    void clear();
    void setStart( const AiRoadIt &it, const int32_t side );
    void setEnd( const AiRoadIt &it );
    void setStartFromFrame( const Matrix4x4f_t &frame );
    void setEndFromFrame( const Matrix4x4f_t &frame );
    int32_t solve( const bool bReversePenalty );
    void computeBounds();
    void append( AiPath &leg, const bool bMergeJoint );
    void popFront();
    inline void setStartFrame( const Matrix4x4f_t &frame ) {
        StartFrame = frame;
    }
    inline void setEndFrame( const Matrix4x4f_t &frame ) { EndFrame = frame; }

private:
    AiPathPool *pPool;
    int32_t Status;
    bool bIgnoreOneWay;
    bool bAllowReverseStart;
    bool bIgnoreRoadLocks;
    Matrix4x4f_t StartFrameSaved;
    Matrix4x4f_t StartFrame;
    AiRoadIt Start;
    int32_t StartSide;
    Matrix4x4f_t EndFrameSaved;
    Matrix4x4f_t EndFrame;
    AiRoadIt End;
    int32_t EndSide;
    AiRouteElem *pHead;
    AiRouteElem *pTail;
    int32_t NumElems;
    // In meters
    int32_t TotalDistance;
};
