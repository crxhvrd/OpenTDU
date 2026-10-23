#include "shared.h"
#include "shader_binary_cache.h"

#include <algorithm>

#include "core/hash/crc64.h"
#include "filesystem/gs_file.h"
#include "render/ed_render.h"
#include "render/gs_render.h"
#include "render/material.h"

#include "render/render_context.h"
#include "render/scene_renderer.h"
#include "shader_table_entry.h"
#include "shader_types.h"

ShaderBinaryCache gShaderBinaryCache = {};

#if OTDU_VULKAN
namespace {
    // D3D9 shader bytecode (DXSO) tokens
    constexpr uint32_t kDxsoVertexShaderVersion = 0xfffe;
    constexpr uint32_t kDxsoOpDcl = 0x1f;
    constexpr uint32_t kDxsoOpComment = 0xfffe;
    constexpr uint32_t kDxsoOpEnd = 0xffff;
    constexpr uint32_t kDxsoRegInput = 1;

    inline uint32_t DxsoRegType( uint32_t token ) {
        return ( ( token >> 28 ) & 0x7 ) | ( ( token >> 8 ) & 0x18 );
    }

    // Vertex shader input signature: one entry per 'dcl_<usage> v#'. The
    // rebuilt modules declare input v# at location #, as dxvk does.
    void ParseDxsoInputSignature( const ShaderBytecode_t &dxso,
                                  ShaderMetadataSPIRVRuntime &metadata ) {
        const uint32_t *pTokens = (const uint32_t *)std::get<0>( dxso );
        const size_t numTokens = std::get<1>( dxso ) / sizeof( uint32_t );
        if ( numTokens == 0 ||
             ( pTokens[0] >> 16 ) != kDxsoVertexShaderVersion ) {
            return;
        }

        size_t offset = 1;
        while ( offset < numTokens ) {
            const uint32_t token = pTokens[offset];
            const uint32_t opcode = token & 0xffff;
            if ( opcode == kDxsoOpEnd ) {
                break;
            }
            if ( opcode == kDxsoOpComment ) {
                offset += 1 + ( ( token >> 16 ) & 0x7fff );
                continue;
            }

            const uint32_t numParams = ( token >> 24 ) & 0xf;
            if ( opcode == kDxsoOpDcl && numParams == 2 &&
                 offset + 2 < numTokens ) {
                const uint32_t usage = pTokens[offset + 1];
                const uint32_t dst = pTokens[offset + 2];
                if ( DxsoRegType( dst ) == kDxsoRegInput ) {
                    SPIRVInputElement element = {};
                    element.Location = dst & 0x7ff;
                    element.Usage = static_cast<uint8_t>( usage & 0x1f );
                    element.UsageIndex =
                        static_cast<uint8_t>( ( usage >> 16 ) & 0xf );
                    metadata.InputElements.push_back( element );
                }
            }
            offset += 1 + numParams;
        }
        metadata.NumInputElements =
            static_cast<uint32_t>( metadata.InputElements.size() );
    }

    // SPIR-V module reflection (the subset the D3D9 ABI uses, see
    // resources/shaders/include/d3d9_abi.glsl)
    constexpr uint32_t kSpirvMagic = 0x07230203;
    constexpr uint32_t kSpirvHeaderSize = 5;
    constexpr uint32_t kSpirvOpTypeImage = 25;
    constexpr uint32_t kSpirvOpTypeArray = 28;
    constexpr uint32_t kSpirvOpTypeRuntimeArray = 29;
    constexpr uint32_t kSpirvOpTypeStruct = 30;
    constexpr uint32_t kSpirvOpTypePointer = 32;
    constexpr uint32_t kSpirvOpVariable = 59;
    constexpr uint32_t kSpirvOpDecorate = 71;
    constexpr uint32_t kSpirvDecorationFlat = 14;
    constexpr uint32_t kSpirvDecorationLocation = 30;
    constexpr uint32_t kSpirvDecorationBinding = 33;
    constexpr uint32_t kSpirvDecorationDescriptorSet = 34;
    constexpr uint32_t kSpirvStorageClassUniformConstant = 0;
    constexpr uint32_t kSpirvStorageClassInput = 1;
    constexpr uint32_t kSpirvStorageClassUniform = 2;
    constexpr uint32_t kSpirvStorageClassOutput = 3;
    constexpr uint32_t kSpirvDim3D = 2;
    constexpr uint32_t kSpirvDimCube = 3;
    constexpr uint32_t kSpirvUnset = 0xffffffffu;

    // Descriptor set 0 (constants, clip planes, spec state, textures). Set 1
    // is the sampler heap, bound by the pipeline layout.
    constexpr uint32_t kShaderResourceSet = 0;

    // render_state.sampler_idx[8]: two 16-bit sampler heap indices per dword.
    constexpr uint32_t kSamplerPushConstantsSize = 8 * sizeof( uint32_t );

    struct SpirvId {
        uint32_t Opcode = 0;
        uint32_t Operand = kSpirvUnset; // pointee/element type, image dim
        uint32_t DescriptorSet = kSpirvUnset;
        uint32_t Binding = kSpirvUnset;
        uint32_t Location = kSpirvUnset;
        uint32_t StorageClass = kSpirvUnset; // OpVariable only
        bool bFlat = false;
    };

    bool ReflectSPIRV( const ShaderBytecode_t &spirv,
                       ShaderMetadataSPIRVRuntime &metadata ) {
        const uint32_t *pWords = (const uint32_t *)std::get<0>( spirv );
        const size_t numWords = std::get<1>( spirv ) / sizeof( uint32_t );
        if ( numWords < kSpirvHeaderSize || pWords[0] != kSpirvMagic ) {
            return false;
        }

        std::vector<SpirvId> ids( pWords[3] );
        std::vector<uint32_t> variables;
        size_t offset = kSpirvHeaderSize;
        while ( offset < numWords ) {
            const uint32_t opcode = pWords[offset] & 0xffff;
            const uint32_t numWordsOp = pWords[offset] >> 16;
            if ( numWordsOp == 0 || offset + numWordsOp > numWords ) {
                return false;
            }
            const uint32_t *pOperands = pWords + offset + 1;

            switch ( opcode ) {
            case kSpirvOpDecorate:
                if ( numWordsOp >= 3 && pOperands[0] < ids.size() ) {
                    SpirvId &target = ids[pOperands[0]];
                    if ( pOperands[1] == kSpirvDecorationFlat ) {
                        target.bFlat = true;
                    } else if ( numWordsOp >= 4 ) {
                        if ( pOperands[1] == kSpirvDecorationLocation ) {
                            target.Location = pOperands[2];
                        } else if ( pOperands[1] == kSpirvDecorationBinding ) {
                            target.Binding = pOperands[2];
                        } else if ( pOperands[1] ==
                                    kSpirvDecorationDescriptorSet ) {
                            target.DescriptorSet = pOperands[2];
                        }
                    }
                }
                break;
            case kSpirvOpTypeImage:
                if ( numWordsOp >= 4 && pOperands[0] < ids.size() ) {
                    ids[pOperands[0]].Opcode = opcode;
                    ids[pOperands[0]].Operand = pOperands[2];
                }
                break;
            case kSpirvOpTypeArray:
            case kSpirvOpTypeRuntimeArray:
            case kSpirvOpTypeStruct:
                if ( numWordsOp >= 2 && pOperands[0] < ids.size() ) {
                    ids[pOperands[0]].Opcode = opcode;
                    if ( numWordsOp >= 3 ) {
                        ids[pOperands[0]].Operand = pOperands[1];
                    }
                }
                break;
            case kSpirvOpTypePointer:
                if ( numWordsOp >= 4 && pOperands[0] < ids.size() ) {
                    ids[pOperands[0]].Opcode = opcode;
                    ids[pOperands[0]].Operand = pOperands[2];
                }
                break;
            case kSpirvOpVariable:
                if ( numWordsOp >= 4 && pOperands[1] < ids.size() ) {
                    ids[pOperands[1]].Opcode = opcode;
                    ids[pOperands[1]].Operand = pOperands[0];
                    ids[pOperands[1]].StorageClass = pOperands[2];
                    variables.push_back( pOperands[1] );
                }
                break;
            default:
                break;
            }
            offset += numWordsOp;
        }

        for ( uint32_t variableId : variables ) {
            const SpirvId &variable = ids[variableId];
            if ( variable.StorageClass == kSpirvStorageClassInput ||
                 variable.StorageClass == kSpirvStorageClassOutput ) {
                // Built-ins (gl_Position, ...) have no location.
                if ( variable.Location >= 32 ) {
                    continue;
                }
                const uint32_t locationBit = 1u << variable.Location;
                if ( variable.StorageClass == kSpirvStorageClassOutput ) {
                    metadata.OutputMask |= locationBit;
                    continue;
                }
                metadata.InputMask |= locationBit;
                if ( variable.bFlat ) {
                    metadata.FlatShadingInputs |= locationBit;
                }
                continue;
            }

            if ( ( variable.StorageClass != kSpirvStorageClassUniformConstant &&
                   variable.StorageClass != kSpirvStorageClassUniform ) ||
                 variable.DescriptorSet != kShaderResourceSet ||
                 variable.Binding == kSpirvUnset ||
                 variable.Operand >= ids.size() ) {
                continue;
            }

            uint32_t typeId = ids[variable.Operand].Operand;
            while ( typeId < ids.size() &&
                    ( ids[typeId].Opcode == kSpirvOpTypeArray ||
                      ids[typeId].Opcode == kSpirvOpTypeRuntimeArray ) ) {
                typeId = ids[typeId].Operand;
            }
            if ( typeId >= ids.size() ) {
                continue;
            }

            SPIRVBindingInfo binding = {};
            binding.set = variable.DescriptorSet;
            binding.binding = variable.Binding;
            binding.resourceIndex = variable.Binding;
            if ( ids[typeId].Opcode == kSpirvOpTypeImage ) {
                binding.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
                binding.viewType = ( ids[typeId].Operand == kSpirvDimCube )
                                       ? VK_IMAGE_VIEW_TYPE_CUBE
                                   : ( ids[typeId].Operand == kSpirvDim3D )
                                       ? VK_IMAGE_VIEW_TYPE_3D
                                       : VK_IMAGE_VIEW_TYPE_2D;
                binding.access = VK_ACCESS_SHADER_READ_BIT;
            } else if ( ids[typeId].Opcode == kSpirvOpTypeStruct ) {
                binding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
                binding.access = VK_ACCESS_UNIFORM_READ_BIT;
            } else {
                continue;
            }
            metadata.Bindings.push_back( binding );
        }

        std::sort( metadata.Bindings.begin(), metadata.Bindings.end(),
                   []( const SPIRVBindingInfo &a, const SPIRVBindingInfo &b ) {
                       return a.binding < b.binding;
                   } );
        metadata.NumBindings =
            static_cast<uint32_t>( metadata.Bindings.size() );
        metadata.SamplerPushConstantsSize = kSamplerPushConstantsSize;

        return true;
    }
} // namespace
#endif

ShaderBinaryCache::ShaderBinaryCache() {}

ShaderBinaryCache::~ShaderBinaryCache() {
    shaderBinaries.clear();
    for ( auto &table : shaderTableBinary ) {
        if ( table.second != nullptr ) {
            TestDrive::Free( table.second );
        }
    }
    shaderTableBinary.clear();

#if OTDU_VULKAN
    shaderMetadata.clear();
    spirvBinaries.clear();
#endif
}

bool ShaderBinaryCache::loadShaderTable(
    const std::string &shaderTablePath,
    std::unordered_map<uint64_t, ShaderBytecode_t> &outBinaries ) {
    OTDU_LOG_DEBUG( "Loading shader table '%s'...\n", shaderTablePath.c_str() );

    void *pShaderTable = nullptr;
    uint32_t contentSize = 0;
    bool bFoundShaderTable = gpFile->loadFile( shaderTablePath.c_str(),
                                               &pShaderTable, &contentSize );
    if ( !bFoundShaderTable ) {
        OTDU_LOG_ERROR( "Shader table '%s' does not exist\n",
                        shaderTablePath.c_str() );
        return false;
    }

    shaderTableBinary.insert( std::make_pair( shaderTablePath, pShaderTable ) );

    ShaderTableHeader header;
    header.NumEntries = *(uint64_t *)pShaderTable;

    int8_t *pShaderContent = (int8_t *)pShaderTable;
    const uint64_t tableSizeInBytes = contentSize;
    const uint64_t headerSizeInBytes = header.GetHeaderSize();

    if ( header.NumEntries == 0ull || headerSizeInBytes > tableSizeInBytes ) {
        OTDU_LOG_ERROR(
            "Shader table '%s' is corrupt or was extracted by another "
            "build: its header claims %llu entries (%llu bytes) but the "
            "file is only %u bytes. Delete 'EURO/Shaders' and "
            "'opentdu.txt', then relaunch to force a re-extraction.\n",
            shaderTablePath.c_str(), header.NumEntries, headerSizeInBytes,
            contentSize );
        return false;
    }

    ShaderTableHeaderEntry *pHeaderIt =
        (ShaderTableHeaderEntry *)( (int8_t *)pShaderTable +
                                    sizeof( uint64_t ) );
    for ( uint64_t i = 0; i < header.NumEntries; i++ ) {
        if ( pHeaderIt->Offset < headerSizeInBytes ||
             pHeaderIt->Offset + pHeaderIt->Size > tableSizeInBytes ) {
            OTDU_LOG_ERROR(
                "Shader table '%s' is corrupt: entry %llu (hashcode "
                "0x%016llx) bytecode range [%llu, %llu) is outside the %u "
                "byte table\n",
                shaderTablePath.c_str(), i, pHeaderIt->Hashcode,
                pHeaderIt->Offset, pHeaderIt->Offset + pHeaderIt->Size,
                contentSize );
            return false;
        }

        outBinaries.insert(
            std::make_pair( pHeaderIt->Hashcode,
                            std::make_tuple( pShaderContent + pHeaderIt->Offset,
                                             pHeaderIt->Size ) ) );
        pHeaderIt++;
    }

    OTDU_LOG_INFO( "Successfully parsed '%s' (found %llu shaders in table)\n",
                   shaderTablePath.c_str(), header.NumEntries );
    return true;
}

ShaderBytecode_t
ShaderBinaryCache::getOrLoadBytecode( const ShaderTableEntry *pTableEntry ) {
    std::string shaderTablePath = kShadersRootFolder;
    shaderTablePath += kShadersD3D9Folder;
    shaderTablePath += pTableEntry->pShaderCategory;
    shaderTablePath += kShaderTableExtension;
    if ( shaderTableBinary.find( shaderTablePath ) == shaderTableBinary.end() &&
         !loadShaderTable( shaderTablePath, shaderBinaries ) ) {
        OTDU_LOG_ERROR( "Failed to register shader 0x%016llx: shader table "
                        "'%s' could not be loaded\n",
                        pTableEntry->Hashcode, pTableEntry->pShaderCategory );
        return std::make_tuple( nullptr, 0ull );
    }

    // Cache shader (note that we do not create the shader yet; this will be
    // done on its first use)
    auto shaderBinIt = shaderBinaries.find( pTableEntry->Hashcode );
    if ( shaderBinIt == shaderBinaries.end() ) {
        OTDU_LOG_ERROR( "Failed to register shader 0x%016llx from shader table "
                        "'%s': shader not found in hashtable!\n",
                        pTableEntry->Hashcode, pTableEntry->pShaderCategory );
        return std::make_tuple( nullptr, 0ull );
    }

#if OTDU_VULKAN
    return loadSPIRV( pTableEntry, shaderBinIt->second );
#else
    return shaderBinIt->second;
#endif
}

#if OTDU_VULKAN
ShaderBytecode_t
ShaderBinaryCache::loadSPIRV( const ShaderTableEntry *pTableEntry,
                              const ShaderBytecode_t &dxso ) {
    std::string packPath = kShadersRootFolder;
    packPath += kShadersRebuiltPack;
    if ( shaderTableBinary.find( packPath ) == shaderTableBinary.end() &&
         !loadShaderTable( packPath, spirvBinaries ) ) {
        // Keeps the failed load from being retried for every shader.
        shaderTableBinary.insert( std::make_pair( packPath, nullptr ) );
    }

    auto it = spirvBinaries.find( pTableEntry->Hashcode );
    if ( it == spirvBinaries.end() ) {
        OTDU_LOG_ERROR( "Failed to load shader 0x%016llx (shader table '%s'): "
                        "missing from '%s'\n",
                        pTableEntry->Hashcode, pTableEntry->pShaderCategory,
                        packPath.c_str() );
        return std::make_tuple( nullptr, 0ull );
    }

    if ( shaderMetadata.find( pTableEntry->Hashcode ) ==
         shaderMetadata.end() ) {
        ShaderMetadataSPIRVRuntime metadata = {};
        if ( !ReflectSPIRV( it->second, metadata ) ) {
            OTDU_LOG_ERROR( "Failed to load shader 0x%016llx: invalid SPIR-V\n",
                            pTableEntry->Hashcode );
            return std::make_tuple( nullptr, 0ull );
        }
        ParseDxsoInputSignature( dxso, metadata );
        shaderMetadata.insert(
            std::make_pair( pTableEntry->Hashcode, metadata ) );
    }
    return it->second;
}
#endif

#if OTDU_VULKAN
ShaderMetadataSPIRVRuntime *
ShaderBinaryCache::getMetadataSPIRV( const ShaderTableEntry *pTableEntry ) {
    auto shaderIt = shaderMetadata.find( pTableEntry->Hashcode );
    return ( shaderIt != shaderMetadata.end() ) ? &shaderIt->second : nullptr;
}
#endif
