#include "shared.h"
#include "avatar_clothes.h"

#include "render/gs_render.h"

static constexpr int32_t kClotheScratchPoolASize[AC_Count] = { // DAT_010c4508
    0, 0, 0, 0x15800, 0x15800, 0, 0x15800, 0, 0 };
static constexpr int32_t kClotheScratchPoolBSize[AC_Count] = { // DAT_010c452c
    0, 0, 0, 0x1ec00, 0x1ec00, 0, 0x12400, 0, 0 };

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

void RenderAvatarBase::releaseCurrentPart() {
    // FUN_008f1d30 (body) / FUN_008f2320 (clothe)
    OTDU_UNIMPLEMENTED_ONCE; // TODO: AVATAR
}

void RenderAvatarBase::instanciateHierarchy() {
    // FUN_008f0f80
    OTDU_UNIMPLEMENTED_ONCE; // TODO: AVATAR
}

void AvatarLOD::stepLoading() {
    // FUN_008f3030
    OTDU_UNIMPLEMENTED_ONCE; // TODO: AVATAR
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

void AvatarClothe::releaseCurrentPart() {
    // FUN_008f2320
    OTDU_UNIMPLEMENTED_ONCE; // TODO: AVATAR
}

void AvatarClothe::stepLoading() {
    // FUN_008f2d60
    OTDU_UNIMPLEMENTED_ONCE; // TODO: AVATAR
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
        if ( pLoadingBnk != nullptr && pLoadingBnk->bLoaded ) {
            if ( pLoadingBnk->NumReferences > 0 ) {
                pLoadingBnk->NumReferences--;
            }
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

    OTDU_UNIMPLEMENTED_ONCE; // TODO: AVATAR

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
