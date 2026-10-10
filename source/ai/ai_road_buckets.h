#pragma once

#include "physics/road_phantom_manager.h"

class AiRoadMap;
class GameObject;

// Road phantom listener
class AiRoadBuckets : public RoadPhantomCallback {
public:
    // Object overlapping the phantom of a road curve
    struct PhantomNode {
        int32_t RoadID;
        int32_t EntityIndex;
        int32_t CurveIndex;
        GameObject *pObject;
        PhantomNode *pPrevious;
        PhantomNode *pNext;
    };

    struct Bucket {
        int32_t NumObjects;
        GameObject *pFirstObject;
    };

public:
    AiRoadBuckets();
    ~AiRoadBuckets();

    void initialize( AiRoadMap *pMap, RoadPhantomManager *pPhantomManager );
    void terminate();
    void build();

    void insertObject( GameObject *pObject );
    void removeObject( GameObject *pObject );

    PhantomNode *allocNode();
    void freeNode( PhantomNode *pNode );

    void onRoadPhantomAdd( const RoadPhantomEvt &evt ) override;
    void onRoadPhantomRemove( const RoadPhantomEvt &evt ) override;

private:
    static constexpr int32_t kNumPhantomNodes = 0x100;

private:
    Bucket *getBucket( std::vector<Bucket> &buckets, const uint32_t id );

private:
    AiRoadMap *pRoadMap;
    RoadPhantomManager *pPhantomManager;
    std::vector<PhantomNode> phantomNodes;
    PhantomNode *pFreePhantomNodes;
    int32_t numUsedPhantomNodes;
    std::vector<Bucket> roadBuckets;
    std::vector<Bucket> junctionBuckets;
};
