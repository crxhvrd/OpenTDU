#include "shared.h"
#include "render_pass_manager.h"

#if OTDU_VULKAN
#include "city.h"
#include "render_device.h"

static uint64_t
HashAttachmentReferences( uint64_t hashcode,
                          const VkAttachmentReference *pReferences,
                          const uint32_t count ) {
    hashcode =
        CombineCityHash64( hashcode, (const char *)&count, sizeof( uint32_t ) );
    if ( pReferences != nullptr && count != 0 ) {
        hashcode = CombineCityHash64( hashcode, (const char *)pReferences,
                                      sizeof( VkAttachmentReference ) * count );
    }
    return hashcode;
}

uint64_t CalculateRenderPassHashcode( const VkRenderPassCreateInfo &desc ) {
    uint64_t hashcode = 0;

    for ( uint32_t i = 0; i < desc.attachmentCount; i++ ) {
        VkAttachmentDescription attachment = desc.pAttachments[i];
        attachment.format = GetPortableAttachmentFormat( attachment.format );
        hashcode = CombineCityHash64( hashcode, (const char *)&attachment,
                                      sizeof( VkAttachmentDescription ) );
    }

    for ( uint32_t i = 0; i < desc.subpassCount; i++ ) {
        const VkSubpassDescription &subpass = desc.pSubpasses[i];
        const uint32_t subpassInfos[2] = {
            static_cast<uint32_t>( subpass.flags ),
            static_cast<uint32_t>( subpass.pipelineBindPoint ) };
        hashcode = CombineCityHash64( hashcode, (const char *)subpassInfos,
                                      sizeof( subpassInfos ) );

        hashcode = HashAttachmentReferences(
            hashcode, subpass.pInputAttachments, subpass.inputAttachmentCount );
        hashcode = HashAttachmentReferences(
            hashcode, subpass.pColorAttachments, subpass.colorAttachmentCount );
        hashcode =
            HashAttachmentReferences( hashcode, subpass.pResolveAttachments,
                                      ( subpass.pResolveAttachments != nullptr )
                                          ? subpass.colorAttachmentCount
                                          : 0u );
        hashcode = HashAttachmentReferences(
            hashcode, subpass.pDepthStencilAttachment,
            ( subpass.pDepthStencilAttachment != nullptr ) ? 1u : 0u );

        hashcode = CombineCityHash64(
            hashcode, (const char *)&subpass.preserveAttachmentCount,
            sizeof( uint32_t ) );
        if ( subpass.pPreserveAttachments != nullptr &&
             subpass.preserveAttachmentCount != 0 ) {
            hashcode = CombineCityHash64(
                hashcode, (const char *)subpass.pPreserveAttachments,
                sizeof( uint32_t ) * subpass.preserveAttachmentCount );
        }
    }

    for ( uint32_t i = 0; i < desc.dependencyCount; i++ ) {
        hashcode =
            CombineCityHash64( hashcode, (const char *)&desc.pDependencies[i],
                               sizeof( VkSubpassDependency ) );
    }

    return hashcode;
}

void RenderPassManager::initialize() {
    PassDesc = {};
    PassDesc.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    PassDesc.pNext = nullptr;

    PassAttachments.fill( {} );
    SubPassReferences.fill( {} );
    pBoundRTs.fill( nullptr );

    NumAttachments = 0;

    bUseDepthStencil = false;
    pDepthStencil = nullptr;
}

void RenderPassManager::releaseCachedRenderPass( VkDevice device ) {
    for ( auto &entry : cache ) {
        vkDestroyRenderPass( device, entry.second, nullptr );
    }
    cache.clear();
}

void RenderPassManager::setAttachment( GPUTexture *pTexture,
                                       const uint32_t index ) {
    if ( pTexture == nullptr ) {
        return;
    }

    if ( pBoundRTs[index] == pTexture ) {
        return;
    }
    bool bNewAttachment = ( pBoundRTs[index] == nullptr );
    pBoundRTs[index] = pTexture;

    OTDU_ASSERT( VK_FORMAT_UNDEFINED != pTexture->ViewFormat );

    VkAttachmentDescription &colorAttachment = PassAttachments[index];
    colorAttachment.flags = 0;
    colorAttachment.format = pTexture->ViewFormat;
    colorAttachment.samples = pTexture->SampleCount;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachment.initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    colorAttachment.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkAttachmentReference &attachmentRef = SubPassReferences[index];
    attachmentRef.attachment = index;
    attachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    if ( bNewAttachment ) {
        NumAttachments++;
    }
}

CachedRenderPass RenderPassManager::getOrCreateRenderPass( VkDevice device ) {
    return getOrCreateRenderPass( device, PassAttachments.data(),
                                  SubPassReferences.data(), NumAttachments,
                                  bUseDepthStencil );
}

CachedRenderPass
RenderPassManager::getOrCreateRenderPass( VkDevice device,
                                          const RenderPassDesc &desc ) {
    return getOrCreateRenderPass(
        device, desc.Attachments.data(), desc.References.data(),
        desc.NumColorAttachments, desc.bUseDepthStencil );
}

void RenderPassManager::captureDesc( RenderPassDesc &outDesc ) const {
    outDesc.Attachments.fill( {} );
    outDesc.References.fill( {} );
    outDesc.NumColorAttachments = NumAttachments;
    outDesc.bUseDepthStencil = bUseDepthStencil;

    const uint32_t numAttachments = outDesc.getNumAttachments();
    for ( uint32_t i = 0; i < numAttachments; i++ ) {
        outDesc.Attachments[i] = PassAttachments[i];
        if ( i < SubPassReferences.size() ) {
            outDesc.References[i] = SubPassReferences[i];
        }
    }
}

CachedRenderPass RenderPassManager::getOrCreateRenderPass(
    VkDevice device,
    const VkAttachmentDescription *pAttachments,
    const VkAttachmentReference *pReferences,
    const uint32_t numColorAttachments,
    const bool bDepthStencil ) {
    VkSubpassDescription subpassDesc = {};
    subpassDesc.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpassDesc.colorAttachmentCount = numColorAttachments;
    subpassDesc.pColorAttachments = pReferences;
    subpassDesc.inputAttachmentCount = 0;
    subpassDesc.preserveAttachmentCount = 0;
    if ( bDepthStencil ) {
        subpassDesc.pDepthStencilAttachment = &pReferences[numColorAttachments];
    }

    PassDesc.attachmentCount =
        ( bDepthStencil ) ? ( numColorAttachments + 1 ) : numColorAttachments;
    PassDesc.pAttachments = pAttachments;
    PassDesc.subpassCount = 1;
    PassDesc.pSubpasses = &subpassDesc;

    uint64_t hashcode = CalculateRenderPassHashcode( PassDesc );

    CachedRenderPass cachedRP;
    cachedRP.Hashcode = hashcode;

    auto cacheIt = cache.find( hashcode );
    if ( cacheIt != cache.end() ) {
        cachedRP.Instance = cacheIt->second;
        return cachedRP;
    }

    VkRenderPass renderPass = VK_NULL_HANDLE;

    VkResult renderPassCreationResult =
        vkCreateRenderPass( device, &PassDesc, nullptr, &renderPass );
    OTDU_ASSERT( renderPassCreationResult == VK_SUCCESS );

    cache.insert( std::make_pair( hashcode, renderPass ) );

    cachedRP.Instance = renderPass;
    return cachedRP;
}

void RenderPassManager::setDepthStencilAttachment( GPUTexture *pTexture ) {
    if ( pTexture == nullptr ) {
        pDepthStencil = nullptr;
        bUseDepthStencil = false;
        return;
    }

    if ( pDepthStencil == pTexture ) {
        return;
    }
    pDepthStencil = pTexture;

    OTDU_ASSERT( VK_FORMAT_UNDEFINED != pTexture->ViewFormat );

    // TODO: This is extremely fragile (it assumes that the zbuffer will always
    // be bound last). Make this more robust.
    VkAttachmentDescription &depthStencilDesc = PassAttachments[NumAttachments];
    depthStencilDesc.flags = 0;
    depthStencilDesc.format = pTexture->ViewFormat;
    depthStencilDesc.samples = pTexture->SampleCount;

    // D3D9 had no loadOp/storeOp concept — the depth buffer is always
    // preserved unless explicitly cleared (handled separately via vkCmdClear).
    // Per-draw depth test/write state (ZENABLE/ZWRITEENABLE) is per-PSO and
    // must not drive the per-pass attachment config.
    depthStencilDesc.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    depthStencilDesc.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

    // Stencil load/store is determined by format, not by PSO stencil-enable
    // state. The attachment must always preserve stencil when the format has
    // stencil bits so that stencil test can be toggled mid-frame without
    // changing the render pass (which would force a pipeline rebuild).
    if ( HasStencilBits( pTexture->ViewFormat ) ) {
        depthStencilDesc.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
        depthStencilDesc.stencilStoreOp = VK_ATTACHMENT_STORE_OP_STORE;
    } else {
        depthStencilDesc.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        depthStencilDesc.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    }

    VkImageLayout attachmentLayout =
        HasStencilBits( pTexture->ViewFormat )
            ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
            : VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
    depthStencilDesc.initialLayout = attachmentLayout;
    depthStencilDesc.finalLayout = attachmentLayout;

    VkAttachmentReference &depthStencilAttachment =
        SubPassReferences[NumAttachments];
    depthStencilAttachment.attachment = NumAttachments;
    depthStencilAttachment.layout = attachmentLayout;

    bUseDepthStencil = true;
}
#endif
