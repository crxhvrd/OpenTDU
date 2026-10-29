#include "shared.h"
#include "gamma_ramp_pass.h"

#if OTDU_VULKAN
#include "render_device.h"
#include "shaders/gamma_ramp_spirv.h"

static VkShaderModule CreateShaderModule( VkDevice device,
                                          const uint32_t *pSPIRV,
                                          const size_t size ) {
    VkShaderModuleCreateInfo createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = size;
    createInfo.pCode = pSPIRV;

    VkShaderModule shaderModule = VK_NULL_HANDLE;
    VkResult result =
        vkCreateShaderModule( device, &createInfo, nullptr, &shaderModule );
    OTDU_ASSERT( result == VK_SUCCESS );

    return shaderModule;
}

void GammaRampPass::initialize( VkDevice device,
                                VmaAllocator allocator,
                                VkPipelineCache psoCache ) {
    pipelineCache = psoCache;
    vertexShader = CreateShaderModule( device, kGammaRampVertexShaderSPIRV,
                                       sizeof( kGammaRampVertexShaderSPIRV ) );
    fragmentShader =
        CreateShaderModule( device, kGammaRampFragmentShaderSPIRV,
                            sizeof( kGammaRampFragmentShaderSPIRV ) );

    VkDescriptorSetLayoutBinding bindings[2] = {};
    bindings[0].binding = 0;
    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    bindings[1].binding = 1;
    bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    bindings[1].descriptorCount = 1;
    bindings[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo setLayoutInfo = {};
    setLayoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    setLayoutInfo.bindingCount = 2;
    setLayoutInfo.pBindings = bindings;

    VkResult result = vkCreateDescriptorSetLayout(
        device, &setLayoutInfo, nullptr, &descriptorSetLayout );
    OTDU_ASSERT( result == VK_SUCCESS );

    VkPushConstantRange pushConstantRange = {};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    pushConstantRange.size = sizeof( OutputConstants );

    VkPipelineLayoutCreateInfo pipelineLayoutInfo = {};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &descriptorSetLayout;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

    result = vkCreatePipelineLayout( device, &pipelineLayoutInfo, nullptr,
                                     &pipelineLayout );
    OTDU_ASSERT( result == VK_SUCCESS );

    VkDescriptorPoolSize poolSizes[2] = {
        {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,  PendingFrameCount},
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, PendingFrameCount}
    };

    VkDescriptorPoolCreateInfo poolInfo = {};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.maxSets = PendingFrameCount;
    poolInfo.poolSizeCount = 2;
    poolInfo.pPoolSizes = poolSizes;

    result =
        vkCreateDescriptorPool( device, &poolInfo, nullptr, &descriptorPool );
    OTDU_ASSERT( result == VK_SUCCESS );

    VkDescriptorSetLayout setLayouts[PendingFrameCount];
    for ( VkDescriptorSetLayout &setLayout : setLayouts ) {
        setLayout = descriptorSetLayout;
    }

    VkDescriptorSetAllocateInfo setAllocInfo = {};
    setAllocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    setAllocInfo.descriptorPool = descriptorPool;
    setAllocInfo.descriptorSetCount = PendingFrameCount;
    setAllocInfo.pSetLayouts = setLayouts;

    result = vkAllocateDescriptorSets( device, &setAllocInfo, descriptorSets );
    OTDU_ASSERT( result == VK_SUCCESS );

    VkBufferCreateInfo bufferInfo = {};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = sizeof( controlPoints );
    bufferInfo.usage =
        VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo allocInfo = {};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO;

    result = vmaCreateBuffer( allocator, &bufferInfo, &allocInfo, &rampBuffer,
                              &rampAllocation, nullptr );
    OTDU_ASSERT( result == VK_SUCCESS );

    VkDescriptorBufferInfo rampBufferInfo = {};
    rampBufferInfo.buffer = rampBuffer;
    rampBufferInfo.offset = 0;
    rampBufferInfo.range = sizeof( controlPoints );

    VkWriteDescriptorSet rampWrites[PendingFrameCount] = {};
    for ( uint32_t i = 0; i < PendingFrameCount; i++ ) {
        rampWrites[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        rampWrites[i].dstSet = descriptorSets[i];
        rampWrites[i].dstBinding = 1;
        rampWrites[i].descriptorCount = 1;
        rampWrites[i].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        rampWrites[i].pBufferInfo = &rampBufferInfo;
    }
    vkUpdateDescriptorSets( device, PendingFrameCount, rampWrites, 0, nullptr );
}

void GammaRampPass::destroy( VkDevice device, VmaAllocator allocator ) {
    vmaDestroyBuffer( allocator, rampBuffer, rampAllocation );
    vkDestroyDescriptorPool( device, descriptorPool, nullptr );
    vkDestroyPipelineLayout( device, pipelineLayout, nullptr );
    vkDestroyDescriptorSetLayout( device, descriptorSetLayout, nullptr );
    vkDestroyShaderModule( device, fragmentShader, nullptr );
    vkDestroyShaderModule( device, vertexShader, nullptr );
}

void GammaRampPass::createSwapchainResources(
    VkDevice device,
    const GPUTexture *pBackbuffer,
    const VkFormat swapchainFormat,
    const eOutputTransfer transfer,
    const std::vector<VkImageView> &swapchainViews ) {
    extent = { pBackbuffer->Width, pBackbuffer->Height };
    outputTransfer = transfer;

    VkAttachmentDescription attachment = {};
    attachment.format = swapchainFormat;
    attachment.samples = VK_SAMPLE_COUNT_1_BIT;
    attachment.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentReference colorReference = {};
    colorReference.attachment = 0;
    colorReference.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass = {};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorReference;

    VkSubpassDependency dependency = {};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.srcAccessMask = 0;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo renderPassInfo = {};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = 1;
    renderPassInfo.pAttachments = &attachment;
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;
    renderPassInfo.dependencyCount = 1;
    renderPassInfo.pDependencies = &dependency;

    VkResult result =
        vkCreateRenderPass( device, &renderPassInfo, nullptr, &renderPass );
    OTDU_ASSERT( result == VK_SUCCESS );

    framebuffers.resize( swapchainViews.size(), VK_NULL_HANDLE );
    for ( size_t i = 0; i < swapchainViews.size(); i++ ) {
        VkFramebufferCreateInfo framebufferInfo = {};
        framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebufferInfo.renderPass = renderPass;
        framebufferInfo.attachmentCount = 1;
        framebufferInfo.pAttachments = &swapchainViews[i];
        framebufferInfo.width = extent.width;
        framebufferInfo.height = extent.height;
        framebufferInfo.layers = 1;

        result = vkCreateFramebuffer( device, &framebufferInfo, nullptr,
                                      &framebuffers[i] );
        OTDU_ASSERT( result == VK_SUCCESS );
    }

    createPipeline( device, swapchainFormat );

    VkDescriptorImageInfo backbufferInfos[PendingFrameCount] = {};
    VkWriteDescriptorSet backbufferWrites[PendingFrameCount] = {};
    for ( uint32_t i = 0; i < PendingFrameCount; i++ ) {
        backbufferInfos[i].imageView =
            pBackbuffer->ImageView[pBackbuffer->getResourceIndex( i )];
        backbufferInfos[i].imageLayout =
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        backbufferWrites[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        backbufferWrites[i].dstSet = descriptorSets[i];
        backbufferWrites[i].dstBinding = 0;
        backbufferWrites[i].descriptorCount = 1;
        backbufferWrites[i].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
        backbufferWrites[i].pImageInfo = &backbufferInfos[i];
    }
    vkUpdateDescriptorSets( device, PendingFrameCount, backbufferWrites, 0,
                            nullptr );
}

void GammaRampPass::releaseSwapchainResources( VkDevice device ) {
    vkDestroyPipeline( device, pipeline, nullptr );
    pipeline = VK_NULL_HANDLE;

    for ( VkFramebuffer &framebuffer : framebuffers ) {
        vkDestroyFramebuffer( device, framebuffer, nullptr );
    }
    framebuffers.clear();

    vkDestroyRenderPass( device, renderPass, nullptr );
    renderPass = VK_NULL_HANDLE;
}

void GammaRampPass::setRamp( const GammaRamp &ramp ) {
    bool bIsIdentity = true;
    for ( uint32_t i = 0; i < GammaRamp::kNumControlPoints; i++ ) {
        const uint16_t identity = uint16_t( i * 0x101u );
        bIsIdentity &= ( ramp.Red[i] == identity && ramp.Green[i] == identity &&
                         ramp.Blue[i] == identity );

        controlPoints[i][0] = ramp.Red[i] / 65535.0f;
        controlPoints[i][1] = ramp.Green[i] / 65535.0f;
        controlPoints[i][2] = ramp.Blue[i] / 65535.0f;
        controlPoints[i][3] = 1.0f;
    }

    bEnabled = !bIsIdentity;
    bRampDirty = true;
}

void GammaRampPass::captureFrameParameters( FrameParameters &outParameters ) {
    outParameters.bUploadRamp = bRampDirty;
    if ( bRampDirty ) {
        memcpy( outParameters.ControlPoints, controlPoints,
                sizeof( controlPoints ) );
        bRampDirty = false;
    }

    outParameters.Constants = { outputTransfer, bEnabled ? 1u : 0u,
                                outputScale,    bTestPattern ? 1u : 0u,
                                outputPeak,     outputHighlightStrength };
}

void GammaRampPass::uploadRamp( VkCommandBuffer cmdBuffer,
                                const FrameParameters &parameters ) {
    if ( !parameters.bUploadRamp ) {
        return;
    }

    vkCmdPipelineBarrier( cmdBuffer, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                          VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0,
                          nullptr, 0, nullptr );

    vkCmdUpdateBuffer( cmdBuffer, rampBuffer, 0,
                       sizeof( parameters.ControlPoints ),
                       parameters.ControlPoints );

    VkBufferMemoryBarrier barrier = {};
    barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_UNIFORM_READ_BIT;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.buffer = rampBuffer;
    barrier.offset = 0;
    barrier.size = VK_WHOLE_SIZE;
    vkCmdPipelineBarrier( cmdBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT,
                          VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr,
                          1, &barrier, 0, nullptr );
}

void GammaRampPass::draw( VkCommandBuffer cmdBuffer,
                          const uint32_t frameIndex,
                          const uint32_t imageIndex,
                          const FrameParameters &parameters ) {
    VkRenderPassBeginInfo beginInfo = {};
    beginInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    beginInfo.renderPass = renderPass;
    beginInfo.framebuffer = framebuffers[imageIndex];
    beginInfo.renderArea.extent = extent;
    vkCmdBeginRenderPass( cmdBuffer, &beginInfo, VK_SUBPASS_CONTENTS_INLINE );

    vkCmdBindPipeline( cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline );
    vkCmdBindDescriptorSets( cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                             pipelineLayout, 0, 1, &descriptorSets[frameIndex],
                             0, nullptr );

    vkCmdPushConstants( cmdBuffer, pipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT,
                        0, sizeof( parameters.Constants ),
                        &parameters.Constants );
    vkCmdDraw( cmdBuffer, 3, 1, 0, 0 );

    vkCmdEndRenderPass( cmdBuffer );
}

void GammaRampPass::createPipeline( VkDevice device, const VkFormat format ) {
    VkPipelineShaderStageCreateInfo stages[2] = {};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vertexShader;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fragmentShader;
    stages[1].pName = "main";

    VkPipelineVertexInputStateCreateInfo vertexInputState = {};
    vertexInputState.sType =
        VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

    VkPipelineInputAssemblyStateCreateInfo inputAssemblyState = {};
    inputAssemblyState.sType =
        VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssemblyState.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkViewport viewport = {};
    viewport.width = float( extent.width );
    viewport.height = float( extent.height );
    viewport.maxDepth = 1.0f;

    VkRect2D scissor = {};
    scissor.extent = extent;

    VkPipelineViewportStateCreateInfo viewportState = {};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.pViewports = &viewport;
    viewportState.scissorCount = 1;
    viewportState.pScissors = &scissor;

    VkPipelineRasterizationStateCreateInfo rasterState = {};
    rasterState.sType =
        VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterState.polygonMode = VK_POLYGON_MODE_FILL;
    rasterState.cullMode = VK_CULL_MODE_NONE;
    rasterState.frontFace = VK_FRONT_FACE_CLOCKWISE;
    rasterState.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo multisampleState = {};
    multisampleState.sType =
        VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampleState.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineColorBlendAttachmentState blendAttachment = {};
    blendAttachment.colorWriteMask =
        VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
        VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

    VkPipelineColorBlendStateCreateInfo blendState = {};
    blendState.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    blendState.attachmentCount = 1;
    blendState.pAttachments = &blendAttachment;

    VkGraphicsPipelineCreateInfo pipelineInfo = {};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = stages;
    pipelineInfo.pVertexInputState = &vertexInputState;
    pipelineInfo.pInputAssemblyState = &inputAssemblyState;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterState;
    pipelineInfo.pMultisampleState = &multisampleState;
    pipelineInfo.pColorBlendState = &blendState;
    pipelineInfo.layout = pipelineLayout;
    pipelineInfo.renderPass = renderPass;
    pipelineInfo.subpass = 0;

    VkResult result = vkCreateGraphicsPipelines(
        device, pipelineCache, 1, &pipelineInfo, nullptr, &pipeline );
    OTDU_ASSERT( result == VK_SUCCESS );
}
#endif
