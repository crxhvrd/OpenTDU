#pragma once

#if OTDU_VULKAN

#include "render/render_api.h"
#include "volk.h"

struct GPUTexture;

struct CachedRenderPass {
    VkRenderPass Instance;
    uint64_t Hashcode;
};

struct RenderPassDesc {
    std::array<VkAttachmentDescription, kMaxSimultaneousRT + 1> Attachments;
    std::array<VkAttachmentReference, kMaxSimultaneousRT + 1> References;
    uint32_t NumColorAttachments;
    bool bUseDepthStencil;

    inline uint32_t getNumAttachments() const {
        return NumColorAttachments + ( bUseDepthStencil ? 1u : 0u );
    }
};

struct RenderPassManager {
    void initialize();
    void releaseCachedRenderPass( VkDevice device );

    void setAttachment( GPUTexture *pTexture, const uint32_t index );
    void setDepthStencilAttachment( GPUTexture *pTexture );

    CachedRenderPass getOrCreateRenderPass( VkDevice device );
    CachedRenderPass getOrCreateRenderPass( VkDevice device,
                                            const RenderPassDesc &desc );
    void captureDesc( RenderPassDesc &outDesc ) const;

    VkRenderPassCreateInfo PassDesc;
    std::array<GPUTexture *, kMaxSimultaneousRT> pBoundRTs;
    std::array<VkAttachmentDescription, kMaxSimultaneousRT + 1>
        PassAttachments; // + 1 for zbuffer
    std::array<VkAttachmentReference, kMaxSimultaneousRT> SubPassReferences;
    uint32_t NumAttachments;

    uint8_t bUseDepthStencil : 1;
    GPUTexture *pDepthStencil;

private:
    std::unordered_map<uint64_t, VkRenderPass> cache;

private:
    CachedRenderPass
    getOrCreateRenderPass( VkDevice device,
                           const VkAttachmentDescription *pAttachments,
                           const VkAttachmentReference *pReferences,
                           const uint32_t numColorAttachments,
                           const bool bDepthStencil );
};
#endif
