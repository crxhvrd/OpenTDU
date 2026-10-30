#pragma once

#if OTDU_VULKAN
#ifndef VK_NO_PROTOTYPES
#define VK_NO_PROTOTYPES 1
#endif

#if defined( OTDU_WIN32 )
#define VK_USE_PLATFORM_WIN32_KHR 1
#elif defined( OTDU_MACOS )
#define VK_USE_PLATFORM_MACOS_MVK 1
#endif

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>

#include "render/gs_render_helper.h"
#include "vk_mem_alloc.h"
#include "volk.h"

#include "gamma_ramp_pass.h"
#include "pso_factory.h"
#include "pso_library.h"
#include "render_pass_manager.h"
#include "sampler_heap_manager.h"

// TODO: Can we find those constants in dxvk??
static constexpr uint32_t kVulkanSRVStartBindingSlot = 13;
static constexpr uint32_t kVulkanMaxNumSRV =
    8; // TODO: Pretty sure this is higher (highest register used so far is 18
       // so that's good enough)

class RenderTarget;
class Render2DB;
struct ShaderMetadataSPIRVRuntime;

struct GPUBuffer {
    VkBuffer NativeBuffer[PendingFrameCount] = { VK_NULL_HANDLE, VK_NULL_HANDLE,
                                                 VK_NULL_HANDLE };
    VmaAllocation Allocation[PendingFrameCount] = { nullptr, nullptr, nullptr };
    uint32_t NumResources = 1; // 1 if static; 3 if dynamic
    uint32_t ByteSize = 0;

    union {
        uint32_t Stride;       // VB
        VkIndexType IndexType; // IB
    };

    // Dynamic buffers emulate a single persistent D3D9 buffer: locks write to
    // this CPU copy, and each per-frame resource re-syncs the bytes it missed
    // (StaleStart/StaleEnd) before the GPU reads it.
    std::vector<uint8_t> ShadowCopy;
    uint32_t LockStart = 0;
    uint32_t LockEnd = 0;
    uint32_t StaleStart[PendingFrameCount] = { ~0u, ~0u, ~0u };
    uint32_t StaleEnd[PendingFrameCount] = { 0u, 0u, 0u };
};

// dxvk push constants for D3D9 RS emulation (copied from dxvk)
struct D3D9RenderStateInfo {
    // For dxvk compatibility sake (not used by the game AFAIK; as they use
    // their own impl)
    std::array<float, 3> fogColor = {};
    float fogScale = 0.0f;
    float fogEnd = 1.0f;
    float fogDensity = 1.0f;

    uint32_t alphaRef = 0u;

    union {
        float pointSize = 1.0f;
        uint32_t pointSizeInt;
    };

    float pointSizeMin = 1.0f;
    float pointSizeMax = 64.0f;
    float pointScaleA = 1.0f;
    float pointScaleB = 0.0f;
    float pointScaleC = 0.0f;
};

struct QueueInfo {
    uint32_t familyIndex =
        ~0U; // Family index of the queue (graphic, compute, transfer, ...)
    uint32_t queueIndex = ~0U; // Index of the queue in the family
    VkQueue queue{};           // The queue object
};

struct GPUTexture {
    // Allocations for this render target. To get access to the image used for
    // the current frame, retrieve the internal frame index using
    // RenderDevice::getInternalFrameIndex().
    VkImage Image[PendingFrameCount] = { VK_NULL_HANDLE, VK_NULL_HANDLE,
                                         VK_NULL_HANDLE };
    VmaAllocation Allocation[PendingFrameCount] = { nullptr, nullptr, nullptr };

    // TODO: For now we assume that RTs will always use its original view
    // format.
    VkImageView ImageView[PendingFrameCount] = { VK_NULL_HANDLE, VK_NULL_HANDLE,
                                                 VK_NULL_HANDLE };
    VkImageLayout CurrentLayout[PendingFrameCount] = {
        VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_UNDEFINED,
        VK_IMAGE_LAYOUT_UNDEFINED };
    uint64_t ViewIds[PendingFrameCount] = { 0ull, 0ull, 0ull };

    VkFormat ViewFormat;
    VkSampleCountFlagBits SampleCount;
    uint32_t NumMips;
    uint32_t Depth;
    uint32_t Width = 0;
    uint32_t Height = 0;
    uint32_t NumResources = 3;
    uint8_t bNeedMSAAResolve : 1; // If true, resolve the texture the next time
                                  // it's used in a render pass

    inline uint32_t getResourceIndex( const uint32_t frameIndex ) const {
        return ( frameIndex < NumResources ) ? frameIndex
                                             : ( NumResources - 1 );
    }
};

struct GPUShader {
    VkShaderModule ShaderModule;
    const ShaderMetadataSPIRVRuntime *pMetadata;
    uint64_t SRVMask;  // Mask for texture binding (extracted from metadata)
    uint64_t Hashcode; // ShaderTable hashcode (for debug purpose)
};

// Per-attribute semantic info stored in GPUVertexLayout. The SPIRV location is
// determined at PSO-build time by matching against the active VS's ISGN.
struct GPUVertexLayoutSemantic {
    uint32_t Binding; // vertex buffer slot (= stream index)
    uint32_t Offset;  // byte offset within that buffer
    VkFormat Format;
    uint8_t Usage; // D3DDECLUSAGE value
    uint8_t UsageIndex;
};

struct GPUVertexLayout {
    std::vector<GPUVertexLayoutSemantic> Semantics; // per-attribute info
    std::vector<VkVertexInputBindingDescription>
        InputBindings;        // per-binding desc
    uint64_t Hashcode = 0ull; // Content hash (see updateHashcode)
    bool bHasInstanceRateBinding = false;

    // Must be called once Semantics/InputBindings are filled
    void updateHashcode();
};

// ITU-R BT.2408 reference (graphics) white
static constexpr float kDefaultHDRPaperWhiteNits = 203.0f;
static constexpr float kMinHDRPaperWhiteNits = 80.0f;
static constexpr float kMaxHDRPaperWhiteNits = 500.0f;
static constexpr float kDefaultHDRPeakNits = 1000.0f;
static constexpr float kMinHDRPeakNits = 400.0f;
static constexpr float kMaxHDRPeakNits = 10000.0f;
// Peak setting that follows the display headroom reported by the OS
static constexpr float kAutoHDRPeakNits = 0.0f;
static constexpr float kDefaultHDRHighlightStrength = 1.0f;
static constexpr float kMaxHDRHighlightStrength = 2.0f;

struct VulkanDeviceCaps {
    uint32_t ApiVersion = 0;
    bool bDemoteExtension = false; // VK_EXT_shader_demote_to_helper_invocation
    bool bExtendedDynamicState = false; // Core 1.3 or extension
    bool bExtendedDynamicStateExtension = false;
    bool bDepthClamp = false;
    bool bFillModeNonSolid = false;
    bool bSamplerAnisotropy = false;
    float MaxSamplerAnisotropy = 1.0f;
    bool bMaintenance4 = false;
    bool bHDRMetadata = false;
    bool bPortabilitySubset = false;
    bool bUpdateUnusedWhilePending = false;
    bool bMultiDrawIndirect = false;
    uint32_t MaxDrawIndirectCount = 1;
};

class RenderDevice {
public:
    inline uint32_t getInternalFrameIndex() const {
        return frameIndex % PendingFrameCount;
    }
    inline void setTime( float param_2 ) { time = param_2; }

    inline bool isInitialized() const { return device != VK_NULL_HANDLE; }
    inline bool isDeviceLost() const {
        return bDeviceLost.load( std::memory_order_acquire );
    }

    inline GammaRampPass &getGammaRampPass() { return gammaRampPass; }

    inline bool isHDROutputEnabled() const { return bHDROutputEnabled; }
    inline bool isHDROutputSupported() const { return bHDROutputSupported; }
    // True as soon as HDR output is enabled on a capable display, before the
    // swapchain is recreated (resources sized for it can be allocated early)
    inline bool isHDROutputRequested() const {
        return bHDROutputEnabled && bHDROutputSupported;
    }
    inline bool isHDROutputActive() const {
        return activeOutputTransfer != eOutputTransfer::OT_SDR;
    }
    inline float getHDRPaperWhiteNits() const { return hdrPaperWhiteNits; }
    inline float getHDRPeakNits() const { return hdrPeakNits; }
    inline bool isHDRPeakAuto() const { return hdrPeakNits <= 0.0f; }
    // 0 when the OS reports no headroom above the SDR white
    inline float getDetectedHDRPeakNits() const { return detectedPeakNits; }
    inline float getEffectiveHDRPeakNits() const {
        if ( !isHDRPeakAuto() ) {
            return hdrPeakNits;
        }
        return ( detectedPeakNits > 0.0f ) ? detectedPeakNits
                                           : kDefaultHDRPeakNits;
    }
    inline float getHDRHighlightStrength() const {
        return hdrHighlightStrength;
    }

public:
    RenderDevice();
    ~RenderDevice();

    void initialize();

    GPUTexture *createTexture( struct Texture *pTexture );
    GPUTexture *createRenderTarget( const uint32_t width,
                                    const uint32_t height,
                                    const eViewFormat format,
                                    const eAntiAliasingMethod msaaSampleCount );
    GPUBuffer *createBuffer( const struct GPUBufferDesc *desc );
    GPUShader *createShader( eShaderType type,
                             const void *pBytecode,
                             const size_t bytecodeSize,
                             const ShaderMetadataSPIRVRuntime *pMetadata,
                             const uint64_t hashcode );
    GPUVertexLayout *
    createVertexLayout( VertexLayoutAttribute *pAttributes,
                        const bool bInterleavedAttributes = false );

    void *lockBuffer( GPUBuffer *pBuffer,
                      const uint32_t offset,
                      const uint32_t size,
                      const bool bDiscardContent );
    void unlockBuffer( GPUBuffer *pBuffer );

    void generateMipchain( Texture *pTexture );
    void generateMipchainInline( Texture *pTexture );

    void destroyTexture( GPUTexture *pTexture );

private:
    void recordMipchainGeneration( Texture *pTexture,
                                   VkCommandBuffer cmdBuffer );

public:
    size_t prebuildRecordedPSOs( const size_t maxCount );
    inline void flushPSOCaches() {
        savePipelineCache();
        savePSOLibrary();
    }
    inline size_t getNumCachedPSOs() const {
        return psoFactory.getNumCachedPSOs();
    }
    inline size_t getNumRecordedPSOs() const {
        return psoLibrary.getRecords().size() + psoSeed.getRecords().size();
    }

    void destroyBuffer( GPUBuffer *pBuffer );
    void destroyShader( GPUShader *pShader );
    void destroyVertexLayout( GPUVertexLayout *pVertexLayout );

    bool resize( uint32_t width, uint32_t height );
    void setVSyncEnabled( const bool bEnabled );
    void setHDROutput( const bool bEnabled, const float paperWhiteNits );
    void setHDRPeakNits( const float peakNits );
    void setHDRHighlightStrength( const float strength );
    void updateDisplayPeak();
    void waitForIdle();
    void flushAndWaitForIdle();

    GPUBackbuffer *getBackbuffer();
    FormatCapabilities getFormatCapabilities( eViewFormat format );

    void blitBound2DBToRT( RenderTarget *pRenderTarget ); // FUN_005fd1d0

    void setViewport( Viewport &vp );
    void setScissor( Scissor &scissor );

    bool retrieveAdapterInfos( const uint32_t adapterIndex,
                               struct GPUAdapterDesc *pOutDesc );

    // D3D9 Emulation (might want to remove this later and treat this as a
    // regular cbuffer).
    void setFloatConstants( eShaderType stage,
                            float *pFloats,
                            const uint32_t offset,
                            const uint32_t numVectors );

    void bindTexture( Texture *pTexture, const uint32_t index );

    // Clear ALL attachments (mirrors D3D9 clear behavior). Single RT clear
    // should be implemented in the future.
    void clearFramebuffer( const ColorRGBA clearColor,
                           const float clearDepth,
                           const uint32_t clearStencil,
                           const bool bClearColor,
                           const bool bClearDepth,
                           const bool bClearStencil );

    void clearColorRenderTarget( GPUTexture *pTexture,
                                 const ColorRGBA clearColor,
                                 const ClearRegion &clearRegion );

    void bindVertexLayout( GPUVertexLayout *pLayout );
    void bindVertexBuffer( GPUBuffer *pBuffer,
                           const uint32_t index,
                           const uint32_t stride,
                           const uint32_t offset );
    void draw( ePrimitiveType primitiveType,
               uint32_t numVertex,
               uint32_t numInstance,
               uint32_t firstVertex = 0 );
    void drawIndexedPrimitive( ePrimitiveType primitiveType,
                               int32_t baseVertexIndex,
                               uint32_t minVertexIndex,
                               uint32_t numVertices,
                               uint32_t startIndex,
                               uint32_t primitiveCount );

    void
    blit( Render2DB *pSrc, RenderTarget *pDst, const bool bLinearFiltering );
    void
    blit( GPUTexture *pSrc, RenderTarget *pDst, const bool bLinearFiltering );

    void resetCachedStates();

    void present();
    void setGammaRamp( const GammaRamp &ramp );

    bool isFramebufferUsingMSAA() const;
    void setMSAAState( bool param_1 );

    void bumpInternalFrameIndex();
    void setCullMode( eCullMode param_1 );

    bool
    isVertexAttributeFormatSupported( eVertexAttributeFormat format ) const;

    void setStreamFrequency( int32_t param_1, int32_t param_3 );
    void setColorWriteChannels( bool bWriteRed,
                                bool bWriteGreen,
                                bool bWriteBlue,
                                bool bWriteAlpha );

    void setClipPlaneEnabled( uint32_t param_1 );
    void bindIndexBuffer( GPUBuffer *param_1 );

    void setPointSize( uint32_t param_1 );

    bool retrieveDeviceCapabilities();

    void bindVertexShader( GPUShader *param_1 );
    void bindPixelShader( GPUShader *param_1 );

    void setRasterState( uint32_t param_1, uint32_t param_2 );
    void setSamplerState( const SamplerDesc &samplerDesc, uint32_t index );

    void setFillMode( eFillMode param_1 );
    void setAlphaBlendingEnabled( bool param_1 );
    void setAlphaRef( uint8_t param_1 );
    void setAlphaTestEnabled( bool param_1 );
    void setAlphaTestFunc( eComparisonFunction param_1 );
    void setStencilEnabled( bool param_1 );
    void setTwoSidedStencilEnabled( bool param_1 );

    void setStencilFail( eStencilFunction param_1 );
    void setStencilZFail( eStencilFunction param_1 );
    void setStencilPass( eStencilFunction param_1 );
    void setStencilFunc( eComparisonFunction param_1 );

    void setStencilRef( uint32_t param_1 );
    void setStencilMask( uint32_t param_1 );
    void setStencilWriteMask( uint32_t param_1 );

    void setCCWStencilFail( eStencilFunction param_1 );
    void setCCWStencilZFail( eStencilFunction param_1 );
    void setCCWStencilPass( eStencilFunction param_1 );
    void setCCWStencilFunc( eComparisonFunction param_1 );

    void setDepthWrite( bool param_1 );

    // Upload whole texture to GPU (all mips/slices stored in 2DB)
    bool uploadTexture( Texture *pTexture );

    // Upload texels to a specific/single mip level.
    void uploadTexels( Texture *pTexture,
                       const void *pTexels,
                       const size_t texelsSize,
                       const uint32_t mipIndex = 0 );

    void beginScene();
    void endScene();

    void bindRenderTarget( GPUTexture *param_1, uint32_t param_2 );
    void bindDepthStencil( GPUTexture *param_1 );

    void setClipPlane( int32_t param_1, float *param_2 );
    void resolveMSAA( RenderTarget *param_1, uint32_t param_2 );

    void setDepthCompareFunc( eComparisonFunction param_1 );
    void setDepthStencilEnabled( bool param_1 );

    void setBlendSrc( eBlendMode param_1 );
    void setBlendDst( eBlendMode param_1 );
    void setBlendOP( eBlendOP param_1 );

    void pushDebugLabel( const char *pLabel );
    void popDebugLabel();
    bool doesFormatSupportsMSAA( eAntiAliasingMethod msaaMode,
                                 eViewFormat format );
    bool doesFormatSupportsBlending( eViewFormat format );
    VkSampleCountFlagBits
    clampSampleCount( VkSampleCountFlagBits requested ) const;
    bool isMultisampled( const GPUTexture *pTexture ) const;
    bool copyBackbufferToHost( std::vector<uint8_t> &texels,
                               uint32_t &outWidth,
                               uint32_t &outHeight,
                               eViewFormat &outFormat );

#if OTDU_DEVBUILD
    void requestRenderTargetDump( GPUTexture *pTexture, const char *pFilePath );
    // Copies the render target as it is at this point of the frame; written to
    // '<pFilePath>_<width>x<height>_<VkFormat>.raw' once the frame completed
    void snapshotRenderTarget( GPUTexture *pTexture, const char *pFilePath );
    // Fills a region of an RGBA16F render target with a solid color at this
    // point of the frame
    void fillRenderTargetRegion( GPUTexture *pTexture,
                                 const VkOffset2D offset,
                                 const VkExtent2D extent,
                                 const float color[4] );
#endif

#if OTDU_DEVBUILD
private:
    struct PendingRenderTargetDump {
        GPUTexture *pTexture;
        std::string FilePath;
    };

    struct PendingRenderTargetSnapshot {
        VkBuffer Buffer;
        VmaAllocation Allocation;
        VkDeviceSize Size;
        // Empty for an upload buffer (only released)
        std::string FilePath;
    };

    VkBuffer createHostVisibleBuffer( const VkDeviceSize size,
                                      const VkBufferUsageFlags usage,
                                      VmaAllocation &outAllocation );
#endif

private:
    VkInstance instance;
    std::vector<const char *> instanceExtensions;
    std::vector<const char *> instanceLayers;
    std::vector<VkExtensionProperties> instanceExtensionsAvailable;
    std::vector<VkExtensionProperties> deviceExtensionsAvailable;

    std::vector<VkPhysicalDevice> physicalDevices;
    VkPhysicalDevice physicalDevice;
    uint32_t physicalDeviceIndex;
    std::vector<const char *> deviceExtensions;
    VkPhysicalDeviceProperties2 deviceProperties;
    VulkanDeviceCaps deviceCaps;
    PFN_vkCmdSetCullMode cmdSetCullMode = nullptr;
    PFN_vkCmdSetDepthTestEnable cmdSetDepthTestEnable = nullptr;
    PFN_vkCmdSetDepthWriteEnable cmdSetDepthWriteEnable = nullptr;
    PFN_vkCmdSetDepthCompareOp cmdSetDepthCompareOp = nullptr;
    PFN_vkCmdSetStencilTestEnable cmdSetStencilTestEnable = nullptr;
    PFN_vkCmdSetStencilOp cmdSetStencilOp = nullptr;

    VkDevice device;
    std::vector<QueueInfo> queues;

#if OTDU_DEVBUILD
    VkDebugUtilsMessengerEXT debugCallback;
#endif

    VmaAllocator allocator;
    VkSurfaceKHR surface;

    uint32_t frameIndex;
    uint32_t absoluteFrameIndex;

    FormatCapabilities formatCaps[eViewFormat::VF_Count];
    uint32_t bufferFormatSupport; // DAT_00fada5c

    VkFramebufferCreateInfo framebufferInfos;

    VkSwapchainKHR swapchain;
    VkFormat swapchainImageFormat;
    std::vector<VkImage> swapchainImages;
    std::vector<VkImageView> swapchainImageViews;
    VkSemaphore nextImageSemaphore[PendingFrameCount];

    // One per swapchain image: the presentation engine may still hold the
    // semaphore of an image after its frame fence signaled
    std::vector<VkSemaphore> presentSemaphores;
    VkFence inFlightFence[PendingFrameCount];

    float time;

    uint32_t activeColorMask;

    PSOFactory psoFactory;
    RenderPassManager renderPassManager;
    std::array<VkPipelineColorBlendAttachmentState, kMaxSimultaneousRT>
        blendOpAttachments;

    SamplerHeapManager samplerHeap;
    uint32_t boundSamplers[8];

    GammaRampPass gammaRampPass;

    GPUBuffer *pIndexBuffer;
#if defined( OTDU_MACOS )
    GPUBuffer *pFanEmulationIndexBuffer = nullptr;
    void drawTriangleFanAsList( int32_t baseVertexIndex,
                                uint32_t primitiveCount );
#endif
    VkBuffer vertexBuffers[0x10];
    VkDeviceSize vertexBufferOffsets[0x10];

    // Bindings last recorded into the active command buffer (redundant binds
    // are skipped)
    struct CommandBufferBindings {
        VkBuffer VertexBuffers[0x10];
        VkDeviceSize VertexBufferOffsets[0x10];
        int32_t NumVertexBuffers;

        VkBuffer IndexBuffer;
        VkIndexType IndexType;

        D3D9RenderStateInfo RenderStates;
        bool bRenderStatesValid;
        uint32_t Samplers[8];
        uint32_t SamplersSize;

        bool bStencilDynamicStateValid;
        VkStencilOpState StencilFront;
        VkStencilOpState StencilBack;

        bool bExtendedDynamicStateValid;
        VkCullModeFlags CullMode;
        VkBool32 DepthTestEnable;
        VkBool32 DepthWriteEnable;
        VkCompareOp DepthCompareOp;
        VkBool32 StencilTestEnable;
    };
    CommandBufferBindings recordedBindings;

    struct FramebufferKey {
        std::array<VkImageView, kMaxSimultaneousRT> Attachments;
        uint32_t NumAttachments;
        VkRenderPass RenderPass;
        uint32_t Width;
        uint32_t Height;

        inline bool operator==( const FramebufferKey &other ) const {
            return Attachments == other.Attachments &&
                   NumAttachments == other.NumAttachments &&
                   RenderPass == other.RenderPass && Width == other.Width &&
                   Height == other.Height;
        }
    };
    FramebufferKey activeFramebufferKey;

    GPUBackbuffer backbuffer;

    uint64_t nextViewId = 1ull;
    inline uint64_t allocateViewId() { return nextViewId++; }

    VkImage fallbackSRVImage = VK_NULL_HANDLE;
    VmaAllocation fallbackSRVAllocation = nullptr;
    VkImageView fallbackSRVView = VK_NULL_HANDLE;
    void createFallbackSRV();

    enum ePreFrameCommandList : uint32_t {
        PFCL_TextureUpload = 0,
        PFCL_MipchainGen = 1,
        PFCL_Count
    };
    struct PreFrameCommands {
        VkCommandPool Pool = VK_NULL_HANDLE;
        VkCommandBuffer CommandBuffers[PFCL_Count] = { VK_NULL_HANDLE,
                                                       VK_NULL_HANDLE };
        bool bRecording[PFCL_Count] = { false, false };
        // Submission which consumed the commands (0 if the pool is free)
        uint64_t Serial = 0ull;
    };
    static constexpr uint32_t kNumPreFrameCommands = PendingFrameCount + 1;
    PreFrameCommands preFrameCommands[kNumPreFrameCommands];
    uint32_t activePreFrameCommands = 0;

    VkCommandPool framePools[PendingFrameCount];
    VkCommandBuffer commandBuffer;
    VkCommandBuffer commandBuffers[PendingFrameCount];

    struct DrawBatch {
        VkDrawIndexedIndirectCommand First; // emitted directly if alone
        uint32_t Count = 0u;
        uint32_t FirstIndirectCommand = 0u;
    };
    DrawBatch pendingDrawBatch;

    struct IndirectDrawBuffer {
        VkBuffer Buffer = VK_NULL_HANDLE;
        VmaAllocation Allocation = nullptr;
        VkDrawIndexedIndirectCommand *pCommands = nullptr;
        uint32_t NumCommands = 0u;
    };
    IndirectDrawBuffer indirectDrawBuffers[PendingFrameCount];
    bool bDrawBatching = false;
    bool bMultiDrawBatching = false;

    VkCommandPool presentPools[PendingFrameCount];
    VkCommandBuffer presentCommandBuffers[PendingFrameCount];

    GPUTexture *pFramebufferAttachments[kMaxSimultaneousRT];
    GPUTexture *pDepthStencilAttachment;

    bool bFramebufferAttachmentValid[kMaxSimultaneousRT];
    bool bDepthStencilAttachmentValid;

    uint8_t bIsMainRTUsingMSAA : 1;
    uint8_t bDepthStencilEnabled : 1;
    uint8_t bRecordingCommands : 1;
    uint8_t bNeedRecreateSwapchain : 1;
    uint8_t bVSyncEnabled : 1;
    uint8_t bNeedBackbufferClear : 1;
    uint8_t bHDROutputEnabled : 1;
    uint8_t bSwapchainColorSpaceSupported : 1;
    uint8_t bHDRMetadataSupported : 1;
    uint8_t bHDROutputSupported : 1;

    float hdrPaperWhiteNits;
    float hdrPeakNits;
    float detectedPeakNits;
    float hdrHighlightStrength;
    eOutputTransfer activeOutputTransfer;

    VkPipelineCache psoCache = VK_NULL_HANDLE;
    std::filesystem::path psoCachePath;
    size_t numPSOsAtLastCacheSave = 0;
    size_t psoCacheSizeOnDisk = 0;
    size_t numPSOsAtLastCheck = 0;
    std::chrono::steady_clock::time_point lastPSOCreationTime;

    PSOLibrary psoLibrary;
    std::filesystem::path psoLibraryPath;
    PSOFactory prebuildFactory;
    bool bPrebuildFactoryInitialized = false;
    size_t nextPrebuildRecord = 0;
    PSOLibrary psoSeed;
    size_t nextPrebuildSeedRecord = 0;
    size_t numSkippedSeedRecords = 0;
    std::unordered_map<uint64_t, GPUShader *> prebuildShaders;
    std::vector<uint64_t> stalePSORecords;
    std::vector<std::pair<uint64_t, uint64_t>> rehashedPSORecords;

    ColorRGBA activeClearColor;
    VkRect2D activeRenderRegion;

    std::unordered_map<uint64_t, VkFramebuffer>
        framebufferCache; // TODO: MOVE ME TO A SEPARATE CLASS

    VkPipeline boundPSO;
    VkRenderPass activeRenderPass;
    ResourceBinder resourceBinder;
    D3D9RenderStateInfo renderStates;

    struct GPUBufferDeletion {
        VkBuffer buffer;
        VmaAllocation allocation;
    };
    struct GPUTextureDeletion {
        VkImage image;
        VmaAllocation allocation; // nullptr if not VMA-allocated
        VkImageView imageView;
    };
    struct RetireList {
        std::vector<GPUBufferDeletion> Buffers;
        std::vector<GPUTextureDeletion> Textures;
    };
    RetireList pendingRetire;
    RetireList nextFrameRetire;

    // Everything the submit thread needs to submit and present a frame
    struct FrameSubmission {
        uint64_t Serial = 0ull;
        uint32_t Slot = 0u;

        VkCommandBuffer CommandBuffers[PFCL_Count + 1];
        uint32_t NumCommandBuffers = 0u;

        bool bGammaRampPass = false;
        GammaRampPass::FrameParameters GammaRamp;
        VkImage Backbuffer = VK_NULL_HANDLE;
        VkExtent2D BackbufferExtent = { 0u, 0u };

        bool bUpdateHDRMetadata = false;
        VkHdrMetadataEXT HDRMetadata = {};

        RetireList Retire;
    };

    uint64_t recordingSerial = 0ull;
    uint64_t lastSerial = 0ull;
    uint64_t slotSerials[PendingFrameCount] = { 0ull, 0ull, 0ull };
    VkCommandBuffer recordingPreFrameCommands[PFCL_Count];
    uint32_t numRecordingPreFrameCommands = 0u;

    bool bHDRMetadataDirty = false;
    VkHdrMetadataEXT pendingHDRMetadata = {};

    // Submit thread
    std::thread submitThread;
    std::mutex submitMutex;
    std::condition_variable submitCondition;
    std::deque<FrameSubmission> queuedSubmissions;
    std::deque<FrameSubmission> inFlightSubmissions; // Submit thread only
    uint64_t lastQueuedSerial = 0ull;
    uint64_t retiredSerial = 0ull;
    uint64_t retireRequestSerial = 0ull;
    bool bProcessingSubmission = false;
    bool bStopSubmitThread = false;

    std::atomic<bool> bDeviceLost{ false };
    std::atomic<bool> bSwapchainOutOfDate{ false };

    VkFramebuffer activeFramebuffer;

    // Set by setStreamFrequency when D3DSTREAMSOURCE_INDEXEDDATA is used;
    // consumed and reset to 1 by the next drawIndexedPrimitive call.
    uint32_t activeInstanceCount = 1;

#if OTDU_DEVBUILD
    std::vector<PendingRenderTargetDump> pendingRenderTargetDumps;
    std::vector<PendingRenderTargetSnapshot> pendingRenderTargetSnapshots;

    VkViewport activeViewport = {};
    VkRect2D activeScissor = {};
#endif

private:
    QueueInfo getQueue( VkQueueFlagBits flags ) const;

    bool createVulkanInstance();
    bool selectPhysicalDevice();
    bool createDevice();
    void createDebugCallback();
    void createVMAAllocator();
    void createPipelineCache();
    void savePipelineCache();
    void savePipelineCacheWhenIdle();
    void bindStencilDynamicState();
    void bindExtendedDynamicState();
    void markExtendedDynamicStateDirty();
    void savePSOLibrary();
    bool prebuildPSO( const PSORecord &record, uint64_t &outHashcode );
    bool isPSORecordSupported( const PSORecord &record ) const;
    GPUShader *getOrCreatePrebuildShader( const uint64_t hashcode,
                                          const eShaderType stage );
    void releasePrebuildResources();
    bool createSwapchain();

    void presentBackbuffer( FrameSubmission &submission );
    void updateOutputParameters();
    void createOutputPassResources();
    bool createBackbufferTexture( const VkFormat format,
                                  const VkExtent2D extent );

    inline VkCommandBuffer getCommandBuffer() {
        if ( pendingClear.bActive && !bPreparingDrawCall ) {
            flushPendingClear();
        }
        if ( pendingDrawBatch.Count != 0u ) {
            flushDrawBatch();
        }
        return commandBuffer;
    }

    struct PendingClear {
        bool bActive = false;
        bool bColor = false;
        bool bDepth = false;
        bool bStencil = false;
        VkClearColorValue Color = {};
        VkClearDepthStencilValue DepthStencil = {};
        GPUTexture *pColorTargets[kMaxSimultaneousRT] = {};
        GPUTexture *pDepthStencil = nullptr;
    };
    PendingClear pendingClear;
    bool bPreparingDrawCall = false;
    bool isPendingClearForBoundAttachments() const;
    void flushPendingClear();
    void recordImageClear( const PendingClear &clear );

    void endActiveRenderPass();
    void recordIndexedDraw( const VkDrawIndexedIndirectCommand &command );
    void flushDrawBatch();
    void createIndirectDrawBuffers();
    void destroyIndirectDrawBuffers();

    bool prepareDrawCall( ePrimitiveType primitiveType );
    uint32_t getDrawFirstInstance( const uint32_t instanceCount ) const;
    VkFramebuffer getOrCreateFramebuffer( const FramebufferKey &key );
    void resetRecordedBindings();
    void bindIndexBufferToCmdBuffer( VkBuffer buffer, VkIndexType indexType );
    void insertMemoryBarrier( GPUTexture *pTexture,
                              const VkImageLayout prevLayout,
                              const VkImageLayout nextLayout,
                              const VkPipelineStageFlagBits stageSync,
                              uint32_t imageIndex );
    void insertImageBarrier( GPUTexture *pTexture,
                             uint32_t imageIndex,
                             const VkImageLayout prevLayout,
                             const VkImageLayout nextLayout,
                             const VkPipelineStageFlags srcStage,
                             const VkAccessFlags srcAccess,
                             const VkPipelineStageFlags dstStage,
                             const VkAccessFlags dstAccess );

    void recreateSwapchain();
    void releaseSwapchain();
    void releaseFramebufferCache();
    VkCommandBuffer
    getPreFrameCommandBuffer( const ePreFrameCommandList commandList );
    void collectPreFrameCommands( const uint64_t serial );
    void submitPreFrameCommandsNow();
    RetireList &getRetireList();
    void releaseRetireList( RetireList &list );

    void startSubmitThread();
    void stopSubmitThread();
    void submitThreadMain();
    void queueSubmission( FrameSubmission &&submission );
    void processSubmission( FrameSubmission &submission );
    void recordPresentPass( VkCommandBuffer cmdBuffer,
                            const FrameSubmission &submission,
                            const uint32_t imageIndex );
    void retireOldestSubmission( std::unique_lock<std::mutex> &lock );
    void waitForRetiredSubmission( const uint64_t serial );
    void waitForSubmissionsRetired();
    void syncDynamicBuffer( GPUBuffer *pBuffer, const uint32_t resourceIndex );

    bool copyRenderTargetToHost( GPUTexture *pTexture,
                                 const uint32_t imageIndex,
                                 std::vector<uint8_t> &texels );

#ifdef OTDU_DEVBUILD
    void processPendingRenderTargetDumps();
#endif
};

bool HasStencilBits( VkFormat fmt );

VkFormat GetPortableAttachmentFormat( const VkFormat format );
VkFormat GetNativeAttachmentFormat( const VkFormat format );
#endif