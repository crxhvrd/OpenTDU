#pragma once

struct RoadPhantom;

class RoadPhantomCallback {
public:
    virtual ~RoadPhantomCallback() = default;

    virtual void onRoadPhantomAdd( const struct RoadPhantomEvt &evt ) = 0;
    virtual void onRoadPhantomRemove( const struct RoadPhantomEvt &evt ) = 0;
};

struct RoadPhantomEvt {
    bool bAdding;
    int32_t RoadID;
    int32_t EntityIndex;
    int32_t CurveIndex;
    void *pObject;
};

class RoadPhantomManager {
public:
    RoadPhantomManager();
    ~RoadPhantomManager();

    void initialize( int32_t param_1, int32_t param_3 );

    void dispatchEvents();
    void clearEvents() { numRoadPhantomEvents = 0; }

private:
    static constexpr int32_t kNumMaxRoadPhantomEvents = 0x10;

private:
    int32_t numMaxRoadPhantoms;
    int32_t numRoadPhantoms;
    std::vector<RoadPhantom *> roadPhantoms;

    int32_t numRoadPhantomEvents;
    std::vector<RoadPhantomEvt> roadPhantomEvents;

    int32_t numMaxCallbacks;
    int32_t numCallbacks;
    std::vector<RoadPhantomCallback *> callbacks;
};
