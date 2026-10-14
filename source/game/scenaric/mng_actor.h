#pragma once

#include "game/mng_base.h"
#include "world/gs_world_streaming.h"

class RenderScene;
class Entity;
class GameMechanism;
class PlayerGameMech;
class SceneryBinary;

class MngActor : public Manager, public StreamingCallback {
public:
    const char *getName() const override {
        return "MngActor"; /* FUN_0042b8e0 */
    }

    inline int32_t getActorCount() const { return numActors; }
    inline Entity *getActor( int32_t index ) const { return pActors[index]; }
    Entity *findActorByName( const char *pName ) const;
    inline void setIsManager( bool bEnabled ) { bIsManager = bEnabled; }
    inline void setParentScenery( SceneryBinary *pScenery ) {
        pParent = pScenery;
    }
    inline bool isRenderingInCockpitPass() const {
        return bRenderInCockpitRenderPass;
    }
    inline void setGameMech( PlayerGameMech *pMech ) { pGameMech = pMech; }
    inline PlayerGameMech *getGameMech() const { return pGameMech; }

public:
    MngActor();
    ~MngActor();

    void tick( float deltaTime, float totalTime ) override;
    void draw() override;
    void terminate() override;

    virtual RenderScene *getRenderPass();

    bool onWorldStreamed( Eigen::Vector4f *param_1,
                          Eigen::Vector4f *param_2,
                          Eigen::Vector4f *param_3 ) override;

    Entity *instanciateActor( Entity *pEntity );
    Matrix4x4f_t placePlayerVehicle( int32_t spawnMode, bool bApply );
    static void applyPlayerVehicleFrame( const Matrix4x4f_t &frame,
                                         bool bApply );

    void registerActor( Entity *pActor );
    void removeActorFromList( Entity *pActor );
    int32_t refreshMissingModels();

    // NOTE: the original takes the owning MngActor explicitly and tolerates a
    // null manager (entities not owned by an MngActor).
    static void Uninstanciate( MngActor *pMngActor, Entity *pActor );

protected:
    static constexpr int32_t kMaxNumEntity = 1024;

protected:
    int32_t numActors;
    std::array<Entity *, kMaxNumEntity> pActors;
    SceneryBinary *pParent;
    PlayerGameMech *pGameMech;

    uint8_t bIsManager : 1;
    uint8_t bRenderInCockpitRenderPass : 1;
};

class MngActorWorld : public MngActor {
public:
    MngActorWorld() = default;
    ~MngActorWorld() = default;

    RenderScene *getRenderPass() override;
};