#include "shared.h"
#include "shader_binary_cache.h"

#include "core/arg_parser.h"
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

static bool gbUseRebuiltShaders = false;
static CmdLineArg CmdLineArgRebuiltShaders( "rebuilt_shaders",
                                            []( const char *pArg ) {
                                                gbUseRebuiltShaders = true;
                                            } );

ShaderBinaryCache::ShaderBinaryCache() {}

ShaderBinaryCache::~ShaderBinaryCache() {
    shaderMetadata.clear();
    shaderBinaries.clear();
    for ( auto &table : shaderTableBinary ) {
        TestDrive::Free( table.second );
    }
    for ( auto &rebuilt : rebuiltBinaries ) {
        if ( std::get<0>( rebuilt.second ) != nullptr ) {
            TestDrive::Free( std::get<0>( rebuilt.second ) );
        }
    }
    rebuiltBinaries.clear();
    shaderTableBinary.clear();
}

ShaderBytecode_t
ShaderBinaryCache::getOrLoadBytecode( const ShaderTableEntry *pTableEntry ) {
    auto it = shaderTableBinary.find( pTableEntry->pShaderCategory );
    if ( it == shaderTableBinary.end() ) {
        std::string shaderTablePath = kShadersRootFolder;
#if OTDU_D3D9
        shaderTablePath += kShadersD3D9Folder;
#elif OTDU_VULKAN
        shaderTablePath += kShadersVulkanFolder;
#elif OTDU_OPENGL
        shaderTablePath += kShadersOpenGLFolder;
#endif
        shaderTablePath += pTableEntry->pShaderCategory;
        shaderTablePath += kShaderTableExtension;

        OTDU_LOG_DEBUG( "Loading shader table '%s'...\n",
                        shaderTablePath.c_str() );

        void *pShaderTable = nullptr;
        uint32_t contentSize = 0;
        bool bFoundShaderTable = gpFile->loadFile(
            shaderTablePath.c_str(), &pShaderTable, &contentSize );
        if ( !bFoundShaderTable ) {
            OTDU_LOG_ERROR( "Failed to register shader '%s' (hashcode "
                            "0x%016llx): shader table '%s' does not exist\n",
                            shaderTablePath.c_str(), pTableEntry->Hashcode,
                            pTableEntry->pShaderCategory );
            return std::make_tuple( nullptr, 0ull );
        }

        shaderTableBinary.insert(
            std::make_pair( pTableEntry->pShaderCategory, pShaderTable ) );

        ShaderTableHeader header;
        header.NumEntries = *(uint64_t *)pShaderTable;

        int8_t *pShaderContent = (int8_t *)pShaderTable;
        const uint64_t tableSizeInBytes = contentSize;
        const uint64_t headerSizeInBytes = header.GetHeaderSize();

        OTDU_LOG_DEBUG(
            "  '%s': %u bytes on disk, NumEntries=%llu (header: %llu bytes)\n",
            shaderTablePath.c_str(), contentSize, header.NumEntries,
            headerSizeInBytes );

        if ( header.NumEntries == 0ull ||
             headerSizeInBytes > tableSizeInBytes ) {
            OTDU_LOG_ERROR(
                "Shader table '%s' is corrupt or was extracted by another "
                "build: its header claims %llu entries (%llu bytes) but the "
                "file is only %u bytes. Delete 'EURO/Shaders' and "
                "'opentdu.txt', then relaunch to force a re-extraction.\n",
                shaderTablePath.c_str(), header.NumEntries, headerSizeInBytes,
                contentSize );
            return std::make_tuple( nullptr, 0ull );
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
                return std::make_tuple( nullptr, 0ull );
            }

            int8_t *pShaderPointer = pShaderContent + pHeaderIt->Offset;

            shaderBinaries.insert( std::make_pair(
                pHeaderIt->Hashcode,
                std::make_tuple( pShaderPointer, pHeaderIt->Size ) ) );

            if ( pHeaderIt->MetadataOffset != kInvalidMetadataOffset ) {
                if ( pHeaderIt->MetadataOffset < headerSizeInBytes ||
                     pHeaderIt->MetadataOffset + sizeof( ShaderMetadataSPIRV ) >
                         tableSizeInBytes ) {
                    OTDU_LOG_ERROR(
                        "Shader table '%s' is corrupt: entry %llu (hashcode "
                        "0x%016llx) metadata offset %llu is outside the %u "
                        "byte table\n",
                        shaderTablePath.c_str(), i, pHeaderIt->Hashcode,
                        pHeaderIt->MetadataOffset, contentSize );
                    return std::make_tuple( nullptr, 0ull );
                }

                ShaderMetadataSPIRV *pShaderMetadataPointer =
                    (ShaderMetadataSPIRV *)( pShaderContent +
                                             pHeaderIt->MetadataOffset );

                const uint64_t metadataSizeInBytes =
                    sizeof( ShaderMetadataSPIRV ) +
                    static_cast<uint64_t>(
                        pShaderMetadataPointer->NumBindings ) *
                        sizeof( SPIRVBindingInfo ) +
                    static_cast<uint64_t>(
                        pShaderMetadataPointer->NumInputElements ) *
                        sizeof( SPIRVInputElement );

                OTDU_LOG_DEBUG(
                    "  entry %llu (hashcode 0x%016llx): bytecode @%llu (%llu "
                    "bytes), metadata @%llu (NumBindings=%u, "
                    "NumInputElements=%u, %llu bytes)\n",
                    i, pHeaderIt->Hashcode, pHeaderIt->Offset, pHeaderIt->Size,
                    pHeaderIt->MetadataOffset,
                    pShaderMetadataPointer->NumBindings,
                    pShaderMetadataPointer->NumInputElements,
                    metadataSizeInBytes );

                if ( pHeaderIt->MetadataOffset + metadataSizeInBytes >
                     tableSizeInBytes ) {
                    OTDU_LOG_ERROR(
                        "Shader table '%s' is corrupt: entry %llu (hashcode "
                        "0x%016llx) metadata range [%llu, %llu) is outside the "
                        "%u byte table (NumBindings=%u, NumInputElements=%u)\n",
                        shaderTablePath.c_str(), i, pHeaderIt->Hashcode,
                        pHeaderIt->MetadataOffset,
                        pHeaderIt->MetadataOffset + metadataSizeInBytes,
                        contentSize, pShaderMetadataPointer->NumBindings,
                        pShaderMetadataPointer->NumInputElements );
                    return std::make_tuple( nullptr, 0ull );
                }

                ShaderMetadataSPIRVRuntime runtimeInfos;
                runtimeInfos.InputMask = pShaderMetadataPointer->InputMask;
                runtimeInfos.OutputMask = pShaderMetadataPointer->OutputMask;
                runtimeInfos.FlatShadingInputs =
                    pShaderMetadataPointer->FlatShadingInputs;
                runtimeInfos.SamplerPushConstantsSize =
                    pShaderMetadataPointer->SamplerPushConstantsSize;
                runtimeInfos.NumBindings = pShaderMetadataPointer->NumBindings;
                runtimeInfos.NumInputElements =
                    pShaderMetadataPointer->NumInputElements;
                runtimeInfos.Bindings.resize( runtimeInfos.NumBindings );

                SPIRVBindingInfo *pBindings =
                    (SPIRVBindingInfo *)( pShaderMetadataPointer + 1 );
                for ( auto &binding : runtimeInfos.Bindings ) {
                    binding = *pBindings;
                    pBindings++;
                }

                runtimeInfos.InputElements.resize(
                    runtimeInfos.NumInputElements );
                const SPIRVInputElement *pInputElements =
                    (const SPIRVInputElement *)pBindings;
                for ( auto &elem : runtimeInfos.InputElements ) {
                    elem = *pInputElements;
                    pInputElements++;
                }

                shaderMetadata.insert(
                    std::make_pair( pHeaderIt->Hashcode, runtimeInfos ) );
            }
            pHeaderIt++;
        }

        OTDU_LOG_INFO(
            "Successfully parsed '%s' (found %llu shaders in table)\n",
            shaderTablePath.c_str(), header.NumEntries );
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
    if ( gbUseRebuiltShaders ) {
        ShaderBytecode_t rebuilt = getRebuiltBytecode( pTableEntry );
        if ( std::get<0>( rebuilt ) != nullptr ) {
            return rebuilt;
        }
    }
#endif

    return shaderBinIt->second;
}

ShaderBytecode_t
ShaderBinaryCache::getRebuiltBytecode( const ShaderTableEntry *pTableEntry ) {
    auto it = rebuiltBinaries.find( pTableEntry->Hashcode );
    if ( it != rebuiltBinaries.end() ) {
        return it->second;
    }

    char hashcode[17];
    snprintf( hashcode, sizeof( hashcode ), "%016llX",
              (unsigned long long)pTableEntry->Hashcode );

    std::string path = kShadersRootFolder;
    path += kShadersRebuiltFolder;
    path += hashcode;
    path += ( pTableEntry->ShaderStage == eShaderType::ST_Vertex )
                ? ".vert.spv"
                : ".frag.spv";

    void *pBytecode = nullptr;
    uint32_t bytecodeSize = 0;
    ShaderBytecode_t rebuilt = std::make_tuple( nullptr, 0ull );
    if ( gpFile->fileExists( path.c_str() ) &&
         gpFile->loadFile( path.c_str(), &pBytecode, &bytecodeSize ) ) {
        OTDU_LOG_INFO( "Using rebuilt shader '%s'\n", path.c_str() );
        rebuilt = std::make_tuple( pBytecode, (uint64_t)bytecodeSize );
    }
    rebuiltBinaries.insert( std::make_pair( pTableEntry->Hashcode, rebuilt ) );
    return rebuilt;
}

ShaderMetadataSPIRVRuntime *
ShaderBinaryCache::getMetadataSPIRV( const ShaderTableEntry *pTableEntry ) {
    auto shaderIt = shaderMetadata.find( pTableEntry->Hashcode );
    return ( shaderIt != shaderMetadata.end() ) ? &shaderIt->second : nullptr;
}
