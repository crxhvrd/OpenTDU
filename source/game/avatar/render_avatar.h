#pragma once

#include "shared.h"

#include "core/color.h"
#include "render/2db.h"
#include "render/2dm.h"
#include "render/3dd.h"

struct AvatarDBEntry;
struct AvatarEnvMap;
struct AvatarEyePart;
struct RenderAvatar;
class Render2DB;
class Render3DG;
class RenderTarget;
struct HiearchyNode;
struct FileCollection3D;
class LightSetupNode;
class ShaderParameterSetupNode;
struct AvatarImpostor;
class SkeletonInstance;
class AvatarBnk;
class RenderScene;

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

struct AvatarLightAttenuation {
    float HeightBase = 0.0f;
    float HeightEnd = 0.0f;
    float Ambiant = 0.0f;
    float Diffuse = 0.0f;
    float Specular = 0.0f;
    float EnvFactor = 0.0f;
};

struct RenderAvatarBase {
    AvatarDBEntry *pAvatarPart = nullptr;
    void *p2DMData = nullptr;
    Render2DM Material;
    void *p3DDData = nullptr;
    Render3DD Scene;
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

    virtual AvatarDBEntry *getAvatarPart() { /* FUN_008f06d0 */
        return pAvatarPart;
    }

    virtual bool installPart( AvatarDBEntry *pPart, AvatarBnk *pBnk );
    virtual void releaseCurrentPart();

    virtual void registerRenderData();
    virtual void unregisterRenderData();

    virtual void bindPartTextures() = 0;

    void draw( RenderScene *pScene, const ColorRGBA &color );
    void applyLightingAttenuation( const AvatarLightAttenuation &attenuation );

    void instanciateHierarchy();
    void releaseHierarchy();
    void showLowResHierarchy();
    void hideLowResHierarchy();
    void setLowResVisible( bool bNewVisible );

    void setMatrix( const Matrix4x4f_t &matrix );

    void setMeshVisibility( uint64_t meshHashcode, bool bMeshVisible );
    void tickEyeMaterial( const AvatarEyePart *pEyeResource );
    void tickBodyMaterial( const float *pSkinTint, RenderAvatar *pBodyHighRes );
    void setEnvMap( const AvatarEnvMap *pEnvMap );

protected:
    bool installRenderData( AvatarDBEntry *pPart, AvatarBnk *pBnk );
    void releaseRenderData();
    void parseRenderData();
    void destroyRenderData();

private:
    HiearchyNode *instanciatePartHierarchy();
    void releaseHierarchyNodes();
};

struct AvatarLOD : RenderAvatarBase {
    Render3DG *p3DG = nullptr;
    eAvatarLoadingState LoadingState = AVT_Unknown;
    AvatarDBEntry *pAvatarPartInLoading = nullptr;

    AvatarDBEntry *getAvatarPart() override;

    bool installPart( AvatarDBEntry *pPart, AvatarBnk *pBnk ) override;
    void releaseCurrentPart() override;

    void registerRenderData() override;
    void unregisterRenderData() override;

    void stepLoading();
    void requestPart( AvatarDBEntry *pPart );
    void decreaseReferenceCount();
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

    void bindPartTextures() override;
};

struct AvatarImpostor : AvatarLOD {
    Render2DB Front;
    void *pFrontBuffer = nullptr;
    Render2DB Back;
    void *pBackBuffer = nullptr;
    RenderTarget *pRenderTargetFront = nullptr;
    RenderTarget *pRenderTargetBack = nullptr;
    int32_t bDirty = 0;

    void initialize( int32_t index,
                     FileCollection3D *pCollection,
                     LightSetupNode *pLightSetup,
                     ShaderParameterSetupNode *pShaderSetup ) override;
    void terminate() override;

    void bindPartTextures() override;

    void updateSceneVisibility();
};
