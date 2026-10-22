#include "shared.h"
#include "vehicle_base.h"

#include "car_state.h"
#include "mng_car_alpha.h"
#include "game/scenaric/entity/entity_driver.h"
#include "game/scenaric/entity/entity_avatar_passenger.h"
#include "physics/vehicle/physics_vehicle.h"
#include "world/weather/gs_weather.h"
#include "world/gs_world_streaming.h"

#include "database/gs_database.h"
#include "gs_timer.h"
#include "render/camera/gs_camera.h"
#include "gs_car_display.h"
#include "tdu_instance.h"
#include "bank_vehicle.h"

#include "render/material.h"
#include "render/2dm.h"
#include "render/2db.h"
#include "render/mng_shadow.h"
#include "render/mng_env_map.h"
#include "render/render_context.h"

#include "gs_car_common.h"
#include "gs_car_colors.h"
#include "game/mng_hud.h"
#include "world/gs_mng_resources.h"
#include "world/otdu_lights.h"

static constexpr uint64_t kHashcodeGPSScreenShader = 0x7dad16b47eb57b95ull;
static constexpr float kGPSScreenParamZ = 3.0f; // DAT_00f416b4

static const uint64_t kHashcodeForkFL =
    GetIdentifier64bit( "FORK_FL" ); // DAT_010e74c0
static const uint64_t kHashcodeForkFR =
    GetIdentifier64bit( "FORK_FR" ); // DAT_010e74c8
static const uint64_t kHashcodeForkRL =
    GetIdentifier64bit( "FORK_RL" ); // DAT_010e74d0
static const uint64_t kHashcodeForkRR =
    GetIdentifier64bit( "FORK_RR" ); // DAT_010e74d8

static constexpr const char *kShadowHashcode = "D_SHADOW";      // DAT_00f7ce4c
static constexpr const char *kShadowPlaneHashcode = "D_SPLANE"; // DAT_00f7ce58

// DAT_00fab4a4
static constexpr const char *kWheelHashcodes[] = {
    "WLR_FL", "WLR_FR", "WLR_RL", "WLR_RR", "WLR_F",
    "WLR_F",  "WLR_R",  "WLR_R" }; // Cars + Bike hashcodes (NOTE: duplication
                                   // is intentional)

float VehicleBase::FlareScaleStart = 0.0f;
float VehicleBase::FlareScaleRatio = 0.0f;
float VehicleBase::FlareZBiasRatio = 0.0f;
float VehicleBase::FlareOldStartDistScale = 0.0f;
float VehicleBase::FlareOldLengthDistScale = 0.0f;
float VehicleBase::FlareOldScaleRatio = 0.0f;
static constexpr float gFlareZBiasRatio = 0.004f; // DAT_00f7ced4

// TODO: I am not sure if VehicleBase is supposed to own those or if those are
// globals...
bool VehicleBase::bApplyWheelAttenuation = true; // DAT_00f7f198
float VehicleBase::MinWheelAttenuation = 0.2f;   // DAT_00f7f194
bool VehicleBase::bCalibratedDeluxePaint = false;

static float gSunLODFactor = 1.0f; // DAT_00f7ced0

VehicleBase *gpCurEnvMappedCar = nullptr; // DAT_010e7514

static bool BOOL_010e74e0 = false; // DAT_010e74e0
static uint64_t DAT_010c4818[64];  // DAT_010c4818

// DAT_00f7cee8
static constexpr const char *kExhaustNames[] = {
    "EXH_FX0", "EXH_FX1", "EXH_FX2", "EXH_FX3",
    "EXO_FX0", "EXO_FX1", "EXO_FX2", "EXO_FX3",
};

// DAT_010c47f8
static uint64_t gExhaustHashcodesH[4];

// DAT_010c47d8
static uint64_t gExhaustHashcodesO[4];

static bool gbExhaustHashcodeListInit = false; // DAT_010e74e2

inline bool
FUN_008bbfa9( const int32_t iVar17, const char *pcVar20, const char *pcVar21 ) {
    // FUN_008bbfa9 (inlined)
    bool bVar22 = true;
    int32_t iVar13 = iVar17;
    do {
        if ( iVar13 == 0 )
            break;
        iVar13 = iVar13 + -1;
        bVar22 = *pcVar20 == *pcVar21;
        pcVar20 = pcVar20 + 1;
        pcVar21 = pcVar21 + 1;
    } while ( bVar22 );

    return bVar22;
}

static const Eigen::Vector4f kTurnSignalOrange( 1.0f, 0.45f, 0.05f, 1.0f );

struct TurnSignalMaterialBackup {
    Eigen::Vector4f emissive;
    Eigen::Vector4f diffuse;
    float shaderColor3e[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    bool bHasShaderColor3e = false;
};

static std::unordered_map<Material *, TurnSignalMaterialBackup>
    g_turnSignalMaterialBackups;

static void captureTurnSignalMaterialBackup( Material *pMaterial ) {
    if ( pMaterial == nullptr ||
         g_turnSignalMaterialBackups.find( pMaterial ) !=
             g_turnSignalMaterialBackups.end() ) {
        return;
    }

    TurnSignalMaterialBackup backup;
    backup.emissive = pMaterial->EmissiveColor;
    backup.diffuse = pMaterial->DiffuseColor;

    MaterialParameter *pParameter = pMaterial->getParameterByIndex( 0 );
    if ( pParameter != nullptr && pParameter->Type == 2 ) {
        MaterialShaderParameterArray *pShaderParams =
            (MaterialShaderParameterArray *)pParameter;
        uint32_t parameterIndex = 0;
        if ( pShaderParams->getParameter( 0x3e, &parameterIndex ) != nullptr &&
             parameterIndex < pShaderParams->NumParameters ) {
            const float *pColorParams =
                (const float *)pShaderParams->getParameterDataByIndex(
                    parameterIndex );
            if ( pColorParams != nullptr ) {
                backup.shaderColor3e[0] = pColorParams[0];
                backup.shaderColor3e[1] = pColorParams[1];
                backup.shaderColor3e[2] = pColorParams[2];
                backup.shaderColor3e[3] = pColorParams[3];
                backup.bHasShaderColor3e = true;
            }
        }
    }

    g_turnSignalMaterialBackups[pMaterial] = backup;
}

static void restoreTurnSignalMaterialBackup( Material *pMaterial ) {
    if ( pMaterial == nullptr ) {
        return;
    }

    const auto backupIt = g_turnSignalMaterialBackups.find( pMaterial );
    if ( backupIt == g_turnSignalMaterialBackups.end() ) {
        return;
    }

    const TurnSignalMaterialBackup &backup = backupIt->second;
    pMaterial->EmissiveColor = backup.emissive;
    pMaterial->DiffuseColor = backup.diffuse;

    MaterialParameter *pParameter = pMaterial->getParameterByIndex( 0 );
    if ( backup.bHasShaderColor3e && pParameter != nullptr &&
         pParameter->Type == 2 ) {
        MaterialShaderParameterArray *pShaderParams =
            (MaterialShaderParameterArray *)pParameter;
        uint32_t parameterIndex = 0;
        if ( pShaderParams->getParameter( 0x3e, &parameterIndex ) != nullptr &&
             parameterIndex < pShaderParams->NumParameters ) {
            float *pColorParams =
                (float *)pShaderParams->getParameterDataByIndex(
                    parameterIndex );
            if ( pColorParams != nullptr ) {
                pColorParams[0] = backup.shaderColor3e[0];
                pColorParams[1] = backup.shaderColor3e[1];
                pColorParams[2] = backup.shaderColor3e[2];
                pColorParams[3] = backup.shaderColor3e[3];
            }
        }
    }
}

// Only called from blink flare updates (FT_BlinkL / FT_BlinkR materials).
static void applyTurnSignalColorToMaterial( Material *pMaterial,
                                            const float intensity ) {
    if ( pMaterial == nullptr ) {
        return;
    }

    captureTurnSignalMaterialBackup( pMaterial );

    if ( intensity <= 0.0f ) {
        restoreTurnSignalMaterialBackup( pMaterial );
        return;
    }

    const Eigen::Vector4f scaledColor = kTurnSignalOrange * intensity;
    pMaterial->EmissiveColor = scaledColor;
    pMaterial->DiffuseColor = Eigen::Vector4f( scaledColor.x(), scaledColor.y(),
                                               scaledColor.z(), 1.0f );

    MaterialParameter *pParameter = pMaterial->getParameterByIndex( 0 );
    if ( pParameter == nullptr || pParameter->Type != 2 ) {
        return;
    }

    MaterialShaderParameterArray *pShaderParams =
        (MaterialShaderParameterArray *)pParameter;

    uint32_t parameterIndex = 0;
    if ( pShaderParams->getParameter( 0x3e, &parameterIndex ) != nullptr &&
         parameterIndex < pShaderParams->NumParameters ) {
        float *pColorParams =
            (float *)pShaderParams->getParameterDataByIndex( parameterIndex );
        if ( pColorParams != nullptr ) {
            pColorParams[0] = scaledColor.x();
            pColorParams[1] = scaledColor.y();
            pColorParams[2] = scaledColor.z();
        }
    }
}

static void applyTurnSignalColorToFlareNode( HiearchyNode *pNode,
                                             const float intensity ) {
    if ( pNode == nullptr || pNode->pInstance == nullptr ) {
        return;
    }

    Instance *pInstance = pNode->pInstance;
    const uint32_t numMaterials = pInstance->getMaterialBankSize();
    for ( uint32_t materialIndex = 0; materialIndex < numMaterials;
          materialIndex++ ) {
        Material *pMaterial = pInstance->getMaterial( materialIndex );
        applyTurnSignalColorToMaterial( pMaterial, intensity );
    }
}

inline void LAB_008bc033( Material *puVar15,
                          const VehicleBase::eType type,
                          const VehicleBase::eFlareType param_2,
                          const float param_3 ) {
    // FUN_008bc033 (inlined)
    MaterialParameter *pParameter = puVar15->getParameterByIndex( 0 );
    if ( pParameter != nullptr && pParameter->Type == 2 ) {
        MaterialShaderParameterArray *peVar8 =
            (MaterialShaderParameterArray *)pParameter;

        uint32_t uVar16 = 0;
        MaterialShaderParameter *peVar11 =
            peVar8->getParameter( 0x4e, &uVar16 );
        uint32_t uVar14 = peVar8->NumParameters;
        if ( uVar16 != uVar14 && peVar11 != nullptr ) {
            float *pfVar7 = (float *)peVar8->getParameterDataByIndex( uVar16 );

            if ( type == VehicleBase::eType::VBT_TrafficCar ) {
                switch ( param_2 ) {
                case VehicleBase::eFlareType::FT_Brake:
                case VehicleBase::eFlareType::FT_BrakeFL:
                case VehicleBase::eFlareType::FT_BrakeFR:
                case VehicleBase::eFlareType::FT_BrakeRL:
                case VehicleBase::eFlareType::FT_BrakeRR:
                    *pfVar7 = param_3;
                    break;
                case VehicleBase::eFlareType::FT_BlinkL:
                case VehicleBase::eFlareType::FT_BlinkR:
                    pfVar7[1] = param_3;
                    break;
                case VehicleBase::eFlareType::FT_Headlights:
                case VehicleBase::eFlareType::FT_HeadlightsRear:
                case VehicleBase::eFlareType::FT_HeadlightsFL:
                case VehicleBase::eFlareType::FT_HeadlightsFR:
                case VehicleBase::eFlareType::FT_HeadlightsRL:
                case VehicleBase::eFlareType::FT_HeadlightsRR:
                    pfVar7[2] = param_3;
                    break;
                default:
                    break;
                }
            } else {
                switch ( param_2 ) {
                case VehicleBase::eFlareType::FT_Brake:
                    *pfVar7 = param_3;
                    break;
                case VehicleBase::eFlareType::FT_BlinkL:
                case VehicleBase::eFlareType::FT_BlinkR:
                    pfVar7[3] = param_3;
                    break;
                case VehicleBase::eFlareType::FT_Reverse:
                    pfVar7[1] = param_3;
                    break;
                case VehicleBase::eFlareType::FT_Headlights:
                case VehicleBase::eFlareType::FT_HeadlightsRear:
                    pfVar7[2] = param_3;
                    break;
                default:
                    break;
                }
            }
        }
    }

    if ( type != VehicleBase::eType::VBT_TrafficCar &&
         ( param_2 == VehicleBase::eFlareType::FT_BlinkL ||
           param_2 == VehicleBase::eFlareType::FT_BlinkR ) ) {
        applyTurnSignalColorToMaterial( puVar15, param_3 );
    }
}

static bool IsMaterialForColorPart( const VehicleBase::eType type,
                                    const eCarColorPart part,
                                    const uint64_t hashcode ) {
    // FUN_008b9fd0
    const bool bIsTraffic = ( type == VehicleBase::eType::VBT_TrafficCar ||
                              type == VehicleBase::eType::VBT_TrafficBike );
    const bool bIsTrafficLightCluster =
        bIsTraffic && ( TestDriveString8StartsWith( hashcode, "LGT_L" ) ||
                        TestDriveString8StartsWith( hashcode, "LGT_R" ) );

    const bool bIsMainPaint = TestDriveString8StartsWith( hashcode, "PAINT" ) &&
                              !TestDriveString8StartsWith( hashcode, "PAINT2" );

    switch ( part ) {
    case CCP_BodyColor:
        return bIsMainPaint || TestDriveString8StartsWith( hashcode, "P_" ) ||
               bIsTrafficLightCluster;

    case CCP_InteriorColor1:
        return TestDriveString8StartsWith( hashcode, "C1" );

    case CCP_InteriorColor2:
        return TestDriveString8StartsWith( hashcode, "C2" );

    case CCP_LogoColor1:
        return bIsMainPaint || bIsTrafficLightCluster;

    case CCP_LogoColor2:
        return TestDriveString8StartsWith( hashcode, "PAINT2" ) ||
               bIsTrafficLightCluster;

    case CCP_InteriorMaterial:
        return TestDriveString8StartsWith( hashcode, "MAT_" );

    case CCP_BodyColor2:
        return TestDriveString8StartsWith( hashcode, "PAINT2" );

    default:
        break;
    }

    return false;
}

template <typename TCallback>
static void
ForEachColorPartMaterial( const std::array<VehicleResource *, 2> &pResNodes,
                          const int32_t numResNodes,
                          const VehicleBase::eType type,
                          const eCarColorPart part,
                          TCallback &&callback ) {
    // FUN_008bc940 / FUN_008bc6a0 / FUN_008bcbd0 / FUN_008bce60 / FUN_008bd0b0
    // (inlined)
    for ( int32_t nodeIndex = 0; nodeIndex < numResNodes; nodeIndex++ ) {
        VehicleResource *pResNode = pResNodes[nodeIndex];
        if ( pResNode == nullptr || pResNode->pRender3DD == nullptr ||
             pResNode->pResourceBank == nullptr ) {
            continue;
        }

        Render2DM &resource2DM = pResNode->Resource2DM;
        const uint32_t numMaterials = resource2DM.getNumMaterials();
        for ( uint32_t matIndex = 0; matIndex < numMaterials; matIndex++ ) {
            Material *pMaterial = resource2DM.getMaterialAtIndex( matIndex );
            if ( pMaterial == nullptr ||
                 !IsMaterialForColorPart(
                     type, part,
                     resource2DM.getMaterialHashcode( matIndex ) ) ) {
                continue;
            }

            callback( pMaterial );
        }
    }
}

static void InstallClonedInteriorMaterial( VehicleResource &resource,
                                           Material *pClone,
                                           const uint64_t hashcode ) {
    // FUN_008ba480
    if ( pClone == nullptr ) {
        return;
    }

    resource.Resource3DD.rebindMaterial( hashcode,
                                         (RenderFile::Section *)pClone - 1 );
    gpRenderContext->retrieveShadersForMaterial( pClone, 0 );
}

VehicleBase::VehicleBase()
    : CarStateEventListener(),
      CarAlphaListener(),
      pCarState( nullptr ),
      numHiearchyNodes( 0 ),
      bValidPosition( false ),
      bLightsDirty( false ),
      bInitialized( false ),
      bShared( false ),
      bShouldDraw( false ),
      bVisible( false ),
      bNeedToUnstream( false ),
      bUseHQInterior( false ),
      bUnknownFlag( false ),
      bDisableShadow( false ),
      bForceDrawShadow( false ),
      bFirstRenderFrame( false ),
      bPaintChange( false ),
      bHasColorStrips( false ),
      bUseTwoTonesPaint( false ),
      bDrawReflections( false ),
      bDebugForceLOD( false ),
      bDebugForcePrimVisibility( false ),
      bIsRealTimeEnvMap( false ),
      currentEnvFactor( 0.0f ),
      currentEnvFactorBackFace( 0.0f ),
      currentLuminosity( 0.0f ),
      type( eType::VBT_Car ),
      currentLOD( eLOD::VBL_Lvl0 ),
      armAngleOffset( 0.0f ),
      armLength( 0.0f ),
      dotFork( 0.0f ),
      pVehicleBodyLowResInstance( nullptr ),
      hashGlassExterior( 0ull ),
      hashGlassInterior( 0ull ),
      alpha( 1.0f ),
      alphaSlotIndex( -1 ),
      bAlphaCacheInUse( false ),
      interiorMaterials(),
      tireBufferOffset( 0 ) {
    // FUN_008baf80
    memset( pTireBuffer, 0, sizeof( uint8_t ) * 4096 );
    vehicleWheelLowResInstances.fill( nullptr );
    pCurResNodesUsed.fill( nullptr );
}

VehicleBase::~VehicleBase() {
    // FUN_008bae90
}

void VehicleBase::setupLights() {
    // FUN_008c0dc0
    if ( gpWeather->isConfigDirty() ) {
        gpWeather->updateActiveConfig();
    }

    // Exterior
    gpWeather->getActiveWeatherConfig().fillDirectionalLight(
        mainLightInterior );
    gpWeather->getActiveWeatherConfig().fillDirectionalLight( mainLight );
    lightCfg.addLight( &mainLight );
    lightCfg.addLight( &mainLightInterior );

    // Wheels (left)
    wheelLLight = mainLight;
    wheelLILight = mainLightInterior;

    lightCfgWheelL.addLight( &wheelLLight );
    lightCfgWheelL.addLight( &wheelLILight );

    // Wheels (right)
    wheelRLight = mainLight;
    wheelRILight = mainLightInterior;

    lightCfgWheelR.addLight( &wheelRLight );
    lightCfgWheelR.addLight( &wheelRILight );

    currentEnvFactor = 1.0f;
    currentEnvFactorBackFace = 0.1f;
    pWheelOffsets[0] = 0.0f;
    pWheelOffsets[1] = 0.0f;
    pWheelOffsets[2] = 0.0f;
    pWheelOffsets[3] = 0.0f;
}

eRenderPass VehicleBase::getRenderPass() {
    // FUN_00413ab0
    return eRenderPass::RP_Near;
}

eRenderPass VehicleBase::getRenderPassWithAlpha() {
    // FUN_008b9e00
    eRenderPass GVar1 = eRenderPass::RP_Near;
    if ( alphaSlotIndex != -1 ) {
        GVar1 = gpMngCarAlpha->bindAndGetRenderPassForAlpha(
            eRenderPass::RP_Near, alphaSlotIndex );
    }
    return GVar1;
}

void VehicleBase::reset() {
    // FUN_008ba4f0
    bInitialized = false;
    bValidPosition = false;
    bShared = false;
    bVisible = true;
    bShouldDraw = true;

    pParentNode = nullptr;
    pParentShadowNode = nullptr;
    pParentShadowPlaneNode = nullptr;

    pCarState = nullptr;

    numSpokesRim = 5.0f;

    bNeedToUnstream = false;
    bUnknownFlag = false;
    bDisableShadow = false;
    bForceDrawShadow = false;

    pShadowTexture = nullptr;

    for ( VehicleSceneNode &node : childrenNodes ) {
        node.pSceneNode = nullptr;
        node.pInstance = nullptr;
    }

    numKits = 0;
    currentKitIndex = 0;
    kitHiearchyNodes.clear();

    pMaterialGlassExterior = nullptr;
    pMaterialGlassInterior = nullptr;

    bodyResource.pResourceBank = nullptr;
    bodyResource.pRender2DM = nullptr;
    bodyResource.pRender3DD = nullptr;

    interiorResource.pResourceBank = nullptr;
    interiorResource.pRender2DM = nullptr;
    interiorResource.pRender3DD = nullptr;

    particles.reset();

    pCurrentRimDB = nullptr;
    bHasColorStrips = false;
    bUseTwoTonesPaint = false;

    interiorMaterials.reset();

    VehicleBase::FlareScaleStart = 20.0f;
    VehicleBase::FlareScaleRatio = 25.0f;
    VehicleBase::FlareZBiasRatio = 3.0f;
    VehicleBase::FlareOldStartDistScale = 0.0f;
    VehicleBase::FlareOldLengthDistScale = 0.0f;
    VehicleBase::FlareOldScaleRatio = 0.0f;

    bDrawReflections = false;
    bDebugForceLOD = false;
    bDebugForcePrimVisibility = false;
    bIsRealTimeEnvMap = false;
    bPaintChange = false;

    pExteriorBank = nullptr;
    pInteriorBank = nullptr;
    pRimsFrontBank = nullptr;
    pRimsRearBank = nullptr;

    flaresRL.fill( nullptr );
    flaresRR.fill( nullptr );
    flaresFL.fill( nullptr );
    flaresFR.fill( nullptr );
    flares.fill( nullptr );

    headlightState = eHeadlightState::VHS_Open;

    pVehicleBodyInstance = nullptr;
    vehicleWheelInstances.fill( nullptr );

    paint.clear();
}

Instance *VehicleBase::getInstanceByIndex( const uint32_t param_2 ) {
    if ( bNeedToUnstream ) {
        return nullptr;
    }

    OTDU_ASSERT( param_2 < displayInstances.size() );
    return displayInstances[param_2].pInstance;
}

HiearchyNode *VehicleBase::getNodeByIndex( const uint32_t param_2 ) {
    // FUN_004138a0
    if ( bNeedToUnstream ) {
        return nullptr;
    }

    OTDU_ASSERT( param_2 < displayInstances.size() );
    return displayInstances[param_2].pNode;
}

const Matrix4x4f_t *VehicleBase::getNodeTransform( const uint32_t param_2 ) {
    // FUN_00413750
    if ( bNeedToUnstream ) {
        return nullptr;
    }

    OTDU_ASSERT( param_2 < displayInstances.size() );
    return &displayInstances[param_2].pNode->TransformMatrix;
}

static bool RebindReflectionTexture( MaterialParameter *pParam,
                                     uint64_t cubemapHash,
                                     RenderFile::Section *pCubemapSection,
                                     uint64_t cubemapBHash,
                                     RenderFile::Section *pCubemapBSection ) {
    // FUN_008b5cd0
    if ( pParam->NumLayers == 0 ) {
        return false;
    }

    MaterialLayer *pLayer = pParam->getLayer( 0 );
    for ( uint32_t i = 0; i < pLayer->NumTextures; i++ ) {
        MaterialLayerTexture &texture = pLayer->pLayerTextures[i];
        if ( texture.Hashcode == cubemapHash ) {
            texture.pTextureSection = pCubemapSection;
            return true;
        }
        if ( texture.Hashcode == cubemapBHash ) {
            texture.pTextureSection = pCubemapBSection;
            return true;
        }
    }

    return false;
}

void VehicleBase::setRealTimeEnvMap( Render2DB *param_1,
                                     bool param_2,
                                     Render2DB *param_3 ) {
    // FUN_008b6380
    if ( param_2 && gpMngEnvMap == nullptr ) {
        return;
    }

    bIsRealTimeEnvMap = param_2;

    const uint64_t cubemapHash = GetIdentifier64bit( "CUBEMAP" );
    RenderFile::Section *pCubemapSection = nullptr;
    if ( param_2 ) {
        param_1 = &gpMngResources->getCubemapResources().getCubemap().Render2DB;
        pCubemapSection = param_1->getBitmapSection();
    } else {
        if ( param_1 == nullptr && gpCarCommon != nullptr ) {
            param_1 = gpCarCommon->getRender2DB( cubemapHash );
        }
        if ( param_1 != nullptr ) {
            pCubemapSection = param_1->getBitmapSection();
        }
    }

    const uint64_t cubemapBHash = GetIdentifier64bit( "CUBEMAPB" );
    RenderFile::Section *pCubemapBSection = nullptr;
    if ( param_2 ) {
        param_3 =
            &gpMngResources->getCubemapResources().getCubemapB().Render2DB;
        pCubemapBSection = param_3->getBitmapSection();
    } else {
        if ( param_3 == nullptr && gpCarCommon != nullptr ) {
            param_3 = gpCarCommon->getRender2DB( cubemapBHash );
        }
        if ( param_3 == nullptr ) {
            return;
        }
        pCubemapBSection = param_3->getBitmapSection();
    }

    if ( pCubemapSection == nullptr || pCubemapBSection == nullptr ) {
        return;
    }

    VehicleResource *pResourceNodes[4] = { &bodyResource, &wheelsFront,
                                           &interiorResource, &wheelsRear };

    for ( int32_t nodeIndex = 0; nodeIndex < 4; nodeIndex++ ) {
        VehicleResource *pResource = pResourceNodes[nodeIndex];
        for ( Render2DM *pRender2DM : pResource->Collection2D.Render2DMs ) {
            if ( pRender2DM == nullptr ) {
                continue;
            }

            const uint32_t numMaterials = pRender2DM->getNumMaterials();
            for ( uint32_t matIndex = 0; matIndex < numMaterials; matIndex++ ) {
                Material *pMaterial =
                    pRender2DM->getMaterialAtIndex( matIndex );
                if ( pMaterial == nullptr || pMaterial->NumParams == 0 ) {
                    continue;
                }

                MaterialParameter *pParam = pMaterial->getParameterByIndex( 0 );
                if ( pParam->Type != eMaterialParameterType::MPT_Generic ) {
                    continue;
                }

                const uint64_t guid = pParam->Hashcode;
                const bool bGroupA = ( guid == 0x183157b81c6c7357ull ||
                                       guid == 0xc288dfc455ae5ef8ull ||
                                       guid == 0x1b38348331d3942aull );
                const bool bGroupB = ( guid == 0xb39c35015789e65cull ||
                                       guid == 0xcab12190a00d5c4eull ||
                                       guid == 0x69f07f72719bf862ull );

                const bool bGroupBAllowed =
                    bGroupB && ( type == eType::VBT_Bike ||
                                 pResource != &interiorResource );

                if ( ( bGroupA || bGroupBAllowed ) &&
                     pParam->Flags.bUsingReflection ) {
                    if ( RebindReflectionTexture( pParam, cubemapHash,
                                                  pCubemapSection, cubemapBHash,
                                                  pCubemapBSection ) ) {
                        pParam->Flags.pCustomFlags[2] = param_2;
                        gpRenderContext->retrieveShadersForMaterial( pMaterial,
                                                                     0 );
                    }
                }
            }
        }
    }
}

void VehicleBase::setGPSTexture( Render2DB *param_2 ) {
    // FUN_008b6740
    const uint64_t satelliteMapHashcode = GetIdentifier64bit( "satellitemap" );
    const uint64_t gpsMaterialHashcode = GetIdentifier64bit( "GPS" );

    float uScale = 1.0f;
    float vScale = 1.0f;

    if ( param_2 == nullptr ) {
        for ( Render2DB *pRender2DB :
              interiorResource.Collection2D.Render2DBs ) {
            if ( pRender2DB == nullptr ||
                 pRender2DB->getBitmapSection() == nullptr ) {
                continue;
            }
            if ( pRender2DB->getBitmapHashcode() == satelliteMapHashcode ) {
                param_2 = pRender2DB;
                break;
            }
        }

        if ( param_2 == nullptr ) {
            return;
        }
    } else if ( gpMngHud != nullptr ) {
        uScale = gpMngHud->getMinimapViewportU();
        vScale = gpMngHud->getMinimapViewportV();
    }

    for ( Render2DM *pRender2DM : interiorResource.Collection2D.Render2DMs ) {
        if ( pRender2DM == nullptr || pRender2DM->getHeader() == nullptr ||
             pRender2DM->getMaterialArray() == nullptr ) {
            continue;
        }

        RenderFile::Section *pMaterialSection =
            pRender2DM->getMaterial( gpsMaterialHashcode );
        if ( pMaterialSection == nullptr ) {
            continue;
        }

        Material *pMaterial = (Material *)( pMaterialSection + 1 );
        if ( pMaterial->NumParams == 0 ) {
            return;
        }

        MaterialParameter *pParam = pMaterial->getParameterByIndex( 0 );
        if ( pParam == nullptr ||
             pParam->Hashcode != kHashcodeGPSScreenShader ) {
            return;
        }
        if ( pParam->NumLayers == 0 ) {
            return;
        }

        MaterialLayer *pLayer = pParam->getLayer( 0 );
        if ( pLayer != nullptr ) {
            for ( uint8_t texIndex = 0; texIndex < pLayer->NumTextures;
                  texIndex++ ) {
                MaterialLayerTexture &texture =
                    pLayer->pLayerTextures[texIndex];
                if ( texture.Hashcode == satelliteMapHashcode ) {
                    texture.pTextureSection = param_2->getBitmapSection();
                }
            }
        }

        float *pStaticParams =
            (float *)( (MaterialShaderParameterArray *)pParam )
                ->getParameterDataByIndex( 0 );
        if ( pStaticParams != nullptr ) {
            pStaticParams[0] = uScale;
            pStaticParams[1] = vScale;
            pStaticParams[2] = kGPSScreenParamZ;
        }
        return;
    }
}

void VehicleBase::updateRendering( float param_2, float param_3 ) {
    // FUN_008c0450
    particles.update( this );
    updateLights();

    float fVar2 = 0.0f;
    if ( bLightsDirty && !bFirstRenderFrame && type != eType::VBT_TrafficCar ) {
        fVar2 = pCarState->isReversing() ? 1.0f : 0.0f;
        setFlareValue( eFlareType::FT_Reverse, fVar2 );

        // OTDU_ADDITION: at night the brake lights glow dimly as tail lights
        // (retail player cars have no tail light state; their rear channel
        // masks are unreliable).
        fVar2 = pCarState->isBraking()  ? 1.0f
                : AreOTDUHeadlightsOn() ? GetOTDUTailLightLevel()
                                        : 0.0f;
        setFlareValue( eFlareType::FT_Brake, fVar2 );

        fVar2 = pCarState->isIndicatingLeft() ? 1.0f : 0.0f;
        setFlareValue( eFlareType::FT_BlinkL, fVar2 );

        fVar2 = pCarState->isIndicatingRight() ? 1.0f : 0.0f;
        setFlareValue( eFlareType::FT_BlinkR, fVar2 );

        fVar2 = ( !pCarState->headlightsOn() ||
                  headlightState != eHeadlightState::VHS_Open )
                    ? 0.0f
                    : 1.0f;
        setFlareValue( eFlareType::FT_Headlights, fVar2 );
        setFlareValue( eFlareType::FT_HeadlightsRear, 0.0f );

        fVar2 = pCarState->hazardLightsOn() ? 1.0f : 0.0f;
        setFlareValue( eFlareType::FT_Warnings, fVar2 );

        bLightsDirty = false;
    }
    bFirstRenderFrame = false;

    if ( !displayInstances.empty() && !bNeedToUnstream &&
         gpMngCarAlpha != nullptr && bValidPosition ) {
        updateCarAlpha();
    }
}

void VehicleBase::draw() {
    // FUN_008bb090
    auto enqueueFlares = []( RenderScene *pScene, VehicleBase *pVehicle ) {
        if ( pScene == nullptr || pVehicle->bNeedToUnstream ) {
            return;
        }
        for ( int32_t iVar2 = 0; iVar2 < 0x3c; iVar2++ ) {
            if ( pVehicle->flares[iVar2] != nullptr ) {
                pScene->enqueueDynamicInstance( pVehicle->flares[iVar2] );
            }
        }
    };

    if ( alphaSlotIndex != -1 && !alphaInstances.empty() ) {
        enqueueFlares(
            gpRender->getRenderPass<eRenderPass::RP_NearAfterCarAlpha>().pScene,
            this );
    } else if ( bShouldDraw && bVisible && bValidPosition ) {
        enqueueFlares( gpRender->getRenderPass<eRenderPass::RP_Near>().pScene,
                       this );
    }
}

void VehicleBase::unstream() {
    // FUN_008ba850
    onAlphaChange();

    if ( bodyResource.pResourceBank != nullptr &&
         bodyResource.pResourceBank->isLoaded() ) {
        if ( pVehicleBodyLowResInstance != nullptr ) {
            pVehicleBodyLowResInstance->removeSetupFromGraph( &lightCfg );
            bodyResource.Collection3D.uninstantiate(
                pVehicleBodyLowResInstance );
            pVehicleBodyLowResInstance = nullptr;
        }

        for ( Instance *&pWheelLowRes : vehicleWheelLowResInstances ) {
            if ( pWheelLowRes != nullptr ) {
                pWheelLowRes->removeSetupFromGraph( &lightCfg );
                bodyResource.Collection3D.uninstantiate( pWheelLowRes );
                pWheelLowRes = nullptr;
            }
        }

        if ( pParentShadowPlaneNode != nullptr ) {
            bodyResource.Collection3D.uninstantiate( pParentShadowPlaneNode );
            pParentShadowPlaneNode = nullptr;
        }

        if ( pParentShadowNode != nullptr ) {
            bodyResource.Collection3D.uninstantiate( pParentShadowNode );
            pParentShadowNode = nullptr;
        }
    }
}

void VehicleBase::popLights() {
    // FUN_008b9cd0
    lightCfgWheelR.removeLight( &wheelRILight );
    lightCfgWheelR.removeLight( &wheelRLight );

    lightCfgWheelL.removeLight( &wheelLILight );
    lightCfgWheelL.removeLight( &wheelLLight );

    lightCfg.removeLight( &mainLightInterior );
    lightCfg.removeLight( &mainLight );
}

void VehicleBase::setFlareValue( VehicleBase::eFlareType param_2,
                                 const float param_3 ) {
    // FUN_008bb7a0
    uint8_t bVar6 = 0;
    int32_t local_6c = 0;

    TestDriveString8 local_58[10];

    if ( type == eType::VBT_TrafficCar ) {
        bVar6 = 1;
        switch ( param_2 ) {
        case eFlareType::FT_Brake:
            local_58[0] = "P_FL";
            local_58[3] = "P_FL";
            local_58[7] = "P_FR";
            local_58[8] = "P_RR";
            local_6c = 2;
            break;
        case eFlareType::FT_BrakeFL:
        case eFlareType::FT_BrakeRL:
            local_58[0] = "P_FL";
            local_6c = 2;
            break;
        case eFlareType::FT_BrakeFR:
            local_58[0] = "P_FR";
            local_6c = 2;
            break;
        case eFlareType::FT_BrakeRR:
            local_58[0] = "P_RR";
            local_6c = 2;
            break;
        case eFlareType::FT_BlinkL:
            local_58[1] = "LGT_L";
            local_58[0] = "P_FL";
            local_58[3] = "P_FL";
            local_6c = 3;
            break;
        case eFlareType::FT_BlinkR:
            local_58[1] = "LGT_R";
            local_58[0] = "P_FR";
            local_58[3] = "P_RR";
            local_6c = 3;
            break;
        case eFlareType::FT_Reverse:
            local_58[1] = "LGT_L";
            local_58[2] = "LGT_R";
            local_58[0] = "P_FL";
            local_58[3] = "P_FL";
            local_58[7] = "P_FR";
            local_58[8] = "P_RR";
            local_6c = 4;
            break;
        case eFlareType::FT_ReverseFL:
            local_58[0] = "P_FL";
            local_6c = 4;
            break;
        case eFlareType::FT_ReverseFR:
            local_58[0] = "P_FR";
            local_6c = 4;
            break;
        case eFlareType::FT_ReverseRL:
            local_58[0] = "P_FL";
            local_6c = 4;
            break;
        case eFlareType::FT_ReverseRR:
            local_58[0] = "P_RR";
            local_6c = 4;
            break;
        case eFlareType::FT_Headlights:
            local_58[1] = "LGT_L";
            local_58[2] = "LGT_R";
            local_58[0] = "P_FL";
            local_58[3] = "P_FR";
            local_6c = 0;
            break;
        case eFlareType::FT_HeadlightsRear:
            local_58[1] = "LGT_L";
            local_58[2] = "LGT_R";
            local_58[0] = "P_FL";
            local_58[3] = "P_RR";
            local_6c = 2;
            break;
        case eFlareType::FT_Warnings:
            local_58[0] = "P_FL";
            local_58[3] = "P_FL";
            local_58[7] = "P_FR";
            local_58[8] = "P_RR";
            local_6c = 1;
            break;
        case eFlareType::FT_HeadlightsFL:
            local_58[0] = "P_FL";
            local_6c = 0;
            break;
        case eFlareType::FT_HeadlightsFR:
            local_58[0] = "P_FR";
            local_6c = 0;
            break;
        case eFlareType::FT_HeadlightsRL:
            local_58[0] = "P_FL";
            local_6c = 0;
            break;
        case eFlareType::FT_HeadlightsRR:
            local_58[0] = "P_RR";
            local_6c = 0;
            break;
        default:
            break;
        }
    } else {
        bVar6 = 0;
        switch ( param_2 ) {
        case eFlareType::FT_Brake:
            local_58[1] = "LGT_RL";
            local_58[2] = "LGT_FL";
            local_58[5] = "LGT_FR";
            local_58[6] = "LGT_RR";
            local_6c = 2;
            break;
        case eFlareType::FT_BlinkL:
            local_58[1] = "LGT_RL";
            local_58[2] = "LGT_FL";
            local_6c = 3;
            break;
        case eFlareType::FT_BlinkR:
            local_58[1] = "LGT_FR";
            local_58[2] = "LGT_RR";
            local_6c = 3;
            break;
        case eFlareType::FT_Reverse:
            local_58[1] = "LGT_RL";
            local_58[2] = "LGT_FL";
            local_58[5] = "LGT_FR";
            local_58[6] = "LGT_RR";
            local_6c = 4;
            break;
        case eFlareType::FT_Headlights:
            local_58[1] = "LGT_FR";
            local_58[2] = "LGT_FL";
            local_6c = 0;
            break;
        case eFlareType::FT_HeadlightsRear:
            local_58[1] = "LGT_RR";
            local_58[2] = "LGT_RL";
            local_6c = 3;
            break;
        case eFlareType::FT_Warnings:
            local_58[1] = "LGT_RL";
            local_58[2] = "LGT_FL";
            local_58[5] = "LGT_FR";
            local_58[6] = "LGT_RR";
            local_6c = 1;
            break;
        default:
            break;
        }
    }

    int32_t resourcePass = 0;
    const int32_t numPasses = ( type == eType::VBT_Bike ) ? 2 : 1;
    const int32_t nameCmpLen = 6 - bVar6;
    RenderFile::Section *pMatArray = nullptr;
    do {
        uint32_t numMaterials = 0;
        if ( resourcePass == 0 ) {
            pMatArray = bodyResource.Resource2DM.getMaterialArray();
            numMaterials = bodyResource.Resource2DM.getNumMaterials();
        } else if ( interiorResource.pRender3DD != nullptr &&
                    interiorResource.pResourceBank != nullptr ) {
            pMatArray = interiorResource.Resource2DM.getMaterialArray();
            numMaterials = interiorResource.Resource2DM.getNumMaterials();
        }

        if ( pMatArray != nullptr ) {
            if ( numMaterials != 0 ) {
                int32_t matIndex = 0;
                Material *puVar15 = nullptr;
                uint64_t uVar12 = 0;
                do {
                    if ( resourcePass == 0 ) {
                        puVar15 = bodyResource.Resource2DM.getMaterialAtIndex(
                            matIndex );
                        uVar12 = bodyResource.Resource2DM.getMaterialHashcode(
                            matIndex );
                    } else {
                        puVar15 =
                            interiorResource.Resource2DM.getMaterialAtIndex(
                                matIndex );
                        uVar12 =
                            interiorResource.Resource2DM.getMaterialHashcode(
                                matIndex );
                    }
                    local_58[4] = uVar12;

                    static constexpr int32_t kFlareNamesLUT[] = { 1, 2, 5, 6,
                                                                  0, 3, 7, 8 };

                    bool bMaterialMatched = false;
                    for ( int32_t i = 0; i < 8; i++ ) {
                        // P_XX slots (i>=4): Ghidra uses 2×int32 = 8-byte
                        // compare.
                        int32_t cmpLength = ( i < 4 ) ? nameCmpLen : 8;
                        bool bVar22 = FUN_008bbfa9(
                            cmpLength, local_58[kFlareNamesLUT[i]].AsChar,
                            local_58[4].AsChar );
                        if ( bVar22 ) {
                            LAB_008bc033( puVar15, type, param_2, param_3 );
                            bMaterialMatched = true;
                            break;
                        }
                    }

                    if ( !bMaterialMatched && bVar6 == 0 &&
                         ( param_2 == eFlareType::FT_BlinkL ||
                           param_2 == eFlareType::FT_BlinkR ) ) {
                        static const uint64_t kBlinkLeftMaterialHashes[] = {
                            GetIdentifier64bit( "LGT_FL" ),
                            GetIdentifier64bit( "LGT_RL" ) };
                        static const uint64_t kBlinkRightMaterialHashes[] = {
                            GetIdentifier64bit( "LGT_FR" ),
                            GetIdentifier64bit( "LGT_RR" ) };

                        const uint64_t *pHashes = nullptr;
                        size_t numHashes = 0;
                        if ( param_2 == eFlareType::FT_BlinkL ) {
                            pHashes = kBlinkLeftMaterialHashes;
                            numHashes = sizeof( kBlinkLeftMaterialHashes ) /
                                        sizeof( kBlinkLeftMaterialHashes[0] );
                        } else {
                            pHashes = kBlinkRightMaterialHashes;
                            numHashes = sizeof( kBlinkRightMaterialHashes ) /
                                        sizeof( kBlinkRightMaterialHashes[0] );
                        }

                        for ( size_t hashIndex = 0; hashIndex < numHashes;
                              hashIndex++ ) {
                            if ( uVar12 == pHashes[hashIndex] ) {
                                LAB_008bc033( puVar15, type, param_2, param_3 );
                                break;
                            }
                        }
                    }

                    matIndex++;
                    numMaterials--;
                } while ( numMaterials != 0 );
            }
        }

        resourcePass++;
    } while ( resourcePass < numPasses );

    int32_t nodeIndex = local_6c * 3;
    int32_t iStack_64 = 3;
    do {
        HiearchyNode *peVar3 = flaresRL[nodeIndex];
        HiearchyNode *peVar4 = flaresRR[nodeIndex];
        HiearchyNode *peVar5 = flaresFL[nodeIndex];
        HiearchyNode *peVar6 = flaresFR[nodeIndex];
        if ( type == eType::VBT_TrafficCar ) {
            if ( param_3 <= 0.0f ) {
                if ( ( ( ( ( param_2 == eFlareType::FT_BlinkL ) ||
                           ( param_2 == eFlareType::FT_BrakeRL ) ) ||
                         ( param_2 == eFlareType::FT_HeadlightsRL ) ) ||
                       ( ( ( param_2 == eFlareType::FT_HeadlightsRear ||
                             ( param_2 == eFlareType::FT_Brake ) ) ||
                           ( ( param_2 == eFlareType::FT_Warnings ||
                               ( param_2 ==
                                 eFlareType::FT_Reverse ) ) ) ) ) ) &&
                     ( peVar3 != nullptr ) ) {
                    peVar3->setAndPropagateFlags( 4 );
                }
                if ( ( ( ( param_2 == eFlareType::FT_BlinkL ) ||
                         ( param_2 == eFlareType::FT_BrakeFL ) ) ||
                       ( ( param_2 == eFlareType::FT_HeadlightsFL ||
                           ( ( ( ( param_2 == eFlareType::FT_Headlights ||
                                   ( param_2 == eFlareType::FT_Brake ) ) ||
                                 ( param_2 == eFlareType::FT_Warnings ) ) ||
                               ( param_2 ==
                                 eFlareType::FT_Reverse ) ) ) ) ) ) &&
                     ( peVar5 != nullptr ) ) {
                    peVar5->setAndPropagateFlags( 4 );
                }
                if ( ( ( ( ( ( param_2 == eFlareType::FT_BlinkR ) ||
                             ( param_2 == eFlareType::FT_BrakeRR ) ) ||
                           ( ( param_2 == eFlareType::FT_HeadlightsRR ||
                               ( ( param_2 == eFlareType::FT_HeadlightsRear ||
                                   ( param_2 ==
                                     eFlareType::FT_Brake ) ) ) ) ) ) ||
                         ( param_2 == eFlareType::FT_Warnings ) ) ||
                       ( param_2 == eFlareType::FT_Reverse ) ) &&
                     ( peVar4 != nullptr ) ) {
                    peVar4->setAndPropagateFlags( 4 );
                }
                if ( ( ( ( ( param_2 == eFlareType::FT_BlinkR ) ||
                           ( param_2 == eFlareType::FT_BrakeFR ) ) ||
                         ( param_2 == eFlareType::FT_HeadlightsFR ) ) ||
                       ( ( param_2 == eFlareType::FT_Headlights ||
                           ( param_2 == eFlareType::FT_Brake ) ) ) ) ||
                     ( ( param_2 == eFlareType::FT_Warnings ||
                         ( param_2 == eFlareType::FT_Reverse ) ) ) )
                    goto LAB_008bc388;
            } else {
                if ( ( ( ( ( param_2 == eFlareType::FT_BlinkL ) ||
                           ( param_2 == eFlareType::FT_HeadlightsRL ) ) ||
                         ( param_2 == eFlareType::FT_BrakeRL ) ) ||
                       ( ( ( ( param_2 == eFlareType::FT_HeadlightsRear ||
                               ( param_2 == eFlareType::FT_Brake ) ) ||
                             ( param_2 == eFlareType::FT_Warnings ) ) ||
                           ( param_2 == eFlareType::FT_Reverse ) ) ) ) &&
                     ( peVar3 != nullptr ) ) {
                    peVar3->unsetAndPropagateFlags( 4 );
                }
                if ( ( ( ( param_2 == eFlareType::FT_BlinkL ) ||
                         ( param_2 == eFlareType::FT_HeadlightsFL ) ) ||
                       ( ( param_2 == eFlareType::FT_Headlights ||
                           ( param_2 == eFlareType::FT_Reverse ) ) ) ) &&
                     ( peVar5 != nullptr ) ) {
                    peVar5->unsetAndPropagateFlags( 4 );
                }
                if ( ( ( ( param_2 == eFlareType::FT_BlinkR ) ||
                         ( param_2 == eFlareType::FT_BrakeRR ) ) ||
                       ( ( param_2 == eFlareType::FT_HeadlightsRear ||
                           ( ( ( ( param_2 == eFlareType::FT_HeadlightsRR ||
                                   ( param_2 == eFlareType::FT_Brake ) ) ||
                                 ( param_2 == eFlareType::FT_Warnings ) ) ||
                               ( param_2 ==
                                 eFlareType::FT_Reverse ) ) ) ) ) ) &&
                     ( peVar4 != nullptr ) ) {
                    peVar4->unsetAndPropagateFlags( 4 );
                }
                if ( ( ( param_2 == eFlareType::FT_BlinkR ) ||
                       ( param_2 == eFlareType::FT_BrakeFR ) ) ||
                     ( ( param_2 == eFlareType::FT_Headlights ||
                         ( ( param_2 == eFlareType::FT_HeadlightsFR ||
                             ( param_2 == eFlareType::FT_Reverse ) ) ) ) ) )
                    goto LAB_008bc1ce;
            }
        } else if ( param_3 <= 0.0f ) {
            if ( param_2 != eFlareType::FT_BlinkR ) {
                if ( peVar3 != nullptr ) {
                    peVar3->setAndPropagateFlags( 4 );
                }
                if ( peVar5 != nullptr ) {
                    peVar5->setAndPropagateFlags( 4 );
                }
                if ( param_2 == eFlareType::FT_BlinkL ) {
                    applyTurnSignalColorToFlareNode( peVar3, param_3 );
                    applyTurnSignalColorToFlareNode( peVar5, param_3 );
                }
            }
            if ( param_2 != eFlareType::FT_BlinkL ) {
                if ( peVar4 != nullptr ) {
                    peVar4->setAndPropagateFlags( 4 );
                }
            LAB_008bc388:
                if ( peVar6 != nullptr ) {
                    peVar6->setAndPropagateFlags( 4 );
                }
                if ( param_2 == eFlareType::FT_BlinkR ) {
                    applyTurnSignalColorToFlareNode( peVar4, param_3 );
                    applyTurnSignalColorToFlareNode( peVar6, param_3 );
                }
            }
        } else {
            if ( param_2 != eFlareType::FT_BlinkR ) {
                if ( peVar3 != nullptr ) {
                    peVar3->unsetAndPropagateFlags( 4 );
                }
                if ( peVar5 != nullptr ) {
                    peVar5->unsetAndPropagateFlags( 4 );
                }
                if ( param_2 == eFlareType::FT_BlinkL ) {
                    applyTurnSignalColorToFlareNode( peVar3, param_3 );
                    applyTurnSignalColorToFlareNode( peVar5, param_3 );
                }
            }
            if ( param_2 != eFlareType::FT_BlinkL ) {
                if ( peVar4 != nullptr ) {
                    peVar4->unsetAndPropagateFlags( 4 );
                }
            LAB_008bc1ce:
                if ( peVar6 != nullptr ) {
                    peVar6->unsetAndPropagateFlags( 4 );
                }
                if ( param_2 == eFlareType::FT_BlinkR ) {
                    applyTurnSignalColorToFlareNode( peVar4, param_3 );
                    applyTurnSignalColorToFlareNode( peVar6, param_3 );
                }
            }
        }
        nodeIndex++;
        iStack_64 = iStack_64 + -1;
        if ( iStack_64 == 0 ) {
            return;
        }
    } while ( true );
}

void VehicleBase::updateLOD( const float *param_1 ) {
    // FUN_008baad0
    if ( bDebugForceLOD ) {
        return;
    }

    Eigen::Vector4f local_20 = pCarState->getWorldPosition();

    Eigen::Vector4f cameraPosition;
    cameraPosition.head<3>() = gpCamera->getDefaultCamera()->getWorldPosition();
    cameraPosition.w() = 1.0f;

    Eigen::Vector4f fVar4 = local_20 - cameraPosition;
    Eigen::Vector4f fVar2 = { fVar4.x() * fVar4.x() + fVar4.y() * fVar4.y(),
                              fVar4.y() * fVar4.y() + fVar4.x() * fVar4.x(),
                              fVar4.z() * fVar4.z() + fVar4.w() * fVar4.w(),
                              fVar4.w() * fVar4.w() + fVar4.z() * fVar4.z() };

    local_20.y() = fVar2.y() + fVar2.w();
    local_20.x() = fVar2.x() + fVar2.z();
    local_20.z() = fVar2.z() + fVar2.x();
    local_20.w() = fVar2.w() + fVar2.y();

    auto auVar4 = local_20.array().sqrt();

    float fVar3 = gpCamera->getDefaultCamera()->getTanHalfFov() * auVar4.x() *
                  gSunLODFactor;

    for ( int32_t DVar1 = 0; DVar1 < eLOD::VBL_Count || param_1[DVar1] == 0.0f;
          DVar1++ ) {
        if ( param_1[DVar1] <= fVar3 && fVar3 != param_1[DVar1] ) {
            currentLOD = (eLOD)DVar1;
            return;
        }
    }

    currentLOD = eLOD::VBL_Lvl4;
}

bool VehicleBase::drawResourcesReady() const {
    return !displayInstances.empty() && displayInstances[0].pNode != nullptr &&
           !bNeedToUnstream;
}

bool VehicleBase::isInitialized() const { return bInitialized; }

bool VehicleBase::shouldUnstream() const { return bNeedToUnstream; }

void VehicleBase::addLightSetupForNode( HiearchyNode *param_2 ) {
    // FUN_00413790
    if ( param_2 == nullptr ) {
        return;
    }

    int32_t iVar5 = 0;
    int32_t iVar4 = 0;
    do {
        Instance *peVar1 = param_2->pInstance;
        uint64_t iVar2 = peVar1->getHashcode();

        if ( iVar2 == kHashcodeForkFL || iVar2 == kHashcodeForkRL ) {
            iVar4 = param_2->NumChildren + 1;
        } else if ( iVar2 == kHashcodeForkFR || iVar2 == kHashcodeForkRR ) {
            iVar5 = param_2->NumChildren + 1;
        }

        if ( iVar4 == 0 ) {
            if ( iVar5 == 0 ) {
                peVar1->addSetupToGraph( &lightCfg );
            } else {
                peVar1->addSetupToGraph( &lightCfgWheelR );
                iVar5 = iVar5 + -1;
            }
        } else {
            peVar1->addSetupToGraph( &lightCfgWheelL );
            iVar4 = iVar4 + -1;
        }

        param_2 = param_2->pNext;
    } while ( param_2 != nullptr );
}

static uint32_t GetLinkedAvatars( VehicleBase *pVehicle,
                                  EntityAvatar **ppOutAvatars,
                                  const uint32_t maxAvatars ) {
    // FUN_008b9e30
    const CarState *pCarState = pVehicle->getCarState();
    if ( pCarState == nullptr ) {
        return 0;
    }

    EntityDriver *pDriver = pCarState->getDriver();
    uint32_t numAvatars = ( pDriver != nullptr ) ? 1 : 0;
    if ( pCarState->getPassenger() != nullptr ) {
        numAvatars++;
    }

    if ( ppOutAvatars != nullptr && numAvatars <= maxAvatars ) {
        if ( pDriver != nullptr ) {
            ppOutAvatars[0] = pDriver;
        }

        EntityAvatarPassenger *pPassenger = pCarState->getPassenger();
        if ( pPassenger != nullptr ) {
            ppOutAvatars[( pDriver != nullptr ) ? 1 : 0] = pPassenger;
        }
    }
    return numAvatars;
}

void VehicleBase::onAlphaChange() {
    // FUN_008bad70
    std::array<EntityAvatar *, 2> linkedAvatars;
    const uint32_t numLinkedAvatars = GetLinkedAvatars(
        this, linkedAvatars.data(), (uint32_t)linkedAvatars.size() );
    for ( uint32_t i = 0; i < numLinkedAvatars; i++ ) {
        linkedAvatars[i]->setAlpha( 1.0f );
    }

    alphaInstances.clear();
    bAlphaCacheInUse = false;

    if ( alphaSlotIndex != -1 ) {
        registerCarAlphaInstances( alphaSlotIndex, false );
        if ( gpMngCarAlpha != nullptr ) {
            gpMngCarAlpha->freeElement( alphaSlotIndex );
        }
        alphaSlotIndex = -1;
    }

    alpha = 1.0f;
}

void VehicleBase::updateLights() {
    if ( type == eType::VBT_Car || type == eType::VBT_TrafficCar ) {
        wheelLLight = mainLight;
        wheelRLight = mainLight;

        wheelLILight = mainLightInterior;
        wheelRILight = mainLightInterior;

        if ( bApplyWheelAttenuation ) {
            gpWeather->updateActiveConfig();

            Eigen::Vector3f local_30 =
                gpWeather->getActiveWeatherConfig().getSunDirection();
            local_30.y() = 0.0f;
            local_30.normalize();

            Eigen::Vector4f local_20 = pCarState->getWorldMatrix().row( 0 );

            // Call to FUN_00408070 (0x008c01ad) implicitly set w to 0.0f.
            // We do it inline since we are not re-using their custom math lib
            Eigen::Vector4f pTVar2 =
                Eigen::Vector4f( local_20.x(), 0.0f, local_20.z(), 0.0f );
            pTVar2.normalize();

            DirectionalLight *pLightOut = &wheelLLight;
            DirectionalLight *pLightInt = &wheelLILight;

            float fVar8 = pTVar2.z() * local_30.z() +
                          pTVar2.y() * local_30.y() + pTVar2.x() * local_30.x();
            if ( -0.01f <= fVar8 ) {
                if ( fVar8 <= 0.01f ) {
                    return;
                }
            } else {
                fVar8 = 0.0f - fVar8;

                pLightOut = &wheelRLight;
                pLightInt = &wheelRILight;
            }
            float fVar9 = MinWheelAttenuation;
            if ( pCarState != nullptr && pCarState->isIndoor() ) {
                fVar9 = MinWheelAttenuation * 4.0f;
            }
            fVar9 = ( 1.0f - fVar9 ) * ( 1.0f - fVar8 ) + fVar9;

            ( pLightOut->Specular ).x() = ( pLightOut->Specular ).x() * fVar9;
            ( pLightOut->Specular ).y() = ( pLightOut->Specular ).y() * fVar9;
            ( pLightOut->Specular ).z() = ( pLightOut->Specular ).z() * fVar9;
            ( pLightOut->Specular ).w() = ( pLightOut->Specular ).w();
            ( pLightOut->Diffuse ).x() = ( pLightOut->Diffuse ).x() * fVar9;
            ( pLightOut->Diffuse ).y() = ( pLightOut->Diffuse ).y() * fVar9;
            ( pLightOut->Diffuse ).z() = ( pLightOut->Diffuse ).z() * fVar9;
            ( pLightOut->Diffuse ).w() = ( pLightOut->Diffuse ).w();

            ( pLightInt->Specular ).x() =
                ( pLightInt->Specular ).x() * fVar9 * 0.5f;
            ( pLightInt->Specular ).y() =
                ( pLightInt->Specular ).y() * fVar9 * 0.5f;
            ( pLightInt->Specular ).z() =
                ( pLightInt->Specular ).z() * fVar9 * 0.5f;
            ( pLightInt->Specular ).w() = ( pLightInt->Specular ).w();
            ( pLightInt->Diffuse ).x() =
                ( pLightInt->Diffuse ).x() * fVar9 * 0.5f;
            ( pLightInt->Diffuse ).y() =
                ( pLightInt->Diffuse ).y() * fVar9 * 0.5f;
            ( pLightInt->Diffuse ).z() =
                ( pLightInt->Diffuse ).z() * fVar9 * 0.5f;
            ( pLightInt->Diffuse ).w() = ( pLightInt->Diffuse ).w();
        }
    }
}

void VehicleBase::updateCarAlpha() {
    // FUN_008bf510
    if ( ( 1.0f < alpha || alpha == 1.0f ) && ( alphaSlotIndex != -1 ) ) {
        registerCarAlphaInstances( alphaSlotIndex, false );
        gpMngCarAlpha->freeElement( alphaSlotIndex );
        alphaSlotIndex = -1;
    }

    if ( alpha < 1.0f && alphaSlotIndex == -1 ) {
        alphaSlotIndex = gpMngCarAlpha->allocateElement( this );
    }

    if ( alphaSlotIndex != -1 ) {
        registerCarAlphaInstances( alphaSlotIndex, true );
        gpMngCarAlpha->setElementAlpha( alphaSlotIndex, alpha );

        setFlaresAlpha( alpha );

        if ( pParentShadowPlaneNode != nullptr ) {
            Instance *pShadowPlane = pParentShadowPlaneNode->pInstance;
            ColorRGBA color = pShadowPlane->getColor();
            color.A = static_cast<uint8_t>( FloatToInt( alpha * 255.0f ) );
            pShadowPlane->setColor( color );
        }

        Eigen::Vector4f screenRect = Eigen::Vector4f::Zero();
        computeScreenRect( screenRect );
        gpMngCarAlpha->setElementViewport( alphaSlotIndex, screenRect );
    }

    std::array<EntityAvatar *, 10> linkedAvatars;
    const uint32_t numLinkedAvatars = GetLinkedAvatars(
        this, linkedAvatars.data(), (uint32_t)linkedAvatars.size() );
    for ( uint32_t i = 0; i < numLinkedAvatars; i++ ) {
        EntityAvatar *pAvatar = linkedAvatars[i];
        EntityDriver *pDriver = pAvatar->getDriver();
        // TODO: Passenger branch (vtable +0xa0, seated when +0x25d0 is 1 or 2)
        if ( pDriver != nullptr && pDriver->isSeating() ) {
            pAvatar->setAlpha( alpha );
        } else {
            pAvatar->setAlpha( 1.0f );
        }
    }
}

void VehicleBase::setFlaresAlpha( const float value ) {
    // FUN_008ba800
    for ( Instance *pFlare : flares ) {
        if ( pFlare != nullptr ) {
            ColorRGBA color = pFlare->getColor();
            color.A = static_cast<uint8_t>( FloatToInt( value * 255.0f ) );
            pFlare->setColor( color );
        }
    }
}

void VehicleBase::computeScreenRect( Eigen::Vector4f &outRect ) {
    // FUN_008bdac0
    const Eigen::Vector4f shift = gpWorldStreaming->isEnabled()
                                      ? gpWorldStreaming->getPosition()
                                      : Eigen::Vector4f::Zero();

    const eRenderPass renderPass = getRenderPassWithAlpha();
    const bool bCarActive =
        pCarState != nullptr && pCarState->isInstanceAlive();
    if ( renderPass == eRenderPass::RP_Invalid || !bCarActive ) {
        outRect = Eigen::Vector4f( 0.0f, 0.0f, MngCarAlpha::kMinViewportSize,
                                   MngCarAlpha::kMinViewportSize );
        return;
    }

    RenderScene *pScene = gpRender->getRenderPassAtIndex( renderPass ).pScene;

    Matrix4x4f_t boundingBox;
    pCarState->retrieveBoundingBox( boundingBox );

    Matrix4x4f_t worldToView = Matrix4x4f_t::Identity();
    worldToView.row( 3 ) +=
        Eigen::Vector4f( -shift.x(), -shift.y(), -shift.z(), 0.0f );
    worldToView = worldToView * pScene->getCamera()->getWorldToCam();

    ProjectBoundingBoxToScreenRect( outRect, boundingBox, worldToView,
                                    pScene->getFrustumWrite() );
    adjustScreenProjectionFromContext( outRect );
}

void VehicleBase::registerCarAlphaInstances( uint32_t param_2, bool param_3 ) {
    // FUN_008babc0
    if ( gpMngCarAlpha == nullptr ) {
        return;
    }

    if ( !bAlphaCacheInUse ) {
        buildCarAlphaInstances();
    }

    if ( bAlphaCacheInUse && !alphaInstances.empty() ) {
        for ( Instance *peVar2 : alphaInstances ) {
            RenderFile::Section *peVar3 = peVar2->getMaterialBank();
            if ( peVar3 != nullptr ) {
                uint32_t uVar8 =
                    ( peVar3->Size - 0x10 ) / sizeof( Render3DDHashEntry );
                Render3DDHashEntry *pIterator =
                    (Render3DDHashEntry *)( peVar3 + 1 );

                for ( uint32_t i = 0; i < uVar8; i++ ) {
                    if ( pIterator->pEntry != nullptr ) {
                        RenderFile::Section *peVar1Section = pIterator->pEntry;
                        Material *peVar1 = (Material *)( peVar1Section + 1 );
                        gpMngCarAlpha->registerMaterial( param_2, peVar1,
                                                         param_3 );
                    }
                    pIterator++;
                }
            }
        }
    }

    for ( Instance *peVar2 : flares ) {
        if ( peVar2 != nullptr ) {
            if ( param_3 ) {
                peVar2->getFlagsWrite() = peVar2->getFlagsWrite() | 0x10;
            } else {
                peVar2->getFlagsWrite() = peVar2->getFlagsWrite() & 0xffffffef;
            }
        }
    }

    if ( pParentShadowPlaneNode != nullptr ) {
        Instance *peVar2 = pParentShadowPlaneNode->pInstance;
        if ( param_3 ) {
            peVar2->getFlagsWrite() = peVar2->getFlagsWrite() | 0x10;
            return;
        }
        peVar2->getFlagsWrite() = peVar2->getFlagsWrite() & 0xffffffef;
    }
}

void VehicleBase::buildCarAlphaInstances() {
    // FUN_008b9d40
    alphaInstances.clear();

    HiearchyNode *pNode =
        ( !bNeedToUnstream ) ? displayInstances[0].pNode : nullptr;
    if ( displayInstances.empty() || pNode == nullptr ) {
        bAlphaCacheInUse = true;
        return;
    }

    for ( ; pNode != nullptr; pNode = pNode->pNext ) {
        Instance *pInstance = pNode->pInstance;
        if ( pInstance != nullptr && std::find( flares.begin(), flares.end(),
                                                pInstance ) == flares.end() ) {
            alphaInstances.push_front( pInstance );
        }
    }
    bAlphaCacheInUse = true;
}

void VehicleBase::appendCarAlphaInstances( HiearchyNode *pNode ) {
    for ( ; pNode != nullptr; pNode = pNode->pNext ) {
        if ( pNode->pInstance != nullptr ) {
            alphaInstances.push_front( pNode->pInstance );
        }
    }
}

void VehicleBase::setRVModelMatrix( Matrix4x4f_t &modelMatrix ) {
    // FUN_008bf440
    Matrix4x4f_t local_50;
    if ( pVehicleBodyLowResInstance != nullptr ) {
        local_50 = displayInstances[3].InstanceMatrix * modelMatrix;
        pVehicleBodyLowResInstance->setModelMatrix( local_50 );
    }

    for ( uint32_t i = 0; i < kMaxNumWheels; i++ ) {
        if ( vehicleWheelLowResInstances[i] != nullptr ) {
            local_50 = displayInstances[8 + i].pInstance->getModelMatrix();
            vehicleWheelLowResInstances[i]->setModelMatrix( local_50 );
        }
    }
}

static inline float ComputeShadowPlaneCenterY( const Eigen::Vector4f *pWheelPos,
                                               const uint32_t numWheels,
                                               const int32_t pairIdx ) {
    // FUN_008c05f0 (inlined)
    if ( (int32_t)numWheels < 3 ) {
        return ( pWheelPos[0].y() + pWheelPos[pairIdx].y() ) * 0.5f;
    }

    const int32_t diagIdx =
        (int32_t)( numWheels + ( numWheels >> 31 & 3u ) ) >> 2;
    return ( pWheelPos[0].y() + pWheelPos[diagIdx + pairIdx].y() ) * 0.5f;
}

void VehicleBase::setShadowPlaneMatrix( Matrix4x4f_t &modelMatrix ) {
    // FUN_008c05f0
    if ( bNeedToUnstream || displayInstances[2].pNode == nullptr ||
         pCarState == nullptr ) {
        return;
    }

    HiearchyNode *shadowNode = displayInstances[2].pNode;

    PhysicsVehicle *pPhysVehicle = pCarState->getPhysicsVehicle();
    const bool bStaticShadow =
        ( pPhysVehicle == nullptr ) ||
        ( !pPhysVehicle->isActive() && !bForceDrawShadow );
    if ( bStaticShadow ) {
        // Static path: instance matrix * model matrix
        propagateNodeTransform( 2u, modelMatrix, 0 );
    } else {
        // Dynamic path: position shadow plane from wheel ground data
        uint32_t numWheels = pCarState->getNumWheels();

        std::array<Eigen::Vector4f, kMaxNumWheels> wheelPos;
        for ( uint32_t i = 0; i < numWheels; i++ ) {
            wheelPos[i] = pCarState->getWheel( i ).ProjectedPositionOnGround;
            wheelPos[i].w() = 0.0f;
        }

        // Select characteristic wheel pair.
        int32_t bVar10 = (int32_t)numWheels / 2 - 1;
        int32_t iVar5 = 1 << bVar10;
        const Eigen::Vector4f &vA = wheelPos[0];
        const Eigen::Vector4f &vB = wheelPos[iVar5];

        float shadowCenterY =
            ComputeShadowPlaneCenterY( wheelPos.data(), numWheels, iVar5 );

        if ( !bDisableShadow ) {
            // Full physics shadow: build an orientation frame from wheel
            // positions.
            Eigen::Vector4f forward4, right4, up4;

            if ( (int32_t)numWheels < 3 ) {
                // 2-wheel (bike): frame from front/rear wheel pair.
                forward4.head<3>() = ( vB - vA ).head<3>().normalized();
                forward4.w() = 0.0f;

                Eigen::Vector3f right3 = Eigen::Vector3f( 0.f, 1.f, 0.f )
                                             .cross( forward4.head<3>() )
                                             .normalized();
                right4.head<3>() = right3;
                right4.w() = 0.0f;

                up4.head<3>() = forward4.head<3>().cross( right3 ).normalized();
                up4.w() = 0.0f;
            } else {
                // 4-wheel (car): frame from longitudinal + lateral wheel
                // directions.
                int32_t diagIdx =
                    (int32_t)( numWheels + ( numWheels >> 31 & 3u ) ) >> 2;
                const Eigen::Vector4f &vDiag = wheelPos[diagIdx + iVar5];

                forward4.head<3>() = ( vA - vB ).head<3>().normalized();
                forward4.w() = 0.0f;

                Eigen::Vector3f lat3 = ( vB - vDiag ).head<3>().normalized();
                right4.head<3>() = lat3;
                right4.w() = 0.0f;

                up4.head<3>() = forward4.head<3>().cross( lat3 ).normalized();
                up4.w() = 0.0f;
            }

            // Apply shadow Y offset (slightly above ground to avoid
            // z-fighting).
            float shadowYOffset = 0.015f; // DAT_00f41d10
            if ( gpMngShadow != nullptr &&
                 !gpMngShadow->getReceiversEnabled() ) {
                shadowYOffset = 0.05f; // DAT_00f41778
            }
            shadowCenterY += shadowYOffset;

            // Build world-space coordinate frame for the shadow plane.
            // Row 0 = right, Row 1 = up, Row 2 = forward, Row 3 = world
            // position.
            Matrix4x4f_t local_f0;
            local_f0.row( 0 ) = right4;
            local_f0.row( 1 ) = up4;
            local_f0.row( 2 ) = forward4;
            local_f0.row( 3 ) = Eigen::Vector4f(
                modelMatrix( 3, 0 ), shadowCenterY, modelMatrix( 3, 2 ), 1.0f );

            // Combine with shadow plane's local instance matrix (clear its Y
            // offset).
            Matrix4x4f_t local_50 = displayInstances[2].InstanceMatrix;
            local_50( 3, 1 ) = 0.0f;

            shadowNode->TransformMatrix = local_50 * local_f0;
        } else {
            // Simplified static shadow: scale instance matrix and apply a fixed
            // Y offset.
            float yOffset = ( type == eType::VBT_Bike ) ? 0.1f : 0.08f;

            Matrix4x4f_t local_f0 = displayInstances[2].InstanceMatrix;
            Eigen::Vector3f shadowScale( 0.95f, 1.0f, 0.95f );
            local_f0 = local_f0 * MakeScaleMatrix( shadowScale );
            local_f0( 3, 1 ) += yOffset;

            shadowNode->TransformMatrix = local_f0 * modelMatrix;
        }

        propagateNodeTransform( shadowNode, modelMatrix, 0 );

        // Compare shadow plane Y against body node Y: flag shadow as detached
        // when the car is more than 0.5m above the shadow plane (e.g.
        // airborne).
        if ( !bNeedToUnstream && displayInstances[0].pNode != nullptr ) {
            float bodyY = displayInstances[0].pNode->TransformMatrix( 3, 1 );
            if ( fabsf( bodyY - shadowCenterY ) <= 0.5f ) { // FLOAT_00f41674
                displayInstances[2].pNode->unsetAndPropagateFlags( 4 );
            } else {
                displayInstances[2].pNode->setAndPropagateFlags( 4 );
            }
        }
    }
}

void VehicleBase::updateBodyTransform( CarState *param_2 ) {
    // FUN_008bf9c0
    Matrix4x4f_t local_50 = param_2->getBody()->OffsetedWorldMatrix;
    Eigen::Vector4f local_70 = param_2->getBody()->Shaking;
    local_70.w() = 0.0f;

    local_50.row( 3 ) += local_70;

    setModelMatrix( local_50 );
    bValidPosition = true;

    if ( param_2->getBody()->bUseWheelMatrix ) {
        for ( uint32_t i = 0; i < param_2->getNumWheels(); i++ ) {
            const WheelState &wheel = param_2->getWheel( i );

            // FORK_FL/FR/RL/RR nodes (indices 12+i) → steering/suspension
            // matrix
            HiearchyNode *pForkNode =
                bNeedToUnstream ? nullptr : displayInstances[12 + i].pNode;
            if ( pForkNode != nullptr ) {
                pForkNode->TransformMatrix = wheel.WorldMatrixFork;
                pForkNode->pInstance->setModelMatrix(
                    pForkNode->TransformMatrix );
                pForkNode->propagateTransformMatricesNoScale();
            }

            // DISC_FL/FR/RL/RR nodes (indices 4+i) → rotor/spinning matrix
            HiearchyNode *pDiscNode =
                bNeedToUnstream ? nullptr : displayInstances[4 + i].pNode;
            if ( pDiscNode != nullptr ) {
                pDiscNode->TransformMatrix = wheel.WorldMatrixRotor;
                pDiscNode->pInstance->setModelMatrix(
                    pDiscNode->TransformMatrix );
                pDiscNode->propagateTransformMatricesNoScale();
            }
        }
    }
}

void VehicleBase::updateVisibilityFlag( float param_2 ) {
    // FUN_008bb280
    if ( bNeedToUnstream || displayInstances[3].pNode == nullptr ) {
        return;
    }

    HiearchyNode *peVar1 = displayInstances[3].pNode;

    CameraGame *pCam = gpCamera->getDefaultCamera();
    Eigen::Vector4f local_60 = Eigen::Vector4f::Zero();
    Eigen::Vector4f local_70 = pCarState->getBody()->WorldMatrix.row( 3 );
    Eigen::Vector4f local_80 = Eigen::Vector4f::Zero();

    const float dist = ( local_70.head<3>() - pCam->getWorldPosition() ).norm();
    if ( param_2 < pCam->getTanHalfFov() * dist ) {
        bVisible = false;
        return;
    }
    float local_a0 = 0.0f;
    float local_94 = 4.0f;
    float local_9c = 0.0f;

    Eigen::Vector4f local_90 = Eigen::Vector4f::Zero();
    if ( peVar1->NumChildren == 0 ) {
        retrieveBoundingRadius( &local_94, peVar1, &local_60 );
        local_a0 = local_94;
    } else {
        local_9c = 0.0f;
        local_a0 = 0.0f;

        HiearchyNode *local_98 = peVar1->pNext;
        for ( uint32_t uVar7 = peVar1->NumChildren; uVar7 != 0; uVar7-- ) {
            retrieveBoundingRadius( &local_9c, local_98, &local_70 );
            if ( uVar7 == peVar1->NumChildren ) {
                local_90 = local_70;
                local_a0 = local_9c;
            } else {
                Eigen::Vector4f auVar13 = local_70 + local_90;
                Eigen::Vector4f auVar12 = auVar13 / 2.0f;

                float fVar10 = local_9c;
                if ( local_9c <= local_a0 ) {
                    fVar10 = local_a0;
                }

                Eigen::Vector4f local_30 = local_70 - local_90;
                float local_20 = local_30.norm();

                local_80 = auVar12;
                local_90 = local_80;
                local_a0 = local_20 * 0.5f + fVar10;
            }
            local_98 = local_98->pNext;
            local_60 = local_90;
        }
    }

    if ( gpWorldStreaming != nullptr ) {
        local_60.head<3>() += gpWorldStreaming->getPosition().head<3>();
    }
    bVisible = gpCamera->getActiveCamera()->sphereInFrustum( &local_60,
                                                             local_a0 ) != 0;
}

void VehicleBase::removeLightSetup( HiearchyNode *param_2 ) {
    // FUN_00413520
    int32_t iVar5 = 0;
    int32_t iVar4 = 0;
    while ( param_2 != nullptr ) {
        Instance *peVar1 = param_2->pInstance;

        if ( peVar1 != nullptr ) {
            uint64_t iVar2 = peVar1->getHashcode();

            if ( iVar2 == kHashcodeForkFL || iVar2 == kHashcodeForkRL ) {
                iVar4 = param_2->NumChildren + 1;
            } else if ( iVar2 == kHashcodeForkFR || iVar2 == kHashcodeForkRR ) {
                iVar5 = param_2->NumChildren + 1;
            }

            if ( iVar4 == 0 ) {
                if ( iVar5 == 0 ) {
                    peVar1->removeSetupFromGraph( &lightCfg );
                } else {
                    peVar1->removeSetupFromGraph( &lightCfgWheelR );
                    iVar5 = iVar5 + -1;
                }
            } else {
                peVar1->removeSetupFromGraph( &lightCfgWheelL );
                iVar4 = iVar4 + -1;
            }
        }

        param_2 = param_2->pNext;
    }
}

char *VehicleBase::setPaintColorFromDB( int32_t paintIndex ) {
    // FUN_009ab210
    if ( -1 < paintIndex ) {
        int32_t iVar1 = gpDatabase->getNumColors( pCarState->getCarID() );
        if ( iVar1 != 0 ) {
            if ( iVar1 <= paintIndex ) {
                paintIndex = 0;
            }
            DBEntryCarColor *pDVar2 = gpDatabase->getColorByIndex(
                pCarState->getCarID(), paintIndex );
            if ( pDVar2 != nullptr ) {
                setVehiclePartColor( CCP_BodyColor,
                                     pDVar2->ColorID1.pAsString );
                setVehiclePartColor( CCP_BodyColor2,
                                     pDVar2->ColorID2.pAsString );
                setCalliperColor( pDVar2->CallipersColor.pAsString );
                return pDVar2->ColorName.pAsString;
            }
        }
    }

    return nullptr;
}

int32_t VehicleBase::getResNodesForColorPart( eCarColorPart param_2 ) {
    // FUN_008b9720
    if ( type == eType::VBT_Car || type == eType::VBT_TrafficCar ) {
        switch ( param_2 ) {
        case CCP_BodyColor:
        case CCP_BodyColor2:
            pCurResNodesUsed[0] = &bodyResource;
            pCurResNodesUsed[1] = &interiorResource;
            return 2;

        case CCP_InteriorColor1:
        case CCP_InteriorColor2:
        case CCP_InteriorMaterial:
            pCurResNodesUsed[1] = nullptr;
            pCurResNodesUsed[0] = &interiorResource;
            return 1;

        case CCP_LogoColor1:
        case CCP_LogoColor2:
            pCurResNodesUsed[0] = &bodyResource;
            pCurResNodesUsed[1] = &interiorResource;
            return 1;

        default:
            break;
        }
    } else if ( type == eType::VBT_Bike || type == eType::VBT_TrafficBike ) {
        if ( param_2 >= CCP_BodyColor && param_2 <= CCP_BodyColor2 ) {
            pCurResNodesUsed[0] = &bodyResource;
            pCurResNodesUsed[1] = &interiorResource;
            return 2;
        }
    } else {
        return 0;
    }

    pCurResNodesUsed[0] = nullptr;
    pCurResNodesUsed[1] = nullptr;
    return 0;
}

void VehicleBase::applyColorToResNodes( const ColorRGBA &param_2,
                                        int32_t param_3,
                                        eCarColorPart param_4 ) {
    // FUN_008bc940
    const int32_t numResNodes = getResNodesForColorPart( param_4 );
    const Eigen::Vector4f color = UnpackColorBytes( param_2 );

    ForEachColorPartMaterial( pCurResNodesUsed, numResNodes, type, param_4,
                              [&]( Material *pMaterial ) {
                                  switch ( param_3 ) {
                                  case 0:
                                      pMaterial->SpecularColor = color;
                                      break;
                                  case 1:
                                      pMaterial->AmbientColor = color;
                                      break;
                                  case 2:
                                      pMaterial->DiffuseColor = color;
                                      break;
                                  default:
                                      break;
                                  }
                              } );
}

void VehicleBase::applyReflectionParamsToResNodes( const float *param_2,
                                                   eCarColorPart param_6,
                                                   bool bNPaint ) {
    // FUN_008bcbd0
    const int32_t numResNodes = getResNodesForColorPart( param_6 );

    ForEachColorPartMaterial(
        pCurResNodesUsed, numResNodes, type, param_6,
        [&]( Material *pMaterial ) {
            MaterialShaderParameterArray *pParameters =
                pMaterial->getShaderParameters();
            if ( pParameters == nullptr ||
                 !IsPaintShaderGUID( pParameters->Hashcode, bNPaint ) ) {
                return;
            }

            float *pParamData = pParameters->getStaticParamDataPS( 0x3e );
            if ( pParamData == nullptr ) {
                return;
            }

            pParamData[0] = param_2[0];
            pParamData[1] = param_2[1];

            const bool bIsTrafficCar = ( type == eType::VBT_TrafficCar );
            pParamData[2] = bIsTrafficCar ? 1.0f : param_2[2]; // FLOAT_00f4165c
            pParamData[3] = bIsTrafficCar ? 1.0f : param_2[3];
        } );
}

void VehicleBase::applySpecularParamToResNodes( float param_2,
                                                eCarColorPart param_3,
                                                bool bNPaint ) {
    // FUN_008bce60
    const int32_t numResNodes = getResNodesForColorPart( param_3 );

    ForEachColorPartMaterial(
        pCurResNodesUsed, numResNodes, type, param_3,
        [&]( Material *pMaterial ) {
            MaterialShaderParameterArray *pParameters =
                pMaterial->getShaderParameters();
            if ( pParameters == nullptr ||
                 !IsPaintShaderGUID( pParameters->Hashcode, bNPaint ) ) {
                return;
            }

            float *pParamData = pParameters->getStaticParamDataPS( 0x1b );
            if ( pParamData != nullptr ) {
                pParamData[0] = param_2;
            }
        } );
}

void VehicleBase::applyColorStripToResNodes( const ColorRGBA &param_2,
                                             const ColorRGBA &param_3,
                                             float param_4,
                                             eCarColorPart param_5,
                                             bool bNPaint ) {
    // FUN_008bd0b0
    const int32_t numResNodes = getResNodesForColorPart( param_5 );
    const Eigen::Vector4f firstColor = UnpackColorBytes( param_2 );
    const Eigen::Vector4f secondColor = UnpackColorBytes( param_3 );

    ForEachColorPartMaterial(
        pCurResNodesUsed, numResNodes, type, param_5,
        [&]( Material *pMaterial ) {
            MaterialShaderParameterArray *pParameters =
                pMaterial->getShaderParameters();
            if ( pParameters == nullptr ||
                 !IsPaintOrTwoTonesShaderGUID( pParameters->Hashcode,
                                               bNPaint ) ) {
                return;
            }

            float *pParamData = pParameters->getStaticParamDataPS( 0x7c );
            if ( pParamData != nullptr ) {
                pParamData[0] = param_4;
            }

            pParamData = pParameters->getStaticParamDataPS( 0x7a );
            if ( pParamData != nullptr ) {
                memcpy( pParamData, firstColor.data(), sizeof( float ) * 4 );
            }

            pParamData = pParameters->getStaticParamDataPS( 0x7b );
            if ( pParamData != nullptr ) {
                memcpy( pParamData, secondColor.data(), sizeof( float ) * 4 );
            }
        } );
}

void VehicleBase::applyTwoTonesColorToResNodes( const ColorRGBA &param_2,
                                                eCarColorPart param_3,
                                                bool bNPaint ) {
    // FUN_008bc6a0
    const int32_t numResNodes = getResNodesForColorPart( param_3 );
    const Eigen::Vector4f color = UnpackColorBytes( param_2 );

    ForEachColorPartMaterial(
        pCurResNodesUsed, numResNodes, type, param_3,
        [&]( Material *pMaterial ) {
            MaterialShaderParameterArray *pParameters =
                pMaterial->getShaderParameters();
            if ( pParameters == nullptr ||
                 !IsPaintShaderGUID( pParameters->Hashcode, bNPaint ) ) {
                return;
            }

            float *pParamData = pParameters->getStaticParamDataPS( 0xd5 );
            if ( pParamData != nullptr ) {
                memcpy( pParamData, color.data(), sizeof( float ) * 4 );
            }
        } );
}

void VehicleBase::cloneInteriorMaterials( eCarColorPart param_2,
                                          Material *pColorMaterial,
                                          Material *pColorMaterialN,
                                          bool bPaintFamilyInterior,
                                          bool bNPaint ) {
    // FUN_008bdd90
    if ( gpCarDisplay == nullptr ) {
        return;
    }

    Material *const *ppInteriorMaterials = nullptr;
    const uint64_t *pInteriorHashcodes = nullptr;
    int32_t numInteriorMaterials = 0;
    int32_t poolSlotBase = 0;

    switch ( param_2 ) {
    case CCP_InteriorColor1:
        ppInteriorMaterials = interiorMaterials.pMaterialsColor0.data();
        pInteriorHashcodes = interiorMaterials.pHashcodesColor0.data();
        numInteriorMaterials = interiorMaterials.NumMaterialColor0;
        poolSlotBase = 0;
        break;
    case CCP_InteriorColor2:
        ppInteriorMaterials = interiorMaterials.pMaterialsColor1.data();
        pInteriorHashcodes = interiorMaterials.pHashcodesColor1.data();
        numInteriorMaterials = interiorMaterials.NumMaterialColor1;
        poolSlotBase = 0x10;
        break;
    case CCP_InteriorMaterial:
        ppInteriorMaterials = interiorMaterials.pMaterialsSurfaces.data();
        pInteriorHashcodes = interiorMaterials.pHashcodesSurfaces.data();
        numInteriorMaterials = interiorMaterials.NumMaterialSurfaces;
        poolSlotBase = 0x20;
        break;
    default:
        return;
    }

    uint8_t *pPool = bPaintFamilyInterior
                         ? gpCarDisplay->getInteriorMaterialSpecialPool()
                         : gpCarDisplay->getInteriorMaterialPool();
    const uint32_t poolStride =
        bPaintFamilyInterior ? gpCarDisplay->getInteriorMaterialSpecialSize()
                             : gpCarDisplay->getInteriorMaterialSize();
    if ( pPool == nullptr || poolStride == 0 ) {
        return;
    }

    const InteriorTextureRemap *pRemap = bPaintFamilyInterior
                                             ? kRemapPoolB[bNPaint ? 1 : 0]
                                             : kRemapPoolA[bNPaint ? 1 : 0];

    const int32_t numSlots =
        Min( numInteriorMaterials,
             (int32_t)CustomMaterialsInterior::kMaxNumMaterial );

    for ( int32_t i = 0; i < numSlots; i++ ) {
        Material *pInteriorMaterial = ppInteriorMaterials[i];
        if ( pInteriorMaterial == nullptr ) {
            continue;
        }

        Material *pSourceMaterial = bNPaint ? pColorMaterialN : pColorMaterial;
        Material *pClone = pSourceMaterial->copyTo(
            pPool + (size_t)( poolSlotBase + i ) * poolStride, 2 );
        if ( pClone == nullptr ) {
            continue;
        }

        MaterialParameter *pInteriorParameters =
            pInteriorMaterial->getParameterByIndex( 0 );
        MaterialLayer *pInteriorLayer = ( pInteriorParameters != nullptr )
                                            ? pInteriorParameters->getLayer( 0 )
                                            : nullptr;
        MaterialParameter *pCloneParameters = pClone->getParameterByIndex( 0 );
        MaterialLayer *pCloneLayer = ( pCloneParameters != nullptr )
                                         ? pCloneParameters->getLayer( 0 )
                                         : nullptr;

        if ( pInteriorLayer != nullptr && pCloneLayer != nullptr ) {
            for ( int32_t r = 0; pRemap[r].Dst >= 0; r++ ) {
                MaterialLayerTexture &dst =
                    pCloneLayer->pLayerTextures[pRemap[r].Dst];
                const MaterialLayerTexture &src =
                    pInteriorLayer->pLayerTextures[pRemap[r].Src];

                dst.Hashcode = src.Hashcode;
                dst.pTextureSection = src.pTextureSection;
            }
        }

        RefreshClonedMaterialFlags( pClone, bNPaint );
        pInteriorMaterial->copyStatesTo( pClone );
        InstallClonedInteriorMaterial( interiorResource, pClone,
                                       pInteriorHashcodes[i] );
    }
}

Material *VehicleBase::setVehiclePartColor( eCarColorPart param_2,
                                            const char *param_3 ) {
    // FUN_008bdd90
    if ( param_3 == nullptr || gpCarColors == nullptr ) {
        return nullptr;
    }

    Render2DM &colors2DM = gpCarColors->get2DM();
    if ( colors2DM.getMaterialArray() == nullptr ) {
        return nullptr;
    }

    char normalMapColorName[64];
    snprintf( normalMapColorName, sizeof( normalMapColorName ), "%s_N",
              param_3 );

    RenderFile::Section *pColorSection =
        colors2DM.getMaterial( GetIdentifier64bit( param_3 ) );
    if ( pColorSection == nullptr ) {
        return nullptr;
    }
    Material *pColorMaterial = (Material *)( pColorSection + 1 );

    RenderFile::Section *pColorSectionN =
        colors2DM.getMaterial( GetIdentifier64bit( normalMapColorName ) );
    Material *pColorMaterialN = ( pColorSectionN != nullptr )
                                    ? (Material *)( pColorSectionN + 1 )
                                    : pColorMaterial;

    Material *pInteriorMaterial = nullptr;
    switch ( param_2 ) {
    case CCP_InteriorColor1:
        pInteriorMaterial = interiorMaterials.pMaterialsColor0[0];
        break;
    case CCP_InteriorColor2:
        pInteriorMaterial = interiorMaterials.pMaterialsColor1[0];
        break;
    case CCP_InteriorMaterial:
        pInteriorMaterial = interiorMaterials.pMaterialsSurfaces[0];
        break;
    default:
        break;
    }

    if ( pInteriorMaterial != nullptr ) {
        MaterialParameter *pInteriorParams =
            pInteriorMaterial->getParameterByIndex( 0 );
        MaterialParameter *pColorParams =
            pColorMaterial->getParameterByIndex( 0 );

        if ( pInteriorParams != nullptr && pColorParams != nullptr ) {
            auto isPaintFamily = []( const uint64_t guid ) {
                return guid == 0xb39c35015789e65cull ||
                       guid == 0xcab12190a00d5c4eull ||
                       guid == 0x69f07f72719bf862ull;
            };
            auto isTwoTonesFamily = []( const uint64_t guid ) {
                return guid == 0x213157b8bc6c7363ull ||
                       guid == 0x111157b8bc6c7111ull ||
                       guid == 0x222157b8bc6c7222ull;
            };

            const uint64_t interiorGUID = pInteriorParams->Hashcode;
            const uint64_t colorGUID = pColorParams->Hashcode;

            const bool bPaintFamilyInterior = isPaintFamily( interiorGUID );
            const bool bNeedsClone =
                ( bPaintFamilyInterior && !isPaintFamily( colorGUID ) ) ||
                ( isTwoTonesFamily( interiorGUID ) &&
                  !isTwoTonesFamily( colorGUID ) );

            if ( bNeedsClone ) {
                const bool bNPaint = ( interiorGUID == 0xcab12190a00d5c4eull ||
                                       interiorGUID == 0x69f07f72719bf862ull ||
                                       interiorGUID == 0x111157b8bc6c7111ull ||
                                       interiorGUID == 0x222157b8bc6c7222ull );

                cloneInteriorMaterials( param_2, pColorMaterial,
                                        pColorMaterialN, bPaintFamilyInterior,
                                        bNPaint );

                bPaintChange = true;
                return nullptr;
            }
        }
    }

    float stripParam = 0.0f;
    ColorRGBA stripFirstColor = gColorBlackNoAlpha;
    ColorRGBA stripSecondColor = gColorBlackNoAlpha;

    Material *pSourceMaterial = pColorMaterial;
    for ( int32_t pass = 0; pass < 2; pass++ ) {
        const bool bNPaint = ( pass != 0 );
        if ( bNPaint ) {
            pSourceMaterial = pColorMaterialN;
        }

        const bool bIsLogoColor =
            ( param_2 == CCP_LogoColor1 || param_2 == CCP_LogoColor2 );

        applyColorToResNodes(
            TruncateColorToBytesOpaque( pSourceMaterial->AmbientColor ), 1,
            param_2 );
        applyColorToResNodes(
            TruncateColorToBytesOpaque( pSourceMaterial->DiffuseColor ), 2,
            param_2 );
        applyColorToResNodes(
            TruncateColorToBytesOpaque( pSourceMaterial->SpecularColor ), 0,
            param_2 );

        MaterialShaderParameterArray *pSourceParams =
            pSourceMaterial->getShaderParameters();

        const float *pReflectionParams =
            pSourceParams->getStaticParamDataPS( 0x3e );
        if ( pReflectionParams != nullptr ) {
            applyReflectionParamsToResNodes( pReflectionParams, param_2,
                                             bNPaint );
            if ( bIsLogoColor ) {
                applyReflectionParamsToResNodes( pReflectionParams,
                                                 CCP_BodyColor, bNPaint );
            }
        }

        const float *pSpecularParam =
            pSourceParams->getStaticParamDataPS( 0x1b );
        if ( pSpecularParam != nullptr ) {
            applySpecularParamToResNodes( pSpecularParam[0], param_2, bNPaint );
            if ( bIsLogoColor ) {
                applySpecularParamToResNodes( pSpecularParam[0], CCP_BodyColor,
                                              bNPaint );
            }
        }

        const float *pStripParam = pSourceParams->getStaticParamDataPS( 0x7c );
        if ( pStripParam != nullptr ) {
            stripParam = pStripParam[0];
        }

        const float *pStripFirstColor =
            pSourceParams->getStaticParamDataPS( 0x7a );
        if ( pStripFirstColor != nullptr ) {
            stripFirstColor = TruncateColorToBytes( pStripFirstColor );
        }

        const float *pStripSecondColor =
            pSourceParams->getStaticParamDataPS( 0x7b );
        if ( pStripSecondColor != nullptr ) {
            stripSecondColor = TruncateColorToBytes( pStripSecondColor );
            applyColorStripToResNodes( stripFirstColor, stripSecondColor,
                                       stripParam, param_2, bNPaint );
            if ( bIsLogoColor ) {
                applyColorStripToResNodes( stripFirstColor, stripSecondColor,
                                           stripParam, CCP_BodyColor, bNPaint );
            }
        }

        const float *pTwoTonesColor =
            pSourceParams->getStaticParamDataPS( 0xd5 );
        const bool bIsBodyColor =
            ( param_2 == CCP_BodyColor || param_2 == CCP_BodyColor2 );

        if ( pTwoTonesColor != nullptr && bIsBodyColor ) {
            const eCarColorPart twoTonesPart =
                ( param_2 == CCP_BodyColor ) ? CCP_LogoColor1 : CCP_LogoColor2;

            applyColorStripToResNodes( stripFirstColor, stripSecondColor,
                                       stripParam, twoTonesPart, bNPaint );
            applyTwoTonesColorToResNodes(
                TruncateColorToBytes( pTwoTonesColor ), twoTonesPart, false );
        } else if ( param_2 == CCP_InteriorMaterial ) {
            MaterialParameter *pSourceParameters =
                pSourceMaterial->getParameterByIndex( 0 );
            MaterialLayer *pSourceLayer = ( pSourceParameters != nullptr )
                                              ? pSourceParameters->getLayer( 0 )
                                              : nullptr;

            for ( int32_t i = 0; i < interiorMaterials.NumMaterialSurfaces;
                  i++ ) {
                Material *pInteriorMaterial =
                    interiorMaterials.pMaterialsSurfaces[i];
                if ( pInteriorMaterial == nullptr || pSourceLayer == nullptr ) {
                    continue;
                }

                MaterialParameter *pInteriorParameters =
                    pInteriorMaterial->getParameterByIndex( 0 );
                MaterialLayer *pInteriorLayer =
                    ( pInteriorParameters != nullptr )
                        ? pInteriorParameters->getLayer( 0 )
                        : nullptr;
                if ( pInteriorLayer == nullptr ) {
                    continue;
                }

                pInteriorLayer->pLayerTextures[0].Hashcode =
                    pSourceLayer->pLayerTextures[0].Hashcode;

                if ( gpCarCommon != nullptr ) {
                    gpCarCommon->getCollection2D().bindBitmapReferences(
                        pInteriorMaterial );
                }
                interiorResource.Collection2D.bindBitmapReferences(
                    pInteriorMaterial );
            }
        }
    }

    bPaintChange = true;
    return pSourceMaterial;
}

void VehicleBase::applyGaragePaint( CarDesc &carDesc ) {
    // FUN_008c1250 + FUN_008c11e0
    if ( !carDesc.isValid() ) {
        return;
    }

    applyPaint( carDesc.getCarConfigID(), carDesc.paintType, carDesc.paintIndex,
                carDesc.rimID, carDesc.interiorID, &carDesc.deluxeMainColor,
                &carDesc.deluxeSecondColor, &carDesc.deluxeLogoColor,
                &carDesc.deluxeSecondColor2 );
}

void VehicleBase::applyPaint( uint32_t carConfigID,
                              ePaintType paintType,
                              int32_t paintIndex,
                              int32_t rimID,
                              int32_t interiorIndex,
                              const ColorRGBA *pMainColor,
                              const ColorRGBA *pSecondColor,
                              const ColorRGBA *pLogoColor,
                              const ColorRGBA *pSecondColor2 ) {
    // FUN_008c1060
    if ( pCarState == nullptr || pCarState->getResource() == nullptr ||
         pCarState->getResource()->bDefault ) {
        return;
    }

    paint.InteriorIndex = interiorIndex;
    paint.bSaved = true;
    paint.Type = paintType;
    paint.PaintIndex = paintIndex;
    if ( pMainColor != nullptr ) {
        paint.CustomMainColor = *pMainColor;
    }
    if ( pSecondColor != nullptr ) {
        paint.CustomSecondColor = *pSecondColor;
    }
    if ( pLogoColor != nullptr ) {
        paint.CustomTwoToneColor1 = *pLogoColor;
    }
    if ( pSecondColor2 != nullptr ) {
        paint.CustomTwoToneColor2 = *pSecondColor2;
    }

    const int32_t carID = static_cast<int32_t>( carConfigID );
    DBEntryCarPhysicsData *pCarEntry = gpDatabase->getCarDBEntry( carID );
    if ( pCarEntry == nullptr ) {
        return;
    }

    setPaintColorFromDB( paintIndex );

    if ( gpDatabase->isTraffic( pCarEntry ) ) {
        return;
    }

    VehicleBaseResources *pResource = pCarState->getResource();
    if ( pResource->RimID != rimID ) {
        pResource->RimID = rimID;

        DBEntryRim *pRim =
            static_cast<DBEntryRim *>( pCarEntry->DefaultRims.pAsRawPointer );
        if ( rimID != -1 ) {
            DBEntryRim *pSelected = gpDatabase->getRim( carID, rimID );
            if ( pSelected != nullptr ) {
                pRim = pSelected;
            }
        }
        setRimsFromDB( pRim );
    }

    DBEntryCarColor *pColor = gpDatabase->getColorByIndex( carID, paintIndex );
    if ( pColor != nullptr ) {
        DBEntryInterior *pInterior =
            gpDatabase->getInteriorByIndex( pColor, interiorIndex );
        if ( pInterior != nullptr ) {
            setInteriorColorsFromDB( pInterior );
        }
    }

    if ( paintType == ePaintType::PT_CustomSolid ||
         paintType == ePaintType::PT_CustomMetal ||
         paintType == ePaintType::PT_CustomSolidTwoTones ||
         paintType == ePaintType::PT_CustomMetalTwoTones ) {
        applyDeluxePaint( paintType, pMainColor, pSecondColor, pLogoColor,
                          pSecondColor2 );
    }
}

void VehicleBase::setInteriorColorsFromDB( DBEntryInterior *pInterior ) {
    // FUN_008b73f0
    if ( pInterior == nullptr ) {
        return;
    }

    setVehiclePartColor( CCP_InteriorColor1,
                         pInterior->InteriorColorID1.pAsString );
    setVehiclePartColor( CCP_InteriorColor2,
                         pInterior->InteriorColorID2.pAsString );
    setVehiclePartColor( CCP_InteriorMaterial, pInterior->Material.pAsString );

    setLowResInteriorColors( pInterior->InteriorColorID1.pAsString,
                             pInterior->InteriorColorID2.pAsString );
}

void VehicleBase::setRimsFromDB( DBEntryRim * ) {
    // NOP
}

void VehicleBase::setLowResInteriorColors( const char *, const char * ) {
    // NOP
}

void VehicleBase::applyDeluxeColorPairToResNodes( const ColorRGBA &param_2,
                                                  const ColorRGBA &param_3,
                                                  eCarColorPart param_4 ) {
    // FUN_008bd600
    const int32_t numResNodes = getResNodesForColorPart( param_4 );
    const Eigen::Vector4f firstColor = UnpackColorBytes( param_2 );
    const Eigen::Vector4f secondColor = UnpackColorBytes( param_3 );

    ForEachColorPartMaterial(
        pCurResNodesUsed, numResNodes, type, param_4,
        [&]( Material *pMaterial ) {
            MaterialShaderParameterArray *pParameters =
                pMaterial->getShaderParameters();

            float *pParamData = pParameters->getStaticParamDataPS( 0x7a );
            if ( pParamData != nullptr ) {
                memcpy( pParamData, firstColor.data(), sizeof( float ) * 4 );
            }

            pParamData = pParameters->getStaticParamDataPS( 0x7b );
            if ( pParamData != nullptr ) {
                memcpy( pParamData, secondColor.data(), sizeof( float ) * 4 );
            }
        } );
}

void VehicleBase::applyRampExpToResNodes( float param_2,
                                          eCarColorPart param_3 ) {
    // FUN_008bd420
    const int32_t numResNodes = getResNodesForColorPart( param_3 );

    ForEachColorPartMaterial(
        pCurResNodesUsed, numResNodes, type, param_3,
        [&]( Material *pMaterial ) {
            float *pParamData =
                pMaterial->getShaderParameters()->getStaticParamDataPS( 0x7c );
            if ( pParamData != nullptr ) {
                pParamData[0] = param_2;
            }
        } );
}

void VehicleBase::applyDeluxePaint( ePaintType paintType,
                                    const ColorRGBA *pMainColor,
                                    const ColorRGBA *pSecondColor,
                                    const ColorRGBA *pLogoColor,
                                    const ColorRGBA *pSecondColor2 ) {
    // FUN_0071fcb0
    if ( pMainColor == nullptr || paintType == ePaintType::PT_Default ) {
        return;
    }

    const bool bIsTwoTones =
        ( paintType == ePaintType::PT_CustomSolidTwoTones ||
          paintType == ePaintType::PT_CustomMetalTwoTones );
    const bool bLogoDrivesTones =
        ( pLogoColor != nullptr &&
          ( paintType == ePaintType::PT_CustomSolid ||
            paintType == ePaintType::PT_CustomMetal ) );

    const bool bCalibrated = bCalibratedDeluxePaint;
    const float colorScale = bCalibrated ? 1.0f : kDeluxeColorScale;

    const ColorRGBA mainColor = ScaleDeluxeColor( *pMainColor, colorScale );
    const ColorRGBA secondColor =
        ( pSecondColor != nullptr )
            ? ScaleDeluxeColor( *pSecondColor, colorScale )
            : gColorBlackNoAlpha;
    const ColorRGBA secondColor2 =
        ( pSecondColor2 != nullptr )
            ? ScaleDeluxeColor( *pSecondColor2, colorScale )
            : gColorBlackNoAlpha;

    ColorRGBA logoColor = gColorBlackNoAlpha;
    if ( bLogoDrivesTones ) {
        logoColor = ScaleDeluxeColor( *pLogoColor, 1.0f ); // FLOAT_00fab97c
    } else if ( bIsTwoTones && pLogoColor != nullptr ) {
        logoColor = ScaleDeluxeColor( *pLogoColor, colorScale );
    }

    auto computeShades =
        [bCalibrated]( const ColorRGBA &color, const ePaintType type,
                       ColorRGBA *pOutDarkened, ColorRGBA *pOutSpecular ) {
            if ( bCalibrated ) {
                ComputeCalibratedDeluxeShades( color, type, pOutDarkened,
                                               pOutSpecular );
            } else {
                ComputeDeluxeShades( color, type, pOutDarkened, pOutSpecular );
            }
        };

    // Calibrated: a flip colour the player did not pick is replaced with the
    // calibrated one
    const bool bUseSecondColor =
        pSecondColor != nullptr &&
        !( bCalibrated && IsAutoDarkenedColor( *pMainColor, *pSecondColor ) );
    const bool bUseSecondColor2 =
        pSecondColor2 != nullptr &&
        !( bCalibrated && pLogoColor != nullptr &&
           IsAutoDarkenedColor( *pLogoColor, *pSecondColor2 ) );

    ColorRGBA darkenedColor = gColorBlackNoAlpha;
    ColorRGBA specularColor = gColorBlackNoAlpha;

    computeShades( mainColor, paintType, &darkenedColor, &specularColor );
    applyDeluxeColorPairToResNodes(
        mainColor, bUseSecondColor ? secondColor : darkenedColor,
        CCP_BodyColor );
    applyColorToResNodes( specularColor, 0, CCP_BodyColor );

    ColorRGBA toneSpecular = gColorBlackNoAlpha;
    if ( bLogoDrivesTones ) {
        applyTwoTonesColorToResNodes( logoColor, CCP_LogoColor1, true );
        applyTwoTonesColorToResNodes( logoColor, CCP_LogoColor1, false );
        applyTwoTonesColorToResNodes( logoColor, CCP_LogoColor2, true );
        applyTwoTonesColorToResNodes( logoColor, CCP_LogoColor2, false );
    } else if ( bIsTwoTones ) {
        computeShades( logoColor, paintType, &darkenedColor, &toneSpecular );
        applyDeluxeColorPairToResNodes(
            logoColor, bUseSecondColor2 ? secondColor2 : darkenedColor,
            CCP_BodyColor2 );
        // Retail reuses the body specular (the tone one is never read back)
        applyColorToResNodes( bCalibrated ? toneSpecular : specularColor, 0,
                              CCP_BodyColor2 );
    }

    // Ford GT always gets the second tone (even for deliveries w/o stripes)
    if ( pCarState != nullptr && pCarState->getCarID() == 0x75 ) {
        computeShades( logoColor, ePaintType::PT_CustomSolid, &darkenedColor,
                       &toneSpecular );
        applyDeluxeColorPairToResNodes( logoColor, darkenedColor,
                                        CCP_BodyColor2 );
        applyColorToResNodes( bCalibrated ? toneSpecular : specularColor, 0,
                              CCP_BodyColor2 );
    }

    const DeluxeSurfaceSettings &surface =
        IsSolidDeluxePaint( paintType )
            ? gSolidSurfaceSettings
            : ( bCalibrated ? gCalibratedMetalSurfaceSettings
                            : gMetalSurfaceSettings );

    applyRampExpToResNodes( surface.RampExp, CCP_BodyColor );
    applySpecularParamToResNodes( surface.SpecularExp, CCP_BodyColor, true );
    applySpecularParamToResNodes( surface.SpecularExp, CCP_BodyColor, false );
    applyReflectionParamsToResNodes( surface.EnvMap, CCP_BodyColor, true );
    applyReflectionParamsToResNodes( surface.EnvMap, CCP_BodyColor, false );
}

Material *VehicleBase::setCalliperColor( const char *param_2 ) {
    // FUN_008bc480
    if ( param_2 == nullptr || gpCarColors == nullptr ) {
        return nullptr;
    }

    Render2DM &colors2DM = gpCarColors->get2DM();
    if ( colors2DM.getMaterialArray() == nullptr ) {
        return nullptr;
    }

    RenderFile::Section *pColorSection =
        colors2DM.getMaterial( GetIdentifier64bit( param_2 ) );
    if ( pColorSection == nullptr ) {
        return nullptr;
    }

    Material *pColorMaterial = (Material *)( pColorSection + 1 );
    MaterialShaderParameterArray *pColorParams =
        (MaterialShaderParameterArray *)pColorMaterial->getParameterByIndex(
            0 );

    Eigen::Vector4f paintShaderColor = Eigen::Vector4f::Zero();
    Eigen::Vector4f otherShaderColor = Eigen::Vector4f::Zero();

    const float *pParamData = pColorParams->getStaticParamDataPS( 0x7a );
    if ( pParamData != nullptr ) {
        paintShaderColor = Eigen::Vector4f( pParamData[0], pParamData[1],
                                            pParamData[2], pParamData[3] );
    }

    pParamData = pColorParams->getStaticParamDataPS( 0xd5 );
    if ( pParamData != nullptr ) {
        otherShaderColor = Eigen::Vector4f( pParamData[0], pParamData[1],
                                            pParamData[2], pParamData[3] );
    }

    HiearchyNode *pNodeFL =
        ( pParentNode != nullptr )
            ? pParentNode->findChild( GetIdentifier64bit( "STIR_FL" ) )
            : nullptr;
    HiearchyNode *pNodeRL =
        ( pParentNode != nullptr )
            ? pParentNode->findChild( GetIdentifier64bit( "STIR_RL" ) )
            : nullptr;
    if ( pNodeFL == nullptr || pNodeRL == nullptr ) {
        return pColorMaterial;
    }

    Instance *pInstance = pNodeFL->pInstance;
    RenderFile::Section *pBankMat =
        ( pInstance != nullptr ) ? pInstance->getMaterialBank() : nullptr;
    if ( pBankMat == nullptr ) {
        return nullptr;
    }

    const uint32_t numMaterials =
        ( pBankMat->DataSize - 0x10 ) / (uint32_t)sizeof( Render3DDHashEntry );
    if ( numMaterials == 0 ) {
        return nullptr;
    }

    Render3DDHashEntry *pEntries = (Render3DDHashEntry *)( pBankMat + 1 );
    for ( uint32_t i = 0; i < numMaterials; i++ ) {
        RenderFile::Section *pMatSection = pEntries[i].pEntry;
        if ( pMatSection == nullptr ) {
            continue;
        }

        Material *pMaterial = (Material *)( pMatSection + 1 );
        MaterialParameter *pParameters = pMaterial->getParameterByIndex( 0 );

        const bool bIsPaintShader =
            ( pParameters != nullptr &&
              pParameters->Hashcode == 0xc288dfc455ae5ef8ull );
        const Eigen::Vector4f &color =
            bIsPaintShader ? paintShaderColor : otherShaderColor;

        pMaterial->DiffuseColor = color;
        pMaterial->AmbientColor = color;
    }

    return pColorMaterial;
}

void VehicleBase::setCurrentLuminosity( const float param_1 ) {
    currentLuminosity = param_1;
    currentEnvFactor =
        ( ( param_1 - 1.0f ) * gReflectionFactor + 1.0f ) * gGlobalEnvValue;
}

HiearchyNode *VehicleBase::getShadowDummyNode() {
    // FUN_004300a0
    return ( !bNeedToUnstream ) ? displayInstances[1].pNode : nullptr;
}

bool VehicleBaseResources::load( int32_t carID,
                                 int32_t rimID,
                                 int32_t tireID,
                                 int32_t flags,
                                 uint64_t packHashcode,
                                 int32_t param_4,
                                 eVehicleResourceType param_6 ) {
    // FUN_008ac630
    RimID = rimID;
    PackHashcode = packHashcode;
    CarID = carID;
    TireID = tireID;
    bDefault = false;
    bIsLoading = true;

    bool bIsBike = false;
    const GameDatabase &gameDB = gpDatabase->getGameDB();
    DBEntryCarPhysicsData *local_488 = nullptr;

    for ( uint32_t i = 0; i < gameDB.pNumItems[1]; i++ ) {
        if ( gameDB.DBCarData[i].CarID == carID ) {
            local_488 = &gameDB.DBCarData[i];
            break;
        }
    }

    // Default car fallback (if no entry found in db)
    if ( local_488 == nullptr ) {
        if ( ( carID < 1500 ) || ( 1998 < carID ) ) {
            CarID = 999;
        } else {
            CarID = 666;
        }

        RimID = -1;
        TireID = -1;
        bDefault = true;

        local_488 = gpDatabase->getCarDBEntry( CarID );
    }

    if ( local_488 == nullptr ) {
        bIsBike = false;
    } else {
        char *pBikeCategory = gpDatabase->getStringByHashcode( 1, 0x35da5b5 );
        bIsBike = ( pBikeCategory == local_488->Category.pAsString );
    }

    auto &dbCarList = gpDatabase->getCarList();
    const CarConfig *local_484 = nullptr;
    for ( auto &entry : dbCarList ) {
        if ( entry.Hash == carID ) {
            local_484 = &entry;
            break;
        }
    }
    if ( ( ~(int8_t)( flags >> 1 ) & 1 ) != 0 ) {
        bUseGamePhysics = 1;
    }
    if ( ( ~(int8_t)( flags >> 2 ) & 1 ) != 0 ) {
        bSoundResource = true;
    }
    bDriveResource = (bool)( ~(int8_t)( flags >> 4 ) & 1 );
    bool bVar12 = param_4;
    if ( ( ~(uint8_t)flags & 1 ) == 0 ) {
        if ( !bVar12 || Exterior.pBankInstance == nullptr ) {
            bIsLoading = false;
            bInitialized = true;
        }
        return true;
    }

    // Special branch for traffic banks
    if ( local_488 != nullptr ) {
        char *pcVar8 = gpDatabase->getStringByHashcode(
            6, 0x3540681 ); // Retrieves TRAFFIC brand
        if ( pcVar8 == local_488->Brand->BrandNameID.pAsString ) {
            std::string local_480 = "Traffic/";
            local_480 += local_484->Model;
            local_480 += local_484->SubModel;

            eVehicleResourceType resourceType =
                param_6 != eVehicleResourceType::VRT_Traffic
                    ? eVehicleResourceType::VRT_Exterior
                    : eVehicleResourceType::VRT_Traffic;

            bool bVar6 =
                gpCarDisplay->allocateFromPool( resourceType, &Exterior );
            if ( bVar6 ) {
                bVar6 = Exterior.pBankInstance->loadToPooledMemory(
                    bVar12, Exterior.pBankData, 0x851eb, local_480.c_str() );
                if ( bVar6 ) {
                    if ( bVar12 == false ||
                         Exterior.pBankInstance == nullptr ) {
                        bIsLoading = false;
                        bInitialized = true;
                    }
                    return true;
                }
                gpCarDisplay->releaseEntry( &Exterior );
                Exterior.pBankInstance = nullptr;
                Exterior.pBankData = nullptr;
                bIsLoading = false;
                bInitialized = false;
            }
            return false;
        }
    }

    // Exterior Bank
    bool bVar6 = gpCarDisplay->allocateFromPool(
        eVehicleResourceType::VRT_Exterior, &Exterior );
    if ( !bVar6 ) {
        return false;
    }
    std::string local_480 = local_484->BankGeometryName;

    bVar6 = Exterior.pBankInstance->validateResourcePath( bIsBike, false,
                                                          local_480, 0x300000 );
    if ( !bVar6 ) {
        bDefault = true;
    }
    Exterior.pBankInstance->loadToPooledMemory( bVar12, Exterior.pBankData,
                                                0x300000, local_480.c_str() );

    // Interior Bank
    if ( param_6 != eVehicleResourceType::VRT_Traffic ) {
        bVar6 = gpCarDisplay->allocateFromPool(
            eVehicleResourceType::VRT_Interior, &Interior );
        if ( !bVar6 ) {
            return false;
        }
        local_480 = local_484->BankGeometryName + "_I";

        bVar6 = Exterior.pBankInstance->validateResourcePath(
            bIsBike, false, local_480, 0x633333 );
        if ( !bVar6 ) {
            bDefault = true;
        }
        Interior.pBankInstance->loadToPooledMemory(
            bVar12, Interior.pBankData, 0x633333, local_480.c_str() );
    }

    gpCarDisplay->allocateFromPool( eVehicleResourceType::VRT_Rims,
                                    &FrontRims );
    bVar6 = gpCarDisplay->allocateFromPool( eVehicleResourceType::VRT_Rims,
                                            &RearRims );
    if ( !bVar6 ) {
        return false;
    }

    // Rims Bank(s)
    DBEntryRim *pGVar3 = (DBEntryRim *)local_488->DefaultRims.pAsRawPointer;
    DBEntryRim *pGVar8 = nullptr;
    if ( RimID == -1 ) {
        RimID = gpDatabase->getRimID( carID, pGVar3 );
        pGVar8 = pGVar3;
        if ( pGVar3 == nullptr ) {
            if ( !bVar12 || Exterior.pBankInstance == nullptr ) {
                bIsLoading = false;
                bInitialized = true;
            }
            return true;
        }
    } else {
        pGVar8 = gpDatabase->getRim( CarID, RimID );
        if ( pGVar8 == nullptr ) {
            pGVar8 = pGVar3;
            if ( pGVar3 == nullptr ) {
                if ( !bVar12 || Exterior.pBankInstance == nullptr ) {
                    bIsLoading = false;
                    bInitialized = true;
                }
                return true;
            }
        }
    }

    if ( pGVar8->ResourcePathFront.Hashcode ==
         pGVar8->ResourcePathRear.Hashcode ) {
        FrontRims.pBankInstance->setOriginal( RearRims.pBankInstance,
                                              RearRims.pBankData, 0x87ae1 );
    } else {
        std::string acStack_400 = "rim/";
        acStack_400 += pGVar8->ResourcePath.pAsString;
        acStack_400 += "/";
        acStack_400 += pGVar8->ResourcePathRear.pAsString;

        Exterior.pBankInstance->validateResourcePath( bIsBike, false,
                                                      acStack_400, 0x87ae1 );

        RearRims.pBankInstance->loadToPooledMemory(
            bVar12, RearRims.pBankData, 0x87ae1, acStack_400.c_str() );
    }

    std::string acStack_450 = "rim/";
    acStack_450 += pGVar8->ResourcePath.pAsString;
    acStack_450 += "/";
    acStack_450 += pGVar8->ResourcePathFront.pAsString;

    Exterior.pBankInstance->validateResourcePath( bIsBike, true, acStack_450,
                                                  0x87ae1 );

    FrontRims.pBankInstance->loadToPooledMemory( bVar12, FrontRims.pBankData,
                                                 0x87ae1, acStack_450.c_str() );

    if ( bVar12 == false || Exterior.pBankInstance == nullptr ) {
        bIsLoading = false;
        bInitialized = true;
    }

    return true;
}

inline void ReleaseVehicleBank( StreamedInBankVehicle &bank ) {
    if ( bank.pBankInstance != nullptr ) {
        bank.pBankInstance->unload();

        gpCarDisplay->releaseEntry( &bank );

        bank.pBankInstance = nullptr;
        bank.pBankData = nullptr;
    }
}

static inline bool IsVehicleBankReady( const StreamedInBankVehicle &bank ) {
    return bank.pBankInstance->getState() == BankVehicle::eState::BVS_Ready;
}

bool VehicleBaseResources::isLoaded() {
    // FUN_008ac4c0
    if ( bIsLoading && Exterior.pBankInstance != nullptr &&
         IsVehicleBankReady( Exterior ) &&
         ( Interior.pBankInstance == nullptr ||
           IsVehicleBankReady( Interior ) ) &&
         ( FrontRims.pBankInstance == nullptr ||
           ( IsVehicleBankReady( FrontRims ) &&
             IsVehicleBankReady( RearRims ) ) ) ) {
        bIsLoading = false;
        bInitialized = true;
    }

    return bInitialized;
}

bool VehicleBase::isInteriorLoading() const {
    // FUN_008b5b80
    BankVehicle *pBank = pInteriorBank->pBankInstance;
    return pBank != nullptr &&
           pBank->getState() == BankVehicle::eState::BVS_Loading;
}

bool VehicleBase::isInteriorLoaded() const {
    // FUN_008b5f40
    BankVehicle *pBank = pInteriorBank->pBankInstance;
    return pBank != nullptr &&
           pBank->getState() == BankVehicle::eState::BVS_Ready;
}

bool VehicleBase::unloadInterior() {
    // FUN_008b7530
    BankVehicle *pBank = pInteriorBank->pBankInstance;
    if ( pBank != nullptr ) {
        if ( pBank->getState() == BankVehicle::eState::BVS_Loading ) {
            return false;
        }

        if ( pBank->getState() != BankVehicle::eState::BVS_Ready &&
             pBank->isWaitingForInstanciation() ) {
            gpTestDriveInstance->flushPendingFileInstanciation( false );
        }

        uninstantiateInterior();

        if ( interiorResource.pRender3DD != nullptr &&
             interiorResource.pResourceBank != nullptr ) {
            interiorResource.release( false );
        }
        interiorResource.pResourceBank = nullptr;
        interiorResource.pRender2DM = nullptr;
        interiorResource.pRender3DD = nullptr;

        pInteriorBank->pBankInstance->unload();
        gpCarDisplay->releaseEntry( pInteriorBank );
        pInteriorBank->pBankInstance = nullptr;
        pInteriorBank->pBankData = nullptr;
    }

    applyLOD();
    return true;
}

bool VehicleBase::loadInterior() {
    // FUN_008b7b70
    static constexpr int32_t kFirstBikeCarID = 1500;
    static constexpr int32_t kLastBikeCarID = 1998;

    if ( interiorResource.pResourceBank != nullptr &&
         interiorResource.pResourceBank->getState() ==
             BankVehicle::eState::BVS_Ready ) {
        return false;
    }

    const bool bAcquired = gpCarDisplay->allocateFromPool(
        eVehicleResourceType::VRT_Interior, pInteriorBank );

    const int32_t carID = pCarState->getCarID();
    const CarConfig *pConfig = nullptr;
    for ( const CarConfig &entry : gpDatabase->getCarList() ) {
        if ( entry.Hash == carID ) {
            pConfig = &entry;
            break;
        }
    }

    if ( !bAcquired || pConfig == nullptr ) {
        return false;
    }

    std::string bankName = pConfig->BankGeometryName + "_I";
    const bool bIsBike = carID >= kFirstBikeCarID && carID <= kLastBikeCarID;
    pInteriorBank->pBankInstance->validateResourcePath( bIsBike, false,
                                                        bankName, 0x633333 );
    if ( pInteriorBank->pBankInstance->loadToPooledMemory(
             true, pInteriorBank->pBankData, 0x633333, bankName.c_str() ) ) {
        return true;
    }

    gpCarDisplay->releaseEntry( pInteriorBank );
    pInteriorBank->pBankInstance = nullptr;
    pInteriorBank->pBankData = nullptr;
    return false;
}

void VehicleBaseResources::unload() {
    // FUN_008ac570
    if ( bInitialized && RefCount < 1 ) {
        ReleaseVehicleBank( Exterior );
        ReleaseVehicleBank( Interior );
        ReleaseVehicleBank( FrontRims );
        ReleaseVehicleBank( RearRims );

        if ( bSoundResource != false ) {
            bSoundResource = false;
        }
        if ( PhysicsBank.isLoaded() ) {
            bUseGamePhysics = 0;
            bEngineResource = false;
        }

        bIsLoading = false;
        bInitialized = false;
    }
}

void VehicleBase::instantiateShadowPlane() {
    // FUN_008bd8a0
    addLightSetupForNode( pParentNode );

    uint64_t uVar17 = GetIdentifier64bit( kShadowHashcode );
    pParentShadowNode = bodyResource.Collection3D.instantiate( uVar17 );
    displayInstances[1].pNode = pParentShadowNode;
    if ( pParentShadowNode != nullptr ) {
        displayInstances[1].InstanceMatrix = pParentShadowNode->TransformMatrix;
        displayInstances[1].pInstance = pParentShadowNode->pInstance;
    }

    uVar17 = GetIdentifier64bit( kShadowPlaneHashcode );
    pParentShadowPlaneNode = bodyResource.Collection3D.instantiate( uVar17 );
    displayInstances[2].pNode = pParentShadowPlaneNode;
    if ( pParentShadowPlaneNode != nullptr ) {
        displayInstances[2].InstanceMatrix =
            pParentShadowPlaneNode->TransformMatrix;
        displayInstances[2].pInstance = pParentShadowPlaneNode->pInstance;

        if ( displayInstances[2].pInstance != nullptr ) {
            RenderFile::Section *peVar3 =
                displayInstances[2].pInstance->getMaterialBank();
            if ( peVar3 != nullptr ) {
                Render3DDHashEntry *pMainEntry =
                    (Render3DDHashEntry *)( peVar3 + 1 );

                RenderFile::Section *pMaterialSection = pMainEntry->pEntry;
                if ( pMaterialSection != nullptr ) {
                    Material *puVar11 = (Material *)( pMaterialSection + 1 );
                    MaterialParameter *peVar6 =
                        puVar11->getParameterByIndex( 0 );

                    if ( peVar6 != nullptr && peVar6->NumLayers != 0 ) {
                        MaterialLayer *peVar8 = peVar6->getLayer( 0 );
                        pShadowTexture =
                            peVar8->pLayerTextures[0].pTextureSection;
                    }
                }
            }
        }
    }

    Instance *peVar9 = displayInstances[3].pInstance;
    if ( peVar9 != nullptr ) {
        peVar9->getFlagsWrite() = peVar9->getFlags() | 4;
    }

    hashGlassExterior = GetIdentifier64bit( "GLASS" );
    hashGlassInterior = GetIdentifier64bit( "GLASS_I" );

    peVar9 = displayInstances[3].pInstance;
    if ( peVar9 != nullptr ) {
        uVar17 = GetIdentifier64bit( "BODY_LR" );
        peVar9 = bodyResource.Collection3D.findAndInstantiate( uVar17 );

        pVehicleBodyLowResInstance = peVar9;
        if ( peVar9 != nullptr ) {
            peVar9->addSetupToGraph( &lightCfg );
        }

        int32_t iVar10 =
            ( type == eType::VBT_Bike || type == eType::VBT_TrafficBike ) ? 4
                                                                          : 0;
        for ( uint32_t i = 0; i < kMaxNumWheels; i++ ) {
            if ( displayInstances[8 + i].pInstance != nullptr ) {
                uVar17 = GetIdentifier64bit( kWheelHashcodes[iVar10 + i] );
                peVar9 = bodyResource.Collection3D.findAndInstantiate( uVar17 );
                vehicleWheelLowResInstances[i] = peVar9;
                if ( peVar9 != nullptr ) {
                    peVar9->addSetupToGraph( &lightCfg );
                }
            }
        }
    }

    FUN_008ba190();
    bindFlareUniforms();
    FUN_008b9e80();
    FUN_008bb100();

    bAlphaCacheInUse = false;
    bUseHQInterior = false;
    bFirstRenderFrame = true;
}

void VehicleBase::FUN_008ba190() {
    // FUN_008ba190
    if ( !BOOL_010e74e0 ) {
        static constexpr char kFlareTypes[] = { 'L', 'B', 'S', 'W', 'R' };

        char local_10[9];
        int32_t iVar11 = 0;
        uint64_t uVar12 = 0;
        int32_t local_14 = 0;
        for ( int32_t iVar9 = 0; iVar9 < 120; iVar9 += 24 ) {
            for ( int32_t local_18 = 0; local_18 < 3; local_18++ ) {
                if ( local_18 == 0 ) {
                    size_t iVar9Offset = ( iVar9 / sizeof( uint64_t ) );
                    sprintf( local_10, "FA_%c_FL", kFlareTypes[iVar11] );
                    uVar12 = GetIdentifier64bit( local_10 );
                    DAT_010c4818[45 + iVar9Offset] = uVar12;

                    sprintf( local_10, "FA_%c_FR", kFlareTypes[iVar11] );
                    uVar12 = GetIdentifier64bit( local_10 );
                    DAT_010c4818[30 + iVar9Offset] = uVar12;

                    sprintf( local_10, "FA_%c_RL", kFlareTypes[iVar11] );
                    uVar12 = GetIdentifier64bit( local_10 );
                    DAT_010c4818[15 + iVar9Offset] = uVar12;

                    sprintf( local_10, "FA_%c_RR", kFlareTypes[iVar11] );
                    uVar12 = GetIdentifier64bit( local_10 );
                    DAT_010c4818[0 + iVar9Offset] = uVar12;
                } else {
                    sprintf( local_10, "FA_%c_FL%d", kFlareTypes[iVar11],
                             local_18 );
                    int32_t iVar10 = local_18 + local_14;
                    uVar12 = GetIdentifier64bit( local_10 );
                    DAT_010c4818[iVar10 + 0x2d] = uVar12;

                    sprintf( local_10, "FA_%c_FR%d", kFlareTypes[iVar11],
                             local_18 );
                    uVar12 = GetIdentifier64bit( local_10 );
                    DAT_010c4818[iVar10 + 0x1e] = uVar12;

                    sprintf( local_10, "FA_%c_RL%d", kFlareTypes[iVar11],
                             local_18 );
                    uVar12 = GetIdentifier64bit( local_10 );
                    DAT_010c4818[iVar10 + 0xf] = uVar12;

                    sprintf( local_10, "FA_%c_RR%d", kFlareTypes[iVar11],
                             local_18 );
                    uVar12 = GetIdentifier64bit( local_10 );
                    DAT_010c4818[iVar10] = uVar12;
                }
            }
            local_14 = local_14 + 3;
            iVar11++;
        }

        BOOL_010e74e0 = true;
    }

    for ( int32_t iVar9 = 0; iVar9 < 15; iVar9++ ) {
        flaresRL[iVar9] =
            displayInstances[0].pNode->findChild( DAT_010c4818[0xf + iVar9] );

        flaresRR[iVar9] =
            displayInstances[0].pNode->findChild( DAT_010c4818[iVar9] );

        flaresFL[iVar9] =
            displayInstances[0].pNode->findChild( DAT_010c4818[0x2d + iVar9] );

        flaresFR[iVar9] =
            displayInstances[0].pNode->findChild( DAT_010c4818[0x1e + iVar9] );

        flares[iVar9 * 4 + 0] =
            flaresRL[iVar9] != nullptr ? flaresRL[iVar9]->pInstance : nullptr;

        flares[iVar9 * 4 + 1] =
            flaresRR[iVar9] != nullptr ? flaresRR[iVar9]->pInstance : nullptr;

        flares[iVar9 * 4 + 2] =
            flaresFL[iVar9] != nullptr ? flaresFL[iVar9]->pInstance : nullptr;

        flares[iVar9 * 4 + 3] =
            flaresFR[iVar9] != nullptr ? flaresFR[iVar9]->pInstance : nullptr;
    }
}

int32_t
VehicleBase::getHeadlightNodes( const int32_t side,
                                const HiearchyNode **ppOutNodes ) const {
    // Slots 0..2 of the front flare arrays are the headlight ('L') flares.
    const std::array<HiearchyNode *, 15> &flares =
        ( side == 0 ) ? flaresFL : flaresFR;
    int32_t numNodes = 0;
    for ( int32_t i = 0; i < kNumHeadlightNodes; i++ ) {
        if ( flares[i] != nullptr ) {
            ppOutNodes[numNodes++] = flares[i];
        }
    }
    if ( numNodes == 0 && !displayInstances.empty() &&
         displayInstances[0].pNode != nullptr ) {
        ppOutNodes[0] = displayInstances[0].pNode->findChild(
            GetIdentifier64bit( ( side == 0 ) ? "LGT_FL" : "LGT_FR" ) );
        numNodes = ( ppOutNodes[0] != nullptr ) ? 1 : 0;
    }
    return numNodes;
}

void VehicleBase::bindFlareUniforms() {
    // FUN_008bb6c0
    if ( bodyResource.pRender2DM == nullptr ) {
        return;
    }

    uint32_t uVar6 = bodyResource.Resource2DM.getNumMaterials();
    for ( uint32_t uVar5 = 0; uVar5 < uVar6; uVar5++ ) {
        RenderFile::Section *peVar2Section =
            bodyResource.Resource2DM.getMaterial( uVar5 );
        if ( peVar2Section == nullptr ) {
            continue;
        }
        Material *peVar2 = (Material *)( peVar2Section + 1 );
        MaterialShaderParameterArray *peVar3 =
            (MaterialShaderParameterArray *)peVar2->getParameterByIndex( 0 );
        if ( peVar3->Hashcode == 0xfcdaa96bdeadbeef ) {
            uint32_t index = 0;
            void *peVar4 = peVar3->getParameter( 0x7a, &index );
            if ( peVar4 != nullptr ) {
                MaterialShaderParameter *peVar4 =
                    peVar3->getParameterByIndex( index );

                float *pfeVar4 = (float *)peVar4;
                pfeVar4[0] = VehicleBase::FlareScaleStart;
                pfeVar4[1] = VehicleBase::FlareScaleRatio;
                pfeVar4[2] = VehicleBase::FlareZBiasRatio;
                pfeVar4[3] = gFlareZBiasRatio;
            }
        }
    }
}

void VehicleBase::FUN_008b9e80() {
    // FUN_008b9e80
    if ( !gbExhaustHashcodeListInit ) {
        for ( int32_t i = 0; i < 4; i++ ) {
            uint64_t uVar6 = GetIdentifier64bit( kExhaustNames[i + 0] );
            gExhaustHashcodesH[i] = uVar6;

            uVar6 = GetIdentifier64bit( kExhaustNames[i + 4] );
            gExhaustHashcodesO[i] = uVar6;
        }
        gbExhaustHashcodeListInit = true;
    }

    if ( !bNeedToUnstream && displayInstances[0].pNode != nullptr ) {
        for ( int32_t i = 0; i < 4; i++ ) {
            exhaustsDummies[i] = displayInstances[0].pNode->findInstance(
                gExhaustHashcodesH[i] );

            if ( exhaustsDummies[i] == nullptr ) {
                exhaustsDummies[i] = displayInstances[0].pNode->findInstance(
                    gExhaustHashcodesO[i] );

                if ( exhaustsDummies[i] != nullptr ) {
                    bUnknownFlag = true;
                }
            }
        }
    }
}

void VehicleBase::FUN_008bb100() {
    // FUN_008bb100
    bHasColorStrips = false;
    bUseTwoTonesPaint = false;

    uint64_t uVar11 = GetIdentifier64bit( "PAINT" );
    uint64_t uVar12 = GetIdentifier64bit( "PAINT2" );

    if ( bodyResource.pRender2DM == nullptr ) {
        return;
    }

    auto pIt = bodyResource.Resource2DM.getHashTableHead();
    for ( uint32_t i = 0; i < bodyResource.Resource2DM.getNumMaterials();
          i++ ) {
        if ( pIt->Hashcode == uVar11 ) {
            RenderFile::Section *pSection = pIt->pEntry;
            Material *peVar4 = (Material *)( pSection + 1 );

            MaterialParameter *peVar5 = peVar4->getParameterByIndex( 0 );
            if ( peVar5 != nullptr && peVar5->NumLayers != 0 ) {
                MaterialLayer *peVar6 = peVar5->getLayer( 0 );
                if ( peVar6->pLayerTextures[0].Hashcode != 0ull ) {
                    bHasColorStrips = true;
                }
            }
        } else if ( pIt->Hashcode == uVar12 ) {
            bUseTwoTonesPaint = true;
        }

        pIt++;
    }
}

void VehicleBase::propagateNodeTransform( uint32_t nodeIndex,
                                          Matrix4x4f_t &matrix,
                                          const int32_t mode ) {
    HiearchyNode *peVar4 = displayInstances[nodeIndex].pNode;
    if ( peVar4 != nullptr ) {
        peVar4->TransformMatrix =
            displayInstances[nodeIndex].InstanceMatrix * matrix;
        propagateNodeTransform( peVar4, matrix, mode );
    }
}

void VehicleBase::propagateNodeTransform( HiearchyNode *peVar4,
                                          Matrix4x4f_t &matrix,
                                          const int32_t mode ) {
    if ( peVar4->pParent == nullptr ) {
        peVar4->pInstance->setModelMatrix( peVar4->TransformMatrix );
    } else {
        Matrix4x4f_t modelMat = peVar4->TransformMatrix *
                                peVar4->pParent->pInstance->getModelMatrix();
        peVar4->pInstance->setModelMatrix( modelMat );
    }

    if ( mode == 0 ) {
        peVar4->updateAndPropagateTransformMatrices( false );
    } else if ( mode == 1 ) {
        peVar4->propagateTransformMatricesNoScale();
    } else if ( mode == 2 ) {
        peVar4->propagateTransformMatricesWithFlag();
    }
}

void VehicleBase::propagateShadowDummyTransform( Matrix4x4f_t &bodyMatrix ) {
    propagateNodeTransform( 1, bodyMatrix, 1 );
}

void VehicleBase::removeShaderSetup( HiearchyNode *param_2,
                                     SetupNode *pShaderSetup ) {
    // FUN_008b5c60, FUN_008c1520
    for ( ; param_2 != nullptr; param_2 = param_2->pNext ) {
        Instance *pInstance = param_2->pInstance;
        if ( pInstance == nullptr ) {
            continue;
        }

        if ( pInstance->getSetupRW().findShaderParameterNode() != nullptr ) {
            pInstance->removeSetupFromGraph( pShaderSetup );
        }
    }
}

void VehicleBase::retrieveBoundingRadius( float *param_1,
                                          HiearchyNode *param_2,
                                          Eigen::Vector4f *param_3 ) {
    // FUN_008baa30
    Instance *peVar1 = param_2->pInstance;
    if ( peVar1->getNumLODs() != 0 ) {
        uint8_t *pvVar2 = (uint8_t *)peVar1->getLOD( 0 ).pHashcode->pEntry;
        if ( pvVar2 == nullptr ) {
            return;
        }

        RenderObject *iVar3 = (RenderObject *)( pvVar2 + 0x10 );
        *param_1 = iVar3->BoundingSphere.w() * peVar1->getBoundingScale();

        Eigen::Vector4f local_20 = iVar3->BoundingSphere;
        local_20.w() = 0.0f;

        *param_3 = Mat4x4fTransform( peVar1->getModelMatrix(), local_20 );
        param_3->w() = 1.0f;
    }
}
