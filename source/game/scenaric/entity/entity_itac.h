#pragma once

#include "entity_actor.h"
#include "render/light.h"
#include "render/setup_node.h"

// A render + light Actor
class EntityItac : public EntityActor {
public:
    EntityItac();
    ~EntityItac();

    void initialize() override;
    void unloadResources() override;
    void
    tick( GameMechanism *pGameMech, float deltaTime, float totalTime ) override;
    bool draw( RenderScene *pScene ) override;
    void deserialize( BinaryReader *param_2 ) override;
    void instanciate( MngActor *pMngActor, Entity *param_1 ) override;

private:
    static constexpr int32_t kNumShaderParams = 6;
    static constexpr int32_t kNumLights = 2;

private:
    void refreshLights();
    void bindRenderSetups();
    void unbindRenderSetups();

private:
    ShaderParameterSetupNode shaderSetup;
    std::array<ShaderParameter, kNumShaderParams> shaderParams;
    Eigen::Vector4f shaderParameterScalars;

    LightSetupNode lightSetup;
    DirectionalLight lights[kNumLights];

    bool bDrawAfterCockpit;
    bool bDrawInWorldPass;
};
