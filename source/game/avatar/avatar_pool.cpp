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
    OTDU_UNIMPLEMENTED;
    gpRender->registerConfigListener( this );
}

void AvatarPool::tick() {
    // FUN_008ee970
    OTDU_UNIMPLEMENTED;
}

void AvatarPool::terminate() {
    // FUN_008ee8d0
    gpRender->unregisterConfigListener( this );
    OTDU_UNIMPLEMENTED;
}

bool AvatarPool::onConfigurationChanged() {
    // FUN_008ee7e0
    OTDU_UNIMPLEMENTED;
    return true;
}
