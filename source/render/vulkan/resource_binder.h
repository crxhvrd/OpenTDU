#pragma once

#if OTDU_VULKAN

#ifndef VK_NO_PROTOTYPES
#define VK_NO_PROTOTYPES 1
#endif

#include "render/render_api.h"

#include <unordered_map>

#include "vk_mem_alloc.h"
#include "volk.h"

#include "shader_constant_layout.h"

struct Texture;
struct GPUTexture;

static constexpr uint32_t GetNumPerDrawStorageBlocks() {
    uint32_t count = 0;
    for ( const ShaderConstantBlockDesc &block : kShaderConstantBlocks ) {
        count += block.bPerDrawStorage ? 1 : 0;
    }
    return count;
}

static_assert( GetNumPerDrawStorageBlocks() == 1 &&
                   kShaderConstantBlocks[SCB_DrawVS].bPerDrawStorage,
               "Only DrawVS can be a per draw storage block" );
static constexpr uint32_t kVulkanNumUBOPerSet =
    SCB_Count - GetNumPerDrawStorageBlocks() + 2;
static_assert( kVulkanNumUBOPerSet <= 8,
               "Vulkan only guarantees 8 dynamic uniform buffers per layout" );

struct LinearAllocator {
    VkBuffer Buffers;
    VmaAllocation Allocations;
    int8_t *pCPUMapping;

    uint32_t Usage;
    uint32_t Capacity;
    uint32_t ElementSize;

    VkDeviceSize Alignment;

    bool
    create( VkDevice device,
            VmaAllocator allocator,
            uint32_t elementSize,
            uint32_t maxNumElements,
            VkDeviceSize uboAlignment,
            VkBufferUsageFlags usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT );
    void destroy( VkDevice device, VmaAllocator allocator );

    uint32_t allocate();
    void onFrameBegin();
    VkDescriptorBufferInfo getCurrentBufferInfo() const;
};

struct ResourceBinder {
    void initialize( VkDevice device,
                     VmaAllocator allocator,
                     VkDeviceSize uboAlignment );
    void destroy( VkDevice device, VmaAllocator allocator );

    void initializeAllocators( VkDevice device,
                               VmaAllocator allocator,
                               VkDeviceSize uboAlignment );
    void destroyAllocators( VkDevice device, VmaAllocator allocator );

    void onFrameBegin( VkDevice device, uint32_t frameIndex );
    VkDescriptorSet getOrAllocateSet( VkDevice device,
                                      uint64_t bindingMask,
                                      uint32_t frameIndex );

    void setFloatConstants( eShaderType stage,
                            float *pFloats,
                            const uint32_t offset,
                            const uint32_t numVectors );
    void setClipPlane( int32_t param_1, float *param_2 );
    uint32_t setClipPlaneMask( uint32_t mask );
    void bindTexture( Texture *pTexture,
                      const uint32_t frameIndex,
                      const uint32_t index );

    bool isDescriptorSetDirty() const;

    void setShaderSpecBits( uint32_t dwordIndex,
                            uint32_t bitIndex,
                            uint32_t numBits,
                            uint32_t value );

    // Ensure that all resources required by a given shader are bound.
    // If any resource is missing, this function will trigger a debug
    // break
    void validateSRVBindings( struct GPUShader *pPixelShader );

    // Descriptor set pool.
    VkDescriptorPool DescriptorSetPool;

    // Latest allocated set. Can be reused if resources bound are still the same
    // (ie. bDirty=false)
    VkDescriptorSet DescriptorSet;

    // Shared layout across all shaders
    VkDescriptorSetLayout DescriptorLayout;

    LinearAllocator constantBlockAllocator[SCB_Count][PendingFrameCount];
    LinearAllocator clipPlanesAllocator[PendingFrameCount];
    LinearAllocator shaderSpecsAllocator[PendingFrameCount];

    std::array<uint32_t, kVulkanNumUBOPerSet> uboOffsets;
    uint32_t drawVSIndex;

    // CPU copy of each constant block (see shader_constant_layout.h)
    std::vector<float> constantBlocks[SCB_Count];
    float clipPlanes[6][4];
    uint32_t shaderSpecs[6];

    VkImageView boundSRVs[0x10];
    GPUTexture *pBoundSRVs[0x10];
    uint64_t boundSRVIds[0x10];
    VkImageView FallbackSRV = VK_NULL_HANDLE;

#if OTDU_DEVBUILD
    // Registers whose value changed since the last draw (see DrawCallStats)
    uint64_t changedConstants[2][4] = {};
#endif

    // True if we need to update the descriptor set
    uint32_t dirtyStates;

    struct CachedDescriptorSet {
        uint64_t SRVBindingHashcode;
        VkDescriptorSet DescriptorSet;
        uint32_t NumFramesUnused;
        uint64_t BoundSRVIds[0x10];

        CachedDescriptorSet()
            : SRVBindingHashcode( 0 ),
              DescriptorSet( VK_NULL_HANDLE ),
              NumFramesUnused( 0 ),
              BoundSRVIds{} {}
    };
    std::vector<CachedDescriptorSet> allocatedSets[PendingFrameCount];
    std::unordered_multimap<uint64_t, uint32_t> setLookup[PendingFrameCount];
    std::vector<uint32_t> freeSetSlots[PendingFrameCount];
    std::vector<VkDescriptorSet> recycledSets[PendingFrameCount];

    VmaAllocator allocator;

    CachedDescriptorSet &findOrAllocateSet( VkDevice device,
                                            const uint64_t hashcode,
                                            uint32_t frameIndex );

private:
    uint32_t findFreeOrEmplace( uint32_t frameIndex );
    void eraseFromLookup( uint32_t frameIndex,
                          const uint64_t hashcode,
                          const uint32_t slotIndex );
    void uploadConstantBlock( const eShaderConstantBlock block,
                              const uint32_t frameIndex );
    VkDescriptorSet acquireDescriptorSet( VkDevice device,
                                          uint32_t frameIndex );
};
#endif
