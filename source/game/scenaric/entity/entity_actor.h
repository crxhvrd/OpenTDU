#pragma once

#include "base_entity.h"
#include "render/serialized_skinned_model.h"
#include "render/animation/animation.h"

class EntityActor : public Entity {
public:
    const char *GetName() override { return "Actor"; /* FUN_00423f40 */ }

public:
    EntityActor();
    ~EntityActor();

    bool onMessage( FlashMessage *pMessage ) override;
    void initializeForCinematicKeyframed( void *param_2,
                                          void *param_3 ) override;
    void initialize() override;
    void loadResources( ScenaricBank *param_2 ) override;
    void initializeInstance( void *param_2, void *param_3 ) override;
    void unloadResources() override;
    void tick() override;
    bool draw( RenderScene *pScene ) override;
    void deserialize( BinaryReader *param_2 ) override;
    void setTransform( Matrix4x4f_t &transformMatrix, bool param_1 ) override;
    void setPosition( Eigen::Vector4f &position, bool param_1 ) override;
    void propagateTransform( Matrix4x4f_t &transformMatrix,
                             bool param_1 ) override;
    void propagatePosition( Eigen::Vector4f &position, bool param_1 ) override;
    void instanciate( MngActor *pMngActor, Entity *param_1 ) override;
    void deinstanciate() override;
    void onWorldStreamed() override;
    void *getGeometryData() override;
    void setColor( ColorRGBA color ) override;
    void onGeometrySubmitted() override;
    bool isPMIInTile( const SectorHeightmap *pHeightmap,
                      int32_t tileKey ) override;
    int32_t refreshMechanismRender(
        const std::vector<std::pair<uint64_t, void *>> &members ) override;
    bool refreshModelFromTemplate() override;

    // Vtable entries specific to actor entities
    virtual void *getAnimation();
    virtual void relight();
    virtual void updateSkinningMatrices();
    virtual void unknownActorFunc();

private:
    bool meshSet( ScenaricBank *pBank );

protected:
    bool bRefreshAnimationThisFrame;
    float DeltaTime;
    void *pLightingContext;

    SerializedSkinnedModel Model;
    RenderAnm AnimStand;
    RenderBas Skeleton;
    Animation animation;
    int32_t NumMaxLayers;
    int32_t LightingMode;
    bool bLightingValid;
    bool bMustProjectOnHMap;
    bool bMustProjectOnRoad;
};
