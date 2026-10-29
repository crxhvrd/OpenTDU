#include "shared.h"
#include "resource_binder.h"

#if OTDU_VULKAN

#include "render/2db.h"
#include "render/shaders/shader_types.h"
#include "render/shaders/shader_binary_cache.h"
#include "render_device.h"

static constexpr uint32_t kVulkanBindingSlotVSUniforms = 0;
static constexpr uint32_t kVulkanBindingSlotVSClipPlanes = 3;
static constexpr uint32_t kVulkanBindingSlotPSUniforms = 10;
static constexpr uint32_t kVulkanBindingSlotPSShared = 12;
static constexpr uint32_t kVulkanBindingSlotPSTextures = 13;
static constexpr uint32_t kVulkanBindingSlotShaderSpec = 31;

static constexpr uint32_t kVulkanNumSetPerPool = 4096;

static constexpr uint32_t kVulkanClipInfosSize = 6 * 4 * sizeof( float );
static constexpr uint32_t kVulkanShaderSpecsUBOSize = 15 * sizeof( uint32_t );

static constexpr uint32_t kVulkanNumAllocsPerFrame = 32768;
static constexpr uint32_t kNumFramesUnusedBeforeSetDestroyed =
    32; // Num of frames before a descriptor set is considered unused and can
        // be released

enum eDirtyStates : uint32_t {
    DS_VSConstants = 1 << 1,
    DS_PSConstants = 1 << 2,
    DS_VSClipPlanes = 1 << 3,
    DS_ShaderSpecs = 1 << 4,
    DS_PSShared = 1 << 5,
    DS_PSTextures = 1 << 6,
};

inline static void FillGlobalDescriptorLayout(
    std::vector<VkDescriptorSetLayoutBinding> &bindings ) {
    VkDescriptorSetLayoutBinding binding = {};
    binding.descriptorCount = 1;
    binding.pImmutableSamplers = nullptr;

    // 0: Uniforms (VS)
    binding.binding = kVulkanBindingSlotVSUniforms;
    binding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
    binding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    bindings.push_back( binding );

    // 3: Clip Planes (VS)
    binding.binding = kVulkanBindingSlotVSClipPlanes;
    binding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
    binding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    bindings.push_back( binding );

    // 10: Uniforms (PS)
    binding.binding = kVulkanBindingSlotPSUniforms;
    binding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
    binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    bindings.push_back( binding );

    // 12: Shared Scalars (PS)
    binding.binding = kVulkanBindingSlotPSShared;
    binding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
    binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    bindings.push_back( binding );

    // 13 .. 20: texture resource views
    binding.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    for ( uint32_t j = kVulkanSRVStartBindingSlot;
          j < ( kVulkanSRVStartBindingSlot + kVulkanMaxNumSRV ); j++ ) {
        binding.binding = j;
        bindings.push_back( binding );
    }

    // 31: Shader Specs
    binding.binding = kVulkanBindingSlotShaderSpec;
    binding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
    binding.stageFlags =
        VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    bindings.push_back( binding );
}

void ResourceBinder::initialize( VkDevice device,
                                 VmaAllocator allocator,
                                 VkDeviceSize uboAlignment ) {
    std::vector<VkDescriptorSetLayoutBinding> bindings;
    FillGlobalDescriptorLayout( bindings );

    VkDescriptorSetLayoutCreateInfo setInfos = {};
    setInfos.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    setInfos.pNext = nullptr;
    setInfos.bindingCount = static_cast<uint32_t>( bindings.size() );
    setInfos.flags = 0;
    setInfos.pBindings = bindings.data();

    VkResult op = vkCreateDescriptorSetLayout( device, &setInfos, nullptr,
                                               &DescriptorLayout );
    OTDU_ASSERT( op == VK_SUCCESS );

    // Descriptor pools
    VkDescriptorPoolSize poolSize[2] = {};
    poolSize[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
    poolSize[0].descriptorCount =
        kVulkanNumUBOPerSet * kVulkanNumSetPerPool * PendingFrameCount;
    poolSize[1].type = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    poolSize[1].descriptorCount =
        kVulkanMaxNumSRV * kVulkanNumSetPerPool * PendingFrameCount;

    VkDescriptorPoolCreateInfo poolInfo;
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.pNext = nullptr;
    poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT;
    poolInfo.maxSets = kVulkanNumSetPerPool * PendingFrameCount;
    poolInfo.poolSizeCount = 2u;
    poolInfo.pPoolSizes = poolSize;

    op = vkCreateDescriptorPool( device, &poolInfo, nullptr,
                                 &DescriptorSetPool );
    OTDU_ASSERT( op == VK_SUCCESS );

    initializeAllocators( device, allocator, uboAlignment );

    memset( clipPlanes, 0, kVulkanClipInfosSize );
    memset( shaderSpecs, 0, sizeof( uint32_t ) * 6 );

    for ( uint32_t i = 0; i < 0x10; i++ ) {
        boundSRVs[i] = VK_NULL_HANDLE;
        pBoundSRVs[i] = nullptr;
    }

    numConstantVectorsUsed[0] = 1;
    numConstantVectorsUsed[1] = 1;

    dirtyStates = 0xffffffff;
    uboOffsets.fill( 0 );
}

void ResourceBinder::destroy( VkDevice device, VmaAllocator allocator ) {
    destroyAllocators( device, allocator );

    vkDestroyDescriptorPool( device, DescriptorSetPool, nullptr );
    vkDestroyDescriptorSetLayout( device, DescriptorLayout, nullptr );

    for ( uint32_t i = 0; i < PendingFrameCount; i++ ) {
        allocatedSets[i].clear();
        setLookup[i].clear();
        freeSetSlots[i].clear();
        recycledSets[i].clear();
    }
}

void ResourceBinder::initializeAllocators( VkDevice device,
                                           VmaAllocator allocator,
                                           VkDeviceSize uboAlignment ) {
    for ( uint32_t i = 0; i < PendingFrameCount; i++ ) {
        for ( uint32_t j = 0; j < 2; j++ ) {
            constantsAllocator[j][i].create(
                device, allocator, sizeof( ShaderConstants ),
                kVulkanNumAllocsPerFrame, uboAlignment );
        }
        clipPlanesAllocator[i].create( device, allocator, kVulkanClipInfosSize,
                                       kVulkanNumAllocsPerFrame, uboAlignment );
        shaderSpecsAllocator[i].create(
            device, allocator, kVulkanShaderSpecsUBOSize,
            kVulkanNumAllocsPerFrame, uboAlignment );
        psSharedAllocator[i].create( device, allocator,
                                     kVulkanShaderSpecsUBOSize,
                                     kVulkanNumAllocsPerFrame, uboAlignment );
    }
}

void ResourceBinder::destroyAllocators( VkDevice device,
                                        VmaAllocator allocator ) {
    for ( uint32_t i = 0; i < PendingFrameCount; i++ ) {
        for ( uint32_t j = 0; j < 2; j++ ) {
            constantsAllocator[j][i].destroy( device, allocator );
        }
        clipPlanesAllocator[i].destroy( device, allocator );
        shaderSpecsAllocator[i].destroy( device, allocator );
        psSharedAllocator[i].destroy( device, allocator );
    }
}

void ResourceBinder::onFrameBegin( VkDevice device, uint32_t frameIndex ) {
    DescriptorSet = VK_NULL_HANDLE;
    dirtyStates = 0xffffffff;

    for ( uint32_t j = 0; j < 2; j++ ) {
        constantsAllocator[j][frameIndex].onFrameBegin();
    }
    clipPlanesAllocator[frameIndex].onFrameBegin();
    shaderSpecsAllocator[frameIndex].onFrameBegin();
    psSharedAllocator[frameIndex].onFrameBegin();

    std::vector<CachedDescriptorSet> &sets = allocatedSets[frameIndex];
    for ( uint32_t i = 0; i < sets.size(); i++ ) {
        CachedDescriptorSet &set = sets[i];
        set.NumFramesUnused++;

        if ( set.DescriptorSet != VK_NULL_HANDLE &&
             set.NumFramesUnused >= kNumFramesUnusedBeforeSetDestroyed ) {
            eraseFromLookup( frameIndex, set.SRVBindingHashcode, i );
            recycledSets[frameIndex].push_back( set.DescriptorSet );
            freeSetSlots[frameIndex].push_back( i );
            set.DescriptorSet = VK_NULL_HANDLE;
            set.SRVBindingHashcode = 0;
        }
    }

    memset( clipPlanes, 0, kVulkanClipInfosSize );
    memset( shaderSpecs, 0, sizeof( uint32_t ) * 6 );

    for ( uint32_t i = 0; i < 0x10; i++ ) {
        boundSRVs[i] = VK_NULL_HANDLE;
        pBoundSRVs[i] = nullptr;
    }

    uboOffsets.fill( 0 );
}

VkDescriptorSet ResourceBinder::getOrAllocateSet( VkDevice device,
                                                  uint64_t bindingMask,
                                                  uint32_t frameIndex ) {
    if ( ( dirtyStates & eDirtyStates::DS_PSTextures ) != 0 ||
         DescriptorSet == VK_NULL_HANDLE ) {
        // Compute descriptor set hashcode.
        // Note that we only hash SRVs; as the UBOs are guaranteed to be the
        // same (always emitted by dxvk).
        uint64_t hashcode =
            CityHash64( (const char *)boundSRVs, sizeof( VkImageView ) * 0x10 );

        CachedDescriptorSet &descSet =
            findOrAllocateSet( device, hashcode, frameIndex );
        DescriptorSet = descSet.DescriptorSet;
    }

    if ( ( dirtyStates & eDirtyStates::DS_VSConstants ) != 0 ) {
        uploadConstants( 0, frameIndex, 0 );
    }

    if ( ( dirtyStates & eDirtyStates::DS_VSClipPlanes ) != 0 ) {
        uboOffsets[1] = clipPlanesAllocator[frameIndex].allocate();
        memcpy( clipPlanesAllocator[frameIndex].pCPUMapping + uboOffsets[1],
                clipPlanes, kVulkanClipInfosSize );

        vmaFlushAllocation( allocator,
                            clipPlanesAllocator[frameIndex].Allocations,
                            uboOffsets[1], kVulkanClipInfosSize );
    }

    if ( ( dirtyStates & eDirtyStates::DS_PSConstants ) != 0 ) {
        uploadConstants( 1, frameIndex, 2 );
    }

    if ( ( dirtyStates & eDirtyStates::DS_PSShared ) != 0 ) {
        uboOffsets[3] = psSharedAllocator[frameIndex].allocate();
        memcpy( psSharedAllocator[frameIndex].pCPUMapping + uboOffsets[3],
                &shaderSpecs, sizeof( uint32_t ) * 6 );

        vmaFlushAllocation( allocator,
                            psSharedAllocator[frameIndex].Allocations,
                            uboOffsets[3], sizeof( uint32_t ) * 6 );
    }

    if ( ( dirtyStates & eDirtyStates::DS_ShaderSpecs ) != 0 ) {
        uboOffsets[4] = shaderSpecsAllocator[frameIndex].allocate();
        memcpy( shaderSpecsAllocator[frameIndex].pCPUMapping + uboOffsets[4],
                &shaderSpecs, sizeof( uint32_t ) * 6 );

        vmaFlushAllocation( allocator,
                            shaderSpecsAllocator[frameIndex].Allocations,
                            uboOffsets[4], sizeof( uint32_t ) * 6 );
    }

    dirtyStates = 0;

    return DescriptorSet;
}

void ResourceBinder::uploadConstants( const uint32_t stageIndex,
                                      const uint32_t frameIndex,
                                      const uint32_t uboIndex ) {
    LinearAllocator &constantsAlloc =
        constantsAllocator[stageIndex][frameIndex];

    const uint32_t zeroedSize = constantsAlloc.ZeroedSize;
    const uint32_t offset = constantsAlloc.allocate();
    const uint32_t uploadSize =
        numConstantVectorsUsed[stageIndex] * 4 * sizeof( float );
    memcpy( constantsAlloc.pCPUMapping + offset, constants[stageIndex].Vectors,
            uploadSize );

    // A slot used for the first time was just zero-filled; flush all of it.
    const bool bFreshSlot =
        ( offset + constantsAlloc.ElementSize ) > zeroedSize;
    vmaFlushAllocation( allocator, constantsAlloc.Allocations, offset,
                        bFreshSlot ? constantsAlloc.ElementSize : uploadSize );

    uboOffsets[uboIndex] = offset;
}

void ResourceBinder::setFloatConstants( eShaderType stage,
                                        float *pFloats,
                                        const uint32_t offset,
                                        const uint32_t numVectors ) {
    OTDU_ASSERT( stage == eShaderType::ST_Vertex ||
                 stage == eShaderType::ST_Pixel );

    uint32_t numScalars = ( numVectors * 4 );
    OTDU_ASSERT_FATAL( offset + numVectors <= kNumShaderConstants );
    uint32_t stageIndex = ( stage == eShaderType::ST_Vertex ) ? 0 : 1;
    float *pDst = &constants[stageIndex].Vectors[( offset + 1 ) * 4];
    if ( memcmp( pDst, pFloats, numScalars * sizeof( float ) ) == 0 ) {
        return;
    }
    memcpy( pDst, pFloats, numScalars * sizeof( float ) );
    numConstantVectorsUsed[stageIndex] =
        Max( numConstantVectorsUsed[stageIndex], offset + 1 + numVectors );

    dirtyStates |= ( ( stageIndex == 0 ) ? eDirtyStates::DS_VSConstants
                                         : eDirtyStates::DS_PSConstants );
}

void ResourceBinder::setClipPlane( int32_t param_1, float *param_2 ) {
    for ( int32_t i = 0; i < 4; i++ ) {
        clipPlanes[param_1][i] = param_2[i];
    }

    dirtyStates |= eDirtyStates::DS_VSClipPlanes;
}

uint32_t ResourceBinder::setClipPlaneMask( const uint32_t mask ) {
    uint32_t count = 0;
    for ( uint32_t i = 0; i < 6; i++ ) {
        if ( ( mask & ( 1u << i ) ) != 0 ) {
            count = i + 1;
        }
    }
    for ( uint32_t i = 0; i < count; i++ ) {
        if ( ( mask & ( 1u << i ) ) == 0 ) {
            memset( clipPlanes[i], 0, sizeof( clipPlanes[i] ) );
        }
    }

    dirtyStates |= eDirtyStates::DS_VSClipPlanes;
    return count;
}

void ResourceBinder::bindTexture( Texture *pTexture,
                                  const uint32_t frameIndex,
                                  const uint32_t index ) {
    VkImageView newView = VK_NULL_HANDLE;
    GPUTexture *pVulkanTex = nullptr;

    // Set constant to bind the right sampler type (required by dxvk pipeline)
    uint32_t samplerType = 0u; // SamplerTypeTexture2D
    if ( pTexture != nullptr ) {
        pVulkanTex = pTexture->pTexture;
        uint32_t resourceIndex = frameIndex;
        if ( pVulkanTex->NumResources <= frameIndex ) {
            resourceIndex = ( pVulkanTex->NumResources - 1 );
        }
        OTDU_ASSERT( pVulkanTex->ImageView[resourceIndex] != VK_NULL_HANDLE );
        newView = pVulkanTex->ImageView[resourceIndex];

        const bool bIs3D = ( ( pTexture->Flags >> 0xb ) & 1u ) != 0;
        const bool bIsCube = !bIs3D && ( ( pTexture->Flags >> 6 ) & 1u ) != 0;
        samplerType = bIs3D ? 1u : ( bIsCube ? 2u : 0u );
    }

    setShaderSpecBits( 0, index * 2u, 2u, samplerType );

    if ( boundSRVs[index] == newView ) {
        return;
    }

    boundSRVs[index] = newView;
    pBoundSRVs[index] = pVulkanTex;
    dirtyStates |= eDirtyStates::DS_PSTextures;
}

void ResourceBinder::setShaderSpecBits( uint32_t dwordIndex,
                                        uint32_t bitIndex,
                                        uint32_t numBits,
                                        uint32_t value ) {
    OTDU_ASSERT( dwordIndex < 6 );

    // Yoinked from dxvk (d3d9_spec_constants.h)
    uint32_t mask = uint32_t( ( 1u << numBits ) - 1 ) << bitIndex;
    const uint32_t newSpecs =
        ( shaderSpecs[dwordIndex] & ~mask ) | ( ( value << bitIndex ) & mask );
    if ( newSpecs == shaderSpecs[dwordIndex] ) {
        return;
    }

    shaderSpecs[dwordIndex] = newSpecs;
    dirtyStates |= eDirtyStates::DS_ShaderSpecs;
}

void ResourceBinder::validateSRVBindings( GPUShader *pPixelShader ) {
    const ShaderMetadataSPIRVRuntime *pMetadata = pPixelShader->pMetadata;
    OTDU_ASSERT( pMetadata != nullptr );

    for ( const SPIRVBindingInfo &binding : pMetadata->Bindings ) {
        if ( binding.resourceIndex < kVulkanSRVStartBindingSlot ) {
            OTDU_ASSERT_FATAL( binding.descriptorType !=
                               VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE );
            continue;
        }

        if ( binding.descriptorType != VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE ) {
            continue;
        }

        uint32_t relativeIndex =
            binding.resourceIndex - kVulkanSRVStartBindingSlot;
        if ( pBoundSRVs[relativeIndex] == nullptr ) {
            OTDU_LOG_WARN(
                "Missing SRV at index %u (ResourceBinder index %u)!\n",
                binding.resourceIndex, relativeIndex );
        }
    }
}

ResourceBinder::CachedDescriptorSet &ResourceBinder::findOrAllocateSet(
    VkDevice device, const uint64_t hashcode, uint32_t frameIndex ) {
    auto range = setLookup[frameIndex].equal_range( hashcode );
    for ( auto it = range.first; it != range.second; it++ ) {
        CachedDescriptorSet &set = allocatedSets[frameIndex][it->second];
        if ( memcmp( set.BoundSRVs, boundSRVs, sizeof( VkImageView ) * 0x10 ) ==
             0 ) {
            set.NumFramesUnused = 0;
            return set;
        }
    }

    // No matching set; need to allocate a new one
    const uint32_t slotIndex = findFreeOrEmplace( frameIndex );
    setLookup[frameIndex].emplace( hashcode, slotIndex );

    CachedDescriptorSet &cachedSet = allocatedSets[frameIndex][slotIndex];
    cachedSet.SRVBindingHashcode = hashcode;
    cachedSet.NumFramesUnused = 0;
    memcpy( cachedSet.BoundSRVs, boundSRVs, sizeof( VkImageView ) * 0x10 );

    cachedSet.DescriptorSet = acquireDescriptorSet( device, frameIndex );

    // Write new descriptor set
    VkDescriptorBufferInfo shaderSpecDesc =
        shaderSpecsAllocator[frameIndex].getCurrentBufferInfo();

    VkDescriptorBufferInfo clipInfosDesc =
        clipPlanesAllocator[frameIndex].getCurrentBufferInfo();

    VkDescriptorBufferInfo vsConstantsDesc =
        constantsAllocator[0][frameIndex].getCurrentBufferInfo();

    VkDescriptorBufferInfo psConstantsDesc =
        constantsAllocator[1][frameIndex].getCurrentBufferInfo();

    VkDescriptorBufferInfo psSharedDesc =
        psSharedAllocator[frameIndex].getCurrentBufferInfo();

    std::array<VkWriteDescriptorSet, kVulkanMaxNumSRV + 5> descWrites;
    descWrites[0] = {};
    descWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descWrites[0].dstSet = cachedSet.DescriptorSet;
    descWrites[0].dstBinding = kVulkanBindingSlotShaderSpec;
    descWrites[0].dstArrayElement = 0;
    descWrites[0].descriptorCount = 1;
    descWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
    descWrites[0].pBufferInfo = &shaderSpecDesc;

    descWrites[1] = {};
    descWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descWrites[1].dstSet = cachedSet.DescriptorSet;
    descWrites[1].dstBinding = kVulkanBindingSlotVSClipPlanes;
    descWrites[1].dstArrayElement = 0;
    descWrites[1].descriptorCount = 1;
    descWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
    descWrites[1].pBufferInfo = &clipInfosDesc;

    descWrites[2] = {};
    descWrites[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descWrites[2].dstSet = cachedSet.DescriptorSet;
    descWrites[2].dstBinding = kVulkanBindingSlotVSUniforms;
    descWrites[2].dstArrayElement = 0;
    descWrites[2].descriptorCount = 1;
    descWrites[2].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
    descWrites[2].pBufferInfo = &vsConstantsDesc;

    descWrites[3] = {};
    descWrites[3].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descWrites[3].dstSet = cachedSet.DescriptorSet;
    descWrites[3].dstBinding = kVulkanBindingSlotPSUniforms;
    descWrites[3].dstArrayElement = 0;
    descWrites[3].descriptorCount = 1;
    descWrites[3].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
    descWrites[3].pBufferInfo = &psConstantsDesc;

    descWrites[4] = {};
    descWrites[4].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descWrites[4].dstSet = cachedSet.DescriptorSet;
    descWrites[4].dstBinding = kVulkanBindingSlotPSShared;
    descWrites[4].dstArrayElement = 0;
    descWrites[4].descriptorCount = 1;
    descWrites[4].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
    descWrites[4].pBufferInfo = &psSharedDesc;

    uint32_t numUpdates = 5;
    std::array<VkDescriptorImageInfo, kVulkanMaxNumSRV> srvHeapInitImageInfos;
    for ( uint32_t i = 0; i < kVulkanMaxNumSRV; i++ ) {
        if ( boundSRVs[i] == VK_NULL_HANDLE ) {
            continue;
        }

        srvHeapInitImageInfos[i] = {};
        srvHeapInitImageInfos[i].imageView = boundSRVs[i];
        srvHeapInitImageInfos[i].imageLayout =
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        VkWriteDescriptorSet &descriptorWrite = descWrites[numUpdates++];
        descriptorWrite = {};
        descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrite.pNext = nullptr;
        descriptorWrite.dstSet = cachedSet.DescriptorSet;
        descriptorWrite.dstBinding = kVulkanSRVStartBindingSlot + i;
        descriptorWrite.dstArrayElement = 0;
        descriptorWrite.descriptorCount = 1;
        descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
        descriptorWrite.pImageInfo = &srvHeapInitImageInfos[i];
    }

    vkUpdateDescriptorSets( device, numUpdates, descWrites.data(), 0, nullptr );

    return cachedSet;
}

uint32_t ResourceBinder::findFreeOrEmplace( uint32_t frameIndex ) {
    if ( !freeSetSlots[frameIndex].empty() ) {
        const uint32_t slotIndex = freeSetSlots[frameIndex].back();
        freeSetSlots[frameIndex].pop_back();
        return slotIndex;
    }

    allocatedSets[frameIndex].emplace_back();
    return static_cast<uint32_t>( allocatedSets[frameIndex].size() - 1 );
}

void ResourceBinder::eraseFromLookup( uint32_t frameIndex,
                                      const uint64_t hashcode,
                                      const uint32_t slotIndex ) {
    auto range = setLookup[frameIndex].equal_range( hashcode );
    for ( auto it = range.first; it != range.second; it++ ) {
        if ( it->second == slotIndex ) {
            setLookup[frameIndex].erase( it );
            return;
        }
    }
}

VkDescriptorSet ResourceBinder::acquireDescriptorSet( VkDevice device,
                                                      uint32_t frameIndex ) {
    if ( !recycledSets[frameIndex].empty() ) {
        VkDescriptorSet recycled = recycledSets[frameIndex].back();
        recycledSets[frameIndex].pop_back();
        return recycled;
    }

    VkDescriptorSetAllocateInfo setInfo = {};
    setInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    setInfo.pNext = nullptr;
    setInfo.descriptorPool = DescriptorSetPool;
    setInfo.descriptorSetCount = 1u;
    setInfo.pSetLayouts = &DescriptorLayout;

    VkDescriptorSet set = VK_NULL_HANDLE;
    VkResult op = vkAllocateDescriptorSets( device, &setInfo, &set );
    OTDU_ASSERT_FATAL( op == VK_SUCCESS );

    return set;
}

bool LinearAllocator::create( VkDevice device,
                              VmaAllocator allocator,
                              uint32_t elementSize,
                              uint32_t maxNumElements,
                              VkDeviceSize uboAlignment ) {
    Capacity = elementSize * maxNumElements;
    ElementSize = elementSize;
    Usage = 0;
    ZeroedSize = 0;
    Alignment = uboAlignment;

    VkBufferCreateInfo bufferInfo = {};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.pNext = nullptr;
    bufferInfo.size = Capacity;
    bufferInfo.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    bufferInfo.flags = 0;

    VmaAllocationCreateInfo allocInfo = {};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
    allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;

    VkResult opResult = vmaCreateBuffer( allocator, &bufferInfo, &allocInfo,
                                         &Buffers, &Allocations, nullptr );
    if ( opResult != VK_SUCCESS ) {
        OTDU_ASSERT( false );
        return false;
    }
    opResult = vmaMapMemory( allocator, Allocations, (void **)&pCPUMapping );
    return true;
}

void LinearAllocator::destroy( VkDevice device, VmaAllocator allocator ) {
    vmaUnmapMemory( allocator, Allocations );
    vmaDestroyBuffer( allocator, Buffers, Allocations );
}

uint32_t LinearAllocator::allocate() {
    OTDU_ASSERT_FATAL( Usage + ElementSize <= Capacity );

    uint32_t offset = Usage;
    Usage += ElementSize;

    // Uploads may only cover part of a slot; never leave uninitialized
    // memory behind the uploaded range.
    if ( offset + ElementSize > ZeroedSize ) {
        memset( pCPUMapping + offset, 0, ElementSize );
        ZeroedSize = offset + ElementSize;
    }

    // Align offset to match device minUniformBufferOffsetAlignment
    Usage = Align( Usage, (uint32_t)Alignment );

    return offset;
}

void LinearAllocator::onFrameBegin() { Usage = 0; }

VkDescriptorBufferInfo LinearAllocator::getCurrentBufferInfo() const {
    VkDescriptorBufferInfo bufferInfo = {};
    bufferInfo.buffer = Buffers;
    bufferInfo.offset = 0;
    bufferInfo.range = ElementSize;
    return bufferInfo;
}
#endif