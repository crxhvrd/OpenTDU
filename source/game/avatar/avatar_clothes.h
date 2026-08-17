#pragma once

#include <cstdint>
#include "avatar_bnk.h"
#include "avatar_configuration.h"
#include "render_avatar.h"

#include "render/file_collection_3d.h"
#include "render/gs_render_helper.h"
#include "render/light.h"
#include "render/setup_node.h"

struct AvatarStylePart;
struct AvatarSkinPart;
struct AvatarEyePart;

class AvatarPool;

static constexpr uint64_t kAvatarShaderHashcode = 0x183157b81c6c7357ull;
static constexpr uint32_t kAvatarShaderParameterID = 0xdf;

enum eAvatarLoadingPass : int32_t {
    ALP_NotUnloaded = 0,   // FUN_008f0490
    ALP_Synchronous = 1,   // FUN_008f03f0
    ALP_AsyncCritical = 2, // FUN_008f0330( true )
    ALP_Async = 3,         // FUN_008f0330( false )
};

enum eAvatarBankRequest : int32_t {
    ABR_Ready = 0,   // the bank is loaded and handed back in pLoadingBnk
    ABR_Failed = 1,  // the part has no bank, or the load failed
    ABR_Pending = 2, // no free slot yet, retry next tick
};

struct AvatarStreamingRequest {
    AvatarBnk *pLoadingBnk = nullptr;
    AvatarDBEntry *pPartInLoading = nullptr;
    AvatarDBEntry *pPartRequesting = nullptr;
    bool bAsync = false;
    eAvatarLoadingState State = AVT_Unknown;
    bool bCritical = false;

    bool step();
    void decreaseReferenceCount();

private:
    bool stepPendingRequest();
    eAvatarBankRequest requestBank();
};

// Bump allocator for avatar clothes
struct AvatarScratchPool {
    uint8_t *pMemory = nullptr;
    uint8_t *pCursor = nullptr;
    int32_t Size = 0;

    void allocate( int32_t size );
    void release();
};

struct AvatarClothe : RenderAvatarBase {
    AvatarStreamingRequest StreamingRequest;
    eAvatarLoadingState LoadingState = AVT_Unknown;
    AvatarScratchPool ScratchPoolA;
    AvatarScratchPool ScratchPoolB;

    using RenderAvatarBase::initialize;
    void initialize( int32_t index,
                     FileCollection3D *pCollection,
                     int32_t poolASize,
                     int32_t poolBSize,
                     LightSetupNode *pLightSetup,
                     ShaderParameterSetupNode *pShaderSetup );
    void terminate() override;
    bool installPart( AvatarDBEntry *pPart, AvatarBnk *pBnk ) override;
    void releaseCurrentPart() override;

    void stepLoading();
    void decreaseReferenceCount();

private:
    void installLoadedPart();
};

struct PooledAvatarEntry : public RenderDeviceResetListener {
    AvatarPool *pOwner = nullptr;
    FileCollection3D Collection3D;
    RenderAvatar Body;
    AvatarImpostor Impostor;

    AvatarClothe Slots[AC_Count];
    AvatarClothe CarBelt;
    AvatarSkinPart *pSkinResource = nullptr;
    RenderAvatar *pBodyHighRes = nullptr;
    AvatarEyePart *pEyeResource = nullptr;

    uint16_t pClothesHC[kNumClotheTypes][AC_Count] = {};
    uint8_t pColors[kNumClotheTypes][AC_Count] = {};
    bool bHasMotoGearsOnBike = false;
    AvatarStylePart *pStylePart = nullptr;
    int32_t ActiveClotheSet = kNoClotheSet;
    void *pUnknown = nullptr;

    eAvatarLoadingState BakerState = AVT_Unknown;

    Matrix4x4f_t Matrix;
    Matrix4x4f_t *pBoneArray = nullptr;
    bool bUsed = false;
    bool bVisible = false;
    bool bUpdatable = false;
    void *pSkeleton = nullptr;
    void *pAnimBuffer = nullptr;
    int32_t AnimBufferSize = 0;

    LightSetupNode LightConfig;
    DirectionalLight AvatarLight;
    ShaderParameterSetupNode ShaderConfig;
    ShaderParameter ShaderParams;
    Eigen::Vector4f ShaderParamData = Eigen::Vector4f::Zero();

    PooledAvatarEntry();
    ~PooledAvatarEntry();

    void initialize( int32_t index );
    void terminate();

    bool onRenderDeviceReset( int32_t reason ) override;

    void stepLoading( eAvatarLoadingPass pass );
    bool stepLoadingCompletion();
    eAvatarLoadingState getLoadingState() const;
    void updateBodyPartVisibility();
    void releaseParts();

    void decreaseRenderRef();
    void tickEyeMaterials();
    void tickBodyMaterials();

private:
    void stepBodyLoading();
};
