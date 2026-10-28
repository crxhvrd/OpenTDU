#include "shared.h"
#include "pso_factory.h"

#if OTDU_VULKAN
#include "city.h"
#include "render/shaders/shader_binary_cache.h"
#include "render_device.h"

// Stencil reference/masks are dynamic to avoid building a PSO per stencil
// reference value.
static constexpr uint32_t kVulkanNumPSODynamicState = 5;
static constexpr const VkDynamicState
    kVulkanPSODynamicStates[kVulkanNumPSODynamicState] = {
        VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR,
        VK_DYNAMIC_STATE_STENCIL_REFERENCE,
        VK_DYNAMIC_STATE_STENCIL_COMPARE_MASK,
        VK_DYNAMIC_STATE_STENCIL_WRITE_MASK };

// Extended dynamic state (Vulkan 1.3 or VK_EXT_extended_dynamic_state)
static constexpr uint32_t kVulkanNumPSOExtendedDynamicState = 11;
static constexpr const VkDynamicState
    kVulkanPSOExtendedDynamicStates[kVulkanNumPSOExtendedDynamicState] = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR,
        VK_DYNAMIC_STATE_STENCIL_REFERENCE,
        VK_DYNAMIC_STATE_STENCIL_COMPARE_MASK,
        VK_DYNAMIC_STATE_STENCIL_WRITE_MASK,
        VK_DYNAMIC_STATE_CULL_MODE,
        VK_DYNAMIC_STATE_DEPTH_TEST_ENABLE,
        VK_DYNAMIC_STATE_DEPTH_WRITE_ENABLE,
        VK_DYNAMIC_STATE_DEPTH_COMPARE_OP,
        VK_DYNAMIC_STATE_STENCIL_TEST_ENABLE,
        VK_DYNAMIC_STATE_STENCIL_OP };

#if defined( OTDU_MACOS ) && defined( OTDU_DEVBUILD )
static const char *DeclUsageToString( uint8_t usage ) {
    static const char *kNames[] = {
        "POSITION", "BLENDWEIGHT", "BLENDINDICES", "NORMAL",     "PSIZE",
        "TEXCOORD", "TANGENT",     "BINORMAL",     "TESSFACTOR", "POSITIONT",
        "COLOR",    "FOG",         "DEPTH",        "SAMPLE" };
    return usage < ( sizeof( kNames ) / sizeof( kNames[0] ) ) ? kNames[usage]
                                                              : "?";
}
#endif

static constexpr const VkPrimitiveTopology
    kPrimitiveTypes[ePrimitiveType::PT_Count] = {
        VK_PRIMITIVE_TOPOLOGY_POINT_LIST,
        VK_PRIMITIVE_TOPOLOGY_LINE_LIST,
        VK_PRIMITIVE_TOPOLOGY_LINE_STRIP,
        VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
        VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP,
        VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN,
        VK_PRIMITIVE_TOPOLOGY_PATCH_LIST,
        VK_PRIMITIVE_TOPOLOGY_MAX_ENUM };

// Builds the PSO hash key from values only
struct PSOHashKeyWriter {
    std::vector<uint32_t> &Key;

    inline void push( const uint32_t value ) { Key.push_back( value ); }
    inline void push64( const uint64_t value ) {
        Key.push_back( static_cast<uint32_t>( value ) );
        Key.push_back( static_cast<uint32_t>( value >> 32 ) );
    }
    inline void pushFloat( const float value ) {
        uint32_t bits = 0;
        memcpy( &bits, &value, sizeof( uint32_t ) );
        Key.push_back( bits );
    }
    // compareMask/writeMask/reference are dynamic states
    inline void pushStencilOp( const VkStencilOpState &state ) {
        push( state.failOp );
        push( state.passOp );
        push( state.depthFailOp );
        push( state.compareOp );
    }
};

static void BuildPSOHashKey(
    std::vector<uint32_t> &key,
    const VkGraphicsPipelineCreateInfo &psoDesc,
    const GPUShader *const pShaderStages[2],
    const std::vector<VkVertexInputBindingDescription> &inputBindings,
    const std::vector<VkVertexInputAttributeDescription> &inputAttribs,
    const uint64_t psoLayoutHashcode,
    const uint64_t renderPassHashcode,
    const uint32_t vsClipPlaneCount,
    const bool bExtendedDynamicState ) {
    key.clear();
    PSOHashKeyWriter writer{ key };

    writer.push( psoDesc.stageCount );
    for ( uint32_t i = 0; i < psoDesc.stageCount; i++ ) {
        const VkPipelineShaderStageCreateInfo &stage = psoDesc.pStages[i];
        writer.push( stage.stage );
        writer.push64( pShaderStages[i]->Hashcode );

        const VkSpecializationInfo *pSpecInfo = stage.pSpecializationInfo;
        writer.push( ( pSpecInfo != nullptr ) ? pSpecInfo->mapEntryCount : 0u );
        if ( pSpecInfo != nullptr ) {
            for ( uint32_t j = 0; j < pSpecInfo->mapEntryCount; j++ ) {
                const VkSpecializationMapEntry &entry =
                    pSpecInfo->pMapEntries[j];
                writer.push( entry.constantID );
                writer.push( entry.offset );
                writer.push( static_cast<uint32_t>( entry.size ) );
            }

            const uint32_t *pSpecData =
                static_cast<const uint32_t *>( pSpecInfo->pData );
            for ( size_t j = 0; j < pSpecInfo->dataSize / sizeof( uint32_t );
                  j++ ) {
                writer.push( pSpecData[j] );
            }
        }
    }
    writer.push( vsClipPlaneCount );

    writer.push( static_cast<uint32_t>( inputBindings.size() ) );
    for ( const VkVertexInputBindingDescription &binding : inputBindings ) {
        writer.push( binding.binding );
        writer.push( binding.stride );
        writer.push( binding.inputRate );
    }

    writer.push( static_cast<uint32_t>( inputAttribs.size() ) );
    for ( const VkVertexInputAttributeDescription &attrib : inputAttribs ) {
        writer.push( attrib.location );
        writer.push( attrib.binding );
        writer.push( attrib.format );
        writer.push( attrib.offset );
    }

    const VkPipelineInputAssemblyStateCreateInfo *pInputAssembly =
        psoDesc.pInputAssemblyState;
    writer.push( pInputAssembly != nullptr );
    if ( pInputAssembly != nullptr ) {
        writer.push( pInputAssembly->flags );
        writer.push( pInputAssembly->topology );
        writer.push( pInputAssembly->primitiveRestartEnable );
    }

    const VkPipelineTessellationStateCreateInfo *pTessellation =
        psoDesc.pTessellationState;
    writer.push( pTessellation != nullptr );
    if ( pTessellation != nullptr ) {
        writer.push( pTessellation->flags );
        writer.push( pTessellation->patchControlPoints );
    }

    const VkPipelineViewportStateCreateInfo *pViewport = psoDesc.pViewportState;
    writer.push( pViewport != nullptr );
    if ( pViewport != nullptr ) {
        // Viewports and scissors are dynamic; only the counts matter
        writer.push( pViewport->flags );
        writer.push( pViewport->viewportCount );
        writer.push( pViewport->scissorCount );
    }

    const VkPipelineRasterizationStateCreateInfo *pRaster =
        psoDesc.pRasterizationState;
    writer.push( pRaster != nullptr );
    if ( pRaster != nullptr ) {
        writer.push( pRaster->flags );
        writer.push( pRaster->depthClampEnable );
        writer.push( pRaster->rasterizerDiscardEnable );
        writer.push( pRaster->polygonMode );
        if ( !bExtendedDynamicState ) {
            writer.push( pRaster->cullMode );
        }
        writer.push( pRaster->frontFace );
        writer.push( pRaster->depthBiasEnable );
        writer.pushFloat( pRaster->depthBiasConstantFactor );
        writer.pushFloat( pRaster->depthBiasClamp );
        writer.pushFloat( pRaster->depthBiasSlopeFactor );
        writer.pushFloat( pRaster->lineWidth );
    }

    const VkPipelineMultisampleStateCreateInfo *pMultisample =
        psoDesc.pMultisampleState;
    writer.push( pMultisample != nullptr );
    if ( pMultisample != nullptr ) {
        writer.push( pMultisample->flags );
        writer.push( pMultisample->rasterizationSamples );
        writer.push( pMultisample->sampleShadingEnable );
        writer.pushFloat( pMultisample->minSampleShading );
        writer.push( pMultisample->pSampleMask != nullptr );
        if ( pMultisample->pSampleMask != nullptr ) {
            const uint32_t numMaskWords =
                ( pMultisample->rasterizationSamples + 31u ) / 32u;
            for ( uint32_t i = 0; i < numMaskWords; i++ ) {
                writer.push( pMultisample->pSampleMask[i] );
            }
        }
        writer.push( pMultisample->alphaToCoverageEnable );
        writer.push( pMultisample->alphaToOneEnable );
    }

    const VkPipelineDepthStencilStateCreateInfo *pDepthStencil =
        psoDesc.pDepthStencilState;
    writer.push( pDepthStencil != nullptr );
    if ( pDepthStencil != nullptr ) {
        writer.push( pDepthStencil->flags );
        writer.push( pDepthStencil->depthBoundsTestEnable );
        if ( !bExtendedDynamicState ) {
            writer.push( pDepthStencil->depthTestEnable );
            writer.push( pDepthStencil->depthWriteEnable );
            writer.push( pDepthStencil->depthCompareOp );
            writer.push( pDepthStencil->stencilTestEnable );
            writer.pushStencilOp( pDepthStencil->front );
            writer.pushStencilOp( pDepthStencil->back );
        }
        writer.pushFloat( pDepthStencil->minDepthBounds );
        writer.pushFloat( pDepthStencil->maxDepthBounds );
    }

    const VkPipelineColorBlendStateCreateInfo *pColorBlend =
        psoDesc.pColorBlendState;
    writer.push( pColorBlend != nullptr );
    if ( pColorBlend != nullptr ) {
        writer.push( pColorBlend->flags );
        writer.push( pColorBlend->logicOpEnable );
        writer.push( pColorBlend->logicOp );
        writer.push( pColorBlend->attachmentCount );
        for ( uint32_t i = 0; i < pColorBlend->attachmentCount; i++ ) {
            const VkPipelineColorBlendAttachmentState &attachment =
                pColorBlend->pAttachments[i];
            writer.push( attachment.blendEnable );
            writer.push( attachment.srcColorBlendFactor );
            writer.push( attachment.dstColorBlendFactor );
            writer.push( attachment.colorBlendOp );
            writer.push( attachment.srcAlphaBlendFactor );
            writer.push( attachment.dstAlphaBlendFactor );
            writer.push( attachment.alphaBlendOp );
            writer.push( attachment.colorWriteMask );
        }
        for ( const float blendConstant : pColorBlend->blendConstants ) {
            writer.pushFloat( blendConstant );
        }
    }

    const VkPipelineDynamicStateCreateInfo *pDynamic = psoDesc.pDynamicState;
    writer.push( pDynamic != nullptr );
    if ( pDynamic != nullptr ) {
        writer.push( pDynamic->flags );
        writer.push( pDynamic->dynamicStateCount );
        for ( uint32_t i = 0; i < pDynamic->dynamicStateCount; i++ ) {
            writer.push( pDynamic->pDynamicStates[i] );
        }
    }

    writer.push( psoDesc.subpass );
    writer.push64( psoLayoutHashcode );
    writer.push64( renderPassHashcode );
}

inline void
FillPushConstantRanges( std::vector<VkPushConstantRange> &pushConstants ) {
    // dxvk will always output three push constants blocks (at least for D3D9)
    //     [0] => D3D9 Render State (for fixed functions emulation; e.g. fog or
    //     point size) (VS/PS) [1] => Samplers (VS) [2] => Samplers (PS)

    // Add RS push constants (always added to the layout no matter what)
    VkPushConstantRange pushConstantsVS = {};
    pushConstantsVS.offset = 0;
    pushConstantsVS.size =
        kDxvkMaxSharedPushDataSize + kDxvkMaxSharedPushDataSize;
    pushConstantsVS.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    pushConstants.push_back( pushConstantsVS );

    VkPushConstantRange pushConstantsPS = {};
    pushConstantsPS.offset = 0;
    pushConstantsPS.size =
        kDxvkMaxSharedPushDataSize + kDxvkMaxSharedPushDataSize;
    pushConstantsPS.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    pushConstants.push_back( pushConstantsPS );
}

void PSOFactory::initialize( VkDevice device,
                             SamplerHeapManager &samplerHeap,
                             ResourceBinder &resourceBinder ) {
    VkPipelineLayoutCreateInfo pipelineLayoutInfos = {};
    pipelineLayoutInfos.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfos.pNext = nullptr;
    pipelineLayoutInfos.flags = 0;

    std::vector<VkPushConstantRange> pushConstants;
    FillPushConstantRanges( pushConstants );

    pipelineLayoutInfos.pushConstantRangeCount =
        static_cast<uint32_t>( pushConstants.size() );
    pipelineLayoutInfos.pPushConstantRanges = pushConstants.data();

    VkDescriptorSetLayout setLayouts[2];
    setLayouts[0] = resourceBinder.DescriptorLayout;
    setLayouts[1] = samplerHeap.DescriptorLayout;

    pipelineLayoutInfos.pSetLayouts = setLayouts;
    pipelineLayoutInfos.setLayoutCount = 2;

    VkResult psoLayout = vkCreatePipelineLayout( device, &pipelineLayoutInfos,
                                                 nullptr, &PipelineLayout );
    OTDU_ASSERT( psoLayout == VK_SUCCESS );

    reset();
}

CachedPSO &PSOFactory::getOrBuildPSO( VkDevice device,
                                      VkPipelineCache psoCache ) {
    if ( !bDirty ) {
        return *latestPSO;
    }
    bDirty = false;

    VkGraphicsPipelineCreateInfo psoInfos;
    fillGraphicsDescriptor( psoInfos );

    psoInfos.layout = PipelineLayout;

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;
    psoInfos.pViewportState = &viewportState;

    OTDU_ASSERT( pVertexLayout != nullptr );

    // Build VkVertexInputAttributeDescriptions by matching each VS ISGN entry
    // to a vertex layout semantic (Usage + UsageIndex). This mirrors DXVK's
    // runtime approach and assigns the correct SPIRV location per attribute.
    std::vector<VkVertexInputAttributeDescription> inputAttribs;
    const ShaderMetadataSPIRVRuntime *pVSMeta =
        pGraphicsShaderStages[0] ? pGraphicsShaderStages[0]->pMetadata
                                 : nullptr;

    if ( pVSMeta && !pVSMeta->InputElements.empty() ) {
        for ( const SPIRVInputElement &isgnElem : pVSMeta->InputElements ) {
            bool bFound = false;
            for ( const GPUVertexLayoutSemantic &sem :
                  pVertexLayout->Semantics ) {
                if ( sem.Usage == isgnElem.Usage &&
                     sem.UsageIndex == isgnElem.UsageIndex ) {
                    VkVertexInputAttributeDescription attrib = {};
                    attrib.location = isgnElem.Location;
                    attrib.binding = sem.Binding;
                    attrib.format = sem.Format;
                    attrib.offset = sem.Offset;
                    inputAttribs.push_back( attrib );
                    bFound = true;
                    break;
                }
            }
#ifdef OTDU_MACOS
            // Use dummy attribute for unbound stream (only required on Apple
            // platforms)
            if ( !bFound && !pVertexLayout->InputBindings.empty() ) {
                const VkVertexInputBindingDescription &dummyBind =
                    pVertexLayout->InputBindings[0];
                const VkFormat dummyFormat =
                    dummyBind.stride >= 16   ? VK_FORMAT_R32G32B32A32_SFLOAT
                    : dummyBind.stride >= 12 ? VK_FORMAT_R32G32B32_SFLOAT
                    : dummyBind.stride >= 8  ? VK_FORMAT_R32G32_SFLOAT
                                             : VK_FORMAT_R32_SFLOAT;

#ifdef OTDU_DEVBUILD
                // One log per (VS hashcode, Usage, UsageIndex) so the absent
                // semantic stays discoverable without spamming every frame.
                static std::set<uint64_t> loggedUnboundStreams;
                const uint64_t vsHash = pGraphicsShaderStages[0]
                                            ? pGraphicsShaderStages[0]->Hashcode
                                            : 0ull;
                const uint64_t key =
                    ( vsHash ^ ( uint64_t( isgnElem.Usage ) << 8 ) ^
                      uint64_t( isgnElem.UsageIndex ) );
                if ( loggedUnboundStreams.insert( key ).second ) {
                    OTDU_LOG_DEBUG(
                        "VS %016llx: input loc=%u %s%u absent from vertex "
                        "layout; substituting dummy attribute on binding %u "
                        "(stride=%u)\n",
                        (unsigned long long)vsHash, isgnElem.Location,
                        DeclUsageToString( isgnElem.Usage ),
                        isgnElem.UsageIndex, dummyBind.binding,
                        dummyBind.stride );
                }
#endif

                VkVertexInputAttributeDescription attrib = {};
                attrib.location = isgnElem.Location;
                attrib.binding = dummyBind.binding;
                attrib.format = dummyFormat;
                attrib.offset = 0;
                inputAttribs.push_back( attrib );
            }
#endif
        }
    } else {
        // Fallback: sequential locations (shaders without ISGN metadata,
        // e.g. imgui).
        for ( uint32_t i = 0;
              i < static_cast<uint32_t>( pVertexLayout->Semantics.size() );
              i++ ) {
            const GPUVertexLayoutSemantic &sem = pVertexLayout->Semantics[i];
            VkVertexInputAttributeDescription attrib = {};
            attrib.location = i;
            attrib.binding = sem.Binding;
            attrib.format = sem.Format;
            attrib.offset = sem.Offset;
            inputAttribs.push_back( attrib );
        }
    }

    VkPipelineVertexInputStateCreateInfo vertexInputInfo = {};
    vertexInputInfo.sType =
        VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.pNext = nullptr;
    vertexInputInfo.flags = 0;
    vertexInputInfo.pVertexBindingDescriptions =
        pVertexLayout->InputBindings.data();
    vertexInputInfo.vertexBindingDescriptionCount =
        static_cast<uint32_t>( pVertexLayout->InputBindings.size() );
    vertexInputInfo.pVertexAttributeDescriptions = inputAttribs.data();
    vertexInputInfo.vertexAttributeDescriptionCount =
        static_cast<uint32_t>( inputAttribs.size() );
    psoInfos.pVertexInputState = &vertexInputInfo;

    std::vector<VkPipelineShaderStageCreateInfo> graphicsShaderStages;
    VkPipelineShaderStageCreateInfo VertexShader = {};
    VertexShader.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    VertexShader.pNext = nullptr;
    VertexShader.stage = VK_SHADER_STAGE_VERTEX_BIT;
    VertexShader.module = pGraphicsShaderStages[0]->ShaderModule;
    VertexShader.pName = "main";
    vsSpecData[0] = 1u;
    vsSpecData[1] = VSClipPlaneCount << 21;
    vsSpecEntries[0] = { 12, 0, sizeof( uint32_t ) };
    vsSpecEntries[1] = { 5, sizeof( uint32_t ), sizeof( uint32_t ) };
    vsSpecInfo.mapEntryCount = 2;
    vsSpecInfo.pMapEntries = vsSpecEntries;
    vsSpecInfo.dataSize = sizeof( vsSpecData );
    vsSpecInfo.pData = vsSpecData;
    VertexShader.pSpecializationInfo = &vsSpecInfo;
    graphicsShaderStages.push_back( VertexShader );

    if ( bIsStageBound[1] ) {
        VkPipelineShaderStageCreateInfo PixelShader = {};
        PixelShader.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        PixelShader.pNext = nullptr;
        PixelShader.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        PixelShader.module = pGraphicsShaderStages[1]->ShaderModule;
        PixelShader.pName = "main";
        PixelShader.pSpecializationInfo = nullptr; // &spinf;
        graphicsShaderStages.push_back( PixelShader );
    }

    psoInfos.pStages = graphicsShaderStages.data();

    BuildPSOHashKey( hashKey, psoInfos, pGraphicsShaderStages,
                     pVertexLayout->InputBindings, inputAttribs,
                     psoLayoutHashcode, renderPassHashcode, VSClipPlaneCount,
                     bExtendedDynamicState );
    const uint64_t psoHashcode = CityHash64(
        (const char *)hashKey.data(), hashKey.size() * sizeof( uint32_t ) );

    auto cacheIt = cache.find( psoHashcode );
    if ( cacheIt != cache.end() ) {
#if OTDU_DEVBUILD
        if ( hashKeys[psoHashcode] != hashKey ) {
            OTDU_LOG_ERROR( "PSO hashcode collision (%016llx)!\n",
                            (unsigned long long)psoHashcode );
            OTDU_TRIGGER_BREAKPOINT;
        }
#endif
        latestPSO = &cacheIt->second;
        return cacheIt->second;
    }

    if ( pParentFactory != nullptr ) {
        auto parentIt = pParentFactory->cache.find( psoHashcode );
        if ( parentIt != pParentFactory->cache.end() ) {
            latestPSO = &parentIt->second;
            return parentIt->second;
        }
    }

    // TODO: This is synchronous for now; ideally we should have an async thread
    // in the background processing PSO compilation requests...
    VkPipeline pso = VK_NULL_HANDLE;

    VkResult psoCreationResult = vkCreateGraphicsPipelines(
        device, psoCache, 1, &psoInfos, nullptr, &pso );
    OTDU_ASSERT( psoCreationResult == VK_SUCCESS );

#if OTDU_DEVBUILD
    std::string objName =
        IntegerToHexString( static_cast<size_t>( psoHashcode ) );
    objName += " (VS: ";
    objName += IntegerToHexString(
        static_cast<size_t>( pGraphicsShaderStages[0]->Hashcode ) );
    if ( bIsStageBound[1] ) {
        objName += " | PS: ";
        objName += IntegerToHexString(
            static_cast<size_t>( pGraphicsShaderStages[1]->Hashcode ) );
    }
    objName += " )";

    VkDebugUtilsObjectNameInfoEXT nameInfo = {};
    nameInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT;
    nameInfo.objectType = VkObjectType::VK_OBJECT_TYPE_PIPELINE;
    nameInfo.objectHandle = (uint64_t)pso;
    nameInfo.pObjectName = objName.c_str();
    vkSetDebugUtilsObjectNameEXT( device, &nameInfo );
#endif

    CachedPSO psoCached;
    psoCached.Hashcode = psoHashcode;
    psoCached.Instance = pso;

    // Bitmask for all stages included in this PSO
    psoCached.TextureBindingMask = 0ull;
    psoCached.TextureBindingMask |= pGraphicsShaderStages[0]->SRVMask;
    if ( pGraphicsShaderStages[1] != nullptr ) {
        psoCached.TextureBindingMask |= pGraphicsShaderStages[1]->SRVMask;
    }

    cache.insert( std::make_pair( psoHashcode, psoCached ) );
#if OTDU_DEVBUILD
    hashKeys[psoHashcode] = hashKey;
#endif

    CachedPSO &cachedPso = cache[psoHashcode];
    latestPSO = &cachedPso;
    return cachedPso;
}

void PSOFactory::releaseCachedPSOs( VkDevice device ) {
    vkDestroyPipelineLayout( device, PipelineLayout, nullptr );

    for ( auto &entry : cache ) {
        vkDestroyPipeline( device, entry.second.Instance, nullptr );
    }
    cache.clear();
#if OTDU_DEVBUILD
    hashKeys.clear();
#endif
}

void PSOFactory::adoptCachedPSOs( VkDevice device, PSOFactory &other ) {
    OTDU_ASSERT( PipelineLayout == other.PipelineLayout );

    for ( auto &entry : other.cache ) {
        if ( !cache.insert( entry ).second ) {
            vkDestroyPipeline( device, entry.second.Instance, nullptr );
        }
    }
    other.cache.clear();
    other.latestPSO = nullptr;
    other.bDirty = true;

#if OTDU_DEVBUILD
    for ( auto &entry : other.hashKeys ) {
        hashKeys.insert( entry );
    }
    other.hashKeys.clear();
#endif
}

void PSOFactory::setShader( eShaderType stage, GPUShader *pShader ) {
    uint32_t stageIndex = ( stage == eShaderType::ST_Vertex ) ? 0 : 1;

    if ( pShader != nullptr ) {
        pGraphicsShaderStages[stageIndex] = pShader;
        bIsStageBound[stageIndex] = true;
    } else {
        pGraphicsShaderStages[stageIndex] = nullptr;
        bIsStageBound[stageIndex] = false;
    }

    bDirty = true;
}

void PSOFactory::reset() {
    VSClipPlaneCount = 0;

    RasterState = {};
    RasterState.pNext = nullptr;
    RasterState.sType =
        VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    RasterState.lineWidth = 1.0f;
    RasterState.frontFace = VK_FRONT_FACE_CLOCKWISE;

    DepthStencil = {};
    DepthStencil.pNext = nullptr;
    DepthStencil.sType =
        VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;

    BlendState = {};
    BlendState.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    BlendState.pNext = nullptr;
    BlendState.flags = 0;
    BlendState.logicOpEnable = VK_FALSE;
    BlendState.attachmentCount = 0;
    BlendState.blendConstants[0] = 1.0f;
    BlendState.blendConstants[1] = 1.0f;
    BlendState.blendConstants[2] = 1.0f;
    BlendState.blendConstants[3] = 1.0f;

    AlphaBlending = {};

    srcBlendFactor = VkBlendFactor::VK_BLEND_FACTOR_ONE;
    dstBlendFactor = VkBlendFactor::VK_BLEND_FACTOR_ZERO;
    blendOP = VkBlendOp::VK_BLEND_OP_MAX_ENUM;

    pGraphicsShaderStages[0] = nullptr;
    pGraphicsShaderStages[1] = nullptr;

    // Viewport and scissors are always set at runtime.
    DynamicState = {};
    DynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    DynamicState.dynamicStateCount = bExtendedDynamicState
                                         ? kVulkanNumPSOExtendedDynamicState
                                         : kVulkanNumPSODynamicState;
    DynamicState.pDynamicStates = bExtendedDynamicState
                                      ? kVulkanPSOExtendedDynamicStates
                                      : kVulkanPSODynamicStates;

    InputAssembly = {};
    InputAssembly.sType =
        VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    InputAssembly.flags = 0;
    InputAssembly.topology =
        VkPrimitiveTopology::VK_PRIMITIVE_TOPOLOGY_POINT_LIST;

    MultisampleState = {};
    MultisampleState.sType =
        VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    MultisampleState.sampleShadingEnable = VK_FALSE;
    MultisampleState.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    renderPass = VK_NULL_HANDLE;

    PrimitiveType = ePrimitiveType::PT_Point;

    psoLayoutHashcode = 0ull;
    renderPassHashcode = 0ull;

    latestPSO = nullptr;
    bDirty = true;
    bDirtyStencilDynamicState = true;
    bDirtyExtendedDynamicState = true;
    bTwoSidedStencil = false;
    bIsStageBound[0] = false;
    bIsStageBound[1] = false;

    pVertexLayout = nullptr;
}

void PSOFactory::fillGraphicsDescriptor(
    VkGraphicsPipelineCreateInfo &psoInfos ) {
    OTDU_ASSERT( bIsStageBound[0] );
    OTDU_ASSERT( renderPass != VK_NULL_HANDLE );

    uint32_t numShaderStages = 1;

    // TDU1 seems to always bind a pixel shader (but let's keep this check just
    // in case...)
    if ( bIsStageBound[1] ) {
        numShaderStages++;
    }

    InputAssembly.topology = kPrimitiveTypes[PrimitiveType];
#ifdef OTDU_MACOS
    // TODO: Vulkan validation complains if this is disabled on Mac (even though
    // it doesn't break visual correctness). Do we really need this? Or is the
    // validation layer wrong?
    // Restart on list topologies requires primitiveTopologyListRestart
    InputAssembly.primitiveRestartEnable =
        PrimitiveType == ePrimitiveType::PT_LineStrip ||
        PrimitiveType == ePrimitiveType::PT_TriangleStrip ||
        PrimitiveType == ePrimitiveType::PT_TriangleFan;
#endif

    psoInfos = {};
    psoInfos.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    psoInfos.pNext = nullptr;
    psoInfos.stageCount = numShaderStages;
    psoInfos.pRasterizationState = &RasterState;
    psoInfos.pDepthStencilState = &DepthStencil;
    psoInfos.pTessellationState = nullptr;
    psoInfos.pColorBlendState = &BlendState;
    psoInfos.pDynamicState = &DynamicState;
    psoInfos.pInputAssemblyState = &InputAssembly;
    psoInfos.renderPass = renderPass;
    psoInfos.subpass = 0;
    psoInfos.basePipelineHandle = VK_NULL_HANDLE;
    psoInfos.pMultisampleState = &MultisampleState;
}
#endif
