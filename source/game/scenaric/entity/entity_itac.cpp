#include "shared.h"
#include "entity_itac.h"

#include "filesystem/binary.h"
#include "game/gm_scene.h"
#include "game/scenaric/gs_scenaric.h"
#include "game/scenaric/mng_actor.h"
#include "render/gs_render.h"
#include "render/hiearchy_node.h"
#include "render/instance.h"
#include "render/render_scene.h"
#include "world/weather/gs_weather.h"

static constexpr uint32_t kItacStaticParameterID = 0xdf;

static constexpr uint64_t kItacShaderHashcodes[6] = {
    0x183157b81c6c7357, 0x1b38348331d3942a, 0xc288dfc455ae5ef8,
    0xb39c35015789e65c, 0x69f07f72719bf862, 0xcab12190a00d5c4e };

static constexpr float kItacScalar0 = 0.7f; // DAT_00f41828
static constexpr float kItacScalar2 = 1.0f; // FLOAT_00f4165c

static inline bool IsGameModeRequestingDOFPass() {
    return gpActiveGMScene != nullptr && false;
}

static inline bool IsIntroCockpitSequenceRunning() {
    OTDU_UNIMPLEMENTED_ONCE; // TODO: GMIntro is not ported yet.
    return false;
}

static inline bool IsRenderingCockpitPass() {
    return gpActiveGMScene != nullptr &&
           gpActiveGMScene->getActorManager().isRenderingInCockpitPass();
}

static inline void UnbindSetupsFromInstance( Instance *pInstance,
                                             SetupNode *pShaderSetup,
                                             SetupNode *pLightSetup ) {
    if ( pInstance->getSetupRW().findShaderParameterNode() == nullptr ) {
        return;
    }

    pInstance->removeSetupFromGraph( pShaderSetup );
    pInstance->removeSetupFromGraph( pLightSetup );
}

EntityItac::EntityItac()
    : EntityActor(),
      shaderSetup(),
      shaderParams(),
      shaderParameterScalars( Eigen::Vector4f::Zero() ),
      lightSetup(),
      lights{ DirectionalLight(), DirectionalLight() },
      bDrawAfterCockpit( false ),
      bDrawInWorldPass( true ) {
    // FUN_009c8260
    shaderSetup.setFlags( 0x103 );
    shaderSetup.bindParameters( nullptr, 0 );
}

EntityItac::~EntityItac() {
    // FUN_00425850
}

void EntityItac::refreshLights() {
    // FUN_009c8360
    if ( gpWeather->isConfigDirty() ) {
        gpWeather->updateActiveConfig();
    }

    const WeatherConfig &config = gpWeather->getActiveWeatherConfig();

    const Eigen::Vector4f ambient = config.getSunAmbientColor();
    const Eigen::Vector4f diffuse = config.getSunDiffuseColor();
    const Eigen::Vector4f specular = config.getSunSpecularColor();
    const Eigen::Vector3f direction = config.getSunDirection();

    for ( int32_t i = 0; i < kNumLights; i++ ) {
        lights[i].Ambient = ambient;
        lights[i].Diffuse = diffuse;
        lights[i].Specular = specular;
        lights[i].Direction = direction;
    }
}

void EntityItac::bindRenderSetups() {
    // FUN_009c84c0
    refreshLights();

    lightSetup.addLight( &lights[0] );
    lightSetup.addLight( &lights[1] );

    shaderParameterScalars =
        Eigen::Vector4f( kItacScalar0, 0.0f, kItacScalar2, 0.0f );

    for ( int32_t i = 0; i < kNumShaderParams; i++ ) {
        shaderParams[i].ShaderHashcode = kItacShaderHashcodes[i];
        shaderParams[i].pMaterial = nullptr;
        shaderParams[i].ParameterID = kItacStaticParameterID;
        shaderParams[i].pParameters = shaderParameterScalars.data();
        shaderParams[i].NumParameters = 1;
        shaderParams[i].bBindToPixelShader = false;
    }

    shaderSetup.bindParameters( shaderParams.data(), kNumShaderParams );

    SerializedSkinnedModel *pModel =
        static_cast<SerializedSkinnedModel *>( getGeometryData() );

    if ( pModel->pInstance != nullptr ) {
        pModel->pInstance->addSetupToGraph( &shaderSetup );
        pModel->pInstance->addSetupToGraph( &lightSetup );
        return;
    }

    for ( HiearchyNode *pNode = pModel->pHiearchNode; pNode != nullptr;
          pNode = pNode->pNext ) {
        if ( pNode->pInstance != nullptr ) {
            pNode->pInstance->addSetupToGraph( &shaderSetup );
            pNode->pInstance->addSetupToGraph( &lightSetup );
        }
    }
}

void EntityItac::unbindRenderSetups() {
    // FUN_009c81b0
    SerializedSkinnedModel *pModel =
        static_cast<SerializedSkinnedModel *>( getGeometryData() );

    if ( pModel->pInstance != nullptr ) {
        UnbindSetupsFromInstance( pModel->pInstance, &shaderSetup,
                                  &lightSetup );
    } else {
        for ( HiearchyNode *pNode = pModel->pHiearchNode; pNode != nullptr;
              pNode = pNode->pNext ) {
            if ( pNode->pInstance != nullptr ) {
                UnbindSetupsFromInstance( pNode->pInstance, &shaderSetup,
                                          &lightSetup );
            }
        }
    }

    lightSetup.removeLight( &lights[0] );
    lightSetup.removeLight( &lights[1] );
}

void EntityItac::initialize() {
    // FUN_009c8830
    EntityActor::initialize();
    bindRenderSetups();
}

void EntityItac::unloadResources() {
    // FUN_009c86e0
    unbindRenderSetups();
    EntityActor::unloadResources();
}

void EntityItac::tick() {
    // FUN_009c8800
    EntityActor::tick();
    refreshLights();
}

bool EntityItac::draw( RenderScene * ) {
    // FUN_009c8730
    const bool bCockpitPass = IsRenderingCockpitPass();
    const bool bDOFPass = IsGameModeRequestingDOFPass();

    eRenderPass targetPass = eRenderPass::RP_Near;
    if ( bCockpitPass && !bDrawInWorldPass ) {
        targetPass = ( bDrawAfterCockpit && IsIntroCockpitSequenceRunning() )
                         ? eRenderPass::RP_PostCockpit
                         : eRenderPass::RP_Cockpit;
    } else {
        const bool bUseDOFPass =
            bCockpitPass ? bDOFPass : ( bDOFPass && bDrawInWorldPass );
        targetPass =
            bUseDOFPass ? eRenderPass::RP_FakeDOF : eRenderPass::RP_Near;
    }

    if ( ( flags & 1 ) == 0 || ( flags & 4 ) == 0 ) {
        return false;
    }

    if ( !bLightingValid ) {
        updateSkinningMatrices();
    }

    Model.draw( gpRender->getRenderPassAtIndex( targetPass ).pScene );
    return true;
}

void EntityItac::deserialize( BinaryReader *param_2 ) {
    // FUN_009c8310
    EntityActor::deserialize( param_2 );

    bDrawAfterCockpit = *reinterpret_cast<uint32_t *>( param_2->pCursor ) != 0;
    param_2->pCursor += sizeof( uint32_t );

    if ( gpScenaric->getVersion() > 0x22 ) {
        bDrawInWorldPass =
            *reinterpret_cast<uint32_t *>( param_2->pCursor ) != 0;
        param_2->pCursor += sizeof( uint32_t );
    }
}

void EntityItac::instanciate( MngActor *pMngActor, Entity *param_1 ) {
    // FUN_009c82e0
    EntityActor::instanciate( pMngActor, param_1 );

    EntityItac *pTarget = static_cast<EntityItac *>( param_1 );
    pTarget->bDrawAfterCockpit = bDrawAfterCockpit;
    pTarget->bDrawInWorldPass = bDrawInWorldPass;
}
