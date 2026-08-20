#include "shared.h"
#include "avatar_pool.h"

#include "render/gs_render.h"

AvatarPool::AvatarPool() {
    // FUN_008ee930
    for ( int32_t i = 0; i < kNumInstances; ++i ) {
        pInstances[i].pOwner = this;
    }
}

void AvatarPool::initialize() {
    // FUN_008ee830
    for ( int32_t i = 0; i < kNumInstances; ++i ) {
        pInstances[i].initialize( i );
    }

    gpRender->registerConfigListener( this );
}

void AvatarPool::tick() {
    // FUN_008ee970
    static constexpr eAvatarLoadingPass kPasses[] = {
        ALP_NotUnloaded, ALP_Synchronous, ALP_AsyncCritical, ALP_Async };

    for ( const eAvatarLoadingPass pass : kPasses ) {
        for ( PooledAvatarEntry &instance : pInstances ) {
            instance.stepLoading( pass );
        }
    }

    for ( PooledAvatarEntry &instance : pInstances ) {
        if ( instance.stepLoadingCompletion() ) {
            instance.bUpdatable = !instance.bUpdatable;
            instance.updateBodyPartVisibility();
        }

        if ( instance.CarBelt.LoadingState == AVT_LoadCompleted ) {
            if ( instance.CarBelt.StreamingRequest.State ==
                 AVT_LoadCompleted ) {
                instance.CarBelt.releaseCurrentPart();
                instance.CarBelt.LoadingState = AVT_Unloaded;
            } else {
                instance.CarBelt.instanciateHierarchy();
                instance.CarBelt.LoadingState = AVT_Ready;
            }
        }
    }
}

PooledAvatarEntry *AvatarPool::allocateInstance() {
    // FUN_008ee790
    for ( PooledAvatarEntry &instance : pInstances ) {
        if ( instance.bUsed || !instance.hasScratchPools() ) {
            continue;
        }

        instance.bUsed = true;
        return &instance;
    }

    return nullptr;
}

void AvatarPool::terminate() {
    // FUN_008ee8d0
    gpRender->unregisterConfigListener( this );

    for ( int32_t i = 0; i < kNumInstances; ++i ) {
        pInstances[i].terminate();
    }
}

bool AvatarPool::onConfigurationChanged() {
    // FUN_008ee7e0
    OTDU_UNIMPLEMENTED;

    for ( PooledAvatarEntry &instance : pInstances ) {
        instance.Impostor.bDirty = 2;
    }

    return true;
}
