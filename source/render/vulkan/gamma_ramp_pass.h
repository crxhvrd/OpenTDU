#pragma once

#if OTDU_VULKAN

#ifndef VK_NO_PROTOTYPES
#define VK_NO_PROTOTYPES 1
#endif

#include "render/render_api.h"

#include "vk_mem_alloc.h"
#include "volk.h"

struct GPUTexture;

// Must match gamma_ramp.frag
enum class eOutputTransfer : uint32_t {
    OT_SDR = 0,
    OT_PQ = 1,
    OT_ExtendedLinear = 2
};

// D3D9 Gamma ramp emulation (based on dxvk implementation)
// Also converts the SDR backbuffer to the swapchain color space when HDR
// output is enabled
struct GammaRampPass {
public:
    inline bool isActive() const {
        return ( bEnabled || bTestPattern ||
                 outputTransfer != eOutputTransfer::OT_SDR ) &&
               pipeline != VK_NULL_HANDLE;
    }

public:
    void initialize( VkDevice device, VmaAllocator allocator );
    void destroy( VkDevice device, VmaAllocator allocator );

    void
    createSwapchainResources( VkDevice device,
                              const GPUTexture *pBackbuffer,
                              const VkFormat swapchainFormat,
                              const eOutputTransfer transfer,
                              const std::vector<VkImageView> &swapchainViews );
    void releaseSwapchainResources( VkDevice device );

    void setRamp( const GammaRamp &ramp );
    // scale: paper white in the output encoding; peak: display peak relative
    // to the paper white
    // highlightStrength: scales what lies above the paper white
    inline void setOutputParameters( const float scale,
                                     const float peak,
                                     const float highlightStrength ) {
        outputScale = scale;
        outputPeak = peak;
        outputHighlightStrength = highlightStrength;
    }
    void uploadRamp( VkCommandBuffer cmdBuffer );
    void draw( VkCommandBuffer cmdBuffer,
               const uint32_t frameIndex,
               const uint32_t imageIndex );

    // TODO: Only exposed for ImGui dbg (should be moved to private)
    bool bEnabled = false;
    // Reference patches drawn in the output color space (see gamma_ramp.frag)
    bool bTestPattern = false;

private:
    VkShaderModule vertexShader = VK_NULL_HANDLE;
    VkShaderModule fragmentShader = VK_NULL_HANDLE;

    VkDescriptorSetLayout descriptorSetLayout = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    VkDescriptorPool descriptorPool = VK_NULL_HANDLE;

    VkDescriptorSet descriptorSets[PendingFrameCount] = {
        VK_NULL_HANDLE, VK_NULL_HANDLE, VK_NULL_HANDLE };

    VkBuffer rampBuffer = VK_NULL_HANDLE;
    VmaAllocation rampAllocation = nullptr;

    VkRenderPass renderPass = VK_NULL_HANDLE;
    VkPipeline pipeline = VK_NULL_HANDLE;

    std::vector<VkFramebuffer> framebuffers;
    VkExtent2D extent = { 0u, 0u };

    float controlPoints[GammaRamp::kNumControlPoints][4] = {};

    eOutputTransfer outputTransfer = eOutputTransfer::OT_SDR;
    float outputScale = 1.0f;
    float outputPeak = 1.0f;
    float outputHighlightStrength = 1.0f;

    bool bRampDirty = false;

private:
    void createPipeline( VkDevice device, const VkFormat format );
};
#endif
