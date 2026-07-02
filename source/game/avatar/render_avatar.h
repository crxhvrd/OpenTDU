#pragma once

#include "shared.h"

struct AvatarDBEntry;
class Render2DM;
class Render2DB;
class RuntimeRender2DB;
class Render3DG;
class RenderTarget;
struct HiearchyNode;
struct FileCollection3D;
class LightSetupNode;
struct AvatarImpostor;
class SkeletonInstance;

enum eAvatarLoadingState : int32_t {
    AVT_Unknown = 0,
    AVT_Unloaded = 1,
    AVT_LoadPending = 2,
    AVT_LoadCompleted = 3,
    AVT_Ready = 4
};

struct RenderAvatarBase {
    AvatarDBEntry *pAvatarPart = nullptr;
    void *p2DMData = nullptr;
    Render2DM *p2DM = nullptr;
    void *p3DDData = nullptr;
    void *p3DD = nullptr;
    HiearchyNode *pHierarchyNode = nullptr;
    HiearchyNode *pHierarchyNodeLowRes = nullptr;
    Matrix4x4f_t Matrix;
    Matrix4x4f_t *pBoneArray = nullptr;
    void *pMemoryPool = nullptr;
    uint8_t *p2DMPool = nullptr;
    uint8_t *p3DDPool = nullptr;
    AvatarImpostor *pLowRes = nullptr;
    FileCollection3D *p3DManager = nullptr;
    int32_t Index = 0;
    LightSetupNode *pLightConfig = nullptr;
    bool bVisible = false;

    void tickEyeMaterial();
    void tickBodyMaterial();
};

struct AvatarLOD : RenderAvatarBase {
    Render3DG *p3DG = nullptr;
    eAvatarLoadingState LoadingState = AVT_Unknown;
    AvatarDBEntry *pAvatarPartInLoading = nullptr;
};

struct RenderAvatar : AvatarLOD {
    Render2DB *p2DBPilosity = nullptr;
    Render2DB *p2DBPilosityMask = nullptr;
    Render2DB *p2DBGloss = nullptr;
    Render2DB *p2DBTeeth = nullptr;
    Render2DB *p2DBEye = nullptr;
    Render2DB *p2DBOcclusion = nullptr;
    Render2DB *p2DBSpecular = nullptr;
    SkeletonInstance *pSkeleton = nullptr;
};

struct AvatarImpostor : AvatarLOD {
    RuntimeRender2DB *pFront = nullptr;
    RuntimeRender2DB *pBack = nullptr;
    RenderTarget *pRenderTargetFront = nullptr;
    RenderTarget *pRenderTargetBack = nullptr;
    int32_t bDirty = 0;
};
