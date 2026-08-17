#pragma once

#include "shared.h"

#include "render/2dm.h"

struct AvatarDBEntry;
class Render2DB;
class RuntimeRender2DB;
class Render3DG;
class RenderTarget;
struct HiearchyNode;
struct FileCollection3D;
class LightSetupNode;
class ShaderParameterSetupNode;
struct AvatarImpostor;
class SkeletonInstance;
class AvatarBnk;

enum eAvatarLoadingState : int32_t {
    AVT_Unknown = 0,
    AVT_Unloaded = 1,
    AVT_LoadPending = 2,
    AVT_LoadCompleted = 3,
    AVT_Ready = 4
};
static constexpr int32_t kAvatarPartPoolSize = 0x2c00;
static constexpr int32_t kAvatarPart2DMPoolSize = 0x2800;
static constexpr int32_t kAvatarPart3DDPoolSize =
    kAvatarPartPoolSize - kAvatarPart2DMPoolSize;

struct RenderAvatarBase {
    AvatarDBEntry *pAvatarPart = nullptr;
    void *p2DMData = nullptr;
    Render2DM Material;
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
    ShaderParameterSetupNode *pShaderConfig = nullptr;
    bool bVisible = false;

    virtual ~RenderAvatarBase() {}

    virtual void initialize( int32_t index,
                             FileCollection3D *pCollection,
                             LightSetupNode *pLightSetup,
                             ShaderParameterSetupNode *pShaderSetup );
    virtual void terminate();

    virtual bool installPart( AvatarDBEntry *pPart, AvatarBnk *pBnk );
    virtual void releaseCurrentPart();

    void instanciateHierarchy();
    void releaseHierarchy();

    void tickEyeMaterial();
    void tickBodyMaterial();

protected:
    bool installRenderData( AvatarDBEntry *pPart, AvatarBnk *pBnk );
    void releaseRenderData();
};

struct AvatarLOD : RenderAvatarBase {
    Render3DG *p3DG = nullptr;
    eAvatarLoadingState LoadingState = AVT_Unknown;
    AvatarDBEntry *pAvatarPartInLoading = nullptr;

    bool installPart( AvatarDBEntry *pPart, AvatarBnk *pBnk ) override;
    void releaseCurrentPart() override;

    void stepLoading();
};

struct RenderAvatar : AvatarLOD {
    Render2DB *p2DBPilosity = nullptr;
    Render2DB *p2DBPilosityMask = nullptr;
    Render2DB *p2DBGloss = nullptr;
    Render2DB *p2DBTeeth = nullptr;
    Render2DB *p2DBEye = nullptr;
    Render2DB *p2DBOcclusion = nullptr;
    Render2DB *p2DBSpecular = nullptr;
    void *pSkeletonData = nullptr;
    SkeletonInstance *pSkeleton = nullptr;

    bool installPart( AvatarDBEntry *pPart, AvatarBnk *pBnk ) override;
    void releaseCurrentPart() override;
};

struct AvatarImpostor : AvatarLOD {
    RuntimeRender2DB *pFront = nullptr;
    RuntimeRender2DB *pBack = nullptr;
    RenderTarget *pRenderTargetFront = nullptr;
    RenderTarget *pRenderTargetBack = nullptr;
    int32_t bDirty = 0;

    void initialize( int32_t index,
                     FileCollection3D *pCollection,
                     LightSetupNode *pLightSetup,
                     ShaderParameterSetupNode *pShaderSetup ) override;
    void terminate() override;
};
