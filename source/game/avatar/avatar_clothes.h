#pragma once

#include <cstdint>
#include "avatar_configuration.h"
#include "render_avatar.h"

struct AvatarStylePart;
struct AvatarSkinPart;
struct AvatarEyePart;

class AvatarPool;

struct AvatarBnk {
    bool bLoaded = false;
    int32_t NumReferences = 0;
};

struct AvatarStreamingRequest {
    AvatarBnk *pLoadingBnk = nullptr;
    AvatarDBEntry *pPartInLoading = nullptr;
    AvatarDBEntry *pPartRequesting = nullptr;
    bool bAsync = false;
    eAvatarLoadingState State = AVT_Unknown;
    bool bCritical = false;

    void decreaseReferenceCount();
};

struct AvatarClothe : AvatarLOD {
    AvatarStreamingRequest StreamingRequest;
    eAvatarLoadingState LoadingState = AVT_Unknown;

    void decreaseReferenceCount();
};

struct PooledAvatarEntry {
    AvatarPool *pOwner = nullptr;
    RenderAvatar Body;
    AvatarImpostor Impostor;

    AvatarClothe Slots[AC_Count];
    AvatarClothe CarBelt;
    AvatarSkinPart *pSkinResource = nullptr;
    AvatarEyePart *pEyeResource = nullptr;

    uint16_t pClothesHC[kNumClotheTypes][AC_Count] = {};
    uint8_t pColors[kNumClotheTypes][AC_Count] = {};
    bool bHasMotoGearsOnBike = false;
    AvatarStylePart *pStylePart = nullptr;
    int32_t ActiveClotheSet = 0;
    void *pUnknown = nullptr;

    eAvatarLoadingState BakerState = AVT_Unknown;

    void decreaseRenderRef();
    void tickEyeMaterials();
    void tickBodyMaterials();
};
