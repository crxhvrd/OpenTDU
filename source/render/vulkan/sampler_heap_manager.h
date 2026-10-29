#pragma once

#if OTDU_VULKAN

#include "render/render_api.h"
#include "volk.h"

static constexpr uint32_t kSamplerHeapCapacity = 32u;

struct SamplerHeapManager {
    struct SamplerEntry {
        VkSampler Object = VK_NULL_HANDLE;
        uint32_t HeapIndex = 0u;
    };

    // maxAnisotropy = 1 if anisotropic filtering is not supported
    void initialize( VkDevice device,
                     const float maxAnisotropy,
                     const bool bUpdateUnusedWhilePending );
    void destroy( VkDevice device );

    void createAndUploadCommonSamplers( VkDevice device );

    uint32_t getSamplerHeapIndex( SamplerDesc sampler );
    SamplerEntry &createSampler( VkDevice device, SamplerDesc sampler );

    VkDevice Device = VK_NULL_HANDLE;
    float MaxAnisotropy = 1.0f;
    VkDescriptorPool DescriptorSetPool;
    VkDescriptorSet DescriptorSet;
    VkDescriptorSetLayout DescriptorLayout;

    std::unordered_map<uint64_t, SamplerEntry> cache;
};
#endif
