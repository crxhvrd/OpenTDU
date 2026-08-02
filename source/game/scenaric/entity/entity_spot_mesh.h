#pragma once

#include "entity_actor.h"
#include "entity_volume.h"

struct SpotMap;
struct SpotSector;

class EntitySpotMesh : public EntityActor {
public:
    const char *GetName() override { return "SpotMesh"; /* FUN_00424210 */ }

    int32_t getTemplateType() const {
        return pData != nullptr ? pData->Type : -1;
    }
    int32_t getTemplateSubType() const {
        return pData != nullptr ? pData->SubType : -1;
    }

    void bindSpot( SpotMap *pWorld, SpotSector *pSector ) {
        pSpotWorld = pWorld;
        pSpotSector = pSector;
    }

    SpotMap *getSpotWorld() const { return pSpotWorld; }
    bool isPlayerInZone() const { return bInZone; }

    void setSpotValid( bool bValid ) {
        // FUN_007d6b80
        if ( bValid ) {
            flags |= kSpotValidFlags;
        } else {
            flags &= ~kSpotValidFlags;
        }
    }

public:
    EntitySpotMesh();
    ~EntitySpotMesh();

    void unloadResources() override;
    void tick() override;
    void deserialize( BinaryReader *param_2 ) override;

    void instanciate( MngActor *pMngActor, Entity *param_1 ) override;
    void setupZoneVolumes();

protected:
    static constexpr uint32_t kSpotValidFlags = 0x3;
    static constexpr uint32_t kSpotAvailableFlag = 0x1;
    static constexpr uint32_t kSpotCandidateFlag = 0x2;
    static constexpr float kRoadZoneExtentX = 25.0f;
    static constexpr float kRoadZoneExtentY = 5.0f;
    static constexpr float kRoadZoneExtentZ = 25.0f;

    enum eSpotZoneEvent : int32_t {
        SZE_Entered = 4,
        SZE_Left = 7,
        SZE_Unloaded = 9,
    };

    struct SerializedData {
        int32_t Type; // eSpotType
        int32_t SubType;
    };

protected:
    void notifySpotZoneEvent( eSpotZoneEvent eventCode );

protected:
    SerializedData *pData;
    EntityVolume volumeRoad;
    EntityVolume volumeSpot;
    SpotMap *pSpotWorld;
    SpotSector *pSpotSector;
    bool bInZone;
};
