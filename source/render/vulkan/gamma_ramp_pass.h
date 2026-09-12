#pragma once

#if OTDU_VULKAN

#ifndef VK_NO_PROTOTYPES
#define VK_NO_PROTOTYPES 1
#endif

#include "render/render_api.h"

#include "vk_mem_alloc.h"
#include "volk.h"

struct GPUTexture;

// D3D9 Gamma ramp emulation (based on dxvk implementation)
struct GammaRampPass {
public:
    inline bool isActive() const {
        return bEnabled && pipeline != VK_NULL_HANDLE;
    }

public:
    void initialize( VkDevice device, VmaAllocator allocator );
    void destroy( VkDevice device, VmaAllocator allocator );

    void
    createSwapchainResources( VkDevice device,
                              const GPUTexture *pBackbuffer,
                              const std::vector<VkImageView> &swapchainViews );
    void releaseSwapchainResources( VkDevice device );

    void setRamp( const GammaRamp &ramp );
    void uploadRamp( VkCommandBuffer cmdBuffer );
    void draw( VkCommandBuffer cmdBuffer,
               const uint32_t frameIndex,
               const uint32_t imageIndex );

    // TODO: Only exposed for ImGui dbg (should be moved to private)
    bool bEnabled = false;

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

    bool bRampDirty = false;

private:
    void createPipeline( VkDevice device, const VkFormat format );
};
#endif
