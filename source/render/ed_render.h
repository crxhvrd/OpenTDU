#pragma once

#include "core/ed_base.h"
#include "core/ed_std_allocator.h"

#include "gs_render_helper.h"

struct Material;
struct RenderBucket;
class RenderDevice;
class FrameGraph;

class Render2DB;
class Render2DM;
class RenderUVA;
class Render3DD;
class Render3DG;
class Instance;
struct HiearchyNode;

class RenderTarget;

using PresentCallback_t = std::function<void( void )>;
typedef int32_t ( *RenderBucketSelector_t )( struct Material * );

struct BucketConfiguration {
    int32_t NumBuckets;
    int32_t MaxNumSceneSetupCommand;
    int32_t MaxNumDrawCommandMaterial;
    int32_t MaxNumTransformMatrixCommand;
    int32_t MaxNumDrawCommand;

    const RenderBucket *pBuckets;
    RenderBucketSelector_t pBucketSelector;
};

struct FrameGraphConfig {
    int32_t MaxNumObject;
    int32_t MaxNumSceneData;
    int32_t MaxNumDrawList;
    int32_t MaxNumInstance;
    int32_t MaxNumInstanceWithCustomMaterial;
    int32_t MaxNumInstanceArray;
    int32_t MaxNumHiearchyNode;
};

struct ShaderBinaryCacheConfig {
    uint32_t TableEntrySize;
    uint32_t MaxTablePoolSize;
    uint32_t MaxNumConstantMap;
    uint32_t MaxNumPS;
    uint32_t MaxNumVS;
    uint32_t MaxNumResourceMap;
};

struct edRenderConfig {
    BucketConfiguration Buckets;
    FrameGraphConfig FrameGraph;
    ShaderBinaryCacheConfig ShaderBinaryCache;

    void *pMemoryAllocator;
    void *pMemoryPool;
    void *pWindowHandle;
    uint32_t MemoryPoolSize;
    uint32_t MaxNum3DD;
    uint32_t MaxNum3DG;
    uint32_t MaxNum2DM;
    uint32_t MaxNum2DB;
    uint32_t MaxNumUVA;
    uint32_t MaxNumInstance;
    uint32_t MaxNumHiearchyNode;
    uint32_t MaxNumSetupNode;
    uint32_t MaxNumLightNode;
    uint32_t MaxNumPostFX;
    uint32_t CoreID;
    uint32_t WindowWidth;
    uint32_t WindowHeight;

    uint32_t field1_0x18;
    uint32_t field2_0x1c;
    uint32_t MemoryPoolCapacity[3];

    uint8_t bUseTestDriveHash : 1;
    uint8_t bAllowGPUCapture : 1;
    uint8_t bAllocateZBufferForMainWindow : 1;

    edRenderConfig();
};

struct edRenderAllocator {
    void *pHead;
    void *pTail;
    // NOTE: Original was 32bit
    uint64_t Capacity;
    uint64_t Usage;
};

struct edRenderAllocatorSubAllocated : public edRenderAllocator {
    edRenderAllocator *pParent;
};

class edRender : public edBase {
public:
    RenderDevice *pRenderDevice;
    FrameGraph *pFrameGraph;
    PresentCallback_t pCallbackBeforePresent;
    PresentCallback_t pCallbackAfterPresent;
    PresentCallback_t pUnknownCallback;
    uint32_t DeviceState;
    float LODFactor;
    float Time;
    edRenderConfig Config;
    Eigen::Vector4f Uniforms[kMaxNumCustomUniforms];
    // OTDU_ADDITION: active weather night parameters, written to registers no
    // retail shader uses (kNightParamsRegisterVS / PS).
    Eigen::Vector4f NightParams = Eigen::Vector4f( 1.0f, 1.0f, 0.0f, 0.0f );
    // OTDU_ADDITION: night lamps (otdu_lights.h): render space position +
    // radius, color + fade weight; lamp intensity comes from the weather
    // (LampParams.x).
    static constexpr uint32_t kMaxNumLamps = 32;
    Eigen::Vector4f LampPositions[kMaxNumLamps];
    Eigen::Vector4f LampColors[kMaxNumLamps];
    uint32_t NumLamps = 0;
    Eigen::Vector4f LampParams = Eigen::Vector4f::Zero();
    // OTDU_ADDITION: car headlights (spot lights): render space position +
    // range, direction + cos(outer half angle), color + cos(inner half angle).
    static constexpr uint32_t kMaxNumHeadlights = 8;
    Eigen::Vector4f HeadlightPositions[kMaxNumHeadlights];
    Eigen::Vector4f HeadlightDirections[kMaxNumHeadlights];
    Eigen::Vector4f HeadlightColors[kMaxNumHeadlights];
    uint32_t NumHeadlights = 0;
    float HeadlightIntensity = 0.0f;
    // OTDU_ADDITION: direction toward the moon (the night "sun"), world space.
    Eigen::Vector4f MoonDirection = Eigen::Vector4f( 0.0f, 1.0f, 0.0f, 0.0f );

public:
    edRender();
    ~edRender();

    virtual int32_t initialize() override;
    virtual void destroy() override;
    void clearDevice();

    int32_t getRequiredMem();
    bool doPoolReallocations();
    void doPoolDeallocations();

    int32_t flushDrawCommands( bool param_1 );
    void resetPoolMemoryAllocation();

    void releaseRendererResources();
    void FUN_005f5720();
    void FUN_005129b0();
    void popDefaultLight();
    void destroyVertexLayoutCache();

    bool selectAdapterAndCreateDevice();
    void createUnknownTextures();
    void freeUnknownTextures();

    void present();
    void bindRenderTarget( RenderTarget *pRenderTarget,
                           const uint32_t index,
                           const bool bBlitBound2DB );
    void bindToFramebuffer( RenderTarget *pRenderTarget, const uint32_t index );
    void invalidateRenderTargetBindings();
    void blitBound2DBToRT( RenderTarget *pRenderTarget );
    RenderTarget *getBoundRenderTargetAtIndex( uint32_t param_1 );
    void bindRenderTargetAndSetViewport( RenderTarget *pRenderTarget,
                                         const uint32_t index );

    GPUVertexLayout *createVertexLayout( VertexLayoutAttribute *param_1,
                                         int32_t numAttributes,
                                         bool bInterleavedAttributes );
    bool doesFormatSupportsMSAA( eAntiAliasingMethod msaaMode,
                                 eViewFormat format );
    bool doesFormatSupportsBlending( eViewFormat format );

    bool applyVideoSettingsChange( const int32_t width,
                                   const int32_t height,
                                   const eVSyncMode vsync,
                                   const eAntiAliasingMethod method );

private:
    uint32_t
    getRequiredMemRender2D( int32_t param_1, int32_t param_2, int32_t param_3 );
    uint32_t getRequiredMemRender3D( int32_t param_1,
                                     int32_t param_2,
                                     int32_t param_3,
                                     int32_t param_4 );
    uint32_t getRequiredMemSceneRenderer( FrameGraphConfig *param_1 );
    uint32_t getRequiredMemRenderBucket( BucketConfiguration *param_1 );
    uint32_t getRequiredMemRenderPoolAndShaderReg();
    uint32_t getRequiredMemPoolUsage( uint32_t *param_1 );
    uint32_t getRequiredMemShaderBinaryCache();

    bool initializeLogicalDevice();
    bool onLogicalDeviceCreation( edRenderConfig *param_1 );
    bool createRenderDevice();
    bool createDeviceResources();
    bool initializeRenderers();
    bool initializeShaderCache();

    void clearVertexLayoutCache();
    void createDefaultInfiniteLight( void *param_1 );
    bool initializeGPUBackend();
};

extern edRenderAllocator gRenderAllocator;
extern edRender gEdRender;

extern edStdAllocator<Render3DD *> g3DDAllocator;
extern edStdAllocator<Render3DG *> g3DGAllocator;
extern edStdAllocator<Instance> gInstanceAllocator;
extern edStdAllocator<HiearchyNode> gHiearchyNodeAllocator;

extern edStdAllocator<Render2DM *> g2DMAllocator;
extern edStdAllocator<Render2DB *> g2DBAllocator;
extern edStdAllocator<RenderUVA *> gUVAAllocator;

extern FramebufferAttachments pBoundRenderTargets;

void UploadConstant( const struct ConstantMap *pMap,
                     uint32_t offsetInMap,
                     float *pScalars,
                     uint32_t numVectors );
void *AllocateFromRenderPool( size_t param_1 );
struct Texture *GetFallbackTexture();
