#include "shared.h"
#include "avatar_clothes.h"

#include "config/gs_config.h"
#include "gs_avatar.h"
#include "render/gs_render.h"

static constexpr int32_t kClotheScratchPoolASize[AC_Count] = { // DAT_010c4508
    0, 0, 0, 0x15800, 0x15800, 0, 0x15800, 0, 0 };
static constexpr int32_t kClotheScratchPoolBSize[AC_Count] = { // DAT_010c452c
    0, 0, 0, 0x1ec00, 0x1ec00, 0, 0x12400, 0, 0 };

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

void AvatarScratchPool::release() {
    // FUN_00410c10
    if ( pMemory != nullptr ) {
        TestDrive::FreeAligned( pMemory );
    }

    pMemory = nullptr;
    pCursor = nullptr;
    Size = 0;
}

void RenderAvatarBase::initialize( int32_t index,
                                   FileCollection3D *pCollection,
                                   LightSetupNode *pLightSetup,
                                   ShaderParameterSetupNode *pShaderSetup ) {
    // FUN_008f0760
    pMemoryPool = TestDrive::AllocAligned( kAvatarPartPoolSize, 0 );
    if ( pMemoryPool == nullptr ) {
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

void RenderAvatarBase::instanciateHierarchy() {
    // FUN_008f0f80
    OTDU_UNIMPLEMENTED_ONCE; // TODO: AVATAR
}

void RenderAvatarBase::releaseHierarchy() {
    // FUN_008f0d00
    OTDU_UNIMPLEMENTED_ONCE;

    pAvatarPart = nullptr;
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

    OTDU_UNIMPLEMENTED_ONCE;

    return true;
}

void RenderAvatar::releaseCurrentPart() {
    // FUN_008f1d30
    pSkeletonData = nullptr;
    p3DG = nullptr;

    releaseRenderData();
}

void AvatarImpostor::initialize( int32_t index,
                                 FileCollection3D *pCollection,
                                 LightSetupNode *pLightSetup,
                                 ShaderParameterSetupNode *pShaderSetup ) {
    // FUN_008f1920
    RenderAvatarBase::initialize( index, pCollection, pLightSetup,
                                  pShaderSetup );

    OTDU_UNIMPLEMENTED_ONCE; // TODO: AVATAR
}

void AvatarImpostor::terminate() {
    // FUN_008f1850
    OTDU_UNIMPLEMENTED_ONCE; // TODO: AVATAR

    RenderAvatarBase::terminate();
}

void AvatarClothe::initialize( int32_t index,
                               FileCollection3D *pCollection,
                               int32_t poolASize,
                               int32_t poolBSize,
                               LightSetupNode *pLightSetup,
                               ShaderParameterSetupNode *pShaderSetup ) {
    // FUN_008f2200
    RenderAvatarBase::initialize( index, pCollection, pLightSetup,
                                  pShaderSetup );

    ScratchPoolA.allocate( poolASize );
    ScratchPoolB.allocate( poolBSize );
}

void AvatarClothe::terminate() {
    // FUN_008f2520
    decreaseReferenceCount();
    StreamingRequest.decreaseReferenceCount();

    ScratchPoolA.release();
    ScratchPoolB.release();

    RenderAvatarBase::terminate();
}

bool AvatarClothe::installPart( AvatarDBEntry *pPart, AvatarBnk *pBnk ) {
    // FUN_008f25c0
    OTDU_UNIMPLEMENTED_ONCE;
    return false;
}

void AvatarClothe::releaseCurrentPart() {
    // FUN_008f2320
    OTDU_UNIMPLEMENTED_ONCE;

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

    OTDU_UNIMPLEMENTED_ONCE;
    return ABR_Failed;
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
        OTDU_UNIMPLEMENTED;
        LoadingState = AVT_LoadCompleted;
    }
    if ( LoadingState == AVT_LoadCompleted ) {
        OTDU_UNIMPLEMENTED;
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
        Slots[i].initialize( index, &Collection3D, kClotheScratchPoolASize[i],
                             kClotheScratchPoolBSize[i], &LightConfig,
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

    OTDU_UNIMPLEMENTED_ONCE; // TODO: AVATAR - Envmap
    tickEyeMaterials();

    return true;
}

void PooledAvatarEntry::updateBodyPartVisibility() {
    // FUN_008ef810
    OTDU_UNIMPLEMENTED_ONCE; // TODO: AVATAR
}

void PooledAvatarEntry::releaseParts() {
    // FUN_008ef560
    CarBelt.decreaseReferenceCount();
    pSkeleton = nullptr;

    decreaseRenderRef();
    decreaseRenderRef();

    for ( AvatarClothe &slot : Slots ) {
        slot.decreaseReferenceCount();
    }
}

void PooledAvatarEntry::decreaseRenderRef() {
    // FUN_008f2ef0
    OTDU_UNIMPLEMENTED;
}

void RenderAvatarBase::tickEyeMaterial() {
    // FUN_00411870
    OTDU_UNIMPLEMENTED;
}

void RenderAvatarBase::tickBodyMaterial() {
    // FUN_00411690
    OTDU_UNIMPLEMENTED;
}

void PooledAvatarEntry::tickEyeMaterials() {
    // FUN_00411540
    for ( AvatarClothe &slot : Slots ) {
        slot.tickEyeMaterial();
    }
    Body.tickEyeMaterial();
    Impostor.tickEyeMaterial();
    CarBelt.tickEyeMaterial();
}

void PooledAvatarEntry::tickBodyMaterials() {
    // FUN_004114a0
    for ( AvatarClothe &slot : Slots ) {
        slot.tickBodyMaterial();
    }
    Body.tickBodyMaterial();
    Impostor.tickBodyMaterial();
    CarBelt.tickBodyMaterial();
}
