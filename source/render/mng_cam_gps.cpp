#include "shared.h"
#include "mng_cam_gps.h"

#include "config/gs_config.h"
#include "core/color.h"
#include "core/maths_helper.h"
#include "render/material.h"
#include "system/system_shared.h"
#include "input/input_device.h"
#include "render/camera/camera_gps.h"
#include "render/camera/gs_camera.h"
#include "render/draw_list.h"
#include "render/gs_render.h"
#include "render/hiearchy_node.h"
#include "render/instance.h"
#include "render/postfx/gs_postfx.h"
#include "render/postfx/effects/postfx_radial_blur.h"
#include "render/render_scene.h"
#include "system/ed_sys.h"
#include "world/gs_mng_resources.h"
#include "world/gs_world.h"
#include "world/gs_world_streaming.h"
#include "world/mng_background.h"
#include "world/mng_ocean.h"
#include "world/streaming_manager.h"
#include "world/weather/gs_weather.h"
#include "world/weather/hdri_config.h"
#include "world/weather/weather_config.h"

MngCamGPS *gpMngCamGPS = nullptr; // DAT_010e804c

static constexpr int32_t kGPSCameraMode = 3;

static constexpr float kGPSAltitudeSpace = 300000.0f;       // DAT_00f41f70
static constexpr float kGPSAltitudeIsland = 15000.0f;       // DAT_00f41f50
static constexpr float kGPSAltitudeSectors = 2000.0f;       // DAT_00f419a8
static constexpr float kGPSSectorLODScale = 0.04f;          // DAT_00f41980
static constexpr float kGPSGlobeInputSpeed = 15.707965f;    // DAT_00f41e8c
static constexpr float kGPSGlobeIdleSpeed = 1.5707964f;     // DAT_00f418a4
static constexpr float kGPSGlobeInputEpsilon = 1e-5f;       // DAT_00f41710
static constexpr float kGPSEarthFadeNearOffset = 200000.0f; // DAT_00fabf60
static constexpr float kGPSEarthFadeFarOffset = 1000000.0f; // DAT_00fabf5c
static constexpr float kGPSRadialBlurScale = 30.0f;         // DAT_00fabf58
static constexpr float kGPSSpaceBackgroundContrast = 0.35f; // DAT_00fabf78
static constexpr float kGPSFadeAlphaScale = 255.5f;         // DAT_00f41f74
static constexpr float kGPSClipPlaneSurfaceOffset = -7.5f;  // DAT_00fabf64
static constexpr float kGPSClipPlaneCloseOffset = 0.1f;     // DAT_00f41668
static constexpr float kGPSFadeMapInset = 0.001953125f;

static constexpr uint64_t kGPSSpaceBackgroundShader = 0x33123465ca373333ull;
static constexpr uint64_t kGPSFadeMapShader = 0x73078865ca37336dull;
static constexpr uint64_t kGPSWaterSeaBucket = 0x4000ull;

static float gGPSOceanSpecularPower = 160.0f;    // DAT_00fabf74
static float gGPSOceanNMapHeightScaler = 0.081f; // DAT_00fabf70
static float gGPSOceanFresnelBias = 0.65f;       // DAT_00fabf6c
static float gGPSOceanFresnelPower = 2.64f;      // DAT_00fabf68
static float gGPSOceanAttNormal = 0.0f;          // DAT_016a6aa4
static float gGPSOceanAttPerturbation = 0.0f;    // DAT_016a6aa8
static float gGPSOceanAttFresnel = 0.0f;         // DAT_016a6aac
static float gGPSOceanBluriness = 0.0f;          // DAT_016a6ab0

static constexpr float kGPSNMapUVScalers[4] = {
    0.0015999998f,  // DAT_00f41e9c
    0.00040000002f, // DAT_00f41e98
    0.00010000001f, // DAT_00f41e94
    5.0000021e-06f, // DAT_00f41e90
};

static bool IsGPSCameraActive() {
    CameraGame *pActiveCamera = gpCamera->getActiveCamera();
    return pActiveCamera != nullptr &&
           pActiveCamera->getType() == kGPSCameraMode;
}

static CameraGPS *GetGPSCamera() {
    return static_cast<CameraGPS *>( gpCamera->findCameraWithMode(
        kGPSCameraMode, gpCamera->getBoundCarState() ) );
}

static int32_t ZoomToAltitudeBand( const int32_t zoom ) {
    // FUN_006b33f0
    switch ( zoom ) {
    case CameraGPS::GZL_Fixed:
        return 0;
    case CameraGPS::GZL_Space:
        return 5;
    case CameraGPS::GZL_Island:
        return 4;
    case CameraGPS::GZL_Far:
        return 3;
    case CameraGPS::GZL_Medium:
        return 2;
    case CameraGPS::GZL_Close:
        return 1;
    default:
        return 6;
    }
}

static bool IsSpaceView( const CameraGPS *pCameraGPS ) {
    // FUN_006b4690
    return pCameraGPS->getZoom() == CameraGPS::GZL_Space ||
           ( pCameraGPS->getPreviousZoom() == CameraGPS::GZL_Space &&
             pCameraGPS->isInTransition() );
}

// Altitude blend from the Earth surface (0) to the Close view (1).
static float GetSurfaceToCloseBlend( CameraGPS *pCameraGPS ) {
    const float earthRadius = pCameraGPS->getEarthRadius();
    const float closeDistance = pCameraGPS->getZoomDistanceToCenter( 0 );
    return Saturate( ( pCameraGPS->getDistanceToEarthCenter() - earthRadius ) /
                     ( closeDistance - earthRadius ) );
}

static Eigen::Vector4f GetStreamOrigin() {
    return gpWorldStreaming->isEnabled() ? gpWorldStreaming->getPosition()
                                         : Eigen::Vector4f::Zero();
}

MngCamGPS::MngCamGPS() : Manager() {
    // FUN_006b3d30
    clipPlaneSetup.setFlags( 0x101 );
    pSpaceBkgDrawList = new DrawList();
    pFadeMapDrawList = new DrawList();

    gpMngCamGPS = this;
}

MngCamGPS::~MngCamGPS() {
    // FUN_006b3cb0
    delete pSpaceBkgDrawList;
    pSpaceBkgDrawList = nullptr;
    delete pFadeMapDrawList;
    pFadeMapDrawList = nullptr;

    if ( gpMngCamGPS == this ) {
        gpMngCamGPS = nullptr;
    }
}

bool MngCamGPS::initialize( TestDriveGameInstance * ) {
    // FUN_006b3c40
    allocateResources();

    earthNodePosBackup = Eigen::Vector4f::Zero();
    HiearchyNode *pEarthNode =
        gpMngResources->getEarthResources().getEarthNode();
    if ( pEarthNode != nullptr ) {
        earthNodePosBackup = pEarthNode->TransformMatrix.row( 3 ).transpose();
    }

    if ( gpMngOcean != nullptr ) {
        oceanSetup = *gpMngOcean->getCurrentSetup();
    }
    return true;
}

void MngCamGPS::tick( float deltaTime, float /*totalTime*/ ) {
    // FUN_006b53f0
    if ( IsGPSCameraActive() ) {
        updateCameraForCapture();
    }

    noiseWindStart += noiseWindSpeed * deltaTime;
    if ( noiseWindStart + noiseWindSize <= 1.0f ) {
        if ( noiseWindStart < 0.0f ) {
            noiseWindStart = 0.0f;
            noiseWindSpeed = -noiseWindSpeed;
        }
    } else {
        noiseWindStart = 1.0f - noiseWindSize;
        noiseWindSpeed = -noiseWindSpeed;
    }

    if ( IsGPSCameraActive() ) {
        CameraGPS *pCameraGPS = GetGPSCamera();
        earthFadeNearDist =
            pCameraGPS->getZoomDistanceToCenter( 3 ) + kGPSEarthFadeNearOffset;
        earthFadeFarDist = earthFadeNearDist + kGPSEarthFadeFarOffset;
    }

    updateSpaceViewRotation( deltaTime );
    updateRadialBlur();
    updateHeightmapLOD();

    if ( gpMngBackground != nullptr ) {
        updateBackground();
    }
    if ( gpMngOcean != nullptr ) {
        updateOcean();
    }
}

void MngCamGPS::reset() {
    // FUN_006b5510
    if ( IsGPSCameraActive() ) {
        updateCameraForCapture();
    }

    gpWeather->setUpdateConfigCallback( &MngCamGPS::OnWeatherConfigUpdate );
    gpWeather->setUpdateHDRConfigCallback( &MngCamGPS::OnHDRConfigUpdate );
}

void MngCamGPS::draw() {
    // FUN_006b5170
    if ( !bDrawEarth || !IsGPSCameraActive() ) {
        return;
    }

    CameraGPS *pCameraGPS = GetGPSCamera();
    if ( !IsSpaceView( pCameraGPS ) ) {
        return;
    }

    const float distance = pCameraGPS->getDistanceToEarthCenter();
    const float islandDistance = pCameraGPS->getZoomDistanceToCenter( 3 );
    pStaticParamsSpaceBackground[0] = noiseWindStart;
    pStaticParamsSpaceBackground[1] = noiseWindStart + noiseWindSize;
    pStaticParamsSpaceBackground[2] = kGPSSpaceBackgroundContrast;
    if ( distance >= islandDistance ) {
        gpRender->getRenderPass<RP_Back2D>().pScene->enqueueDynamicDrawList(
            pSpaceBkgDrawList );
    }

    float islandFade = 0.0f;
    float earthFade = 1.0f;
    if ( pCameraGPS->isInTransition() ) {
        earthFade = Saturate( ( distance - islandDistance ) /
                              ( earthFadeNearDist - islandDistance ) );
        islandFade = earthFade;
        if ( distance > earthFadeNearDist ) {
            islandFade = Saturate( -( distance - earthFadeFarDist ) /
                                   ( earthFadeFarDist - earthFadeNearDist ) );
        }
    }

    EarthResources &earth = gpMngResources->getEarthResources();
    HiearchyNode *pIslandNode = earth.getIslandNode();
    HiearchyNode *pEarthNode = earth.getEarthNode();
    if ( pIslandNode == nullptr || pEarthNode == nullptr ) {
        return;
    }

    if ( pIslandNode->pInstance != nullptr ) {
        pIslandNode->pInstance->setColor( ColorRGBA(
            0xff, 0xff, 0xff,
            static_cast<uint8_t>( islandFade * kGPSFadeAlphaScale ) ) );
    }
    if ( pEarthNode->pInstance != nullptr ) {
        pEarthNode->pInstance->setColor( ColorRGBA(
            0xff, 0xff, 0xff,
            static_cast<uint8_t>( earthFade * kGPSFadeAlphaScale ) ) );
    }

    const Eigen::Vector4f origin = -GetStreamOrigin();
    pIslandNode->TransformMatrix.row( 3 ) =
        Eigen::Vector4f( origin.x(), origin.y(), origin.z(), 1.0f ).transpose();
    pIslandNode->updateAndPropagateTransformMatrices( false );
    gpRender->getRenderPass<RP_Near>().pScene->enqueueDynamicHiearchy(
        pIslandNode );
}

void MngCamGPS::pause() {
    // FUN_006b3fa0
    gpWorld->getStreamingManager()->resetHeightmapLODCoefs( 7 );

    if ( gpMngOcean != nullptr ) {
        gpRender->getShaderUniformWrite( 5 ).x() = 1.0f;
    }

    gpWeather->setUpdateConfigCallback( nullptr );
    gpWeather->setUpdateHDRConfigCallback( nullptr );

    if ( gpMngBackground != nullptr &&
         gpMngBackground->getDomeNode() != nullptr ) {
        gpMngBackground->getDomeNode()->pInstance->getFlagsWrite() &=
            ~kInstanceHiddenFlag;
    }

    toggleGPSClipPlanes( false, false );
}

void MngCamGPS::terminate() {
    // FUN_006b3bc0
    releaseResources();

    if ( bForceStreamingFlush ) {
        StreamingManager *pStreaming = gpWorld->getStreamingManager();
        pStreaming->sectorManager.bNeedStreamUpdate = true;
        pStreaming->sectorManager.bDrawSector = false;
        pStreaming->areaGrid.bNeedStreamUpdate = true;
        pStreaming->areaGrid.bDrawArea = false;
        pStreaming->setActiveRenderLayers( 1 );
    }

    HiearchyNode *pEarthNode =
        gpMngResources->getEarthResources().getEarthNode();
    if ( pEarthNode != nullptr ) {
        pEarthNode->TransformMatrix.row( 3 ) = earthNodePosBackup.transpose();
    }

    if ( gpMngOcean != nullptr ) {
        gpMngOcean->setCurrentSetup( &gDefaultOceanSetup );
    }
}

bool MngCamGPS::allocateResources() {
    // FUN_006b3750
    static constexpr uint32_t kSpaceNoiseSize = 0x200;
    if ( spaceNoise2DB.allocateAndCreate( kSpaceNoiseSize, kSpaceNoiseSize, 1,
                                          1, eViewFormat::VF_SRGB_L16, 0,
                                          "SpaceNoise" ) ) {
        Texture *pTexture = spaceNoise2DB.getFirstBitmap();
        uint16_t *pTexels =
            (uint16_t *)Render2DB::GetTexelsPointer( pTexture, 0 );
        if ( pTexels != nullptr ) {
            for ( uint32_t i = 0; i < kSpaceNoiseSize * kSpaceNoiseSize; i++ ) {
                pTexels[i] = (uint16_t)( ( (float)rand() / 32767.0f ) *
                                         65535.0f ); // DAT_00f41714
            }
            Render2DB::PrepareForUpload( pTexture, true );
        }
    }

    p2DMSpaceBackgroundData =
        TestDrive::Alloc( Render2DM::CalcSize( 1, 0x10 ) );
    if ( p2DMSpaceBackgroundData == nullptr ) {
        return false;
    }

    pMatSpaceBackground = Render2DM::Create(
        p2DMSpaceBackgroundData, kGPSSpaceBackgroundShader, 1, 1, 0x10, 0 );
    MaterialShaderParameterArray *pSpaceParams =
        (MaterialShaderParameterArray *)
            pMatSpaceBackground->getParameterByIndex( 0 );
    pSpaceParams->Flags.NumUVMaps = 1;
    pMatSpaceBackground->SrcBlend = (eBlendMode)2;
    pMatSpaceBackground->DstBlend = (eBlendMode)1;
    pMatSpaceBackground->BlendOP = (eBlendOP)6;
    pMatSpaceBackground->AlphaTest = (eComparisonFunction)1;
    pMatSpaceBackground->DepthTest = 2;
    pMatSpaceBackground->DepthWrite = 2;
    pMatSpaceBackground->BackfaceCullingMode = (eCullMode)3;

    MaterialLayer *pSpaceLayer = pSpaceParams->getLayer( 0 );
    pSpaceLayer->NumTextures = 1;
    pSpaceLayer->pLayerTextures[0].Hashcode = 1;
    pSpaceLayer->pLayerTextures[0].pTextureSection =
        spaceNoise2DB.getBitmapSection();
    pSpaceLayer->pLayerTextures[0].SamplerAddress[0] = (eMaterialTileMode)2;
    pSpaceLayer->pLayerTextures[0].SamplerAddress[1] = (eMaterialTileMode)2;
    pSpaceLayer->pLayerTextures[0].MinMagFilter = (eTextureFiltering)1;
    pSpaceLayer->pLayerTextures[0].MipFilter = (eTextureFiltering)1;
    *pSpaceParams->getParameterByIndex( 0 ) = { 0x3e, 2, 0x10, 0 };
    pStaticParamsSpaceBackground =
        (float *)pSpaceParams->getParameterDataByIndex( 0 );
    dmSpaceBackground.initialize( p2DMSpaceBackgroundData );

    DrawStreams spaceStreams;
    spaceStreams.Normals = 0;
    spaceStreams.Diffuse = 0;
    spaceStreams.UVMap = 1;
    spaceStreams.bIndex16 = 1;
    if ( !pSpaceBkgDrawList->initialize( 1, 4, 0, false, &spaceStreams,
                                         "DListSpaceBkg", false ) ) {
        return false;
    }

    const float halfTexelX = ( 1.0f / (float)gWindowWidth ) * -0.5f;
    const float halfTexelY = ( 1.0f / (float)gWindowHeight ) * -0.5f;
    pSpaceBkgDrawList->reset();
    pSpaceBkgDrawList->setIdentityTransform();
    pSpaceBkgDrawList->setActiveMaterial( pMatSpaceBackground );
    if ( pSpaceBkgDrawList->beginPrimitive( PT_TriangleStrip, 4 ) ) {
        pSpaceBkgDrawList->setTexCoords( 0, Eigen::Vector2f( 0.0f, 0.0f ) );
        pSpaceBkgDrawList->pushVertex( halfTexelX, halfTexelY, 0.0f );
        pSpaceBkgDrawList->setTexCoords( 0, Eigen::Vector2f( 0.0f, 1.0f ) );
        pSpaceBkgDrawList->pushVertex( halfTexelX, halfTexelY + 1.0f, 0.0f );
        pSpaceBkgDrawList->setTexCoords( 0, Eigen::Vector2f( 1.0f, 0.0f ) );
        pSpaceBkgDrawList->pushVertex( halfTexelX + 1.0f, halfTexelY, 0.0f );
        pSpaceBkgDrawList->setTexCoords( 0, Eigen::Vector2f( 1.0f, 1.0f ) );
        pSpaceBkgDrawList->pushVertex( halfTexelX + 1.0f, halfTexelY + 1.0f,
                                       0.0f );
        pSpaceBkgDrawList->commitPrimitive();
    }

    p2DMFadeMapData = TestDrive::Alloc( Render2DM::CalcSize( 0, 0 ) );
    if ( p2DMFadeMapData == nullptr ) {
        return false;
    }

    pMatFadeMap =
        Render2DM::Create( p2DMFadeMapData, kGPSFadeMapShader, 0, 0, 0, 0 );
    ( (MaterialShaderParameterArray *)pMatFadeMap->getParameterByIndex( 0 ) )
        ->Flags.NumPrelight = 1;
    pMatFadeMap->SrcBlend = (eBlendMode)5;
    pMatFadeMap->DstBlend = (eBlendMode)6;
    pMatFadeMap->BlendOP = (eBlendOP)1;
    pMatFadeMap->AlphaTest = (eComparisonFunction)1;
    pMatFadeMap->DepthTest = 2;
    pMatFadeMap->DepthWrite = 2;
    pMatFadeMap->BackfaceCullingMode = (eCullMode)3;
    dmFadeMap.initialize( p2DMFadeMapData );
    pMatFadeMap->OT = 0x2c;

    DrawStreams fadeStreams;
    fadeStreams.Normals = 0;
    fadeStreams.Diffuse = 1;
    fadeStreams.UVMap = 0;
    fadeStreams.bIndex16 = 1;
    return pFadeMapDrawList->initialize( 1, 4, 0, true, &fadeStreams,
                                         "DListFadeMap", false );
}

void MngCamGPS::releaseResources() {
    // FUN_006b3630
    if ( spaceNoise2DB.getHeader() != nullptr ) {
        spaceNoise2DB.destroy();
    }

    if ( dmSpaceBackground.getHeader() != nullptr ) {
        dmSpaceBackground.destroy();
        pMatSpaceBackground = nullptr;
    }
    if ( p2DMSpaceBackgroundData != nullptr ) {
        TestDrive::Free( p2DMSpaceBackgroundData );
        p2DMSpaceBackgroundData = nullptr;
    }
    pStaticParamsSpaceBackground = nullptr;

    if ( dmFadeMap.getHeader() != nullptr ) {
        dmFadeMap.destroy();
        pMatFadeMap = nullptr;
    }
    if ( p2DMFadeMapData != nullptr ) {
        TestDrive::Free( p2DMFadeMapData );
        p2DMFadeMapData = nullptr;
    }

    if ( pSpaceBkgDrawList != nullptr && pSpaceBkgDrawList->isInitialized() ) {
        pSpaceBkgDrawList->destroy();
    }
    if ( pFadeMapDrawList != nullptr && pFadeMapDrawList->isInitialized() ) {
        pFadeMapDrawList->destroy();
    }
}

void MngCamGPS::toggleGPSClipPlanes( const bool bGPSRendering,
                                     bool bClipPlanes ) {
    // FUN_006b3530
    RenderScene *pNearOpaque = gpRender->getRenderPass<RP_NearOpaque>().pScene;
    RenderScene *pPreNear = gpRender->getRenderPass<RP_PreNear>().pScene;
    RenderScene *pNear = gpRender->getRenderPass<RP_Near>().pScene;

    if ( !bGPSRendering ) {
        pNearOpaque->setBucketsMask( pNearOpaque->getBucketsMask() |
                                     kGPSWaterSeaBucket );
        bClipPlanes = false;
        if ( pPreNear != nullptr ) {
            pPreNear->setEnabled( false );
        }
    } else {
        if ( pPreNear != nullptr ) {
            pPreNear->setEnabled( true );
        }
        // NOTE: Retail zero-extends ~0x4000 and wipes the upper 32 buckets.
        pNearOpaque->setBucketsMask(
            (uint32_t)( pNearOpaque->getBucketsMask() & ~kGPSWaterSeaBucket ) );
    }

    SetupGraph &nearOpaqueSetup = pNearOpaque->getSetup();
    if ( nearOpaqueSetup.findClipPlaneNode() == nullptr && bClipPlanes ) {
        nearOpaqueSetup.addNode( &clipPlaneSetup );
        pNear->getSetup().addNode( &clipPlaneSetup );
    }
    if ( nearOpaqueSetup.findClipPlaneNode() != nullptr && !bClipPlanes ) {
        nearOpaqueSetup.removeNode( &clipPlaneSetup );
        pNear->getSetup().removeNode( &clipPlaneSetup );
    }
}

void MngCamGPS::updateCameraForCapture() {
    // FUN_006b4fa0
    if ( !bUpdateStreaming ) {
        return;
    }

    CameraGPS *pCameraGPS = GetGPSCamera();
    StreamingManager *pStreaming = gpWorld->getStreamingManager();
    SectorManager &sectors = pStreaming->sectorManager;
    AreaGrid &areas = pStreaming->areaGrid;

    const float altitude =
        pCameraGPS->getDistanceToEarthCenter() - pCameraGPS->getEarthRadius();
    if ( altitude > kGPSAltitudeSpace ) {
        sectors.bDrawSector = true;
        areas.bDrawArea = true;
        pStreaming->setActiveRenderLayers( 0 );
        sectors.bStreamSector = false;
        areas.bStreamArea = false;
    } else if ( altitude > kGPSAltitudeIsland ) {
        sectors.bDrawSector = true;
        areas.bDrawArea = true;
        pStreaming->setActiveRenderLayers( 1 );
        sectors.bStreamSector = false;
        areas.bStreamArea = false;
    } else {
        if ( altitude > kGPSAltitudeSectors ) {
            sectors.bDrawSector = true;
            areas.bNeedStreamUpdate = true;
            areas.bDrawArea = false;
            pStreaming->setActiveRenderLayers( 1 );
            sectors.bStreamSector = false;
        } else {
            sectors.bNeedStreamUpdate = true;
            sectors.bDrawSector = false;
            areas.bNeedStreamUpdate = true;
            areas.bDrawArea = false;
            pStreaming->setActiveRenderLayers( 1 );
            sectors.bStreamSector = true;
            gpWorldStreaming->updateStreaming();
        }
        areas.bStreamArea = true;
    }

    if ( bForceStreaming ) {
        sectors.bStreamSector = true;
        gpWorldStreaming->updateStreaming();
        areas.bStreamArea = true;
    }

    if ( bForceDraw ) {
        sectors.bNeedStreamUpdate = true;
        sectors.bDrawSector = false;
        areas.bNeedStreamUpdate = true;
        areas.bDrawArea = false;
        pStreaming->setActiveRenderLayers( 1 );
    }
}

void MngCamGPS::updateSpaceViewRotation( const float deltaTime ) {
    // FUN_006b46d0
    if ( !IsGPSCameraActive() ) {
        return;
    }

    CameraGPS *pCameraGPS = GetGPSCamera();
    if ( pCameraGPS->getZoom() != CameraGPS::GZL_Space ||
         pCameraGPS->isInTransition() ) {
        return;
    }

    PlayerConfiguration *pPlayerConfig = gpConfig->pPlayerConfig;
    if ( gpConfig->NbMaxPlayers <= 0 || pPlayerConfig == nullptr ) {
        return;
    }

    const float right = pPlayerConfig->pMapRight->FUN_00434bc0( 0.3f, 0.7f ) *
                        kGPSGlobeInputSpeed;
    const float left = pPlayerConfig->pMapLeft->FUN_00434bc0( 0.3f, 0.7f ) *
                       kGPSGlobeInputSpeed;
    float speed = right - left;
    if ( fabsf( speed ) - kGPSGlobeInputEpsilon <= 0.0f ) {
        speed = kGPSGlobeIdleSpeed;
    }

    float angle = speed * deltaTime + pCameraGPS->getSpaceViewAngle();
    if ( angle < -180.0f ) {
        angle += 360.0f;
    }
    if ( angle > 180.0f ) {
        angle -= 360.0f;
    }
    pCameraGPS->setSpaceViewAngle( angle );
}

void MngCamGPS::updateHeightmapLOD() {
    // FUN_006b3460
    if ( !IsGPSCameraActive() ) {
        return;
    }

    StreamingManager *pStreaming = gpWorld->getStreamingManager();
    pStreaming->resetHeightmapLODCoefs( 7 );
    pStreaming->scaleHeightmapLODCoefs( 1, kGPSSectorLODScale );
}

void MngCamGPS::updateRadialBlur() {
    // FUN_006b4140
    PostFXStepRadialBlur *pStep =
        (PostFXStepRadialBlur *)
            gpPostFX->getEffect<ePostFXStep::PFXS_RadialBlur>();
    if ( pStep == nullptr || pStep->getInput() == nullptr ) {
        return;
    }
    PostFXNodeRadialBlur *pNode = (PostFXNodeRadialBlur *)pStep->getInput();

    if ( !IsGPSCameraActive() ) {
        pNode->setMask( gpPostFX->retrieveRadialBlurBitmap( RBB_Mask ) );
        return;
    }

    pNode->setMask( gpPostFX->retrieveRadialBlurBitmap( RBB_MaskMap ) );
    CameraGPS *pCameraGPS = GetGPSCamera();
    if ( !pCameraGPS->isInTransition() ) {
        pNode->setIntensity( 0.0f );
        pNode->setCenter( 0.5f, 0.5f );
        return;
    }

    Eigen::Vector4f focus =
        TDUMat4x4MulVec( pCameraGPS->getWorldToCam(), pCameraGPS->getFocus() );
    focus = TDUMat4x4MulVec( gpRender->getRenderPass<RP_Near>()
                                 .pScene->getFrustumWrite()
                                 .getProjectionMatrix(),
                             focus );
    focus /= focus.w();

    const float intensity =
        ( 0.5f - fabsf( pCameraGPS->getTransitionBlend() - 0.5f ) ) *
        kGPSRadialBlurScale;
    pNode->setIntensity( ( intensity > 0.0f ) ? intensity : 0.0f );
    pNode->setCenter( ( focus.x() + 1.0f ) * 0.5f,
                      ( 1.0f - focus.y() ) * 0.5f );
}

void MngCamGPS::updateBackground() {
    // FUN_006b4080
    HiearchyNode *pDomeNode = gpMngBackground->getDomeNode();
    if ( !IsGPSCameraActive() ) {
        if ( pDomeNode != nullptr ) {
            pDomeNode->pInstance->getFlagsWrite() &= ~kInstanceHiddenFlag;
        }
        gpMngBackground->setCloudsGlobalFade( 1.0f );
        return;
    }

    CameraGPS *pCameraGPS = GetGPSCamera();
    const float distance = pCameraGPS->getDistanceToEarthCenter();
    const float closeDistance = pCameraGPS->getZoomDistanceToCenter( 0 );
    const float fade =
        Saturate( ( closeDistance - distance ) /
                  ( closeDistance - pCameraGPS->getEarthRadius() ) );
    if ( pDomeNode != nullptr ) {
        if ( distance >= closeDistance ) {
            pDomeNode->pInstance->getFlagsWrite() |= kInstanceHiddenFlag;
        } else {
            pDomeNode->pInstance->getFlagsWrite() &= ~kInstanceHiddenFlag;
        }
    }
    gpMngBackground->setCloudsGlobalFade( fade );
}

void MngCamGPS::updateOcean() {
    // FUN_006b4810
    if ( !IsGPSCameraActive() ) {
        gpMngOcean->setCurrentSetup( &gDefaultOceanSetup );
        toggleGPSClipPlanes( false, false );
        bFirstFrameInGPS = true;
        bClipPlanesLatched = false;
        return;
    }

    if ( bFirstFrameInGPS ) {
        bFirstFrameInGPS = false;
        bClipPlanesLatched = false;
    }

    CameraGPS *pCameraGPS = GetGPSCamera();
    const float distance = pCameraGPS->getDistanceToEarthCenter();
    bool bVisible = true;
    if ( IsSpaceView( pCameraGPS ) ) {
        bVisible = pCameraGPS->isInTransition() && earthFadeNearDist > distance;
    }
    gpMngOcean->setOceanVisible( bVisible );
    if ( !bVisible ) {
        toggleGPSClipPlanes( false, false );
        return;
    }

    const float earthRadius = pCameraGPS->getEarthRadius();
    const float closeDistance = pCameraGPS->getZoomDistanceToCenter( 0 );
    const float t = GetSurfaceToCloseBlend( pCameraGPS );
    const float s = 1.0f - t;
    const OceanSetup &defaults = gDefaultOceanSetup;
    oceanSetup.ReflectionBluriness =
        s * defaults.ReflectionBluriness + t * gGPSOceanBluriness;
    oceanSetup.SpecularPower =
        s * defaults.SpecularPower + t * gGPSOceanSpecularPower;
    oceanSetup.AttenuationFresnel =
        s * defaults.AttenuationFresnel + t * gGPSOceanAttFresnel;
    oceanSetup.AttenuationPerturbation =
        s * defaults.AttenuationPerturbation + t * gGPSOceanAttPerturbation;
    oceanSetup.AttenuationNormal =
        s * defaults.AttenuationNormal + t * gGPSOceanAttNormal;
    oceanSetup.NMapHeightScaler =
        s * defaults.NMapHeightScaler + t * gGPSOceanNMapHeightScaler;
    oceanSetup.FresnelBias =
        s * defaults.FresnelBias + t * gGPSOceanFresnelBias;
    oceanSetup.FresnelPower =
        s * defaults.FresnelPower + t * gGPSOceanFresnelPower;
    gpRender->getShaderUniformWrite( 5 ).x() = Saturate( s );

    const float scalers[CameraGPS::eZoomLevel::GZL_Count] = {
        Max( defaults.NMapUVScalerLR, defaults.NMapUVScalerHR ),
        Min( defaults.NMapUVScalerLR, defaults.NMapUVScalerHR ),
        kGPSNMapUVScalers[0],
        kGPSNMapUVScalers[1],
        kGPSNMapUVScalers[2],
        kGPSNMapUVScalers[3] };
    const float bandDistances[CameraGPS::eZoomLevel::GZL_Count] = {
        earthRadius,
        pCameraGPS->getZoomDistanceToCenter( 0 ),
        pCameraGPS->getZoomDistanceToCenter( 1 ),
        pCameraGPS->getZoomDistanceToCenter( 2 ),
        pCameraGPS->getZoomDistanceToCenter( 3 ),
        pCameraGPS->getSpaceViewDistanceToCenter() };
    const int32_t currentBand = ZoomToAltitudeBand( pCameraGPS->getZoom() );
    const int32_t previousBand =
        ZoomToAltitudeBand( pCameraGPS->getPreviousZoom() );
    if ( pCameraGPS->isInTransition() ) {
        int32_t upperBand = 1;
        while ( upperBand < 5 && distance >= bandDistances[upperBand] ) {
            upperBand++;
        }

        const bool bAscending = ( currentBand - previousBand ) > 0;
        int32_t index = bAscending ? upperBand : upperBand - 1;
        float f = Saturate(
            ( distance - bandDistances[upperBand - 1] ) /
            ( bandDistances[upperBand] - bandDistances[upperBand - 1] ) );
        if ( !bAscending ) {
            f = 1.0f - f;
        }

        float coeffLR = 0.5f;
        float coeffHR = 0.5f;
        if ( bAscending ) {
            if ( 0.5f > f ) {
                index--;
                coeffHR = 0.5f - f;
            } else {
                coeffLR = f - 0.5f;
            }
        } else {
            if ( 0.5f > f ) {
                index++;
                coeffLR = 0.5f - f;
            } else {
                coeffHR = f - 0.5f;
            }
        }

        index = Clamp( index, 1, 5 );
        oceanSetup.NMapUVScalerLR = scalers[index];
        oceanSetup.NMapUVScalerHR = scalers[index - 1];
        oceanSetup.NMapCoeffLR = coeffLR;
        oceanSetup.NMapCoeffHR = coeffHR;
    } else {
        const int32_t index = Clamp( currentBand, 1, 5 );
        oceanSetup.NMapUVScalerLR = scalers[index];
        oceanSetup.NMapUVScalerHR = scalers[index - 1];
        oceanSetup.NMapCoeffLR = 1.0f;
        oceanSetup.NMapCoeffHR = 1.0f;
    }

    const float seaLevel =
        gpMngOcean->getOceanHeight() - gpMngOcean->getWaveAmplitude() * 0.5f;
    const float surfaceDistance = earthRadius + kGPSAltitudeSectors;
    const float u = Saturate( ( distance - closeDistance ) /
                              ( surfaceDistance - closeDistance ) );
    const float planeHeight = u * kGPSClipPlaneSurfaceOffset -
                              ( 1.0f - u ) * kGPSClipPlaneCloseOffset +
                              seaLevel;
    toggleGPSClipPlanes( true, bClipPlanesLatched );
    if ( surfaceDistance >= distance ) {
        toggleGPSClipPlanes( true, true );
        bClipPlanesLatched = true;
    } else {
        bClipPlanesLatched = false;
    }

    Matrix4x4f_t viewProj = gpCamera->getDefaultCamera()->getWorldToCam() *
                            gpCamera->getNearFrustum().getProjectionMatrix();
    Matrix4x4f_t invViewProj = viewProj.inverse();
    invViewProj.transposeInPlace();
    Eigen::Vector4f clipPlane = TDUMat4x4MulVec(
        invViewProj, Eigen::Vector4f( 0.0f, 1.0f, 0.0f, -planeHeight ) );
    clipPlaneSetup.setPlane( clipPlane, 2 );

    const ColorRGBA reflectionColor =
        PackFloatColor( oceanSetup.ReflectionColor );
    const uint8_t alpha = static_cast<uint8_t>( t * 255.0f );
    const uint32_t packedColor =
        ( (uint32_t)alpha << 24 ) | ( (uint32_t)reflectionColor.R << 16 ) |
        ( (uint32_t)reflectionColor.G << 8 ) | (uint32_t)reflectionColor.B;
    pFadeMapDrawList->reset();
    pFadeMapDrawList->setIdentityTransform();
    pFadeMapDrawList->setActiveMaterial( pMatFadeMap );
    if ( pFadeMapDrawList->beginPrimitive( PT_TriangleStrip, 4 ) ) {
        pFadeMapDrawList->setDiffuse( 0, ColorRGBA( packedColor ) );
        pFadeMapDrawList->pushVertex( -kGPSFadeMapInset, -kGPSFadeMapInset,
                                      0.0f );
        pFadeMapDrawList->pushVertex( -kGPSFadeMapInset,
                                      1.0f - kGPSFadeMapInset, 0.0f );
        pFadeMapDrawList->pushVertex( 1.0f - kGPSFadeMapInset,
                                      -kGPSFadeMapInset, 0.0f );
        pFadeMapDrawList->pushVertex( 1.0f - kGPSFadeMapInset,
                                      1.0f - kGPSFadeMapInset, 0.0f );
        pFadeMapDrawList->commitPrimitive();
    }
    gpMngOcean->getReflection2DScene().enqueueDynamicDrawList(
        pFadeMapDrawList );
    gpMngOcean->setCurrentSetup( &oceanSetup );
}

void MngCamGPS::OnWeatherConfigUpdate( WeatherConfig *pConfig ) {
    // FUN_006b4440
    if ( !IsGPSCameraActive() ) {
        return;
    }

    CameraGPS *pCameraGPS = GetGPSCamera();
    const int32_t mapIndex = gpWeather->getWeatherConfigIndex( "MAP" );
    WeatherConfig *pMap =
        ( mapIndex < 0 ||
          mapIndex >= gpWeather->getNumRegisteredWeatherConfig() )
            ? nullptr
            : gpWeather->getWeatherConfigByIndex( mapIndex );
    if ( pCameraGPS == nullptr || pMap == nullptr ) {
        return;
    }

    const float t = GetSurfaceToCloseBlend( pCameraGPS );
    const float s = 1.0f - t;
    pConfig->setFogMax( pMap->getFogMax() * t + pConfig->getFogMax() * s );
    pConfig->setSunAmbientColor( pMap->getSunAmbientColor() * t +
                                 pConfig->getSunAmbientColor() * s );
    pConfig->setSunDiffuseColor( pMap->getSunDiffuseColor() * t +
                                 pConfig->getSunDiffuseColor() * s );
    pConfig->setSunSpecularColor( pMap->getSunSpecularColor() * t +
                                  pConfig->getSunSpecularColor() * s );
    const Eigen::Vector3f sunDirection =
        ( pMap->getSunDirection() * t + pConfig->getSunDirection() * s )
            .normalized();
    pConfig->setSunDirection( Eigen::Vector4f(
        sunDirection.x(), sunDirection.y(), sunDirection.z(), 0.0f ) );
    pConfig->setHeightmapColor( pMap->getHeightmapColor() * t +
                                pConfig->getHeightmapColor() * s );
    pConfig->setHeightmapContrast( pMap->getTerrainContrast() * t +
                                   pConfig->getTerrainContrast() * s );

    // OTDU_ADDITION: the map look ignores night time, like the rest of this
    // blend (MAP keeps the neutral night parameters).
    pConfig->setNightParams( pMap->getNightParams() * t +
                             pConfig->getNightParams() * s );
    pConfig->setLampParams( pMap->getLampParams() * t +
                            pConfig->getLampParams() * s );
}

void MngCamGPS::OnHDRConfigUpdate( HDRIConfig *pConfig ) {
    // FUN_006b4340
    if ( !IsGPSCameraActive() ) {
        return;
    }

    CameraGPS *pCameraGPS = GetGPSCamera();
    HDRIConfig *pMap = gpWeather->getHDRConfigByName( "MAP" );
    if ( pCameraGPS == nullptr || pMap == nullptr ) {
        return;
    }

    const float t = GetSurfaceToCloseBlend( pCameraGPS );
    const float s = 1.0f - t;
    pConfig->BrightThresholdGlow =
        pMap->BrightThresholdGlow * t + pConfig->BrightThresholdGlow * s;
    pConfig->GlowMod = pMap->GlowMod * t + pConfig->GlowMod * s;
    pConfig->BrightThresholdStar =
        pMap->BrightThresholdStar * t + pConfig->BrightThresholdStar * s;
    pConfig->StarMod = pMap->StarMod * t + pConfig->StarMod * s;
}
