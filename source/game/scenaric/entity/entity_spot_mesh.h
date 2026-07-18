#pragma once

#include "entity_actor.h"

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

protected:
    static constexpr uint32_t kSpotValidFlags = 0x3;

    struct SerializedData {
        int32_t Type; // eSpotType
        int32_t SubType;
    };

protected:
    SerializedData *pData;
    SpotMap *pSpotWorld;
    SpotSector *pSpotSector;
    bool bInZone;
};
