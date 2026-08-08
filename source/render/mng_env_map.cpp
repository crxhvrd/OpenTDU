#include "shared.h"
#include "mng_env_map.h"

#include "render/ed_render.h"
#include "render/frame_graph.h"
#include "render/gs_render.h"
#include "render/material.h"
#include "render/render_api.h"
#include "render/render_scene.h"
#include "render/render_target.h"

#include "render/camera/camera_game.h"
#include "render/camera/gs_camera.h"

#include "render/postfx/gs_postfx.h"
#include "render/postfx/effects/postfx_cubemap_to_sphere.h"
#include "render/postfx/effects/postfx_envmap_scene.h"
#include "render/postfx/effects/postfx_texture_sample.h"

#include "game/vehicle/car_state.h"
#include "game/vehicle/gs_car_state.h"
#include "game/vehicle/vehicle_base.h"

#include "world/gs_mng_resources.h"
#include "world/gs_world.h"
#include "world/mng_world.h"
#include "world/streaming_manager.h"
#include "world/weather/gs_weather.h"

#include "core/maths_helper.h"

#ifdef OTDU_USE_IMGUI
#include "debug/gs_imgui.h"
#include "imgui.h"
#endif

MngEnvMap *gpMngEnvMap = nullptr;

bool gbDebugEnvMapDenseReflection = false;
float gDebugEnvMapFarPlaneScale = 4.0f;
float gDebugEnvMapFarPlaneCoeff = 1.0f;
float gDebugEnvMapObjectCullScale = 8.0f;
bool gbDebugEnvMapOverlay = false;
float gDebugEnvMapMinLookSpeed = 0.05f;
float gDebugEnvMapLookSmoothTime = 0.25f;

static constexpr float kCockpitFOV = 1.0471976f;        // DAT_00f417dc (60deg)
static constexpr float kCockpitFarPlane = 150.0f;       // DAT_00f4182c
static constexpr float kHoodFarPlane = 60.0f;           // FLOAT_00f41744
static constexpr float kExteriorFarPlane = 45.0f;       // FLOAT_00f41808
static constexpr float kFarPlaneCoeff = 0.3f;           // FLOAT_00f416bc
static constexpr float kHoodFarPlaneCoeff = 0.6f;       // DAT_00f416c8
static constexpr float kMaxVehicleDistanceSq = 2500.0f; // DAT_00f41c5c
static constexpr float kLookDistance = 2.0f;            // DAT_00f416b0

MngEnvMap::MngEnvMap()
    : Manager(),
      ambientLight(),
      directionalLight(),
      pBuffer2DMCubemap( nullptr ),
      render2DMCubemap(),
      pBuffer2DMCubemapB( nullptr ),
      render2DMCubemapB(),
      drawList(),
      bCompute( false ),
      bInitialized( false ),
      pPostFXTexSample( nullptr ),
      pPostFXCubemapToSphere( nullptr ),
      pPostFXEnvMapScene( nullptr ),
      farPlaneCoeff( kFarPlaneCoeff ),
      view( EMV_Exterior ),
      camHeight( 0.64f ),     // DAT_00f41b68
      heightBias( 1.0f ),     // FLOAT_00f4165c
      hoodHeightBias( 1.1f ), // DAT_00f4174c
      camFOV( 140.0f ),       // DAT_00f41b64
      lumFactor( 1.35f ),     // DAT_00f41b60
      saturation( 0.35f ),    // DAT_00f419fc
      globalAmbiant( 0.5f ),  // FLOAT_00f41674
      coefAmbiant( 0.5f ),
      coefDiffuse( 0.5f ),
      coefSpecular( 0.5f ),
      hmRed( 0.5f ),
      hmGreen( 0.5f ),
      hmBlue( 0.5f ),
      hmContrast( 0.02f ),       // DAT_00f417f0
      cockpitSaturation( 0.8f ), // DAT_00f416d0
      cockpitGlobalAmbiant( 1.0f ),
      cockpitCoefAmbiant( 1.0f ),
      cockpitCoefDiffuse( 1.0f ),
      cockpitCoefSpecular( 0.5f ),
      userVector( Eigen::Vector4f::Zero() ),
      lastLookDirection( Eigen::Vector3f::Zero() ),
      lastLookView( EMV_Exterior ),
      lastDeltaTime( 0.0f ) {
    // FUN_006f0f40
    pSectors.fill( nullptr );
    gpMngEnvMap = this;
}

MngEnvMap::~MngEnvMap() {
    // FUN_006f0ef0
    gpMngEnvMap = nullptr;
}

bool MngEnvMap::initialize( TestDriveGameInstance * ) {
    // FUN_006f1b60
    if ( !createMaterials() ) {
        return true;
    }

    DrawStreams streams;
    streams.Normals = 0;
    streams.Specular = 0;
    streams.Tangent = 0;
    streams.Binormal = 0;
    streams.Diffuse = 1;
    streams.UVMap = 1;
    streams.bIndex16 = 1;

    if ( !drawList.initialize( 1, 4, 0, true, &streams, "EnvMapRList",
                               false ) ) {
        return true;
    }

    if ( setupScenes() && createPostFXStack() ) {
        bindRenderTargets();
        bInitialized = true;
    }

    return true;
}

void MngEnvMap::tick( float deltaTime, float ) {
    // FUN_006f2150
    lastDeltaTime = deltaTime;

    const uint32_t showFlags = gpCamera->getActiveCamera()->getFlags();
    bCompute = ( ( showFlags >> 3 ) & 1 );

    RenderScene *pScene =
        gpRender->getRenderPass<eRenderPass::RP_EnvMap>().pScene;
    if ( !bCompute || !bInitialized ) {
        return;
    }

    if ( gpCurEnvMappedCar == nullptr ||
         gpCurEnvMappedCar->getCarState() == nullptr ) {
        bCompute = false;
        return;
    }

    updateView();

    if ( pPostFXCubemapToSphere != nullptr ) {
        if ( gpWeather->isConfigDirty() ) {
            gpWeather->updateActiveConfig();
        }
        lumFactor =
            gpWeather->getActiveRandomizedWeatherConfig().getCarReflectionLum();

        pPostFXCubemapToSphere->setSaturation(
            ( view == EMV_Cockpit ) ? cockpitSaturation : saturation );
        pPostFXCubemapToSphere->setLumFactor( lumFactor );
        pPostFXCubemapToSphere->setHoodFactor(
            ( view == EMV_Hood ) ? 1.0f /* FLOAT_00f4165c */ : 0.0f );
    }

    if ( gpMngWorld != nullptr ) {
        updateCameras( true, true );
        pSectors.fill( nullptr );

        StreamingManager *pStreamingManager = gpWorld->getStreamingManager();
        if ( pStreamingManager != nullptr ) {
            const CarState *pCarState = gpCurEnvMappedCar->getCarState();
            const Eigen::Vector4f carPosition =
                pCarState->getBody()->WorldMatrix.row( 3 ).transpose();

            std::array<float, kNumGatheredSectors> nearestDistances;
            nearestDistances.fill( 3.4e38f ); // DAT_00f41b80

            for ( Sector &sector : pStreamingManager->sectorManager.sectors ) {
                if ( sector.state != 1 ) {
                    continue;
                }

                const float halfExtent = sector.heightmap.worldExtent * 0.5f;
                const float deltaX =
                    carPosition.x() -
                    ( sector.heightmap.worldOriginX + halfExtent );
                const float deltaZ =
                    carPosition.z() -
                    ( sector.heightmap.worldOriginZ + halfExtent );
                const float distanceSq = deltaX * deltaX + deltaZ * deltaZ;

                for ( uint32_t j = 0; j < kNumGatheredSectors; j++ ) {
                    if ( distanceSq >= nearestDistances[j] ) {
                        continue;
                    }
                    for ( uint32_t k = kNumGatheredSectors - 1; k > j; k-- ) {
                        nearestDistances[k] = nearestDistances[k - 1];
                        pSectors[k] = pSectors[k - 1];
                    }
                    nearestDistances[j] = distanceSq;
                    pSectors[j] = &sector;
                    break;
                }
            }

            for ( Sector *pSector : pSectors ) {
                if ( pSector != nullptr ) {
                    pStreamingManager->gatherSectorEnvImpostors(
                        *pSector, pScene,
                        gbDebugEnvMapDenseReflection ? gDebugEnvMapFarPlaneCoeff
                                                     : farPlaneCoeff );
                }
            }
        }
    }

    const DirectionalLight &sunLight = gpRender->getDefaultDirectionalLight();

    float lightGlobalAmbiant;
    float lightCoefAmbiant;
    float lightCoefDiffuse;
    float lightCoefSpecular;
    if ( view == EMV_Cockpit ) {
        lightGlobalAmbiant = cockpitGlobalAmbiant;
        lightCoefAmbiant = cockpitCoefAmbiant;
        lightCoefDiffuse = cockpitCoefDiffuse;
        lightCoefSpecular = cockpitCoefSpecular;
    } else {
        lightGlobalAmbiant = globalAmbiant;
        lightCoefAmbiant = coefAmbiant;
        lightCoefDiffuse = coefDiffuse;
        lightCoefSpecular = coefSpecular;
    }

    ambientLight.Color = sunLight.Ambient * lightGlobalAmbiant;
    directionalLight.Ambient = sunLight.Ambient * lightCoefAmbiant;
    directionalLight.Diffuse = sunLight.Diffuse * lightCoefDiffuse;
    directionalLight.Specular = sunLight.Specular * lightCoefSpecular;
    directionalLight.Direction = sunLight.Direction;

    if ( view == EMV_Cockpit ) {
        pScene->setUniformAtIndex( 1, nullptr );
        return;
    }

    userVector = Eigen::Vector4f( hmRed, hmGreen, hmBlue, hmContrast );
    pScene->setUniformAtIndex( 1, &userVector );
}

void MngEnvMap::terminate() {
    // FUN_006f2050
    if ( gpCurEnvMappedCar != nullptr ) {
        gpCurEnvMappedCar->setRealTimeEnvMap( nullptr, false, nullptr );
    }

    FramebufferAttachments *pMiniFramebuffer =
        gpRender->getRenderPass<eRenderPass::RP_EnvMapMini>()
            .pFramebufferAttachments;
    for ( uint32_t i = 0; i < 4; i++ ) {
        pMiniFramebuffer->pAttachments[i] = nullptr;
    }

    FramebufferAttachments *pEnvFramebuffer =
        gpRender->getRenderPass<eRenderPass::RP_EnvMap>()
            .pFramebufferAttachments;
    for ( uint32_t i = 0; i < 4; i++ ) {
        pEnvFramebuffer->pAttachments[i] = nullptr;
    }

    gpCurEnvMappedCar = nullptr;

    destroyPostFXStack();
    popLights();

    if ( pBuffer2DMCubemap != nullptr ) {
        gpWorld->removeGlobal2DM( &render2DMCubemap );
        render2DMCubemap.destroy();
        TestDrive::Free( pBuffer2DMCubemap );
        pBuffer2DMCubemap = nullptr;
    }

    if ( pBuffer2DMCubemapB != nullptr ) {
        gpWorld->removeGlobal2DM( &render2DMCubemapB );
        render2DMCubemapB.destroy();
        TestDrive::Free( pBuffer2DMCubemapB );
        pBuffer2DMCubemapB = nullptr;
    }

    if ( drawList.isInitialized() ) {
        drawList.destroy();
    }

    bInitialized = false;
}

void MngEnvMap::draw() {
    // FUN_006f1c20
    RenderScene *pScene =
        gpRender->getRenderPass<eRenderPass::RP_EnvMap>().pScene;
    if ( !bCompute || !bInitialized || gpCurEnvMappedCar == nullptr ) {
        return;
    }

    const CarState *pCarState = gpCurEnvMappedCar->getCarState();
    if ( pCarState == nullptr ) {
        return;
    }

    const Eigen::Vector4f carPosition =
        pCarState->getBody()->OffsetedWorldMatrix.row( 3 ).transpose();

    if ( gpMngWorld != nullptr ) {
        uint32_t heightmapInstanceIndex = 1;
        for ( Sector *pSector : pSectors ) {
            if ( pSector == nullptr ) {
                continue;
            }

            const uint32_t instanceIndex = heightmapInstanceIndex;
            heightmapInstanceIndex = 2;

            if ( pSector->state != 1 ) {
                continue;
            }

            SectorGeometry &geometry = pSector->geometry;
            geometry.submitObjectNodeInstances( pScene );
            geometry.submitHeightmapInstance( pScene, instanceIndex );
        }
    }

    if ( gpCarState != nullptr ) {
        std::array<std::pair<float, CarState *>, kNumGatheredVehicles>
            nearestVehicles;
        nearestVehicles.fill( { 0.0f, nullptr } );

        CarState *pCarStates = gpCarState->getCarStates();
        for ( uint32_t i = 0; i < kMaxNumVehicles; i++ ) {
            CarState &state = pCarStates[i];
            if ( !state.isInstanceAlive() ) {
                continue;
            }

            VehicleBase *pVehicle = state.getVehicleInstance();
            if ( pVehicle == nullptr || pVehicle == gpCurEnvMappedCar ) {
                continue;
            }

            // FUN_00413770
            if ( pVehicle->getParentNode() == nullptr ||
                 pVehicle->shouldUnstream() || !pVehicle->shouldDraw() ) {
                continue;
            }

            const Matrix4x4f_t *pNodeTransform =
                pVehicle->getNodeTransform( 0 );
            if ( pNodeTransform == nullptr ) {
                continue;
            }

            const Eigen::Vector4f delta =
                pNodeTransform->row( 3 ).transpose() - carPosition;
            const float distanceSq = delta.squaredNorm();
            if ( distanceSq >= kMaxVehicleDistanceSq ) {
                continue;
            }

            for ( uint32_t j = 0; j < kNumGatheredVehicles; j++ ) {
                if ( nearestVehicles[j].second == nullptr ) {
                    nearestVehicles[j] = { distanceSq, &state };
                    break;
                }

                if ( distanceSq < nearestVehicles[j].first ) {
                    for ( uint32_t k = kNumGatheredVehicles - 1; k > j; k-- ) {
                        nearestVehicles[k] = nearestVehicles[k - 1];
                    }
                    nearestVehicles[j] = { distanceSq, &state };
                    break;
                }
            }
        }

        for ( auto &entry : nearestVehicles ) {
            if ( entry.second == nullptr ) {
                continue;
            }

            VehicleBase *pVehicle = entry.second->getVehicleInstance();
            Instance *pLowResBody = pVehicle->getLowResBodyInstance();
            if ( pLowResBody == nullptr ) {
                if ( !pVehicle->shouldUnstream() &&
                     pVehicle->getParentNode() != nullptr ) {
                    pScene->enqueueDynamicHiearchy( pVehicle->getParentNode() );
                }
            } else {
                pScene->enqueueDynamicInstance( pLowResBody );
                for ( uint32_t i = 0; i < kMaxNumWheels; i++ ) {
                    Instance *pLowResWheel =
                        pVehicle->getLowResWheelInstance( i );
                    if ( pLowResWheel != nullptr ) {
                        pScene->enqueueDynamicInstance( pLowResWheel );
                    }
                }
            }

            if ( pVehicle->getType() == VehicleBase::eType::VBT_Bike &&
                 entry.second->hasAvatar() ) {
                OTDU_UNIMPLEMENTED_ONCE; // TODO: AVATAR
            }
        }
    }

    drawMiniQuad();
}

void MngEnvMap::reset() {
    // FUN_006f0da0
    view = EMV_Exterior;
    gpMngResources->getCubemapResources().registerUser( this );
}

void MngEnvMap::pause() {
    // FUN_006f0d10
    gpMngResources->getCubemapResources().unregisterUser( this );
}

bool MngEnvMap::onCubemapResourcesChanged() {
    // FUN_006f1100
    bindRenderTargets();
    return gpRender->notifyAllConfigListeners();
}

bool MngEnvMap::createMaterials() {
    // FUN_006f0bf0
    for ( int32_t i = 0; i < 2; i++ ) {
        const uint32_t bufferSize = Render2DM::CalcSize( 0, 0 );
        void *pBuffer = TestDrive::Alloc( bufferSize );
        if ( pBuffer == nullptr ) {
            return false;
        }

        Material *pMaterial =
            Render2DM::Create( pBuffer, 0x230588678a67836dull, 1, 0, 0, 0 );
        pMaterial->DepthTest = 2;
        pMaterial->DepthWrite = 2;

        MaterialShaderParameterArray *pParams =
            (MaterialShaderParameterArray *)pMaterial->getParameterByIndex( 0 );
        pParams->Flags.NumPrelight = 1;
        pParams->Flags.NumTextures = 1;

        MaterialLayer *pLayer = pParams->getLayer( 0 );

        Render2DM *p2DM = nullptr;
        if ( i == 0 ) {
            pLayer->pLayerTextures[0].Hashcode =
                GetIdentifier64bit( "CUBEMAP" );
            pBuffer2DMCubemap = pBuffer;
            p2DM = &render2DMCubemap;
        } else {
            pLayer->pLayerTextures[0].Hashcode =
                GetIdentifier64bit( "CUBEMAPB" );
            pBuffer2DMCubemapB = pBuffer;
            p2DM = &render2DMCubemapB;
        }

        if ( !p2DM->initialize( pBuffer ) ) {
            return false;
        }
    }

    gpWorld->pushGlobal2DM( &render2DMCubemap );
    gpWorld->pushGlobal2DM( &render2DMCubemapB );
    return true;
}

bool MngEnvMap::setupScenes() {
    // FUN_006f1250
    gpRender->setRenderPassEnabled<eRenderPass::RP_EnvMapMini>( true );

    RenderPass &backPass = gpRender->getRenderPass<eRenderPass::RP_Back>();
    RenderPass &envMapBackPass =
        gpRender->getRenderPass<eRenderPass::RP_EnvMapBack>();

    Camera *pBackCamera = envMapBackPass.pCamera;
    pBackCamera->setAspectRatio( 1.0f, 1.0f );
    pBackCamera->setFOV( camFOV );

    const Eigen::Vector4f &backZLimits =
        backPass.pScene->getFrustumWrite().getZLimits();
    envMapBackPass.pScene->getFrustumWrite().setPlanes( backZLimits.x(),
                                                        backZLimits.y() );
    envMapBackPass.pScene->setEnabled( true );

    RenderPass &envMapPass = gpRender->getRenderPass<eRenderPass::RP_EnvMap>();

    Camera *pEnvCamera = envMapPass.pCamera;
    pEnvCamera->setAspectRatio( 1.0f, 1.0f );
    pEnvCamera->setFOV( camFOV );

    envMapPass.pScene->getFrustumWrite().setPlanes( 0.1f, kCockpitFarPlane );

    envMapPass.pLightSetupNode->addLight( &ambientLight );
    envMapPass.pLightSetupNode->addLight( &directionalLight );
    envMapPass.pScene->setEnabled( true );

    return true;
}

bool MngEnvMap::createPostFXStack() {
    // FUN_006f1130
    if ( gpPostFX == nullptr ) {
        return true;
    }

    CubemapResources &cubemapResources = gpMngResources->getCubemapResources();

    pPostFXEnvMapScene =
        new ( TestDrive::Alloc( sizeof( PostFXNodeEnvMapScene ) ) )
            PostFXNodeEnvMapScene();
    if ( pPostFXEnvMapScene == nullptr ) {
        return false;
    }

    pPostFXTexSample = new ( TestDrive::Alloc( sizeof( PostFXNodeTexSample ) ) )
        PostFXNodeTexSample();
    if ( pPostFXTexSample == nullptr ) {
        return false;
    }
    pPostFXTexSample->setTextureToSample(
        cubemapResources.getCubemap().pRenderTarget );

    pPostFXCubemapToSphere =
        new ( TestDrive::Alloc( sizeof( PostFXNodeCubemapToSphere ) ) )
            PostFXNodeCubemapToSphere();
    if ( pPostFXCubemapToSphere == nullptr ) {
        return false;
    }
    if ( cubemapResources.getEnvmap().pRenderTarget != nullptr ) {
        pPostFXCubemapToSphere->setOutput(
            cubemapResources.getEnvmap().pRenderTarget, 0 );
    }

    pPostFXCubemapToSphere->connect( pPostFXTexSample, 0, 0 );
    pPostFXEnvMapScene->connect( pPostFXCubemapToSphere, 0, 0 );

    pPostFXEnvMapScene->setName( "MngEnvMap" );

    gEdRender.pFrameGraph->insertAfterObject(
        pPostFXEnvMapScene,
        gpRender->getRenderPass<eRenderPass::RP_EnvMap>().pScene );

    return true;
}

void MngEnvMap::destroyPostFXStack() {
    // FUN_006f0b60
    if ( pPostFXEnvMapScene != nullptr ) {
        gEdRender.pFrameGraph->removeObject( pPostFXEnvMapScene );
        pPostFXEnvMapScene->~PostFXNodeEnvMapScene();
        TestDrive::Free( pPostFXEnvMapScene );
        pPostFXEnvMapScene = nullptr;
    }

    if ( pPostFXCubemapToSphere != nullptr ) {
        pPostFXCubemapToSphere->~PostFXNodeCubemapToSphere();
        TestDrive::Free( pPostFXCubemapToSphere );
        pPostFXCubemapToSphere = nullptr;
    }

    if ( pPostFXTexSample != nullptr ) {
        pPostFXTexSample->~PostFXNodeTexSample();
        TestDrive::Free( pPostFXTexSample );
        pPostFXTexSample = nullptr;
    }
}

void MngEnvMap::bindRenderTargets() {
    // FUN_006f0e10
    CubemapResources &cubemapResources = gpMngResources->getCubemapResources();

    FramebufferAttachments *pMiniFramebuffer =
        gpRender->getRenderPass<eRenderPass::RP_EnvMapMini>()
            .pFramebufferAttachments;
    for ( uint32_t i = 0; i < 4; i++ ) {
        pMiniFramebuffer->pAttachments[i] = nullptr;
    }
    pMiniFramebuffer->pAttachments[0] =
        cubemapResources.getCubemapB().pRenderTarget;

    FramebufferAttachments *pEnvFramebuffer =
        gpRender->getRenderPass<eRenderPass::RP_EnvMap>()
            .pFramebufferAttachments;
    for ( uint32_t i = 0; i < 4; i++ ) {
        pEnvFramebuffer->pAttachments[i] = nullptr;
    }
    pEnvFramebuffer->pAttachments[0] =
        cubemapResources.getCubemap().pRenderTarget;

    render2DMCubemap.bindBitmapReference(
        &cubemapResources.getCubemap().Render2DB );
    render2DMCubemapB.bindBitmapReference(
        &cubemapResources.getCubemapB().Render2DB );

    if ( pPostFXTexSample != nullptr ) {
        pPostFXTexSample->setTextureToSample(
            cubemapResources.getCubemap().pRenderTarget );
    }
    if ( pPostFXCubemapToSphere != nullptr &&
         cubemapResources.getEnvmap().pRenderTarget != nullptr ) {
        pPostFXCubemapToSphere->setOutput(
            cubemapResources.getEnvmap().pRenderTarget, 0 );
    }
}

void MngEnvMap::popLights() {
    // FUN_006f0e90
    LightSetupNode *pLights =
        gpRender->getRenderPass<eRenderPass::RP_EnvMap>().pLightSetupNode;
    pLights->removeLight( &ambientLight );
    pLights->removeLight( &directionalLight );

    gpRender->getRenderPass<eRenderPass::RP_EnvMap>().pScene->setUniformAtIndex(
        1, nullptr );

    gpRender->setRenderPassEnabled<eRenderPass::RP_EnvMapBack>( false );
    gpRender->setRenderPassEnabled<eRenderPass::RP_EnvMap>( false );
    gpRender->setRenderPassEnabled<eRenderPass::RP_EnvMapMini>( false );
}

void MngEnvMap::updateView() {
    // FUN_006f0d50
    const uint32_t showFlags = gpCamera->getActiveCamera()->getFlags();
    if ( ( showFlags >> 0x13 & 1 ) != 0 ) {
        view = ( gpCurEnvMappedCar->getType() != VehicleBase::eType::VBT_Bike )
                   ? EMV_Hood
                   : EMV_Cockpit;
        return;
    }

    view = ( ( showFlags >> 0x12 & 1 ) == 0 ) ? EMV_Exterior : EMV_Cockpit;
}

void MngEnvMap::updateCameras( bool bUseActiveCamera, bool bUpdateBackScene ) {
    // FUN_006f16c0
    Camera *pEnvCamera =
        gpRender->getRenderPass<eRenderPass::RP_EnvMap>().pCamera;
    CameraGame *pSourceCamera = bUseActiveCamera ? gpCamera->getRenderCamera()
                                                 : gpCamera->getDefaultCamera();

    const Eigen::Vector3f eyePosition =
        pSourceCamera->getCamToWorld().row( 3 ).head<3>().transpose();
    const Eigen::Vector3f velocity = pSourceCamera->getVelocity();

    float fov;
    Eigen::Vector3f position;
    Eigen::Vector3f lookTarget;
    if ( view == EMV_Hood ) {
        fov = camFOV * kDegToRad;
        position = eyePosition;
        lookTarget = eyePosition + velocity * kLookDistance;
        lookTarget.y() += hoodHeightBias;
    } else if ( view == EMV_Cockpit ) {
        fov = kCockpitFOV;
        position = eyePosition;
        lookTarget = eyePosition - velocity * kLookDistance;
    } else {
        fov = camFOV * kDegToRad;
        position = pSourceCamera->getTargetOffset();
        position.y() += camHeight;
        lookTarget = eyePosition;
        if ( ( eyePosition - position ).norm() >= 1.0f ) {
            lookTarget.y() *= heightBias;
        }
    }

    debugCaptureState.bLookDirectionHeld = false;
    if ( view != EMV_Exterior ) {
        lookTarget = position + resolveInteriorLookDirection(
                                    lookTarget - position, pSourceCamera );
    }

    pEnvCamera->updateMatrix( position, kWorldUpVector.head<3>(), lookTarget );
    pEnvCamera->setFOV( fov );

    debugCaptureState.View = view;
    debugCaptureState.Position = position;
    debugCaptureState.LookTarget = lookTarget;
    debugCaptureState.SourceEye = eyePosition;
    debugCaptureState.SourceViewDir =
        -pSourceCamera->getCamToWorld().row( 2 ).head<3>().transpose();
    debugCaptureState.SourceVelocity = velocity;
    debugCaptureState.FOV = fov;

    if ( bUpdateBackScene ) {
        Camera *pBackCamera =
            gpRender->getRenderPass<eRenderPass::RP_EnvMapBack>().pCamera;
        lookTarget.y() += kLookDistance; // same DAT_00f416b0
        pBackCamera->setFOV( fov );
        pBackCamera->updateMatrix( position, kWorldUpVector.head<3>(),
                                   lookTarget );
    }

    Frustum &envFrustum = gpRender->getRenderPass<eRenderPass::RP_EnvMap>()
                              .pScene->getFrustumWrite();
    const float nearPlane = envFrustum.getZLimits().x();

    float viewFarPlane;
    if ( view == EMV_Cockpit ) {
        viewFarPlane = kCockpitFarPlane;
        farPlaneCoeff = kFarPlaneCoeff;
    } else if ( view == EMV_Hood ) {
        viewFarPlane = kHoodFarPlane;
        farPlaneCoeff = kHoodFarPlaneCoeff;
    } else {
        viewFarPlane = kExteriorFarPlane;
        farPlaneCoeff = kFarPlaneCoeff;
    }

    if ( gbDebugEnvMapDenseReflection ) {
        viewFarPlane *= gDebugEnvMapFarPlaneScale;
    }

    envFrustum.setPlanes( nearPlane, viewFarPlane );

    debugCaptureState.FarPlane = viewFarPlane;
}

Eigen::Vector3f
MngEnvMap::resolveInteriorLookDirection( const Eigen::Vector3f &rawDirection,
                                         CameraGame *pSourceCamera ) {
    // FIXME: Hack for hood camera (or any camera relying on the vehicle
    // velocity). This fix tentatively try to prevent the view vector from
    // pointing up at idle.
    if ( lastLookView != view || lastLookDirection.isZero() ) {
        const Eigen::Vector3f viewDirection =
            -pSourceCamera->getCamToWorld().row( 2 ).head<3>().transpose();
        lastLookDirection =
            ( view == EMV_Cockpit ) ? -viewDirection : viewDirection;
        lastLookView = view;
    }

    const float minLookDistance = gDebugEnvMapMinLookSpeed * kLookDistance;
    const float rawLength = rawDirection.norm();
    if ( rawLength <= minLookDistance ) {
        debugCaptureState.bLookDirectionHeld = true;
        return lastLookDirection * kLookDistance;
    }

    const Eigen::Vector3f target = rawDirection / rawLength;
    if ( gDebugEnvMapLookSmoothTime > 0.0f && lastDeltaTime > 0.0f ) {
        const float alpha =
            1.0f - std::exp( -lastDeltaTime / gDebugEnvMapLookSmoothTime );
        lastLookDirection += ( target - lastLookDirection ) * alpha;

        const float smoothedLength = lastLookDirection.norm();
        lastLookDirection = ( smoothedLength > 0.0001f )
                                ? ( lastLookDirection / smoothedLength )
                                : target;
    } else {
        lastLookDirection = target;
    }

    return lastLookDirection * kLookDistance;
}

void MngEnvMap::drawMiniQuad() {
    // FUN_006f1500
    CubemapResources &cubemapResources = gpMngResources->getCubemapResources();
    RenderTarget *pCubemapBRT = cubemapResources.getCubemapB().pRenderTarget;

    const float invWidth = 1.0f / (float)pCubemapBRT->getWidth();
    const float invHeight = 1.0f / (float)pCubemapBRT->getHeight();

    drawList.reset();
    drawList.setIdentityTransform();
    drawList.setActiveMaterial( render2DMCubemap.getMaterialAtIndex( 0 ) );

    if ( !drawList.beginPrimitive( ePrimitiveType::PT_TriangleStrip, 4 ) ) {
        return;
    }

    drawList.setDiffuse( 0, ColorRGBA( 0xffffffffu ) );

    drawList.setTexCoords( 0, Eigen::Vector2f( invWidth, invHeight ) );
    drawList.pushVertex( 0.0f, 0.0f, 0.5f );

    drawList.setTexCoords( 0, Eigen::Vector2f( invWidth, 1.0f - invHeight ) );
    drawList.pushVertex( 0.0f, 1.0f, 0.5f );

    drawList.setTexCoords( 0, Eigen::Vector2f( 1.0f - invWidth, invHeight ) );
    drawList.pushVertex( 1.0f, 0.0f, 0.5f );

    drawList.setTexCoords(
        0, Eigen::Vector2f( 1.0f - invWidth, 1.0f - invHeight ) );
    drawList.pushVertex( 1.0f, 1.0f, 0.5f );

    drawList.commitPrimitive();

    gpRender->getRenderPass<eRenderPass::RP_EnvMapMini>()
        .pScene->enqueueDynamicDrawList( &drawList );
}

#ifdef OTDU_USE_IMGUI
static void DrawDebugVector( const char *pLabel,
                             const Eigen::Vector3f &value ) {
    ImGui::Text( "%-13s %9.2f %9.2f %9.2f", pLabel, value.x(), value.y(),
                 value.z() );
}

void MngEnvMap::DrawEnvMapDebugPanel( float viewSize ) {
    if ( gpMngResources == nullptr || gpImgui == nullptr ) {
        ImGui::TextUnformatted( "EnvMap resources are not available yet." );
        return;
    }

    CubemapResources &cubemapResources = gpMngResources->getCubemapResources();
    const ImVec2 imageSize( viewSize, viewSize );

    ImGui::BeginGroup();
    ImGui::TextUnformatted( "Cubemap (capture)" );
    Material *pCubemapMaterial = gpImgui->getDebugViewMaterial(
        &cubemapResources.getCubemap().Render2DB );
    if ( pCubemapMaterial != nullptr ) {
        ImGui::Image( (ImTextureID)pCubemapMaterial, imageSize );
    }
    ImGui::EndGroup();

    ImGui::SameLine();

    ImGui::BeginGroup();
    ImGui::TextUnformatted( "Envmap (chrome ball)" );
    Material *pEnvmapMaterial = gpImgui->getDebugViewMaterial(
        &cubemapResources.getEnvmap().Render2DB );
    if ( pEnvmapMaterial != nullptr ) {
        ImGui::Image( (ImTextureID)pEnvmapMaterial, imageSize );
    }
    ImGui::EndGroup();

    if ( gpMngEnvMap == nullptr ) {
        return;
    }

    static const char *kViewNames[3] = { "Cockpit", "Hood", "Exterior" };
    const MngEnvMap::DebugCaptureState &state =
        gpMngEnvMap->getDebugCaptureState();

    ImGui::Text( "View: %s   Capture: %s   Look dir: %s",
                 kViewNames[state.View],
                 gpMngEnvMap->isCapturing() ? "on" : "off",
                 state.bLookDirectionHeld ? "HELD (too slow)" : "live" );
    ImGui::Text( "FOV: %.1f deg   Far plane: %.1f m   Speed: %.4f m/s",
                 state.FOV * kRadToDeg, state.FarPlane,
                 state.SourceVelocity.norm() );
    ImGui::SliderFloat( "Min look speed", &gDebugEnvMapMinLookSpeed, 0.0f, 1.0f,
                        "%.3f m/s" );
    ImGui::SliderFloat( "Look smoothing", &gDebugEnvMapLookSmoothTime, 0.0f,
                        1.0f, "%.2f s" );

    Eigen::Vector3f lookDirection = state.LookTarget - state.Position;
    const float lookDistance = lookDirection.norm();
    if ( lookDistance > 0.0001f ) {
        lookDirection /= lookDistance;
    }

    ImGui::SeparatorText( "Capture camera (as fed to updateMatrix)" );
    DrawDebugVector( "Position", state.Position );
    DrawDebugVector( "Look target", state.LookTarget );
    DrawDebugVector( "Look dir", lookDirection );

    ImGui::SeparatorText( "Source camera (game camera)" );
    DrawDebugVector( "Eye", state.SourceEye );
    DrawDebugVector( "View dir", state.SourceViewDir );
    DrawDebugVector( "Velocity", state.SourceVelocity );
    ImGui::Text( "dot( look dir, view dir ): %+.3f  (%s)",
                 lookDirection.dot( state.SourceViewDir ),
                 ( lookDirection.dot( state.SourceViewDir ) < 0.0f )
                     ? "behind"
                     : "ahead" );

    if ( gpRender != nullptr ) {
        // Live matrix, i.e. what the env pass actually renders with.
        const Matrix4x4f_t &camToWorld =
            gpRender->getRenderPass<eRenderPass::RP_EnvMap>()
                .pCamera->getCamToWorld();

        ImGui::SeparatorText( "Capture camera (resolved basis)" );
        DrawDebugVector( "Right", camToWorld.row( 0 ).head<3>().transpose() );
        DrawDebugVector( "Up", camToWorld.row( 1 ).head<3>().transpose() );
        DrawDebugVector( "Forward", camToWorld.row( 2 ).head<3>().transpose() );
        DrawDebugVector( "Translation",
                         camToWorld.row( 3 ).head<3>().transpose() );
    }
}

void MngEnvMap::DrawEnvMapDebugOverlay() {
    if ( !gbDebugEnvMapOverlay ) {
        return;
    }

    if ( gpImgui != nullptr ) {
        gpImgui->requestOverlayFrame();
    }

    ImGui::SetNextWindowBgAlpha( 0.7f );
    ImGui::SetNextWindowPos( ImVec2( 16.0f, 16.0f ), ImGuiCond_FirstUseEver );
    if ( ImGui::Begin( "EnvMap Preview", &gbDebugEnvMapOverlay,
                       ImGuiWindowFlags_AlwaysAutoResize |
                           ImGuiWindowFlags_NoFocusOnAppearing |
                           ImGuiWindowFlags_NoNav ) ) {
        MngEnvMap::DrawEnvMapDebugPanel( 160.0f );
    }
    ImGui::End();
}
#endif
