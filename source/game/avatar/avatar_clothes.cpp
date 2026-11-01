#include "shared.h"
#include "avatar_clothes.h"

#include "config/gs_config.h"
#include "core/maths_helper.h"
#include "gs_avatar.h"
#include "game/vehicle/gs_car_common.h"
#include "render/gs_render.h"
#include "render/material.h"
#include "render/hiearchy_node.h"
#include "render/mng_env_map.h"
#include "render/render_context.h"
#include "render/mng_shadow.h"
#include "render/render_scene.h"
#include "render/render_target.h"
#include "world/gs_mng_resources.h"

static constexpr int32_t kClotheGeometryPoolSize[AC_Count] = { // DAT_010c4508
    0, 0, 0, 0x15800, 0x15800, 0, 0x15800, 0, 0 };
static constexpr int32_t kClotheTexturePoolSize[AC_Count] = { // DAT_010c452c
    0, 0, 0, 0x1ec00, 0x1ec00, 0, 0x12400, 0, 0 };
static constexpr float kAvatarPartLODRadius = 3.0f;
static constexpr char kColorSetSuffix[] = "_MAT"; // DAT_00f12cb0
static constexpr uint64_t kOpaqueClotheShaderHashcode = 0xd5013ed0db79baba;
static constexpr uint64_t kEyeShaderHashcode = 0xd5013ed0db79bbbb;
static constexpr uint32_t kStaticParamSkinBlend = 0x1b;
static constexpr uint32_t kStaticParamSkinDark = 0xd5;
static constexpr uint32_t kStaticParamSkinLight = 0xd6;
static constexpr uint32_t kStaticParamSkinTint = 0xd4;
static constexpr uint32_t kStaticParamEyeUVOffset = 0xd9;
static constexpr uint32_t kStaticParamLightHeightRange = 0xe6;
static constexpr uint32_t kStaticParamLightAttenuation = 0xe7;
static constexpr uint32_t kStaticParamHeadUp = 0xe8;
static constexpr uint32_t kStaticParamHeadRight = 0xe9;
static constexpr uint64_t kLightAttenuationShaderHashcodes[] = {
    kEyeShaderHashcode, kOpaqueClotheShaderHashcode, 0xd5013ed0db79eaeaull,
    0xd5013ed0dbaabebeull, 0xd5013ed0dbdededeull };
static constexpr uint64_t kReflectiveClotheShaderHashcode =
    0xd5013ed0db79dacbull;
static constexpr uint64_t kEnvMapShaderHashcodes[] = {
    kAvatarShaderHashcode, 0xc288dfc455ae5ef8ull, 0x1b38348331d3942aull,
    0xd5013ed0db79dacbull, kEyeShaderHashcode };

static bool IsEmptyPartName( const char *pName ) {
    // FUN_008f3320
    static const char *kEmptyPartNames[] = {
        "M_NO_GLOVES",   "W_NO_GLOVES",  "M_NO_HELMET", "W_NO_HELMET",
        "M_NO_GLASSES",  "W_NO_GLASSES", "M_NO_HAIR",   "M_NO_PILOSITY",
        "W_NO_PILOSITY", "NO_MESH" };

    if ( pName == nullptr ) {
        return false;
    }

    for ( const char *pEmptyName : kEmptyPartNames ) {
        if ( strcmp( pEmptyName, pName ) == 0 ) {
            return true;
        }
    }

    return false;
}

static void *
CopyStreamToPool( uint8_t *pPool, const int32_t poolSize, void *pStream ) {
    if ( pStream == nullptr || pPool == nullptr ) {
        return nullptr;
    }

    const uint32_t streamSize =
        static_cast<RenderFile::Header *>( pStream )->Size;
    if ( streamSize > static_cast<uint32_t>( poolSize ) ) {
        return nullptr;
    }

    memcpy( pPool, pStream, streamSize );
    return pPool;
}

static void SetRootInstanceLODRadius( Render3DD *p3DD, const float radius ) {
    // FUN_008f0b90
    InstanceEntry *pInstanceEntry = nullptr;

    if ( p3DD->getNumHiearchy() != 0 ) {
        HiearchyNodeEntry *pNodeEntry =
            (HiearchyNodeEntry *)( p3DD->getHiearchyEntry( 0 ) + 1 );
        RenderFile::Section *pInstanceSection = pNodeEntry->pInstance;
        pInstanceEntry = (InstanceEntry *)( pInstanceSection + 1 );
    } else if ( p3DD->getNumInstances() != 0 ) {
        pInstanceEntry = (InstanceEntry *)( p3DD->getInstanceEntry( 0 ) + 1 );
    }

    if ( pInstanceEntry == nullptr ) {
        return;
    }

    for ( uint16_t i = 0; i < pInstanceEntry->NumLODs; i++ ) {
        Scene3DDHashEntry *pHashEntry = pInstanceEntry->LODs[i].pHashcode;
        RenderFile::Section *pObjectSection = pHashEntry->pEntry;
        RenderObject *pObject = (RenderObject *)( pObjectSection + 1 );

        pObject->BoundingSphere.w() = radius;
    }
}

static void SetHierarchyColor( HiearchyNode *pNode, const ColorRGBA &color ) {
    // FUN_008f07d0
    if ( pNode->pInstance != nullptr ) {
        pNode->pInstance->setColor( color );
    }

    if ( pNode->NumChildren != 0 && pNode->pNext != nullptr ) {
        SetHierarchyColor( pNode->pNext, color );
    }
}

static void AddSetupToHierarchy( HiearchyNode *pNode, SetupNode *pSetup ) {
    // FUN_008f08c0
    for ( ; pNode != nullptr; pNode = pNode->pNext ) {
        if ( pNode->pInstance != nullptr ) {
            pNode->pInstance->addSetupToGraph( pSetup );
        }
    }
}

static void RemoveSetupFromHierarchy( HiearchyNode *pNode, SetupNode *pSetup ) {
    // FUN_008f08a0
    for ( ; pNode != nullptr; pNode = pNode->pNext ) {
        if ( pNode->pInstance != nullptr ) {
            pNode->pInstance->removeSetupFromGraph( pSetup );
        }
    }
}

static uint64_t GetUnchangeableMaterialHashcode() {
    // DAT_01478a78
    static const uint64_t hashcode =
        AvatarCalcHashcode( "UNCHANGEABLE" ); // FUN_009def00
    return hashcode;
}

static uint64_t GetCubemapTextureHashcode() {
    // DAT_01478a80
    static const uint64_t hashcode =
        AvatarCalcHashcode( "CUBEMAP" ); // FUN_009def20
    return hashcode;
}

// TODO: Move to material.cpp?
static void ApplyColorMaterial( Material *pDestination, Material *pSource ) {
    // FUN_008f48a0
    MaterialParameter *pSourceParam = pSource->getParameterByIndex( 0 );
    MaterialParameter *pDestinationParam =
        pDestination->getParameterByIndex( 0 );
    if ( pSourceParam == nullptr || pDestinationParam == nullptr ||
         pSourceParam->Hashcode != pDestinationParam->Hashcode ) {
        return;
    }

    pDestination->AmbientColor = pSource->AmbientColor;
    pDestination->DiffuseColor = pSource->DiffuseColor;
    pDestination->SpecularColor = pSource->SpecularColor;

    if ( pSourceParam->NumLayers != 0 &&
         pSourceParam->NumLayers == pDestinationParam->NumLayers ) {
        MaterialLayer *pSourceLayer = pSourceParam->getLayer( 0 );
        MaterialLayer *pDestinationLayer = pDestinationParam->getLayer( 0 );
        if ( pSourceLayer != nullptr && pDestinationLayer != nullptr &&
             pSourceLayer->NumTextures == pDestinationLayer->NumTextures ) {
            for ( uint32_t i = 0; i < pDestinationLayer->NumTextures; i++ ) {
                MaterialLayerTexture &destination =
                    pDestinationLayer->pLayerTextures[i];
                const MaterialLayerTexture &source =
                    pSourceLayer->pLayerTextures[i];

                destination.Hashcode = source.Hashcode;
                destination.pTextureSection = source.pTextureSection;
            }
        }
    }

    if ( pSourceParam->Type != MPT_Generic ||
         pDestinationParam->Type != MPT_Generic ) {
        return;
    }

    MaterialShaderParameterArray *pSourceParams =
        static_cast<MaterialShaderParameterArray *>( pSourceParam );
    MaterialShaderParameterArray *pDestinationParams =
        static_cast<MaterialShaderParameterArray *>( pDestinationParam );
    if ( pSourceParams->NumParameters != pDestinationParams->NumParameters ) {
        return;
    }

    for ( uint32_t i = 0; i < pDestinationParams->NumParameters; i++ ) {
        MaterialShaderParameter *pSourceDesc =
            pSourceParams->getParameterByIndex( i );
        MaterialShaderParameter *pDestinationDesc =
            pDestinationParams->getParameterByIndex( i );
        if ( pSourceDesc == nullptr || pDestinationDesc == nullptr ||
             pSourceDesc->Type != pDestinationDesc->Type ) {
            continue;
        }

        void *pDestinationData =
            pDestinationParams->getParameterDataByIndex( i );
        const void *pSourceData = pSourceParams->getParameterDataByIndex( i );
        if ( pDestinationData == nullptr || pSourceData == nullptr ) {
            continue;
        }

        memcpy( pDestinationData, pSourceData, pSourceDesc->Size );
    }
}

// TODO: Move to material.cpp?
static void SetMaterialStaticParam( Material *pMaterial,
                                    const uint32_t type,
                                    const float *pValue ) {
    // FUN_0040fc00
    MaterialParameter *pParameter = pMaterial->getParameterByIndex( 0 );
    if ( pParameter == nullptr || pParameter->Type != MPT_Generic ) {
        return;
    }

    MaterialShaderParameterArray *pParameters =
        static_cast<MaterialShaderParameterArray *>( pParameter );

    uint32_t parameterIndex = 0;
    if ( pParameters->getParameter( type, &parameterIndex ) == nullptr ) {
        return;
    }

    float *pData =
        (float *)pParameters->getParameterDataByIndex( parameterIndex );
    if ( pData == nullptr ) {
        return;
    }

    for ( int32_t i = 0; i < 4; i++ ) {
        pData[i] = pValue[i];
    }
}

static void ApplySkinTintToMaterial( Material *pMaterial,
                                     const float *pSkinTint,
                                     RenderAvatar *pBodyHighRes ) {
    // FUN_008ee610
    MaterialParameter *pParameter = pMaterial->getParameterByIndex( 0 );
    if ( pParameter == nullptr ||
         pParameter->Hashcode != kOpaqueClotheShaderHashcode ) {
        return;
    }

    if ( pBodyHighRes != nullptr &&
         pBodyHighRes->Material.getNumMaterials() != 0 ) {
        Material *pBodyMaterial =
            pBodyHighRes->Material.getMaterialAtIndex( 0 );
        if ( pBodyMaterial != nullptr ) {
            ApplyColorMaterial( pMaterial, pBodyMaterial );
        }

        MaterialParameter *pBodyParameter =
            ( pBodyMaterial != nullptr )
                ? pBodyMaterial->getParameterByIndex( 0 )
                : nullptr;
        if ( pBodyParameter != nullptr &&
             pBodyParameter->Type == MPT_Generic ) {
            MaterialShaderParameterArray *pBodyParameters =
                static_cast<MaterialShaderParameterArray *>( pBodyParameter );

            const float *pDarkTint = nullptr;
            const float *pLightTint = nullptr;

            uint32_t parameterIndex = 0;
            if ( pBodyParameters->getParameter( kStaticParamSkinDark,
                                                &parameterIndex ) != nullptr ) {
                pDarkTint =
                    (const float *)pBodyParameters->getParameterDataByIndex(
                        parameterIndex );
            }

            parameterIndex = 0;
            if ( pBodyParameters->getParameter( kStaticParamSkinLight,
                                                &parameterIndex ) != nullptr ) {
                pLightTint =
                    (const float *)pBodyParameters->getParameterDataByIndex(
                        parameterIndex );
            }

            if ( pDarkTint != nullptr && pLightTint != nullptr &&
                 pSkinTint != nullptr ) {
                const float blendFactor = pSkinTint[1];

                float blendedTint[4];
                for ( int32_t i = 0; i < 4; i++ ) {
                    blendedTint[i] = pLightTint[i] * blendFactor +
                                     pDarkTint[i] * ( 1.0f - blendFactor );
                }

                SetMaterialStaticParam( pMaterial, kStaticParamSkinBlend,
                                        blendedTint );
            }
        }
    }

    if ( pSkinTint != nullptr ) {
        SetMaterialStaticParam( pMaterial, kStaticParamSkinTint, pSkinTint );
    }
}

static void ApplyEyeUVOffsetToMaterial( Material *pMaterial,
                                        const AvatarEyePart *pEyeResource,
                                        const uint64_t meshHashcode ) {
    // FUN_008f2100
    MaterialParameter *pParameter = pMaterial->getParameterByIndex( 0 );
    if ( pParameter == nullptr || pParameter->Hashcode != kEyeShaderHashcode ||
         meshHashcode != gHashcodeEyes || pEyeResource == nullptr ) {
        return;
    }

    SetMaterialStaticParam( pMaterial, kStaticParamEyeUVOffset,
                            pEyeResource->UVOffset );
}

static void
ApplyLightAttenuationToMaterial( Material *pMaterial,
                                 const AvatarLightAttenuation &attenuation ) {
    // FUN_008f0d20
    MaterialParameter *pParameter = pMaterial->getParameterByIndex( 0 );
    if ( pParameter == nullptr || pParameter->Type != MPT_Generic ) {
        return;
    }

    bool bTakesAttenuation = false;
    for ( const uint64_t hashcode : kLightAttenuationShaderHashcodes ) {
        if ( pParameter->Hashcode == hashcode ) {
            bTakesAttenuation = true;
            break;
        }
    }

    if ( !bTakesAttenuation ) {
        bTakesAttenuation =
            pParameter->Hashcode == kReflectiveClotheShaderHashcode &&
            pParameter->Flags.pCustomFlags[3] != 0;
    }

    if ( !bTakesAttenuation ) {
        return;
    }

    const float heightRange[4] = { attenuation.HeightBase,
                                   attenuation.HeightEnd, 0.0f, 1.0f };
    SetMaterialStaticParam( pMaterial, kStaticParamLightHeightRange,
                            heightRange );

    const float factors[4] = { attenuation.Ambiant, attenuation.Diffuse,
                               attenuation.Specular, attenuation.EnvFactor };
    SetMaterialStaticParam( pMaterial, kStaticParamLightAttenuation, factors );
}

static bool RebindEnvMapTexture( MaterialParameter *pParameter,
                                 const uint64_t cubemapHashcode,
                                 RenderFile::Section *pCubemapSection ) {
    // FUN_00900350
    if ( pParameter->NumLayers == 0 ) {
        return false;
    }

    MaterialLayer *pLayer = pParameter->getLayer( 0 );
    for ( uint32_t i = 0; i < pLayer->NumTextures; i++ ) {
        MaterialLayerTexture &texture = pLayer->pLayerTextures[i];
        if ( texture.Hashcode == cubemapHashcode ) {
            texture.pTextureSection = pCubemapSection;
            return true;
        }
    }

    return false;
}

static void ApplyEnvMapToMaterial( Material *pMaterial,
                                   const AvatarEnvMap *pEnvMap ) {
    // FUN_009003c0
    const uint64_t cubemapHashcode = GetIdentifier64bit( "CUBEMAP" );

    Render2DB *pCubemap = ( gpCarCommon != nullptr )
                              ? gpCarCommon->getRender2DB( cubemapHashcode )
                              : nullptr;
    if ( gpMngEnvMap != nullptr ) {
        if ( pEnvMap->bRealTime ) {
            pCubemap =
                &gpMngResources->getCubemapResources().getCubemap().Render2DB;
        } else if ( pEnvMap->p2DB != nullptr ) {
            pCubemap = pEnvMap->p2DB;
        }
    }

    if ( pCubemap == nullptr ) {
        return;
    }

    RenderFile::Section *pCubemapSection = pCubemap->getBitmapSection();
    if ( pCubemapSection == nullptr || pMaterial->NumParams == 0 ) {
        return;
    }

    MaterialParameter *pParameter = pMaterial->getParameterByIndex( 0 );
    if ( pParameter->Type != MPT_Generic ) {
        return;
    }

    bool bHasReflectionShader = false;
    for ( const uint64_t hashcode : kEnvMapShaderHashcodes ) {
        if ( pParameter->Hashcode == hashcode ) {
            bHasReflectionShader = true;
            break;
        }
    }

    if ( !bHasReflectionShader || !pParameter->Flags.bUsingReflection ) {
        return;
    }

    if ( !RebindEnvMapTexture( pParameter, cubemapHashcode,
                               pCubemapSection ) ) {
        return;
    }

    pParameter->Flags.pCustomFlags[2] = pEnvMap->bRealTime;
    gpRenderContext->retrieveShadersForMaterial( pMaterial, 0 );
}

// TODO: Move to material.cpp?
static void BindBitmapToMaterial( Material *pMaterial, Render2DB *pBitmap ) {
    // FUN_00410e50
    if ( pMaterial == nullptr || pBitmap == nullptr ||
         pBitmap->getHeader() == nullptr ||
         pBitmap->getBitmapSection() == nullptr ) {
        return;
    }

    MaterialParameter *pParameter = pMaterial->getParameterByIndex( 0 );
    if ( pParameter == nullptr || pParameter->NumLayers == 0 ) {
        return;
    }

    MaterialLayer *pLayer = pParameter->getLayer( 0 );
    if ( pLayer == nullptr ) {
        return;
    }

    const uint64_t bitmapHashcode = pBitmap->getBitmapHashcode();
    for ( uint32_t i = 0; i < pLayer->NumTextures; i++ ) {
        if ( pLayer->pLayerTextures[i].Hashcode == bitmapHashcode ) {
            pLayer->pLayerTextures[i].pTextureSection =
                pBitmap->getBitmapSection();
        }
    }
}

static bool ShouldStepClothe( const AvatarClothe &clothe,
                              const eAvatarLoadingPass pass ) {
    switch ( pass ) {
    case ALP_NotUnloaded:
        return clothe.StreamingRequest.State != AVT_Unloaded;
    case ALP_Synchronous:
        return !clothe.StreamingRequest.bAsync;
    case ALP_AsyncCritical:
        return clothe.StreamingRequest.bAsync &&
               clothe.StreamingRequest.bCritical;
    case ALP_Async:
        return clothe.StreamingRequest.bAsync &&
               !clothe.StreamingRequest.bCritical;
    }

    return false;
}

void AvatarScratchPool::allocate( int32_t size ) {
    // FUN_008f2200 (inlined)
    pMemory = static_cast<uint8_t *>( TestDrive::AllocAligned( size, 0x10 ) );
    if ( pMemory == nullptr ) {
        return;
    }

    pCursor = pMemory;
    Size = size;
}

void AvatarScratchPool::reset() {
    // FUN_008f2320 (inlined)
    pCursor = pMemory;
}

void *AvatarScratchPool::allocateCopy( const uint32_t size,
                                       const void *pSource ) {
    // FUN_008f13d0
    if ( pMemory == nullptr || pCursor == nullptr || pSource == nullptr ) {
        return nullptr;
    }

    const int32_t available = Size - static_cast<int32_t>( pCursor - pMemory );
    if ( static_cast<int32_t>( size ) >= available ) {
        return nullptr;
    }

    uint8_t *pAllocation = pCursor;

    pCursor += size + ( 0x10 - ( size % 0x10 ) );

    memcpy( pAllocation, pSource, size );
    return pAllocation;
}

void AvatarScratchPool::release() {
    // FUN_00410c10
    if ( pMemory != nullptr ) {
        TestDrive::FreeAligned( pMemory );
    }

    pMemory = nullptr;
    pCursor = nullptr;
    Size = 0;
}

void AvatarGeometryInstance::parseOwnedStream() {
    // FUN_008f11a0
    if ( RefCount == 0 && pStream != nullptr && Kind == AIK_Owned ) {
        Owned.initialize( pStream );
    }
}

void AvatarGeometryInstance::release() {
    // FUN_008f1250
    if ( Kind == AIK_Owned && Owned.getHeader() != nullptr ) {
        Owned.destroy();
        pStream = nullptr;
    }

    pReferenced = nullptr;
}

bool AvatarGeometryInstance::reference( AvatarBnk *pBnk,
                                        const uint64_t hashcode ) {
    // FUN_008f1280
    pReferenced = pBnk->find3DG( hashcode );
    return pReferenced != nullptr;
}

bool AvatarGeometryInstance::copyFromBank( AvatarBnk *pBnk,
                                           const uint64_t hashcode,
                                           AvatarScratchPool &allocator ) {
    // FUN_008f12b0
    void *pBankStream = pBnk->findGeometryStream( hashcode );
    if ( pBankStream != nullptr ) {
        const uint32_t streamSize =
            static_cast<RenderFile::Header *>( pBankStream )->Size;
        pStream = allocator.allocateCopy( streamSize, pBankStream );
    }

    return pStream != nullptr;
}

void AvatarBitmapInstance::parseOwnedStream() {
    // FUN_008f1440
    if ( RefCount == 0 && pStream != nullptr && Kind == AIK_Owned ) {
        Owned.initialize( pStream );
    }
}

void AvatarBitmapInstance::release() {
    // FUN_008f14d0
    if ( Kind == AIK_Owned && Owned.getHeader() != nullptr ) {
        Owned.destroy();
        pStream = nullptr;
    }

    pReferenced = nullptr;
}

bool AvatarBitmapInstance::reference( AvatarBnk *pBnk,
                                      const uint64_t hashcode ) {
    // FUN_008f14a0
    pReferenced = pBnk->find2DB( hashcode );
    return pReferenced != nullptr;
}

bool AvatarBitmapInstance::copyFromBank( AvatarBnk *pBnk,
                                         const uint64_t hashcode,
                                         AvatarScratchPool &allocator ) {
    // FUN_008f1560
    void *pBankStream = pBnk->findBitmapStream( hashcode );
    if ( pBankStream != nullptr ) {
        const uint32_t streamSize =
            static_cast<RenderFile::Header *>( pBankStream )->Size;
        pStream = allocator.allocateCopy( streamSize, pBankStream );
    }

    return pStream != nullptr;
}

void RenderAvatarBase::initialize( int32_t index,
                                   FileCollection3D *pCollection,
                                   LightSetupNode *pLightSetup,
                                   ShaderParameterSetupNode *pShaderSetup ) {
    // FUN_008f0760
    pMemoryPool = TestDrive::AllocAligned( kAvatarPartPoolSize, 0x10 );
    if ( pMemoryPool == nullptr ) {
        OTDU_LOG_ERROR( "RenderAvatarBase pool allocation failed "
                        "(pMemoryPool is null)!\n" );
        return;
    }

    p2DMPool = static_cast<uint8_t *>( pMemoryPool );
    p3DDPool = p2DMPool + kAvatarPart2DMPoolSize;

    p3DManager = pCollection;
    Index = index;
    pLightConfig = pLightSetup;
    pShaderConfig = pShaderSetup;
}

void RenderAvatarBase::terminate() {
    // FUN_008f0710
    pShaderConfig = nullptr;
    pLightConfig = nullptr;

    if ( pMemoryPool != nullptr ) {
        TestDrive::FreeAligned( pMemoryPool );
        pMemoryPool = nullptr;
        p2DMPool = nullptr;
        p3DDPool = nullptr;
    }

    p3DManager = nullptr;
    pLowRes = nullptr;
}

bool RenderAvatarBase::installRenderData( AvatarDBEntry *pPart,
                                          AvatarBnk *pBnk ) {
    // FUN_008f0a50
    const char *pMeshName =
        ( pPart != nullptr ) ? pPart->getMeshName() : nullptr;
    if ( pMeshName != nullptr ) {
        if ( IsEmptyPartName( pMeshName ) ) {
            return true;
        }

        if ( pBnk != nullptr ) {
            p2DMData = CopyStreamToPool( p2DMPool, kAvatarPart2DMPoolSize,
                                         pBnk->get2DM( pMeshName ) );
            if ( p2DMData != nullptr ) {
                Material.initialize( p2DMData );
            }

            p3DDData = CopyStreamToPool( p3DDPool, kAvatarPart3DDPoolSize,
                                         pBnk->get3DD( pMeshName ) );
        }
    }

    if ( p2DMData != nullptr && p3DDData != nullptr ) {
        return true;
    }

    if ( Material.getHeader() != nullptr ) {
        Material.destroy();
    }

    p2DMData = nullptr;
    p3DDData = nullptr;
    return false;
}

void RenderAvatarBase::releaseRenderData() {
    if ( Material.getHeader() != nullptr ) {
        Material.destroy();
        p2DMData = nullptr;
    }

    p3DDData = nullptr;
}

bool RenderAvatarBase::installPart( AvatarDBEntry *, AvatarBnk * ) {
    return false;
}

void RenderAvatarBase::releaseCurrentPart() { releaseRenderData(); }

void RenderAvatarBase::parseRenderData() {
    // FUN_008f0cd0
    if ( p3DDData != nullptr ) {
        Scene.initialize( p3DDData );
    }

    if ( Scene.getHeader() != nullptr ) {
        SetRootInstanceLODRadius( &Scene, kAvatarPartLODRadius );
    }
}

void RenderAvatarBase::destroyRenderData() {
    // FUN_008f0990
    if ( Scene.getHeader() != nullptr ) {
        Scene.destroy();
    }
}

void RenderAvatarBase::registerRenderData() {
    // FUN_008f0960
    if ( Scene.getHeader() != nullptr ) {
        p3DManager->register3DD( &Scene );
        Scene.bindMaterialsBank( &Material );
    }
}

void RenderAvatarBase::unregisterRenderData() {
    // FUN_008f0940
    if ( Scene.getHeader() != nullptr ) {
        p3DManager->unregister3DD( &Scene );
    }
}

HiearchyNode *RenderAvatarBase::instanciatePartHierarchy() {
    // FUN_008f0f80 (inlined)
    if ( Scene.getHeader() == nullptr || pAvatarPart == nullptr ) {
        return nullptr;
    }

    return p3DManager->instantiate(
        GetIdentifier64bit( pAvatarPart->getMeshName() ) );
}

void RenderAvatarBase::instanciateHierarchy() {
    // FUN_008f0f80
    parseRenderData();

    if ( Material.getHeader() != nullptr ) {
        bindPartTextures();
    }

    registerRenderData();

    pHierarchyNode = instanciatePartHierarchy();
    if ( pHierarchyNode != nullptr ) {
        if ( pLightConfig != nullptr ) {
            AddSetupToHierarchy( pHierarchyNode, pLightConfig );
        }
        if ( pShaderConfig != nullptr ) {
            AddSetupToHierarchy( pHierarchyNode, pShaderConfig );
        }
    }

    if ( pLowRes != nullptr ) {
        pHierarchyNodeLowRes = instanciatePartHierarchy();
        if ( pHierarchyNodeLowRes != nullptr && bVisible ) {
            showLowResHierarchy();
        }
    }
}

void RenderAvatarBase::releaseHierarchyNodes() {
    // FUN_008f0c50
    if ( pHierarchyNode != nullptr ) {
        if ( pLightConfig != nullptr ) {
            RemoveSetupFromHierarchy( pHierarchyNode, pLightConfig );
        }
        if ( pShaderConfig != nullptr ) {
            RemoveSetupFromHierarchy( pHierarchyNode, pShaderConfig );
        }

        p3DManager->uninstantiate( pHierarchyNode );
        pHierarchyNode = nullptr;
    }

    if ( pLowRes != nullptr && pHierarchyNodeLowRes != nullptr ) {
        if ( bVisible ) {
            hideLowResHierarchy();
        }

        p3DManager->uninstantiate( pHierarchyNodeLowRes );
        pHierarchyNodeLowRes = nullptr;
    }
}

void RenderAvatarBase::releaseHierarchy() {
    // FUN_008f0d00
    releaseHierarchyNodes();
    unregisterRenderData();
    destroyRenderData();

    pAvatarPart = nullptr;
}

void RenderAvatarBase::showLowResHierarchy() {
    // FUN_008f1720
    if ( pHierarchyNodeLowRes == nullptr ) {
        return;
    }

    pHierarchyNodeLowRes->Flags |= 0x30;
    SetHierarchyColor( pHierarchyNodeLowRes, gColorWhite );

    Instance *pInstance = pHierarchyNodeLowRes->pInstance;
    Matrix4x4f_t identityMatrix = Matrix4x4f_t::Identity(); // DAT_00fac2a0
    pInstance->setModelMatrix( identityMatrix );

    AvatarDBEntry *pLowResPart = pLowRes->getAvatarPart();
    if ( pLowResPart != nullptr ) {
        Matrix4x4f_t *pPalette = nullptr;
        if ( pLowResPart->Gender == 0 ) {
            pPalette = gpAvatar->getIdentityBonePalette( 1 );
        } else if ( pLowResPart->Gender == 1 ) {
            pPalette = gpAvatar->getIdentityBonePalette( 0 );
        }

        pInstance->setBones( pPalette );
    }

    if ( pLowRes->Index < kNumAvtScenes ) {
        RenderScene *pFrontScene =
            gpRender->getRenderPassAtIndex( RP_AvtLRFront0 + pLowRes->Index )
                .pScene;
        if ( pFrontScene != nullptr ) {
            pFrontScene->enqueueStaticHiearchy( pHierarchyNodeLowRes );
        }

        RenderScene *pBackScene =
            gpRender->getRenderPassAtIndex( RP_AvtLRBack0 + pLowRes->Index )
                .pScene;
        if ( pBackScene != nullptr ) {
            pBackScene->enqueueStaticHiearchy( pHierarchyNodeLowRes );
        }
    }

    pLowRes->bDirty = 2;
}

void RenderAvatarBase::hideLowResHierarchy() {
    // FUN_008f1610
    if ( pHierarchyNodeLowRes == nullptr ) {
        return;
    }

    if ( pLowRes->Index < kNumAvtScenes ) {
        RenderScene *pFrontScene =
            gpRender->getRenderPassAtIndex( RP_AvtLRFront0 + pLowRes->Index )
                .pScene;
        if ( pFrontScene != nullptr ) {
            pFrontScene->removeStaticHiearchy( pHierarchyNodeLowRes );
        }

        RenderScene *pBackScene =
            gpRender->getRenderPassAtIndex( RP_AvtLRBack0 + pLowRes->Index )
                .pScene;
        if ( pBackScene != nullptr ) {
            pBackScene->removeStaticHiearchy( pHierarchyNodeLowRes );
        }
    }

    pLowRes->bDirty = 2;
}

void RenderAvatarBase::setLowResVisible( const bool bNewVisible ) {
    // FUN_008f0eb0
    if ( pHierarchyNodeLowRes != nullptr && pLowRes != nullptr &&
         bVisible != bNewVisible ) {
        if ( bNewVisible ) {
            showLowResHierarchy();
        } else {
            hideLowResHierarchy();
        }
    }

    bVisible = bNewVisible;
}

void RenderAvatarBase::setMatrix( const Matrix4x4f_t &matrix ) {
    // FUN_008f1120
    if ( pHierarchyNode == nullptr ) {
        return;
    }

    pHierarchyNode->TransformMatrix = matrix;

    if ( pHierarchyNode->pParent == nullptr ) {
        pHierarchyNode->pInstance->setModelMatrix(
            pHierarchyNode->TransformMatrix );
    } else {
        Matrix4x4f_t worldMatrix =
            pHierarchyNode->TransformMatrix *
            pHierarchyNode->pParent->pInstance->getModelMatrix();
        pHierarchyNode->pInstance->setModelMatrix( worldMatrix );
    }

    pHierarchyNode->pInstance->calculateBiggestScale();
    pHierarchyNode->pInstance->calculateDeterminant();
    pHierarchyNode->propagateTransformMatrices();
}

void RenderAvatarBase::setMeshVisibility( const uint64_t meshHashcode,
                                          const bool bMeshVisible ) {
    // FUN_00411590
    if ( Material.getHeader() == nullptr ) {
        return;
    }

    Render2DMHashEntry *pHashcodes = Material.getHashTableHead();
    if ( pHashcodes == nullptr ) {
        return;
    }

    const uint32_t numMaterials = Material.getNumMaterials();
    for ( uint32_t i = 0; i < numMaterials; i++ ) {
        ::Material *pMaterial = Material.getMaterialAtIndex( i );
        if ( pMaterial == nullptr || pHashcodes[i].Hashcode != meshHashcode ) {
            continue;
        }

        if ( bMeshVisible ) {
            pMaterial->FXFlags &= ~kMaterialFXHidden;
        } else {
            pMaterial->FXFlags |= kMaterialFXHidden;
        }
    }
}

AvatarDBEntry *AvatarLOD::getAvatarPart() {
    // FUN_008f2ed0
    return ( pAvatarPartInLoading != nullptr ) ? pAvatarPartInLoading
                                               : pAvatarPart;
}

bool AvatarLOD::installPart( AvatarDBEntry *pPart, AvatarBnk *pBnk ) {
    // FUN_008f3100
    if ( pPart == nullptr || pBnk == nullptr ||
         !installRenderData( pPart, pBnk ) ) {
        return false;
    }

    const char *pMeshName = pPart->getMeshName();
    if ( pMeshName != nullptr ) {
        p3DG = pBnk->find3DG( AvatarCalcHashcode( pMeshName ) );
    }

    return p3DG != nullptr;
}

void AvatarLOD::releaseCurrentPart() {
    // FUN_008f3000
    p3DG = nullptr;

    releaseRenderData();
}

void AvatarLOD::registerRenderData() {
    // FUN_008f2fc0
    if ( p3DG != nullptr ) {
        p3DManager->register3DG( p3DG );
    }

    RenderAvatarBase::registerRenderData();
}

void AvatarLOD::unregisterRenderData() {
    // FUN_008f2f80
    RenderAvatarBase::unregisterRenderData();

    if ( p3DG != nullptr ) {
        p3DManager->unregister3DG( p3DG );
    }
}

void AvatarLOD::stepLoading() {
    // FUN_008f3030
    releaseCurrentPart();

    AvatarBnk &commonBodies = gpAvatar->getBnkManager().getCommonBodies();
    if ( commonBodies.isInstallOK() &&
         installPart( pAvatarPartInLoading, &commonBodies ) ) {
        pAvatarPart = pAvatarPartInLoading;
        pAvatarPartInLoading = nullptr;
        LoadingState = AVT_LoadCompleted;
        return;
    }

    pAvatarPart = nullptr;
    pAvatarPartInLoading = nullptr;
    LoadingState = AVT_Unknown;
}

void AvatarLOD::requestPart( AvatarDBEntry *pPart ) {
    // FUN_008f3180
    decreaseReferenceCount();

    pAvatarPartInLoading = pPart;
    LoadingState = AVT_LoadPending;

    stepLoading();
}

void AvatarLOD::decreaseReferenceCount() {
    // FUN_008f2ef0
    if ( LoadingState == AVT_Ready ) {
        releaseHierarchy();
        pAvatarPart = nullptr;
        LoadingState = AVT_LoadCompleted;
    }
    if ( LoadingState == AVT_LoadCompleted ) {
        releaseCurrentPart();
        LoadingState = AVT_LoadPending;
    }
    if ( LoadingState == AVT_LoadPending ) {
        pAvatarPartInLoading = nullptr;
        LoadingState = AVT_Unloaded;
    }
    if ( LoadingState == AVT_Unloaded ) {
        LoadingState = AVT_Unknown;
    }
}

bool RenderAvatar::installPart( AvatarDBEntry *pPart, AvatarBnk *pBnk ) {
    // FUN_008f1ec0
    if ( pPart == nullptr || !AvatarLOD::installPart( pPart, pBnk ) ) {
        return false;
    }

    const char *pMeshName = pPart->getMeshName();
    if ( pMeshName != nullptr ) {
        void *pEntry = pBnk->getSkeleton( pMeshName );
        pSkeletonData =
            ( pEntry != nullptr )
                ? static_cast<uint8_t *>( pEntry ) + kAvatarSkeletonDataOffset
                : nullptr;
    }

    if ( Material.getHeader() == nullptr ) {
        return true;
    }

    const uint32_t numMaterials = Material.getNumMaterials();
    for ( uint32_t i = 0; i < numMaterials; i++ ) {
        ::Material *pMaterial = Material.getMaterialAtIndex( i );
        if ( pMaterial == nullptr ) {
            continue;
        }

        MaterialParameter *pParameter = pMaterial->getParameterByIndex( 0 );
        if ( pParameter == nullptr || pParameter->NumLayers == 0 ) {
            continue;
        }

        MaterialLayer *pLayer = pParameter->getLayer( 0 );
        if ( pLayer == nullptr ) {
            continue;
        }

        for ( uint32_t j = 0; j < pLayer->NumTextures; j++ ) {
            const uint64_t textureHashcode = pLayer->pLayerTextures[j].Hashcode;
            if ( textureHashcode == 0 ) {
                continue;
            }

            Render2DB *pBitmap = pBnk->find2DB( textureHashcode );
            if ( pBitmap != nullptr ) {
                pLayer->pLayerTextures[j].pTextureSection =
                    pBitmap->getBitmapSection();
            }
        }
    }

    return true;
}

void RenderAvatar::bindPartTextures() {
    // FUN_008f1d20
    // NOP
}

void RenderAvatar::releaseCurrentPart() {
    // FUN_008f1d30
    pSkeletonData = nullptr;
    p3DG = nullptr;

    releaseRenderData();
}

static constexpr uint32_t kImpostorWidth = 0x80;
static constexpr uint32_t kImpostorHeight = 0x100;
static constexpr uint32_t kImpostorBitmapFlags = 0x210;

static void UploadImpostorBitmap( Render2DB &bitmap ) {
    Texture *pTexture = bitmap.getFirstBitmap();
    if ( pTexture != nullptr && pTexture->pTexture == nullptr &&
         Render2DB::CreateAndUploadTexture( pTexture ) ) {
        Render2DB::PrepareTexture( pTexture );
    }
}

static void BindImpostorRenderTarget( const uint32_t firstPass,
                                      const int32_t index,
                                      RenderTarget *pRenderTarget ) {
    if ( index >= kNumAvtScenes ) {
        return;
    }

    RenderScene *pScene =
        gpRender->getRenderPassAtIndex( firstPass + index ).pScene;
    if ( pScene != nullptr && pScene->getFramebuffer() != nullptr ) {
        pScene->getFramebuffer()->pAttachments[0] = pRenderTarget;
    }
}

static void BindBitmapToMaterialSlot( Material *pMaterial,
                                      const uint32_t slotIndex,
                                      const Render2DB &bitmap ) {
    // FUN_00410db0
    if ( pMaterial == nullptr ) {
        return;
    }

    MaterialParameter *pParameter = pMaterial->getParameterByIndex( 0 );
    if ( pParameter == nullptr || pParameter->NumLayers == 0 ) {
        return;
    }

    MaterialLayerTexture &texture =
        pParameter->getLayer( 0 )->pLayerTextures[slotIndex];
    if ( texture.Hashcode == 0ull || bitmap.getBitmapSection() == nullptr ) {
        return;
    }

    texture.Hashcode = bitmap.getBitmapHashcode();
    texture.pTextureSection = bitmap.getBitmapSection();
}

void AvatarImpostor::initialize( int32_t index,
                                 FileCollection3D *pCollection,
                                 LightSetupNode *pLightSetup,
                                 ShaderParameterSetupNode *pShaderSetup ) {
    // FUN_008f1920
    RenderAvatarBase::initialize( index, pCollection, pLightSetup,
                                  pShaderSetup );

    pFrontBuffer = TestDrive::AllocAligned(
        Render2DB::CalcSize( kImpostorWidth, kImpostorHeight, 1, 1, VF_X8R8G8B8,
                             kImpostorBitmapFlags ),
        0x10 );
    if ( pFrontBuffer == nullptr ) {
        return;
    }

    Front.create( pFrontBuffer, kImpostorWidth, kImpostorHeight, 1, 1,
                  VF_X8R8G8B8, kImpostorBitmapFlags, "AVT_FRT" );
    Front.initialize( pFrontBuffer );
    UploadImpostorBitmap( Front );

    pBackBuffer = TestDrive::AllocAligned(
        Render2DB::CalcSize( kImpostorWidth, kImpostorHeight, 1, 1, VF_X8R8G8B8,
                             kImpostorBitmapFlags ),
        0x10 );
    if ( pBackBuffer == nullptr ) {
        return;
    }

    Back.create( pBackBuffer, kImpostorWidth, kImpostorHeight, 1, 1,
                 VF_X8R8G8B8, kImpostorBitmapFlags, "AVT_BCK" );
    Back.initialize( pBackBuffer );
    UploadImpostorBitmap( Back );

    pRenderTargetFront = CreateRenderTargetFrom2DB( &Front, 0x20 );
    BindImpostorRenderTarget( RP_AvtLRFront0, Index, pRenderTargetFront );

    pRenderTargetBack = CreateRenderTargetFrom2DB( &Back, 0x20 );
    BindImpostorRenderTarget( RP_AvtLRBack0, Index, pRenderTargetBack );
}

void AvatarImpostor::terminate() {
    // FUN_008f1850
    if ( Index < kNumAvtScenes ) {
        RenderScene *pFrontScene =
            gpRender->getRenderPassAtIndex( RP_AvtLRFront0 + Index ).pScene;
        if ( pFrontScene != nullptr ) {
            pFrontScene->setEnabled( false );
        }

        RenderScene *pBackScene =
            gpRender->getRenderPassAtIndex( RP_AvtLRBack0 + Index ).pScene;
        if ( pBackScene != nullptr ) {
            pBackScene->setEnabled( false );
        }
    }

    if ( pRenderTargetBack != nullptr ) {
        ReleasePooledRenderTarget( pRenderTargetBack );
        pRenderTargetBack = nullptr;
    }

    if ( pRenderTargetFront != nullptr ) {
        ReleasePooledRenderTarget( pRenderTargetFront );
        pRenderTargetFront = nullptr;
    }

    Back.destroy();
    Front.destroy();

    if ( pFrontBuffer != nullptr ) {
        TestDrive::FreeAligned( pFrontBuffer );
    }

    if ( pBackBuffer != nullptr ) {
        TestDrive::FreeAligned( pBackBuffer );
    }

    pFrontBuffer = nullptr;
    pBackBuffer = nullptr;

    RenderAvatarBase::terminate();
}

void AvatarImpostor::bindPartTextures() {
    // FUN_008f1b30
    static const uint64_t kBackHashcode = AvatarCalcHashcode( "BACK" );
    static const uint64_t kFrontHashcode = AvatarCalcHashcode( "FRONT" );

    const uint32_t numMaterials = Material.getNumMaterials();
    for ( uint32_t i = 0; i < numMaterials; i++ ) {
        const uint64_t materialHashcode = Material.getMaterialHashcode( i );
        if ( materialHashcode == kBackHashcode ) {
            if ( Back.getHeader() != nullptr ) {
                BindBitmapToMaterialSlot( Material.getMaterialAtIndex( i ), 0,
                                          Back );
            }
        } else if ( materialHashcode == kFrontHashcode ) {
            if ( Front.getHeader() != nullptr ) {
                BindBitmapToMaterialSlot( Material.getMaterialAtIndex( i ), 0,
                                          Front );
            }
        }
    }
}

void AvatarImpostor::updateSceneVisibility() {
    // FUN_008f1670
    const bool bSceneEnabled = bDirty != 0 && pRenderTargetFront != nullptr &&
                               pRenderTargetBack != nullptr;

    if ( Index < kNumAvtScenes ) {
        RenderScene *pFrontScene =
            gpRender->getRenderPassAtIndex( RP_AvtLRFront0 + Index ).pScene;
        if ( pFrontScene != nullptr ) {
            pFrontScene->setEnabled( bSceneEnabled );
        }

        RenderScene *pBackScene =
            gpRender->getRenderPassAtIndex( RP_AvtLRBack0 + Index ).pScene;
        if ( pBackScene != nullptr ) {
            pBackScene->setEnabled( bSceneEnabled );
        }
    }

    if ( bDirty != 0 ) {
        bDirty--;
    }
}

void AvatarClothe::initialize( int32_t index,
                               FileCollection3D *pCollection,
                               int32_t geometryPoolSize,
                               int32_t texturePoolSize,
                               LightSetupNode *pLightSetup,
                               ShaderParameterSetupNode *pShaderSetup ) {
    // FUN_008f2200
    RenderAvatarBase::initialize( index, pCollection, pLightSetup,
                                  pShaderSetup );

    GeometryPool.Allocator.allocate( geometryPoolSize );
    TexturePool.Allocator.allocate( texturePoolSize );
}

void AvatarClothe::terminate() {
    // FUN_008f2520
    decreaseReferenceCount();
    StreamingRequest.decreaseReferenceCount();

    GeometryPool.Allocator.release();
    TexturePool.Allocator.release();

    RenderAvatarBase::terminate();
}

bool AvatarClothe::installPart( AvatarDBEntry *pPart, AvatarBnk *pBnk ) {
    // FUN_008f25c0
    if ( pPart == nullptr ) {
        return false;
    }

    const bool bInstalled = installRenderData( pPart, pBnk );
    if ( bInstalled ) {
        applyColorSet( pPart, pBnk );
    }

    AvatarBnk &commonClothes = gpAvatar->getBnkManager().getCommonClothes();
    AvatarBnk *pCommonBnk =
        commonClothes.isInstallOK() ? &commonClothes : nullptr;

    if ( !bInstalled ) {
        return false;
    }

    const char *pMeshName = pPart->getMeshName();

    if ( pMeshName != nullptr && strcmp( pMeshName, "NO_MESH" ) != 0 ) {
        const uint64_t meshHashcode = pPart->getMeshHashcode();
        if ( meshHashcode != 0 ) {
            AvatarGeometryInstance *pInstance =
                GeometryPool.claim( pBnk, meshHashcode );
            if ( pInstance == nullptr ) {
                pInstance = GeometryPool.claim( pCommonBnk, meshHashcode );
            }

            pInstances[NumInstances] = pInstance;
            if ( pInstance == nullptr ) {
                releaseRenderData();
                return false;
            }

            NumInstances++;
        }
    }

    if ( Material.getHeader() != nullptr ) {
        claimLayerTextures( pBnk, pCommonBnk );
    }

    return true;
}

void AvatarClothe::applyColorSet( AvatarDBEntry *pPart, AvatarBnk *pBnk ) {
    // FUN_008f25c0 (first half)
    NumColors = 0;

    const char *pMeshName = pPart->getMeshName();
    if ( pMeshName == nullptr || pBnk == nullptr ) {
        return;
    }

    std::string colorSetName = pMeshName;
    colorSetName += kColorSetSuffix;

    Render2DM *pColorSet =
        pBnk->getMaterial( AvatarCalcHashcode( colorSetName.c_str() ) );
    if ( pColorSet == nullptr || pColorSet->getHeader() == nullptr ) {
        return;
    }

    NumColors = static_cast<uint8_t>( pColorSet->getNumMaterials() );
    if ( ColorIndex >= NumColors ) {
        return;
    }

    ::Material *pColorMaterial = pColorSet->getMaterialAtIndex( ColorIndex );
    if ( pColorMaterial == nullptr ) {
        return;
    }

    const uint64_t unchangeableHashcode = GetUnchangeableMaterialHashcode();
    const uint32_t numMaterials = Material.getNumMaterials();
    Render2DMHashEntry *pHashcodes = Material.getHashTableHead();

    for ( uint32_t i = 0; i < numMaterials; i++ ) {
        ::Material *pMaterial = Material.getMaterialAtIndex( i );
        if ( pMaterial == nullptr ) {
            continue;
        }

        MaterialParameter *pParameter = pMaterial->getParameterByIndex( 0 );
        if ( pParameter == nullptr ||
             pParameter->Hashcode == kOpaqueClotheShaderHashcode ) {
            continue;
        }

        if ( pHashcodes != nullptr &&
             pHashcodes[i].Hashcode == unchangeableHashcode ) {
            continue;
        }

        ApplyColorMaterial( pMaterial, pColorMaterial );
    }
}

void AvatarClothe::claimLayerTextures( AvatarBnk *pBnk,
                                       AvatarBnk *pCommonBnk ) {
    // FUN_008f25c0 (second half)
    if ( Material.getHeader() == nullptr ) {
        return;
    }

    const uint64_t cubemapHashcode = GetCubemapTextureHashcode();
    const uint32_t numMaterials = Material.getNumMaterials();
    Render2DMHashEntry *pHashcodes = Material.getHashTableHead();
    if ( pHashcodes == nullptr ) {
        return;
    }

    for ( uint32_t i = 0; i < numMaterials; i++ ) {
        RenderFile::Section *pMaterialSection = pHashcodes[i].pEntry;
        if ( pMaterialSection == nullptr ) {
            continue;
        }

        ::Material *pMaterial = (::Material *)( pMaterialSection + 1 );
        MaterialParameter *pParameter = pMaterial->getParameterByIndex( 0 );
        if ( pParameter == nullptr || pParameter->NumLayers == 0 ||
             pParameter->Hashcode == kOpaqueClotheShaderHashcode ) {
            continue;
        }

        MaterialLayer *pLayer = pParameter->getLayer( 0 );
        if ( pLayer == nullptr || pLayer->NumTextures == 0 ) {
            continue;
        }

        for ( uint32_t j = 0; j < pLayer->NumTextures; j++ ) {
            const uint64_t textureHashcode = pLayer->pLayerTextures[j].Hashcode;
            if ( textureHashcode == 0 || textureHashcode == cubemapHashcode ) {
                continue;
            }

            if ( NumTextures >= kMaxRenderInstances ) {
                OTDU_LOG_WARN( "Too many texture references for a clothe\n" );
                return;
            }

            AvatarBitmapInstance *pTexture =
                TexturePool.claim( pBnk, textureHashcode );
            if ( pTexture == nullptr ) {
                pTexture = TexturePool.claim( pCommonBnk, textureHashcode );
            }

            pTextures[NumTextures] = pTexture;
            if ( pTexture != nullptr ) {
                NumTextures++;
            }
        }
    }
}

void AvatarClothe::registerRenderData() {
    // FUN_008f2290
    RenderAvatarBase::registerRenderData();

    if ( Scene.getHeader() == nullptr ) {
        return;
    }

    for ( int32_t i = 0; i < NumInstances; i++ ) {
        if ( pInstances[i] == nullptr ) {
            continue;
        }

        Render3DG *pGeometry = pInstances[i]->getGeometry();
        if ( pGeometry != nullptr && pGeometry->getHeader() != nullptr ) {
            Scene.bindGeometryReferences( pGeometry );
        }
    }
}

void AvatarClothe::bindPartTextures() {
    // FUN_008f2ab0
    const uint32_t numMaterials = Material.getNumMaterials();
    for ( uint32_t i = 0; i < numMaterials; i++ ) {
        ::Material *pMaterial = Material.getMaterialAtIndex( i );

        for ( int32_t j = 0; j < NumTextures; j++ ) {
            if ( pTextures[j] == nullptr ) {
                continue;
            }

            Render2DB *pBitmap = pTextures[j]->getBitmap();
            if ( pBitmap != nullptr && pBitmap->getHeader() != nullptr ) {
                BindBitmapToMaterial( pMaterial, pBitmap );
            }
        }
    }
}

AvatarDBEntry *AvatarClothe::getAvatarPart() {
    // FUN_008f2180
    if ( StreamingRequest.pPartRequesting != nullptr ) {
        return StreamingRequest.pPartRequesting;
    }
    if ( StreamingRequest.pPartInLoading != nullptr ) {
        return StreamingRequest.pPartInLoading;
    }
    return pAvatarPart;
}

void AvatarClothe::requestPart( AvatarDBEntry *pPart,
                                const uint8_t colorIndex,
                                const bool bAsync,
                                const bool bCritical ) {
    // FUN_008f2e10
    if ( pPart != getAvatarPart() || ColorIndex != colorIndex ) {
        StreamingRequest.pPartRequesting = pPart;
        StreamingRequest.bCritical = bCritical;
        StreamingRequest.bAsync = bAsync;
        StreamingRequest.State = AVT_Unloaded;

        ColorIndex = colorIndex;

        if ( LoadingState == AVT_LoadPending || LoadingState == AVT_Unknown ) {
            LoadingState = AVT_Unloaded;
        }
    }

    if ( !bAsync ) {
        stepLoading();
    }
}

void AvatarClothe::releaseCurrentPart() {
    // FUN_008f2320
    for ( int32_t i = NumInstances - 1; i >= 0; i-- ) {
        AvatarRenderInstancePool<AvatarGeometryInstance>::Release(
            pInstances[i] );
        pInstances[i] = nullptr;
        NumInstances--;
    }
    GeometryPool.Allocator.reset();

    for ( int32_t i = NumTextures - 1; i >= 0; i-- ) {
        AvatarRenderInstancePool<AvatarBitmapInstance>::Release( pTextures[i] );
        pTextures[i] = nullptr;
        NumTextures--;
    }
    TexturePool.Allocator.reset();

    NumColors = 0;

    releaseRenderData();
}

void AvatarClothe::installLoadedPart() {
    // FUN_008f2420
    AvatarDBEntry *pPart = StreamingRequest.pPartRequesting;
    if ( pPart == nullptr ) {
        pPart = StreamingRequest.pPartInLoading;
    }

    if ( pPart == nullptr ) {
        return;
    }

    if ( IsEmptyPartName( pPart->getBankName() ) ) {
        pAvatarPart = pPart;
        StreamingRequest.decreaseReferenceCount();
        LoadingState = AVT_LoadCompleted;
        return;
    }

    AvatarBnk *pBnk = StreamingRequest.pLoadingBnk;
    if ( pPart->isCommonPart() ) {
        AvatarBnk &commonClothes = gpAvatar->getBnkManager().getCommonClothes();
        pBnk = commonClothes.isInstallOK() ? &commonClothes : nullptr;
    }

    if ( installPart( pPart, pBnk ) ) {
        pAvatarPart = pPart;
        StreamingRequest.decreaseReferenceCount();
        LoadingState = AVT_LoadCompleted;
        return;
    }

    StreamingRequest.decreaseReferenceCount();
    LoadingState = AVT_Unknown;
}

void AvatarClothe::stepLoading() {
    // FUN_008f2d60
    if ( !StreamingRequest.step() ) {
        decreaseReferenceCount();
        return;
    }

    if ( LoadingState == AVT_Ready &&
         StreamingRequest.State == AVT_LoadCompleted ) {
        releaseHierarchy();
        releaseCurrentPart();
        LoadingState = AVT_Unloaded;
    }

    if ( LoadingState == AVT_Unloaded &&
         StreamingRequest.State == AVT_LoadCompleted ) {
        StreamingRequest.State = AVT_Ready;
        LoadingState = AVT_LoadPending;
    }

    if ( LoadingState == AVT_LoadPending ) {
        installLoadedPart();
    }

    if ( LoadingState == AVT_LoadCompleted &&
         StreamingRequest.State == AVT_LoadCompleted ) {
        releaseCurrentPart();
        LoadingState = AVT_Unloaded;
    }
}

bool AvatarStreamingRequest::step() {
    // FUN_008ec4a0
    bool bResult = true;

    if ( State == AVT_Unknown && pLoadingBnk != nullptr &&
         pLoadingBnk->isInstallOK() && pLoadingBnk->getNumReferences() > 0 ) {
        pLoadingBnk->removeReference();
        pLoadingBnk = nullptr;
    }

    if ( State == AVT_Unloaded ) {
        bResult = stepPendingRequest();
    }

    if ( State == AVT_LoadPending && pLoadingBnk != nullptr &&
         pLoadingBnk->isInstallOK() ) {
        State = AVT_LoadCompleted;
    }

    if ( State == AVT_Unknown && pLoadingBnk != nullptr &&
         pLoadingBnk->isInstallOK() ) {
        pLoadingBnk->removeReference();
        pLoadingBnk = nullptr;
    }

    return bResult;
}

bool AvatarStreamingRequest::stepPendingRequest() {
    // FUN_008ec3f0
    if ( pLoadingBnk != nullptr ) {
        if ( !pLoadingBnk->isInstallOK() ) {
            return true;
        }

        pLoadingBnk->removeReference();
        pLoadingBnk = nullptr;
    }

    if ( IsEmptyPartName( pPartRequesting->getBankName() ) ||
         pPartRequesting->isCommonPart() ) {
        pPartInLoading = pPartRequesting;
        State = AVT_LoadCompleted;
        pPartRequesting = nullptr;
        return true;
    }

    const eAvatarBankRequest requestResult = requestBank();
    if ( requestResult == ABR_Ready ) {
        pPartInLoading = pPartRequesting;
        pLoadingBnk->addReference();
        pPartRequesting = nullptr;
        State = AVT_LoadPending;
        return true;
    }

    if ( requestResult != ABR_Failed ) {
        return true;
    }

    pPartInLoading = pPartRequesting;
    pPartRequesting = nullptr;
    State = AVT_Unknown;
    return false;
}

eAvatarBankRequest AvatarStreamingRequest::requestBank() {
    // FUN_008ec340
    const char *pBankName = pPartRequesting->getBankName();
    const char *pCategoryFolder = pPartRequesting->getCategoryFolder();
    const char *pBrandFolder = pPartRequesting->getBrandFolder();

    if ( pBankName == nullptr || pCategoryFolder == nullptr ||
         pBrandFolder == nullptr ) {
        return ABR_Failed;
    }

    std::string bankFolder = gpConfig->getResRootPath();
    bankFolder += "Avatar/";
    bankFolder += pCategoryFolder;
    bankFolder += "/";
    bankFolder += pBrandFolder;
    bankFolder += "/";

    AvatarBnkCache &cache = gpAvatar->getBnkManager().getBankCache();
    return cache.requestBank( bankFolder.c_str(), pBankName, bAsync, bCritical,
                              &pLoadingBnk );
}

void AvatarStreamingRequest::decreaseReferenceCount() {
    // FUN_008ec230
    if ( State == AVT_Ready ) {
        State = AVT_LoadCompleted;
    }
    if ( State == AVT_LoadCompleted ) {
        State = AVT_LoadPending;
    }
    if ( State == AVT_LoadPending ) {
        pPartInLoading = nullptr;
        State = AVT_Unloaded;
    }
    if ( State == AVT_Unloaded ) {
        pPartRequesting = nullptr;
        if ( pLoadingBnk != nullptr && pLoadingBnk->isInstallOK() ) {
            pLoadingBnk->removeReference();
            pLoadingBnk = nullptr;
        }
        State = AVT_Unknown;
    }
}

void AvatarClothe::decreaseReferenceCount() {
    // FUN_008f24b0
    if ( LoadingState == AVT_Ready ) {
        releaseHierarchy();
        LoadingState = AVT_LoadCompleted;
    }
    if ( LoadingState == AVT_LoadCompleted ) {
        releaseCurrentPart();
        pAvatarPart = nullptr;
        LoadingState = AVT_LoadPending;
    }
    if ( LoadingState == AVT_LoadPending ) {
        LoadingState = AVT_Unloaded;
    }
    if ( LoadingState == AVT_Unloaded ) {
        StreamingRequest.decreaseReferenceCount();
        LoadingState = AVT_Unknown;
    }
}

PooledAvatarEntry::PooledAvatarEntry() {
    // FUN_008f00a0
    AvatarLight.Type = eLightType::LT_Directional;
    LightConfig.setFlags( 0x103 );
    ShaderConfig.setFlags( 0x103 );

    Matrix = Matrix4x4f_t::Identity();
}

PooledAvatarEntry::~PooledAvatarEntry() {}

void PooledAvatarEntry::initialize( int32_t index ) {
    // FUN_008ef240
    pBoneArray = nullptr;
    bUsed = false;

    LightConfig.addLight( &AvatarLight );

    ShaderParams.ShaderHashcode = kAvatarShaderHashcode;
    ShaderParams.pMaterial = nullptr;
    ShaderParams.ParameterID = kAvatarShaderParameterID;
    ShaderParams.pParameters = ShaderParamData.data();
    ShaderParams.NumParameters = 1;
    ShaderParams.bBindToPixelShader = false;
    ShaderConfig.bindParameters( &ShaderParams, 1 );

    Impostor.initialize( index, &Collection3D, &LightConfig, &ShaderConfig );
    Body.initialize( index, &Collection3D, &LightConfig, &ShaderConfig );
    Body.pLowRes = &Impostor;

    CarBelt.initialize( index, &Collection3D, 0, 0, &LightConfig,
                        &ShaderConfig );
    CarBelt.pLowRes = &Impostor;

    for ( int32_t i = 0; i < AC_Count; i++ ) {
        Slots[i].initialize( index, &Collection3D, kClotheGeometryPoolSize[i],
                             kClotheTexturePoolSize[i], &LightConfig,
                             &ShaderConfig );

        // Hair and helmet are the two slots the impostor never bakes
        if ( i != AC_Hair && i != AC_Helmet ) {
            Slots[i].pLowRes = &Impostor;
        }
    }

    pBodyHighRes = &Body;

    AvatarBnk &commonBodies = gpAvatar->getBnkManager().getCommonBodies();
    if ( commonBodies.isInstallOK() ) {
        int32_t bufferSize = 0;
        for ( const char *pBodyName : { "MAN_BODY", "WOMAN_BODY" } ) {
            const uint16_t *pSkeleton = static_cast<const uint16_t *>(
                commonBodies.getSkeleton( pBodyName ) );
            if ( pSkeleton == nullptr ) {
                continue;
            }

            const int32_t numBones = *reinterpret_cast<const uint16_t *>(
                reinterpret_cast<const uint8_t *>( pSkeleton ) +
                kAvatarSkeletonDataOffset );
            const int32_t size = numBones * 0x40 + 0x824;
            if ( size > bufferSize ) {
                bufferSize = size;
            }
        }

        AnimBufferSize = bufferSize;
        pAnimBuffer =
            TestDrive::AllocAligned( ( bufferSize + 0xf ) & ~0xf, 0x10 );
    }

    gpRender->registerDeviceResetListener( this );
}

void PooledAvatarEntry::terminate() {
    // FUN_008efe40
    gpRender->unregisterDeviceResetListener( this );

    LightConfig.removeLight( &AvatarLight );

    if ( pAnimBuffer != nullptr ) {
        TestDrive::FreeAligned( pAnimBuffer );
        pAnimBuffer = nullptr;
    }

    if ( bUsed ) {
        releaseParts();
        bUsed = false;
        pBoneArray = nullptr;
    }

    for ( AvatarClothe &slot : Slots ) {
        slot.terminate();
    }

    CarBelt.terminate();
    Body.terminate();
    Impostor.terminate();

    pBoneArray = nullptr;
}

bool PooledAvatarEntry::onRenderDeviceReset( int32_t reason ) {
    // FUN_008ef420
    if ( reason == 4 ) {
        Impostor.bDirty = 2;
    }

    return true;
}

void PooledAvatarEntry::stepBodyLoading() {
    if ( Body.LoadingState == AVT_Unloaded ) {
        Body.LoadingState = AVT_LoadPending;
    }
    if ( Body.LoadingState == AVT_LoadPending ) {
        Body.stepLoading();
    }

    if ( Impostor.LoadingState == AVT_Unloaded ) {
        Impostor.LoadingState = AVT_LoadPending;
    }
    if ( Impostor.LoadingState == AVT_LoadPending ) {
        Impostor.stepLoading();
    }
}

void PooledAvatarEntry::stepLoading( const eAvatarLoadingPass pass ) {
    // FUN_008f0490 / FUN_008f03f0 / FUN_008f0330
    for ( AvatarClothe &slot : Slots ) {
        if ( ShouldStepClothe( slot, pass ) ) {
            slot.stepLoading();
        }
    }

    stepBodyLoading();

    if ( ShouldStepClothe( CarBelt, pass ) ) {
        CarBelt.stepLoading();
    }
}

eAvatarLoadingState PooledAvatarEntry::getLoadingState() const {
    // FUN_008eeff0
    eAvatarLoadingState state = AVT_Ready;

    auto accumulate = [&state]( const eAvatarLoadingState partState ) {
        if ( partState != AVT_Unknown && partState < state ) {
            state = partState;
        }
    };

    accumulate( Impostor.LoadingState );
    accumulate( Body.LoadingState );
    for ( const AvatarClothe &slot : Slots ) {
        accumulate( slot.LoadingState );
    }

    return state;
}

bool PooledAvatarEntry::stepLoadingCompletion() {
    // FUN_008efbd0
    if ( getLoadingState() != AVT_LoadCompleted ) {
        return false;
    }

    bool bChanged = ( Impostor.LoadingState == AVT_LoadCompleted );
    if ( bChanged ) {
        Impostor.instanciateHierarchy();
        Impostor.LoadingState = AVT_Ready;
    }

    if ( Body.LoadingState == AVT_LoadCompleted ) {
        Body.instanciateHierarchy();
        Body.LoadingState = AVT_Ready;
        bChanged = true;

        OTDU_UNIMPLEMENTED_ONCE; // TODO: AVATAR
    }

    for ( AvatarClothe &slot : Slots ) {
        if ( slot.LoadingState != AVT_LoadCompleted ) {
            continue;
        }

        if ( slot.StreamingRequest.State == AVT_LoadCompleted ) {
            slot.releaseCurrentPart();
            slot.LoadingState = AVT_Unloaded;
        } else {
            slot.instanciateHierarchy();
            slot.LoadingState = AVT_Ready;
        }

        bChanged = true;
    }

    if ( !bChanged ) {
        return false;
    }

    tickBodyMaterials();
    setEnvMap();
    tickEyeMaterials();

    return true;
}

void PooledAvatarEntry::updateBodyPartVisibility() {
    // FUN_008ef810
    AvatarClothe &bottom = Slots[AC_Bottom];
    AvatarClothe &shoes = Slots[AC_Shoes];

    const AvatarClothePart *pTop =
        static_cast<const AvatarClothePart *>( Slots[AC_Top].getAvatarPart() );
    if ( pTop == nullptr || pTop->Length == ACL_None ) {
        Body.setMeshVisibility( gHashcodeArms, true );
        Body.setMeshVisibility( gHashcodeTorso, true );
    } else if ( pTop->Length == ACL_Long ) {
        Body.setMeshVisibility( gHashcodeArms, false );
        Body.setMeshVisibility( gHashcodeTorso, false );
    } else if ( pTop->Length == ACL_Short ) {
        Body.setMeshVisibility( gHashcodeArms, true );
        Body.setMeshVisibility( gHashcodeTorso, false );
    }

    const AvatarClothePart *pBottom =
        static_cast<const AvatarClothePart *>( bottom.getAvatarPart() );
    if ( pBottom == nullptr ) {
        Body.setMeshVisibility( gHashcodeLegs, true );
    } else {
        const bool bLegsVisible = ( pBottom->Length != ACL_Long );
        Body.setMeshVisibility( gHashcodeLegs, bLegsVisible );
        shoes.setMeshVisibility( gHashcodeHideable, bLegsVisible );
    }

    if ( pTop != nullptr ) {
        const bool bBottomVisible = ( pTop->bIsPartBoth == 0 );
        if ( bottom.pLowRes != nullptr &&
             bottom.pHierarchyNodeLowRes != nullptr &&
             bottom.bVisible != bBottomVisible ) {
            if ( bBottomVisible ) {
                bottom.showLowResHierarchy();
            } else {
                bottom.hideLowResHierarchy();
            }
        }

        bottom.bVisible = bBottomVisible;

        if ( !bBottomVisible ) {
            Body.setMeshVisibility( gHashcodeLegs, true );
            shoes.setMeshVisibility( gHashcodeHideable, true );
        }
    }

    const AvatarClothePart *pShoes =
        static_cast<const AvatarClothePart *>( shoes.getAvatarPart() );
    Body.setMeshVisibility( gHashcodeFeet,
                            pShoes == nullptr || pShoes->Length != ACL_Long );

    const AvatarClothePart *pGloves = static_cast<const AvatarClothePart *>(
        Slots[AC_Gloves].getAvatarPart() );
    Body.setMeshVisibility( gHashcodeHands,
                            pGloves == nullptr || pGloves->Length == ACL_None );

    Body.setMeshVisibility( gHashcodeSexe, false );
}

void PooledAvatarEntry::releaseParts() {
    // FUN_008ef560
    CarBelt.decreaseReferenceCount();
    pSkeleton = nullptr;

    Body.decreaseReferenceCount();
    Impostor.decreaseReferenceCount();

    for ( AvatarClothe &slot : Slots ) {
        slot.decreaseReferenceCount();
    }
}

bool PooledAvatarEntry::hasScratchPools() const {
    // FUN_008eed30
    if ( Body.pMemoryPool != nullptr ) {
        return true;
    }

    for ( const AvatarClothe &slot : Slots ) {
        if ( slot.pMemoryPool != nullptr ) {
            return true;
        }
    }

    return false;
}

void PooledAvatarEntry::setMeshVisibility( const uint64_t meshHashcode,
                                           const bool bMeshVisible ) {
    // FUN_00424670
    for ( AvatarClothe &slot : Slots ) {
        slot.setMeshVisibility( meshHashcode, bMeshVisible );
    }

    Body.setMeshVisibility( meshHashcode, bMeshVisible );
    Impostor.setMeshVisibility( meshHashcode, bMeshVisible );
    CarBelt.setMeshVisibility( meshHashcode, bMeshVisible );
}

void PooledAvatarEntry::setBones( Matrix4x4f_t *pPalette ) {
    // FUN_00424850
    const auto bindPart = []( RenderAvatarBase &part, Matrix4x4f_t *pBones ) {
        if ( part.pHierarchyNode != nullptr &&
             part.pHierarchyNode->pInstance != nullptr ) {
            part.pHierarchyNode->pInstance->setBones( pBones );
        }
    };

    for ( int32_t i = 0; i < AC_Count; i++ ) {
        if ( i != AC_Helmet ) {
            bindPart( Slots[i], pPalette );
        }
    }

    bindPart( Body, pPalette );
    bindPart( Impostor, pPalette );
    bindPart( CarBelt, pPalette );
}

void PooledAvatarEntry::setMatrix( const Matrix4x4f_t &matrix ) {
    // FUN_00424800
    for ( AvatarClothe &slot : Slots ) {
        slot.setMatrix( matrix );
    }

    Body.setMatrix( matrix );
    Impostor.setMatrix( matrix );
    CarBelt.setMatrix( matrix );
}

void PooledAvatarEntry::showAllParts() {
    // FUN_008eff20
    const auto showPart = []( RenderAvatarBase &part ) {
        if ( part.pLowRes != nullptr && part.pHierarchyNodeLowRes != nullptr &&
             !part.bVisible ) {
            part.showLowResHierarchy();
        }

        part.bVisible = true;
    };

    showPart( Impostor );
    showPart( Body );
    showPart( CarBelt );

    for ( AvatarClothe &slot : Slots ) {
        showPart( slot );
    }

    bVisible = true;
    ActiveClotheSet = kNoClotheSet;
}

void PooledAvatarEntry::setClothe( const int32_t category,
                                   const uint16_t clotheID,
                                   const uint8_t colorIndex,
                                   const int32_t clotheSet ) {
    // FUN_008eebc0
    if ( category < 0 || category >= AC_Count || clotheSet < 0 ||
         clotheSet >= kNumClotheTypes ) {
        return;
    }

    const bool bSharedBySets = category == AC_Hair ||
                               category == AC_FacePilosity ||
                               category == AC_Eyebrows ||
                               category == AC_Glasses || category == AC_Helmet;

    if ( bSharedBySets ) {
        for ( int32_t set = 0; set < kNumClotheTypes; set++ ) {
            pClothesHC[set][category] = clotheID;
            pColors[set][category] = colorIndex;
        }
    }

    pClothesHC[clotheSet][category] = clotheID;
    pColors[clotheSet][category] = colorIndex;
}

void PooledAvatarEntry::setClothesFromConfig(
    const AvatarConfiguration &config ) {
    // FUN_008eee40
    for ( int32_t set = 0; set < kNumClotheTypes; set++ ) {
        for ( int32_t category = 0; category < AC_Count; category++ ) {
            setClothe( category, config.ClotheID[set][category],
                       config.ColorID[set][category], set );
        }
    }
}

bool PooledAvatarEntry::installClothePart( AvatarDBEntry *pPart,
                                           const uint8_t colorIndex,
                                           const int32_t clotheSet,
                                           const bool bAsync,
                                           const bool bCritical ) {
    // FUN_008eec20
    if ( pPart == nullptr ) {
        return false;
    }

    const AvatarClothePart *pClothePart =
        static_cast<const AvatarClothePart *>( pPart );

    setClothe( pClothePart->Category, pPart->IDHashcode, colorIndex,
               clotheSet );

    if ( clotheSet != ActiveClotheSet ) {
        return false;
    }

    const AvatarDBEntry *pBodyPart = Body.getAvatarPart();
    if ( pBodyPart == nullptr || pPart->Gender != pBodyPart->Gender ) {
        return false;
    }

    if ( pClothePart->Category < 0 || pClothePart->Category >= AC_Count ) {
        return false;
    }

    Slots[pClothePart->Category].requestPart( pPart, colorIndex, bAsync,
                                              bCritical );
    return true;
}

bool PooledAvatarEntry::installBodyPart( AvatarDBEntry *pPart ) {
    // FUN_008eedf0
    if ( pSkeleton == nullptr && pPart != nullptr ) {
        const char *pMeshName = pPart->getMeshName();
        AvatarBnk &commonBodies = gpAvatar->getBnkManager().getCommonBodies();

        if ( pMeshName != nullptr && commonBodies.isInstallOK() ) {
            void *pEntry = commonBodies.getSkeleton( pMeshName );
            if ( pEntry != nullptr ) {
                pSkeleton = static_cast<uint8_t *>( pEntry ) +
                            kAvatarSkeletonDataOffset;
            }
        }
    }

    Body.requestPart( pPart );
    return true;
}

void RenderAvatarBase::tickEyeMaterial( const AvatarEyePart *pEyeResource ) {
    // FUN_00411870
    if ( Material.getHeader() == nullptr ) {
        return;
    }

    Render2DMHashEntry *pHashcodes = Material.getHashTableHead();
    if ( pHashcodes == nullptr ) {
        return;
    }

    const uint32_t numMaterials = Material.getNumMaterials();
    for ( uint32_t i = 0; i < numMaterials; i++ ) {
        ::Material *pMaterial = Material.getMaterialAtIndex( i );
        if ( pMaterial != nullptr ) {
            ApplyEyeUVOffsetToMaterial( pMaterial, pEyeResource,
                                        pHashcodes[i].Hashcode );
        }
    }
}

void RenderAvatarBase::tickBodyMaterial( const float *pSkinTint,
                                         RenderAvatar *pBodyHighRes ) {
    // FUN_00411690
    // The original also reads the mesh hashcode of each entry here, then drops
    // it on the floor: only the eye pass below actually filters on it.
    if ( Material.getHeader() == nullptr ) {
        return;
    }

    const uint32_t numMaterials = Material.getNumMaterials();
    for ( uint32_t i = 0; i < numMaterials; i++ ) {
        ::Material *pMaterial = Material.getMaterialAtIndex( i );
        if ( pMaterial != nullptr ) {
            ApplySkinTintToMaterial( pMaterial, pSkinTint, pBodyHighRes );
        }
    }
}

void RenderAvatarBase::tickHeadMaterial( const Matrix4x4f_t &headMatrix ) {
    // FUN_008f1d80
    if ( Material.getHeader() == nullptr ) {
        return;
    }

    const uint32_t numMaterials = Material.getNumMaterials();
    for ( uint32_t i = 0; i < numMaterials; i++ ) {
        ::Material *pMaterial = Material.getMaterialAtIndex( i );
        if ( pMaterial == nullptr ) {
            continue;
        }

        MaterialParameter *pParameter = pMaterial->getParameterByIndex( 0 );
        if ( pParameter == nullptr || pParameter->Type != MPT_Generic ||
             pParameter->Hashcode != kEyeShaderHashcode ) {
            continue;
        }

        const Eigen::Vector4f up = TDUMat4x4MulVec(
            headMatrix, Eigen::Vector4f( 0.0f, 1.0f, 0.0f, 0.0f ) );
        SetMaterialStaticParam( pMaterial, kStaticParamHeadUp, up.data() );

        const Eigen::Vector4f right = TDUMat4x4MulVec(
            headMatrix, Eigen::Vector4f( 1.0f, 0.0f, 0.0f, 0.0f ) );
        SetMaterialStaticParam( pMaterial, kStaticParamHeadRight,
                                right.data() );
    }
}

void RenderAvatarBase::draw( RenderScene *pScene, const ColorRGBA &color ) {
    // FUN_008f0f20
    if ( !bVisible || pHierarchyNode == nullptr ) {
        return;
    }

    pHierarchyNode->Flags |= 0x30;
    SetHierarchyColor( pHierarchyNode, color );

    pScene->enqueueDynamicHiearchy( pHierarchyNode );
}

void RenderAvatarBase::applyLightingAttenuation(
    const AvatarLightAttenuation &attenuation ) {
    // FUN_008f0d20
    if ( Material.getHeader() == nullptr ) {
        return;
    }

    const uint32_t numMaterials = Material.getNumMaterials();
    for ( uint32_t i = 0; i < numMaterials; i++ ) {
        ::Material *pMaterial = Material.getMaterialAtIndex( i );
        if ( pMaterial != nullptr ) {
            ApplyLightAttenuationToMaterial( pMaterial, attenuation );
        }
    }
}

void RenderAvatarBase::setEnvMap( const AvatarEnvMap *pEnvMap ) {
    // FUN_00411780
    if ( Material.getHeader() == nullptr ) {
        return;
    }

    const uint32_t numMaterials = Material.getNumMaterials();
    for ( uint32_t i = 0; i < numMaterials; i++ ) {
        ::Material *pMaterial = Material.getMaterialAtIndex( i );
        if ( pMaterial != nullptr ) {
            ApplyEnvMapToMaterial( pMaterial, pEnvMap );
        }
    }
}

void PooledAvatarEntry::setEnvMap() {
    // FUN_004114f0
    for ( AvatarClothe &slot : Slots ) {
        slot.setEnvMap( &EnvMap );
    }
    Body.setEnvMap( &EnvMap );
    Impostor.setEnvMap( &EnvMap );
    CarBelt.setEnvMap( &EnvMap );
}

void PooledAvatarEntry::tickEyeMaterials() {
    // FUN_00411540
    for ( AvatarClothe &slot : Slots ) {
        slot.tickEyeMaterial( pEyeResource );
    }
    Body.tickEyeMaterial( pEyeResource );
    Impostor.tickEyeMaterial( pEyeResource );
    CarBelt.tickEyeMaterial( pEyeResource );
}

void PooledAvatarEntry::draw( RenderScene *pScene ) {
    // FUN_008ef1b0
    if ( !bVisible || !gShowAvatar ) {
        return;
    }

    if ( getStrictLoadingState() != AVT_Ready ) {
        return;
    }

    for ( AvatarClothe &slot : Slots ) {
        slot.draw( pScene, gColorWhite );
    }
    Body.draw( pScene, gColorWhite );
    CarBelt.draw( pScene, gColorWhite );
}

void PooledAvatarEntry::addShadowCasters( const bool bLowRes,
                                          const float intensity ) {
    // FUN_008f0000
    if ( gpMngShadow == nullptr || !gShowAvatar ) {
        return;
    }

    const auto addCaster = [intensity]( const RenderAvatarBase &part ) {
        if ( part.pHierarchyNode != nullptr ) {
            gpMngShadow->addShadowCaster( part.pHierarchyNode->pInstance,
                                          intensity );
        }
    };

    if ( bLowRes ) {
        addCaster( Impostor );
        return;
    }

    for ( int32_t i = 0; i < AC_Count; i++ ) {
        if ( i != AC_Helmet || ActiveClotheSet != 0 ) {
            addCaster( Slots[i] );
        }
    }
    addCaster( Body );
}

void PooledAvatarEntry::drawLowRes( RenderScene *pScene ) {
    // FUN_008ef140
    if ( !bVisible || !gShowAvatar ) {
        return;
    }

    Slots[AC_Hair].draw( pScene, gColorWhite );
    Slots[AC_Helmet].draw( pScene, gColorWhite );
    Impostor.draw( pScene, gColorWhite );
}

void PooledAvatarEntry::applyLightingAttenuation(
    const AvatarLightAttenuation &attenuation ) {
    // FUN_008ef760
    for ( AvatarClothe &slot : Slots ) {
        slot.applyLightingAttenuation( attenuation );
    }
    Body.applyLightingAttenuation( attenuation );
    CarBelt.applyLightingAttenuation( attenuation );
}

eAvatarLoadingState PooledAvatarEntry::getStrictLoadingState() const {
    // FUN_008ef0b0
    eAvatarLoadingState state = AVT_Ready;

    auto accumulate = [&state]( const eAvatarLoadingState partState ) {
        if ( partState < state ) {
            state = partState;
        }
    };

    accumulate( Impostor.LoadingState );
    accumulate( Body.LoadingState );
    for ( const AvatarClothe &slot : Slots ) {
        accumulate( slot.LoadingState );
    }

    return state;
}

void PooledAvatarEntry::tickBodyMaterials() {
    // FUN_004114a0
    for ( AvatarClothe &slot : Slots ) {
        slot.tickBodyMaterial( pSkinTint, pBodyHighRes );
    }
    Body.tickBodyMaterial( pSkinTint, pBodyHighRes );
    Impostor.tickBodyMaterial( pSkinTint, pBodyHighRes );
    CarBelt.tickBodyMaterial( pSkinTint, pBodyHighRes );
}
