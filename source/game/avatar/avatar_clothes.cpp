#include "shared.h"
#include "avatar_clothes.h"

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
