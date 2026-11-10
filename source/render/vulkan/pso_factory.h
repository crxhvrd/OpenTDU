#pragma once

#if OTDU_VULKAN

#include "render/render_api.h"
#include "volk.h"

#include "resource_binder.h"
#include "sampler_heap_manager.h"

// Byte offset where DXVK's DXSO compiler places sampler push constants within
// the push constant block. Matches MaxSharedPushDataSize from dxvk_limits.h.
static constexpr uint32_t kDxvkMaxSharedPushDataSize = 64u;

struct GPUShader;
struct GPUVertexLayout;

struct CachedPSO {
    VkPipeline Instance;
    uint64_t Hashcode;
    uint64_t TextureBindingMask; // Combined mask (VS + PS)
};

// Bits of the D3D9 spec dwords 1 and 2 read by pixel shaders: alpha test
// function + point sprite (dword1 21..25), alpha test precision (dword2 27..30)
static constexpr uint32_t kPSSpecDwordMasks[2] = { 0x1fu << 21, 0xfu << 27 };

// OTDU_ADDITION: vertex shader inputs missing from the vertex layout read this
// binding, a zero stride stream of zeros (what D3D9 drivers, Wine and DXVK
// return for elements missing from the declaration; Vulkan leaves them
// undefined). The device binds it in every command buffer. D3D9 streams stay
// below it (the spec guarantees 16 bindings).
static constexpr uint32_t kNullVertexBinding = 15u;

struct PSOFactory {
    // VS and PS
    GPUShader *pGraphicsShaderStages[2];

    GPUVertexLayout *pVertexLayout;

    VkPipelineRasterizationStateCreateInfo RasterState;
    VkPipelineDepthStencilStateCreateInfo DepthStencil;
    VkPipelineColorBlendStateCreateInfo BlendState;

    VkPipelineColorBlendAttachmentState AlphaBlending;
    VkPipelineInputAssemblyStateCreateInfo InputAssembly;

    VkBlendFactor srcBlendFactor;
    VkBlendFactor dstBlendFactor;
    VkBlendOp blendOP;

    VkRenderPass renderPass;

    VkPipelineDynamicStateCreateInfo DynamicState;
    VkPipelineMultisampleStateCreateInfo MultisampleState;

    ePrimitiveType PrimitiveType;

    uint64_t psoLayoutHashcode;
    uint64_t renderPassHashcode;
    uint32_t VSClipPlaneCount;
    // D3D9 spec dwords 1 and 2, restricted to the bits pixel shaders read
    // (kPSSpecDwordMasks)
    uint32_t PSSpecDwords[2];
    uint32_t LampsEnabled; // TODO: Might want to make this generic to make
                           // specicialization available to new render effects

    VkPipelineLayout PipelineLayout;

    uint8_t bDirty : 1;
    uint8_t bDirtyStencilDynamicState : 1;
    uint8_t bDirtyExtendedDynamicState : 1;
    uint8_t bTwoSidedStencil : 1;

    bool bIsStageBound[2];

    bool bExtendedDynamicState = false;
    PSOFactory *pParentFactory = nullptr;

    void initialize( VkDevice device,
                     SamplerHeapManager &samplerHeap,
                     ResourceBinder &resourceBinder );
    CachedPSO &getOrBuildPSO( VkDevice device, VkPipelineCache psoCache );
    void releaseCachedPSOs( VkDevice device );
    inline size_t getNumCachedPSOs() const { return cache.size(); }
    inline bool hasCachedPSO( const uint64_t hashcode ) const {
        return cache.find( hashcode ) != cache.end();
    }
    void adoptCachedPSOs( VkDevice device, PSOFactory &other );
    void setShader( eShaderType stage, struct GPUShader *pShader );
    void reset();

private:
    struct StateCacheEntry {
        std::vector<uint32_t> StateKey;
        CachedPSO *pPSO;
    };

private:
    uint32_t vsSpecData[3];
    VkSpecializationMapEntry vsSpecEntries[3];
    VkSpecializationInfo vsSpecInfo;
    uint32_t psSpecData[4];
    VkSpecializationMapEntry psSpecEntries[4];
    VkSpecializationInfo psSpecInfo;

    std::unordered_map<uint64_t, CachedPSO> cache;
    std::vector<uint32_t> hashKey; // Reused between PSO lookups
    std::unordered_map<uint64_t, StateCacheEntry> stateCache;
    std::vector<uint32_t> stateKey; // Reused between PSO lookups
#if OTDU_DEVBUILD
    std::unordered_map<uint64_t, std::vector<uint32_t>>
        hashKeys; // Used to detect hashcode collisions
#endif
    CachedPSO *latestPSO; // Latest PSO created or found in the cache

private:
    void fillGraphicsDescriptor( VkGraphicsPipelineCreateInfo &psoInfos );
    void buildStateKey( std::vector<uint32_t> &key ) const;
    CachedPSO &cacheStateKey( const uint64_t stateHashcode, CachedPSO &pso );
};
#endif
