#pragma once

#include "core/mutex.h"
#include <unordered_map>

#include <Jolt/Jolt.h>
#include <Jolt/Physics/Body/BodyID.h>

namespace JPH {
    class PhysicsSystem;
}

struct RoadPhantomEvt {
    bool bAdding;
    int32_t RoadID;
    int32_t EntityIndex;
    int32_t CurveIndex;
    void *pObject;
};

class RoadPhantomCallback {
public:
    virtual ~RoadPhantomCallback() = default;

    virtual void onRoadPhantomAdd( const RoadPhantomEvt &evt ) = 0;
    virtual void onRoadPhantomRemove( const RoadPhantomEvt &evt ) = 0;
};

struct RoadPhantom {
    struct Overlap {
        JPH::BodyID BodyID;
        void *pObject;
        int32_t NumContacts;
    };

    JPH::BodyID BodyID;
    const void *pSectorKey;
    int32_t RoadID;
    int32_t EntityIndex;
    int32_t CurveIndex;
    std::vector<Overlap> Overlaps;
};

class RoadPhantomManager {
public:
    RoadPhantomManager();
    ~RoadPhantomManager();

    void initialize( int32_t numMaxPhantoms, int32_t numCallbackSlots );

    void createRoadPhantom( JPH::PhysicsSystem *pPhysicsSystem,
                            const void *pSectorKey,
                            const Eigen::Vector3f &boundsMin,
                            const Eigen::Vector3f &boundsMax,
                            const int32_t roadID,
                            const int32_t entityIndex,
                            const int32_t curveIndex );
    void removeSectorRoadPhantoms( JPH::PhysicsSystem *pPhysicsSystem,
                                   const void *pSectorKey );
    void onOverlapAdded( const JPH::BodyID &phantomID,
                         const JPH::BodyID &bodyID,
                         void *pObject );
    void onOverlapRemoved( const JPH::BodyID &phantomID,
                           const JPH::BodyID &bodyID );

    bool registerCallback( RoadPhantomCallback *pCallback );
    void unregisterCallback( RoadPhantomCallback *pCallback );

    void dispatchEvents();
    void clearEvents() { numRoadPhantomEvents = 0; }

private:
    static constexpr int32_t kNumMaxRoadPhantomEvents = 0x10;

private:
    void queueEvent( const RoadPhantomEvt &evt );

private:
    int32_t numMaxRoadPhantoms;
    int32_t numRoadPhantoms;
    std::vector<RoadPhantom *> roadPhantoms;

    int32_t numRoadPhantomEvents;
    std::vector<RoadPhantomEvt> roadPhantomEvents;

    int32_t numMaxCallbacks;
    int32_t numCallbacks;
    std::vector<RoadPhantomCallback *> callbacks;

    std::unordered_map<uint32_t, int32_t> phantomSlots;

    TestDriveMutex overlapMutex;
};
