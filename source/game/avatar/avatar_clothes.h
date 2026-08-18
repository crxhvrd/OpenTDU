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
    void reset();

    void *allocateCopy( uint32_t size, const void *pSource );
};

enum eAvatarInstanceKind : int32_t {
    AIK_Referenced = 0, // the bank parsed it; the slot only points at it
    AIK_Owned = 1,      // the slot parsed its own copy out of pStream
    AIK_Free = 2,
};

struct AvatarRenderInstance {
    eAvatarInstanceKind Kind = AIK_Free;
    int32_t RefCount = 0;
    uint64_t Hashcode = 0;
    void *pStream = nullptr;
};

struct AvatarGeometryInstance : AvatarRenderInstance {
    Render3DG *pReferenced = nullptr;
    Render3DG Owned;

    inline Render3DG *getGeometry() {
        return ( Kind == AIK_Owned ) ? &Owned : pReferenced;
    }

    void parseOwnedStream();
    void release();
    bool reference( AvatarBnk *pBnk, uint64_t hashcode );
    bool copyFromBank( AvatarBnk *pBnk,
                       uint64_t hashcode,
                       AvatarScratchPool &allocator );
};

struct AvatarBitmapInstance : AvatarRenderInstance {
    Render2DB *pReferenced = nullptr;
    Render2DB Owned;

    inline Render2DB *getBitmap() {
        return ( Kind == AIK_Owned ) ? &Owned : pReferenced;
    }

    void parseOwnedStream();
    void release();
    bool reference( AvatarBnk *pBnk, uint64_t hashcode );
    bool copyFromBank( AvatarBnk *pBnk,
                       uint64_t hashcode,
                       AvatarScratchPool &allocator );
};

template <typename T> struct AvatarRenderInstancePool {
    static constexpr int32_t kNumSlots = 7;

    T Slots[kNumSlots];
    AvatarScratchPool Allocator;

    // FUN_00410ab0
    T *claim( AvatarBnk *pBnk, const uint64_t hashcode ) {
        if ( pBnk == nullptr ) {
            return nullptr;
        }

        for ( T &slot : Slots ) {
            if ( slot.RefCount > 0 && slot.Hashcode == hashcode ) {
                slot.parseOwnedStream();
                slot.RefCount++;
                return &slot;
            }
        }

        for ( T &slot : Slots ) {
            if ( slot.RefCount > 0 ) {
                continue;
            }

            if ( pBnk->hasInstalledResources() ) {
                if ( !slot.reference( pBnk, hashcode ) ) {
                    return nullptr;
                }
                slot.Kind = AIK_Referenced;
            } else {
                if ( !slot.copyFromBank( pBnk, hashcode, Allocator ) ) {
                    return nullptr;
                }
                slot.Kind = AIK_Owned;
            }

            slot.Hashcode = hashcode;
            slot.parseOwnedStream();
            slot.RefCount++;
            return &slot;
        }

        return nullptr;
    }

    static void Release( T *pSlot ) {
        if ( pSlot == nullptr ) {
            return;
        }

        pSlot->RefCount--;
        if ( pSlot->RefCount != 0 ) {
            return;
        }

        pSlot->release();
        pSlot->Kind = AIK_Free;
        pSlot->Hashcode = 0;
    }
};

struct AvatarClothe : RenderAvatarBase {
    static constexpr int32_t kMaxRenderInstances = 16;

    AvatarStreamingRequest StreamingRequest;
    eAvatarLoadingState LoadingState = AVT_Unknown;

    AvatarBitmapInstance *pTextures[kMaxRenderInstances] = {};
    int32_t NumTextures = 0;
    AvatarRenderInstancePool<AvatarBitmapInstance> TexturePool;

    AvatarGeometryInstance *pInstances[kMaxRenderInstances] = {};
    int32_t NumInstances = 0;
    AvatarRenderInstancePool<AvatarGeometryInstance> GeometryPool;
    uint8_t ColorIndex = 0;
    uint8_t NumColors = 0;

    using RenderAvatarBase::initialize;
    void initialize( int32_t index,
                     FileCollection3D *pCollection,
                     int32_t geometryPoolSize,
                     int32_t texturePoolSize,
                     LightSetupNode *pLightSetup,
                     ShaderParameterSetupNode *pShaderSetup );
    void terminate() override;

    AvatarDBEntry *getAvatarPart() override;

    bool installPart( AvatarDBEntry *pPart, AvatarBnk *pBnk ) override;
    void releaseCurrentPart() override;

    void registerRenderData() override;
    void bindPartTextures() override;
    void requestPart( AvatarDBEntry *pPart,
                      uint8_t colorIndex,
                      bool bAsync,
                      bool bCritical );

    void stepLoading();
    void decreaseReferenceCount();

private:
    void installLoadedPart();
    void applyColorSet( AvatarDBEntry *pPart, AvatarBnk *pBnk );
    void claimLayerTextures( AvatarBnk *pBnk, AvatarBnk *pCommonBnk );
};

struct PooledAvatarEntry : public RenderDeviceResetListener {
    AvatarPool *pOwner = nullptr;
    FileCollection3D Collection3D;
    RenderAvatar Body;
    AvatarImpostor Impostor;

    AvatarClothe Slots[AC_Count];
    AvatarClothe CarBelt;
    const float *pSkinTint = nullptr;
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
    void setClothe( int32_t category,
                    uint16_t clotheID,
                    uint8_t colorIndex,
                    int32_t clotheSet );
    void setClothesFromConfig( const AvatarConfiguration &config );

    bool installClothePart( AvatarDBEntry *pPart,
                            uint8_t colorIndex,
                            int32_t clotheSet,
                            bool bAsync,
                            bool bCritical );
    bool installBodyPart( AvatarDBEntry *pPart );

    void tickEyeMaterials();
    void tickBodyMaterials();

private:
    void stepBodyLoading();
};
