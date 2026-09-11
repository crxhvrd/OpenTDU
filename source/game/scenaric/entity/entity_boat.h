#pragma once

#include "entity_actor.h"
#include "render/render_file.h"

#include <memory>

class Instance;
class edAudioSample;
struct InstanceEntry;

class EntityBoat : public EntityActor {
public:
    inline int32_t getBoatType() const { return Type; }
    inline Instance *getRenderInstance() const {
        return pObjectInCommonRenderInstance;
    }
    inline bool hasRenderInstance() const {
        return pObjectInCommonRenderInstance != nullptr;
    }
    inline bool hasInstanceDefChunk() const {
        return pObjectInCommonInstanceDefChunk != nullptr;
    }
    inline void setRenderEcume( bool bEnabled ) { bMustRenderEcume = bEnabled; }
    inline const edAudioSample *getHornSample() const { return pHornSample; }

public:
    EntityBoat();
    ~EntityBoat();

    void initialize() override;
    void loadResources( ScenaricBank *param_2 ) override;
    void unloadResources() override;
    void tick( PlayerGameMech *pGameMech,
               float deltaTime,
               float totalTime ) override;
    bool draw( RenderScene *pScene ) override;
    void deserialize( BinaryReader *param_2 ) override;
    void setTransform( Matrix4x4f_t &transformMatrix, bool param_1 ) override;
    void instanciate( MngActor *pMngActor, Entity *param_1 ) override;
    void deinstanciate() override;

protected:
    int32_t Type;
    uint64_t ObjectInCommonHashcode;
    SerializedSkinnedModel Ecume3DModel;
    void *pHornSound;
    std::unique_ptr<edAudioSample> hornSampleStorage;
    const edAudioSample *pHornSample;
    RenderFile::Section *pObjectInCommonInstanceDefChunk;
    Instance *pObjectInCommonRenderInstance;
    float OceanHeight;
    bool bMustRenderEcume;
};
