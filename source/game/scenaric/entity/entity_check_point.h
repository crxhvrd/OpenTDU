#pragma once

#include "entity_actor.h"

struct SerializedCheckPoint {
    uint64_t ZoneTriggerHashcode = 0;
    uint32_t Type = 0;
    int32_t Index = 0;
    float NoMoreActiveSpeed = 0.0f;
    uint32_t BigCheckpoint = 0;
};
OTDU_SIZE_MUST_MATCH( SerializedCheckPoint, 24 );

// TODO: Move to AI subsystem
struct AIRoadIterator {
    int32_t IndEntity = -1;
    int32_t IndWay = -1;
    bool bWay = true;
    float T = 0.0f;
    void *pRoad = nullptr;
};

class EntityCheckPoint : public EntityActor {
public:
    const char *GetName() override { return "CheckPT"; /* FUN_00423980 */ }

public:
    inline int32_t getIndex() const { return Index; }
    inline const SerializedCheckPoint *getLEDData() const { return pLEDData; }
    inline void setRoadWidth( const float width ) { RoadWidth = width; }
    inline void setIndex( const int32_t index ) { Index = index; }
    inline Entity *getZoneTrigger() const { return pZoneTrigger; }
    inline void resetEventCounters() {
        memset( NbTimesEventReached, 0, sizeof( NbTimesEventReached ) );
        PassedData = 0.0f;
    }

public:
    EntityCheckPoint();
    ~EntityCheckPoint();

    void initialize() override;
    void loadResources( ScenaricBank *param_2 ) override;
    void unloadResources() override;
    void tick( PlayerGameMech *pGameMech,
               float deltaTime,
               float totalTime ) override;
    bool draw( RenderScene *pScene ) override;
    void deserialize( BinaryReader *param_2 ) override;
    void propagateTransform( Matrix4x4f_t &transformMatrix,
                             bool param_1 ) override;
    void propagatePosition( Eigen::Vector4f &position, bool param_1 ) override;
    void instanciate( MngActor *pMngActor, Entity *param_1 ) override;
    void deinstanciate() override;
    int32_t gmeEditionFactory( const int32_t controlIndex,
                               const int32_t request,
                               void *pData ) override;
    void onWorldStreamed() override;

protected:
    static constexpr int32_t kNumGateActors = 2;
    static constexpr int32_t kNumEventSlots = 16;

protected:
    int32_t NbTimesEventReached[kNumEventSlots];
    Entity *pZoneTrigger;
    float PassedData;
    SerializedCheckPoint *pLEDData;
    uint8_t *pSoundGood;
    uint8_t *pSoundBad;
    uint32_t WasIn;
    float RoadWidth;
    int32_t Index;
    SerializedInt32 serializedIndex;
    uint8_t *pBinActors[kNumGateActors];
    Entity *pInstanciatedActors[kNumGateActors];
    AIRoadIterator AIComputedRoadIt;
};
