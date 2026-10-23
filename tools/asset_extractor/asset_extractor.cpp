#include "asset_extractor.h"

#include "shared.h"

#include "core/assert.h"
#include "core/logger.h"

#include "render/shaders/shader_binary_cache.h"
#include "render/shaders/shader_table_master.h"

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <vector>

#if OTDU_EXTRACTOR_DXVK
#include "dxvk/src/d3d9/d3d9_constant_layout.h"
#include "dxvk/src/dxso/dxso_analysis.h"
#include "dxvk/src/dxso/dxso_compiler.h"
#include "dxvk/src/dxso/dxso_module.h"
#include "dxvk/src/dxso/dxso_reader.h"
#endif

namespace {

    struct TestDrivePackedIniEntry {
        uint32_t OffsetInBytes =
            0u; // Absolute offset in game executable (MC 1.66A)
        uint32_t LengthInBytes = 0u;
        const char *Name = nullptr;
    };

    constexpr TestDrivePackedIniEntry kSystemPCIni = {
        0x00f9c460, 0x357, "SystemPC.ini" }; // 0x5B082A0A
    constexpr TestDrivePackedIniEntry kGamePCIni = {
        0x00f9c7b8, 0x14ab, "GamePC.ini" }; // 0x5B093D1A
    constexpr TestDrivePackedIniEntry kDevicesPCIni = {
        0x00f9dc68, 0x1545, "DevicesPC.ini" }; // 0x5B083611
    constexpr TestDrivePackedIniEntry kAudioIni = { 0x00f9f1b0, 0x1265,
                                                    "Audio.ini" }; // 0x0d070707
    constexpr TestDrivePackedIniEntry kReplayIni = {
        0x00fa0418, 0x407, "Replay.ini" }; // 0x5B093715
    constexpr TestDrivePackedIniEntry kPhysicsIni = {
        0x00fa0820, 0xdfe, "Physics.ini" }; // 0x5B182213

    constexpr uint32_t kPackedIniFileCount = 6;
    constexpr TestDrivePackedIniEntry kPackedIniFiles[kPackedIniFileCount] = {
        kSystemPCIni, kGamePCIni, kDevicesPCIni,
        kAudioIni,    kReplayIni, kPhysicsIni };

    // Bundles the extraction settings shared across the helpers below.
    struct ExtractorContext {
        std::string OutputPath;
        bool bForceExtraction = false;
        bool bForceShaderExtraction = false;
        AssetExtractor::ShaderSourceGenerator *pSourceGenerator = nullptr;

        inline bool shouldOverwriteShaderTables() const {
            return bForceExtraction || bForceShaderExtraction;
        }
    };

    constexpr uint32_t kTangentReflectionPS[] = { 0x00d95b18, 0x00d95f48 };
    constexpr uint32_t kTangentReflectionVS[] = { 0x00d95758, 0x00d96c78,
                                                  0x00d97080, 0x00d97488 };
    constexpr uint32_t kTangentBasisRegister = 5;

    constexpr uint32_t kDxsoOpAdd = 0x02;
    constexpr uint32_t kDxsoOpMad = 0x04;
    constexpr uint32_t kDxsoOpMul = 0x05;
    constexpr uint32_t kDxsoOpDp3 = 0x08;
    constexpr uint32_t kDxsoOpMov = 0x01;
    constexpr uint32_t kDxsoOpDcl = 0x1f;
    constexpr uint32_t kDxsoOpComment = 0xfffe;
    constexpr uint32_t kDxsoOpEnd = 0xffff;

    constexpr uint32_t kDxsoRegTemp = 0;
    constexpr uint32_t kDxsoRegConst = 2;
    constexpr uint32_t kDxsoRegTexture = 3;
    constexpr uint32_t kDxsoRegTexCoordOut = 6;

    constexpr uint32_t kDxsoMaxTempPS20 = 12;
    constexpr uint32_t kDxsoMaskXYZ = 0x7;

    inline uint32_t DxsoInstruction( uint32_t opcode, uint32_t numParams ) {
        return opcode | ( numParams << 24 );
    }

    inline uint32_t DxsoRegType( uint32_t token ) {
        return ( ( token >> 28 ) & 0x7 ) | ( ( token >> 8 ) & 0x18 );
    }

    inline uint32_t DxsoRegNumber( uint32_t token ) { return token & 0x7ff; }

    inline uint32_t DxsoWriteMask( uint32_t token ) {
        return ( token >> 16 ) & 0xf;
    }

    inline uint32_t DxsoRegTypeBits( uint32_t type ) {
        return ( ( type & 0x7 ) << 28 ) | ( ( type & 0x18 ) << 8 );
    }

    inline uint32_t DxsoDst( uint32_t type, uint32_t number, uint32_t mask ) {
        return 0x80000000 | DxsoRegTypeBits( type ) | ( mask << 16 ) | number;
    }

    inline uint32_t DxsoSrc( uint32_t type, uint32_t number ) {
        return 0x80000000 | DxsoRegTypeBits( type ) | ( 0xe4 << 16 ) | number;
    }

    // Broadcasts one component of a (possibly swizzled) source operand.
    inline uint32_t DxsoReplicate( uint32_t src, uint32_t component ) {
        uint32_t swizzle = ( src >> 16 ) & 0xff;
        uint32_t channel = ( swizzle >> ( component * 2 ) ) & 0x3;
        return ( src & ~0x00ff0000u ) | ( ( channel * 0x55 ) << 16 );
    }

    // fn( offset, opcode, numParams ) for every instruction token (SM2+).
    template <typename Fn>
    void DxsoForEachInstruction( const std::vector<uint32_t> &tokens, Fn fn ) {
        size_t offset = 1;
        while ( offset < tokens.size() ) {
            uint32_t token = tokens[offset];
            uint32_t opcode = token & 0xffff;
            if ( opcode == kDxsoOpEnd ) {
                return;
            }
            if ( opcode == kDxsoOpComment ) {
                offset += 1 + ( ( token >> 16 ) & 0x7fff );
                continue;
            }

            uint32_t numParams = ( token >> 24 ) & 0xf;
            if ( offset + numParams >= tokens.size() ) {
                return;
            }
            fn( offset, opcode, numParams );
            offset += 1 + numParams;
        }
    }

    // After each dp3 writing the tangent-space view vector (c119 - v0), copy
    // the basis vector it projected onto to oT5 (T) / oT6 (TxN) / oT7 (N).
    bool PatchTangentReflectionVS( std::vector<uint32_t> &tokens ) {
        uint32_t viewTemp = UINT32_MAX;
        std::array<size_t, 3> insertOffsets = {};
        std::array<uint32_t, 3> basisSources = {};
        uint32_t foundComponents = 0;
        bool bBasisRegisterUsed = false;

        DxsoForEachInstruction(
            tokens, [&]( size_t offset, uint32_t opcode, uint32_t numParams ) {
                if ( opcode == kDxsoOpDcl || numParams == 0 ) {
                    return;
                }

                uint32_t dst = tokens[offset + 1];
                if ( DxsoRegType( dst ) == kDxsoRegTexCoordOut &&
                     DxsoRegNumber( dst ) >= kTangentBasisRegister ) {
                    bBasisRegisterUsed = true;
                }

                if ( opcode == kDxsoOpAdd && numParams == 3 &&
                     DxsoRegType( dst ) == kDxsoRegTemp ) {
                    for ( uint32_t i = 2; i <= numParams; i++ ) {
                        uint32_t src = tokens[offset + i];
                        if ( DxsoRegType( src ) == kDxsoRegConst &&
                             DxsoRegNumber( src ) == 119 ) {
                            viewTemp = DxsoRegNumber( dst );
                        }
                    }
                }

                if ( opcode == kDxsoOpDp3 &&
                     DxsoRegType( dst ) == kDxsoRegTexCoordOut ) {
                    uint32_t src0 = tokens[offset + 2];
                    if ( DxsoRegType( src0 ) != kDxsoRegTemp ||
                         DxsoRegNumber( src0 ) != viewTemp ) {
                        return;
                    }

                    uint32_t mask = DxsoWriteMask( dst );
                    uint32_t component = ( mask == 0x1 )   ? 0
                                         : ( mask == 0x2 ) ? 1
                                         : ( mask == 0x4 ) ? 2
                                                           : 3;
                    if ( component < 3 ) {
                        insertOffsets[component] = offset + 1 + numParams;
                        basisSources[component] = tokens[offset + 3];
                        foundComponents |= 1 << component;
                    }
                }
            } );

        if ( foundComponents != 0x7 || bBasisRegisterUsed ) {
            return false;
        }

        // Insert from the back so the recorded offsets stay valid.
        std::array<uint32_t, 3> order = { 0, 1, 2 };
        std::sort( order.begin(), order.end(), [&]( uint32_t a, uint32_t b ) {
            return insertOffsets[a] > insertOffsets[b];
        } );
        for ( uint32_t component : order ) {
            const uint32_t mov[3] = {
                DxsoInstruction( kDxsoOpMov, 2 ),
                DxsoDst( kDxsoRegTexCoordOut, kTangentBasisRegister + component,
                         kDxsoMaskXYZ ),
                basisSources[component] };
            tokens.insert( tokens.begin() + insertOffsets[component],
                           std::begin( mov ), std::end( mov ) );
        }
        return true;
    }

    // Rebuild the object-space reflection (R.x * T + R.y * TxN + R.z * N) from
    // t5-t7 and feed it to the three dp3 against c8-c10.
    bool PatchTangentReflectionPS( std::vector<uint32_t> &tokens ) {
        size_t dclEndOffset = 0;
        size_t firstDp3Offset = SIZE_MAX;
        std::array<size_t, 3> dp3Offsets = {};
        uint32_t reflectionSource = 0;
        uint32_t foundRows = 0;
        uint32_t maxTemp = 0;
        bool bBasisRegisterUsed = false;
        bool bMismatchingSource = false;

        DxsoForEachInstruction(
            tokens, [&]( size_t offset, uint32_t opcode, uint32_t numParams ) {
                if ( opcode == kDxsoOpDcl ) {
                    dclEndOffset = offset + 1 + numParams;
                    uint32_t dst = tokens[offset + 2];
                    if ( DxsoRegType( dst ) == kDxsoRegTexture &&
                         DxsoRegNumber( dst ) >= kTangentBasisRegister ) {
                        bBasisRegisterUsed = true;
                    }
                    return;
                }

                for ( uint32_t i = 1; i <= numParams; i++ ) {
                    uint32_t param = tokens[offset + i];
                    if ( DxsoRegType( param ) == kDxsoRegTemp ) {
                        maxTemp = std::max( maxTemp, DxsoRegNumber( param ) );
                    }
                }

                if ( opcode != kDxsoOpDp3 ) {
                    return;
                }

                uint32_t src1 = tokens[offset + 3];
                uint32_t row = DxsoRegNumber( src1 ) - 8;
                if ( DxsoRegType( src1 ) != kDxsoRegConst || row > 2 ) {
                    return;
                }

                uint32_t src0 = tokens[offset + 2];
                if ( foundRows != 0 && src0 != reflectionSource ) {
                    bMismatchingSource = true;
                }
                reflectionSource = src0;
                dp3Offsets[row] = offset;
                foundRows |= 1 << row;
                firstDp3Offset = std::min( firstDp3Offset, offset );
            } );

        uint32_t freeTemp = maxTemp + 1;
        if ( foundRows != 0x7 || bBasisRegisterUsed || bMismatchingSource ||
             freeTemp >= kDxsoMaxTempPS20 ) {
            return false;
        }

        for ( size_t dp3Offset : dp3Offsets ) {
            tokens[dp3Offset + 2] = DxsoSrc( kDxsoRegTemp, freeTemp );
        }

        const uint32_t rebuild[] = {
            DxsoInstruction( kDxsoOpMul, 3 ),
            DxsoDst( kDxsoRegTemp, freeTemp, kDxsoMaskXYZ ),
            DxsoSrc( kDxsoRegTexture, kTangentBasisRegister ),
            DxsoReplicate( reflectionSource, 0 ),
            DxsoInstruction( kDxsoOpMad, 4 ),
            DxsoDst( kDxsoRegTemp, freeTemp, kDxsoMaskXYZ ),
            DxsoSrc( kDxsoRegTexture, kTangentBasisRegister + 1 ),
            DxsoReplicate( reflectionSource, 1 ),
            DxsoSrc( kDxsoRegTemp, freeTemp ),
            DxsoInstruction( kDxsoOpMad, 4 ),
            DxsoDst( kDxsoRegTemp, freeTemp, kDxsoMaskXYZ ),
            DxsoSrc( kDxsoRegTexture, kTangentBasisRegister + 2 ),
            DxsoReplicate( reflectionSource, 2 ),
            DxsoSrc( kDxsoRegTemp, freeTemp ) };
        tokens.insert( tokens.begin() + firstDp3Offset, std::begin( rebuild ),
                       std::end( rebuild ) );

        for ( uint32_t i = 0; i < 3; i++ ) {
            const uint32_t dcl[3] = {
                DxsoInstruction( kDxsoOpDcl, 2 ), 0x80000000,
                DxsoDst( kDxsoRegTexture, kTangentBasisRegister + i,
                         kDxsoMaskXYZ ) };
            tokens.insert( tokens.begin() + dclEndOffset + i * 3,
                           std::begin( dcl ), std::end( dcl ) );
        }
        return true;
    }

    template <size_t N>
    bool IsOffsetInList( const uint32_t ( &list )[N], uint64_t offset ) {
        return std::find( std::begin( list ), std::end( list ), offset ) !=
               std::end( list );
    }

    void PatchRetailShaderBytecode( const ShaderTableEntry &entry,
                                    std::vector<int8_t> &bytecode ) {
        bool bPatchVS =
            entry.ShaderStage == eShaderType::ST_Vertex &&
            IsOffsetInList( kTangentReflectionVS, entry.OffsetInExecutable );
        bool bPatchPS =
            entry.ShaderStage == eShaderType::ST_Pixel &&
            IsOffsetInList( kTangentReflectionPS, entry.OffsetInExecutable );
        if ( !bPatchVS && !bPatchPS ) {
            return;
        }

        // The extracted stream stops on the first 0xffff word, so the end
        // token may be truncated; keep the trailing bytes untouched.
        size_t numTokens = bytecode.size() / sizeof( uint32_t );
        std::vector<uint32_t> tokens( numTokens );
        memcpy( tokens.data(), bytecode.data(),
                numTokens * sizeof( uint32_t ) );
        std::vector<int8_t> tail(
            bytecode.begin() + numTokens * sizeof( uint32_t ), bytecode.end() );

        bool bPatched = bPatchVS ? PatchTangentReflectionVS( tokens )
                                 : PatchTangentReflectionPS( tokens );
        if ( !bPatched ) {
            OTDU_LOG_WARN( "Failed to patch tangent space reflection in shader "
                           "%llx (offset %llx); keeping retail bytecode\n",
                           (unsigned long long)entry.Hashcode,
                           (unsigned long long)entry.OffsetInExecutable );
            return;
        }

        bytecode.resize( tokens.size() * sizeof( uint32_t ) );
        memcpy( bytecode.data(), tokens.data(),
                tokens.size() * sizeof( uint32_t ) );
        bytecode.insert( bytecode.end(), tail.begin(), tail.end() );
    }

    // Unpack .ini (in place).
    void UnpackIniFromMemory( int8_t *pBuffer,
                              const uint32_t bufferSizeInBytes ) {
        int8_t *pBufferStart = pBuffer;
        int8_t *pBufferEnd = pBuffer + bufferSizeInBytes;

        int8_t currentByte = 0x0;
        while ( pBufferStart != pBufferEnd ) {
            int8_t readByte = *pBufferStart;
            readByte ^= currentByte;
            *pBufferStart = readByte;
            pBufferStart++;
            currentByte = readByte;
        }
    }

    std::string GetIniOutputPath( const ExtractorContext &ctx ) {
        return ctx.OutputPath + "/";
    }

    std::string GetShaderOutputPath( const ExtractorContext &ctx ) {
        return ctx.OutputPath + "/" + kShadersRootFolder;
    }

    void CheckAndCreateOutputFolder( const std::string &folder ) {
        std::filesystem::path folderFS = std::filesystem::path( folder );
        if ( !std::filesystem::exists( folderFS ) ) {
            OTDU_LOG_DEBUG( "'%s' does not exist yet; creating directory...\n",
                            folder.c_str() );
            std::filesystem::create_directories( folderFS );
        }
    }

    void WriteShaderTableHeaderToStream( const ShaderTableHeader &header,
                                         const size_t shaderBytecodeSize,
                                         std::ofstream &outputFileStream ) {
        uint64_t tableSize = header.GetHeaderSize();
        const uint64_t numEntries = header.NumEntries;
        outputFileStream.write( (const char *)&numEntries, sizeof( uint64_t ) );
        for ( auto &headerEntry : header.Entries ) {
            uint64_t offsetInFile = headerEntry.Offset;
            offsetInFile += tableSize;

            uint64_t metadataOffsetInFile = headerEntry.MetadataOffset;
            if ( metadataOffsetInFile != kInvalidMetadataOffset ) {
                metadataOffsetInFile += ( shaderBytecodeSize + tableSize );
            }

            outputFileStream.write( (const char *)&headerEntry.Hashcode,
                                    sizeof( uint64_t ) );
            outputFileStream.write( (const char *)&offsetInFile,
                                    sizeof( uint64_t ) );
            outputFileStream.write( (const char *)&headerEntry.Size,
                                    sizeof( uint64_t ) );
            outputFileStream.write( (const char *)&metadataOffsetInFile,
                                    sizeof( uint64_t ) );
        }
    }

    bool WriteShaderTableToDisk( const ExtractorContext &ctx,
                                 const ShaderTable &shaderTable,
                                 const std::string &fileOutputPath ) {
        std::filesystem::path folderFS =
            std::filesystem::path( fileOutputPath );
        if ( !ctx.shouldOverwriteShaderTables() &&
             std::filesystem::exists( folderFS ) ) {
            OTDU_LOG_INFO( "'%s' already exists; skipping...\n",
                           fileOutputPath.c_str() );
            return false;
        }

        std::ofstream outputFileStream;
        outputFileStream.open( fileOutputPath.c_str(),
                               std::ios_base::out | std::ios_base::binary );

        WriteShaderTableHeaderToStream(
            shaderTable.Header, shaderTable.Shaders.size(), outputFileStream );
        outputFileStream.write( (const char *)shaderTable.Shaders.data(),
                                sizeof( char ) * shaderTable.Shaders.size() );
        outputFileStream.write( (const char *)shaderTable.Metadata.data(),
                                sizeof( char ) * shaderTable.Metadata.size() );
        outputFileStream.close();

        OTDU_LOG_INFO( "Wrote shader table '%s'\n", fileOutputPath.c_str() );
        return true;
    }

    bool WriteShaderTableSourceToDisk( const ExtractorContext &ctx,
                                       const std::string &shaderTable,
                                       const std::string &fileOutputPath ) {
        std::filesystem::path folderFS =
            std::filesystem::path( fileOutputPath );
        if ( !ctx.shouldOverwriteShaderTables() &&
             std::filesystem::exists( folderFS ) ) {
            OTDU_LOG_INFO( "'%s' already exists; skipping...\n",
                           fileOutputPath.c_str() );
            return false;
        }

        std::ofstream outputFileStream;
        outputFileStream.open( fileOutputPath.c_str(), std::ios_base::out );
        outputFileStream.write( shaderTable.data(),
                                sizeof( char ) * shaderTable.size() );
        outputFileStream.close();

        OTDU_LOG_INFO( "Wrote shader table source '%s'\n",
                       fileOutputPath.c_str() );
        return true;
    }

    void WriteShaderTablesToDisk( const ExtractorContext &ctx,
                                  const std::string &currentTableName,
                                  ShaderTable &tableDXSO,
                                  ShaderTable &tableSPIRV,
                                  std::string &tableSource,
                                  uint32_t &shaderExtractedCount ) {
        if ( tableDXSO.Header.Empty() ) {
            return;
        }

        std::string filename = currentTableName + kShaderTableExtension;

        std::string fileOutputPath =
            GetShaderOutputPath( ctx ) + kShadersD3D9Folder + filename;
        bool bWroteToDisk =
            WriteShaderTableToDisk( ctx, tableDXSO, fileOutputPath );
        if ( bWroteToDisk ) {
            shaderExtractedCount++;
        }

#if OTDU_EXTRACTOR_DXVK
        fileOutputPath =
            GetShaderOutputPath( ctx ) + kShadersVulkanFolder + filename;
        bWroteToDisk =
            WriteShaderTableToDisk( ctx, tableSPIRV, fileOutputPath );
        if ( bWroteToDisk ) {
            shaderExtractedCount++;
        }
#endif

        if ( ctx.pSourceGenerator != nullptr ) {
            OTDU_LOG_INFO( "Writing GLSL to disk (shader source generation "
                           "enabled)\n" );
            fileOutputPath = GetShaderOutputPath( ctx ) + kShadersOpenGLFolder +
                             currentTableName + kShaderTableSourceExtension;

            bWroteToDisk = WriteShaderTableSourceToDisk( ctx, tableSource,
                                                         fileOutputPath );
            if ( bWroteToDisk ) {
                shaderExtractedCount++;
            }
        }

        tableDXSO.Clear();
        tableSPIRV.Clear();
        tableSource.clear();
    }

} // namespace

namespace AssetExtractor {

    ExtractionResult Run( const ExtractionParams &params ) {
        ExtractionResult result = {};

        OTDU_ASSERT( params.pExecutablePath != nullptr );
        OTDU_ASSERT( params.pOutputPath != nullptr );
        if ( params.pExecutablePath == nullptr ||
             params.pOutputPath == nullptr ) {
            return result;
        }

        ExtractorContext ctx;
        ctx.OutputPath = params.pOutputPath;
        ctx.bForceExtraction = params.bForceExtraction;
        ctx.bForceShaderExtraction = params.bForceShaderExtraction;
        ctx.pSourceGenerator = params.pSourceGenerator;

        OTDU_LOG_DEBUG( "executable: '%s'\n", params.pExecutablePath );
        OTDU_LOG_DEBUG( "asset_output_path: '%s'\n", ctx.OutputPath.c_str() );

        // Prepare output
        CheckAndCreateOutputFolder( GetIniOutputPath( ctx ) );
        CheckAndCreateOutputFolder( GetShaderOutputPath( ctx ) );
        CheckAndCreateOutputFolder( GetShaderOutputPath( ctx ) +
                                    kShadersD3D9Folder );
#if OTDU_EXTRACTOR_DXVK
        CheckAndCreateOutputFolder( GetShaderOutputPath( ctx ) +
                                    kShadersOpenGLFolder );
        CheckAndCreateOutputFolder( GetShaderOutputPath( ctx ) +
                                    kShadersVulkanFolder );
#endif

        uint32_t shaderExtractedCount = 0;
        uint32_t iniExtractedCount = 0;

        std::ifstream fileStream;
        fileStream.open( params.pExecutablePath,
                         std::ios_base::binary | std::ios_base::in );
        if ( !fileStream.is_open() || !fileStream.good() ) {
            OTDU_LOG_ALWAYS( "Failed to open executable '%s' for read!\n",
                             params.pExecutablePath );
            return result;
        }

        constexpr uint32_t kNumShaderEntries =
            sizeof( kMasterShaderTable ) / sizeof( kMasterShaderTable[0] );

#if OTDU_EXTRACTOR_DXVK
        dxvk::D3D9ConstantLayout vertexConstantLayout;
        vertexConstantLayout.floatCount = 256;
        vertexConstantLayout.intCount = 1;
        vertexConstantLayout.boolCount = 1;
        vertexConstantLayout.bitmaskCount =
            Align( vertexConstantLayout.boolCount, 32 ) / 32;

        dxvk::D3D9ConstantLayout pixelConstantLayout;
        pixelConstantLayout.floatCount = 224;
        pixelConstantLayout.intCount = 1;
        pixelConstantLayout.boolCount = 1;
        pixelConstantLayout.bitmaskCount =
            Align( pixelConstantLayout.boolCount, 32 ) / 32;
#endif

        ShaderTable tableDXSO = {};
        ShaderTable tableSPIRV = {};
        std::string tableSource = "";

        std::string currentTableName = "";
        for ( uint32_t i = 0; i < kNumShaderEntries; i++ ) {
            if ( ( i % 100 ) == 0 ) {
                OTDU_LOG_INFO( "Shader(s) extracted: %u/%u\n", i,
                               kNumShaderEntries );
            }

            const ShaderTableEntry &entry = kMasterShaderTable[i];

            if ( currentTableName != entry.pShaderCategory ) {
                WriteShaderTablesToDisk( ctx, currentTableName, tableDXSO,
                                         tableSPIRV, tableSource,
                                         shaderExtractedCount );
                currentTableName = entry.pShaderCategory;
            }

            std::vector<int8_t> shaderBytecode;

            if ( currentTableName == "imgui" ||
                 currentTableName == "dbg_font" ) {
                // Load precompiled .dxso binaries from disk for SPIRV
                // translation (this way we can avoid runtime hacks and
                // streamline shader binary management)
                std::string shaderBinPath =
                    GetShaderOutputPath( ctx ) + kShadersD3D9Folder;

                if ( entry.ShaderStage == eShaderType::ST_Vertex ) {
                    shaderBinPath += currentTableName + "_vs.bin";
                } else if ( entry.ShaderStage == eShaderType::ST_Pixel ) {
                    shaderBinPath += currentTableName + "_ps.bin";
                }

                std::ifstream customShaderBinStream(
                    shaderBinPath.c_str(),
                    std::ios_base::in | std::ios_base::binary );
                if ( customShaderBinStream.is_open() &&
                     customShaderBinStream.good() ) {
                    customShaderBinStream.seekg( 0, std::ios_base::end );
                    auto fileSize = customShaderBinStream.tellg();
                    customShaderBinStream.seekg( 0, std::ios_base::beg );

                    shaderBytecode.resize( fileSize );
                    customShaderBinStream.read(
                        reinterpret_cast<char *>( shaderBytecode.data() ),
                        fileSize * sizeof( int8_t ) );
                    customShaderBinStream.close();
                } else {
                    OTDU_LOG_WARN(
                        "Failed to open %s shader binary (path: '%s')!\n",
                        currentTableName.c_str(), shaderBinPath.c_str() );
                    continue;
                }
            } else {
                shaderBytecode.reserve( 2
                                        << 10 ); // Should be large enough
                                                 // (biggest shaders are ~2KiB)
                shaderBytecode.resize( 0x18 );

                size_t offsetToSeek = entry.OffsetInExecutable - 0x400000;
                fileStream.seekg( offsetToSeek, std::ios::beg );

                fileStream.read(
                    reinterpret_cast<char *>( shaderBytecode.data() ),
                    0x18 * sizeof( int8_t ) );

                if ( shaderBytecode[8] == 'C' && shaderBytecode[9] == 'T' &&
                     shaderBytecode[10] == 'A' && shaderBytecode[11] == 'B' ) {
                    OTDU_LOG_DEBUG( "Reading shader binary at offset %x\n",
                                    entry.OffsetInExecutable );

                    int16_t word = 0;
                    constexpr int16_t kEndShaderMarker = 0xffff;
                    while ( word != kEndShaderMarker ) {
                        fileStream.read( reinterpret_cast<char *>( &word ),
                                         sizeof( int16_t ) );

                        shaderBytecode.push_back( word & 0xff );
                        shaderBytecode.push_back( ( word >> 8 ) & 0xff );
                    }
                } else {
                    OTDU_FATAL_ERROR( "Invalid magic found (shader table entry "
                                      "might be invalid)\n" );
                }

                PatchRetailShaderBytecode( entry, shaderBytecode );
            }

            ShaderTableHeaderEntry headerEntry;
            headerEntry.Hashcode = entry.Hashcode;
            headerEntry.Offset = tableDXSO.Shaders.size();
            headerEntry.Size = shaderBytecode.size();
            headerEntry.MetadataOffset = kInvalidMetadataOffset;

            tableDXSO.Header.Add( headerEntry );
            tableDXSO.Shaders.insert( tableDXSO.Shaders.end(),
                                      shaderBytecode.begin(),
                                      shaderBytecode.end() );

#if OTDU_EXTRACTOR_DXVK
            // DXSO to SPIRV (using DXVK); reference for the shader lifter
            // tools only, the client runs the shaders rebuilt from source.
            dxvk::DxsoReader reader(
                reinterpret_cast<const char *>( shaderBytecode.data() ) );
            dxvk::DxsoModule dxvkModule( reader );
            dxvk::DxsoAnalysisInfo info = dxvkModule.analyze();

            const dxvk::D3D9ConstantLayout &constantLayout =
                entry.ShaderStage == eShaderType::ST_Vertex
                    ? vertexConstantLayout
                    : pixelConstantLayout;

            dxvk::DxsoModuleInfo dxsoModuleInfo;
            dxvk::OpenTDUOutput *pSpirvBC = dxvkModule.compile(
                dxsoModuleInfo, std::to_string( entry.Hashcode ), info,
                constantLayout );

            headerEntry.Offset = tableSPIRV.Shaders.size();
            headerEntry.Size = pSpirvBC->Code.size();
            headerEntry.MetadataOffset = tableSPIRV.Metadata.size();
            tableSPIRV.Header.Add( headerEntry );

            for ( uint32_t j = 0; j < pSpirvBC->Code.dwords(); j++ ) {
                uint32_t dword = pSpirvBC->Code.data()[j];

                tableSPIRV.Shaders.push_back( (uint8_t)( dword & 0xff ) );
                tableSPIRV.Shaders.push_back( (uint8_t)( dword >> 8 ) & 0xff );
                tableSPIRV.Shaders.push_back( (uint8_t)( dword >> 16 ) & 0xff );
                tableSPIRV.Shaders.push_back( (uint8_t)( dword >> 24 ) & 0xff );
            }

            // Collect ISGN for vertex shaders (pixel shaders have no VS inputs)
            const dxvk::DxsoIsgn *pIsgn = nullptr;
            if ( entry.ShaderStage == eShaderType::ST_Vertex ) {
                pIsgn = &dxvkModule.isgn();
            }

            ShaderMetadataSPIRV infos;
            infos.FlatShadingInputs = pSpirvBC->Infos.flatShadingInputs;
            infos.InputMask = pSpirvBC->Infos.inputMask;
            infos.OutputMask = pSpirvBC->Infos.outputMask;
            // NOTE: We only retrieve the size since the binding points are
            // hardcoded in dxvk
            infos.SamplerPushConstantsSize =
                pSpirvBC->Infos.localPushData.getSize();
            infos.NumBindings = pSpirvBC->Infos.bindingCount;
            infos.NumInputElements = pIsgn ? pIsgn->elemCount : 0u;

            const int8_t *infosBegin = (const int8_t *)&infos;
            std::copy( infosBegin, infosBegin + sizeof( ShaderMetadataSPIRV ),
                       std::back_inserter( tableSPIRV.Metadata ) );

            for ( uint32_t j = 0; j < infos.NumBindings; j++ ) {
                const int8_t *bindingBeg =
                    (const int8_t *)&pSpirvBC->Infos.bindingsCopy[j];
                const int8_t *bindingEnd =
                    bindingBeg + sizeof( dxvk::DxvkBindingInfo );
                std::copy( bindingBeg, bindingEnd,
                           std::back_inserter( tableSPIRV.Metadata ) );
            }

            // Serialize ISGN elements: each maps a SPIRV location to a D3D9
            // semantic
            if ( pIsgn ) {
                for ( uint32_t j = 0; j < pIsgn->elemCount; j++ ) {
                    const dxvk::DxsoIsgnEntry &isgnElem = pIsgn->elems[j];
                    SPIRVInputElement elem = {};
                    elem.Location = isgnElem.slot;
                    elem.Usage =
                        static_cast<uint8_t>( isgnElem.semantic.usage );
                    elem.UsageIndex =
                        static_cast<uint8_t>( isgnElem.semantic.usageIndex );
                    const int8_t *elemBeg = (const int8_t *)&elem;
                    std::copy( elemBeg, elemBeg + sizeof( SPIRVInputElement ),
                               std::back_inserter( tableSPIRV.Metadata ) );
                }
            }

            if ( ctx.pSourceGenerator != nullptr ) {
                // SPIRV to GLSL is delegated to the caller-provided generator
                // so the core (and the game client) never has to depend on
                // SPIRV-Cross.
                ctx.pSourceGenerator->appendShaderSource(
                    entry.Hashcode, entry.ShaderStage == eShaderType::ST_Vertex,
                    pSpirvBC->Code.data(), pSpirvBC->Code.dwords(),
                    tableSource );
            }

            delete pSpirvBC;
#endif
        }

        // Write current shader table to disk.
        WriteShaderTablesToDisk( ctx, currentTableName, tableDXSO, tableSPIRV,
                                 tableSource, shaderExtractedCount );

        // .ini files
        for ( uint32_t i = 0; i < kPackedIniFileCount; i++ ) {
            const TestDrivePackedIniEntry &packedIni = kPackedIniFiles[i];

            std::string fileOutputPath =
                GetIniOutputPath( ctx ) + packedIni.Name;
            if ( !ctx.bForceExtraction &&
                 std::filesystem::exists( fileOutputPath ) ) {
                OTDU_LOG_INFO( "'%s' already exists; skipping...\n",
                               fileOutputPath.c_str() );
                continue;
            }

            size_t offsetToSeek = packedIni.OffsetInBytes - 0x400000;
            fileStream.seekg( offsetToSeek, std::ios::beg );

            OTDU_LOG_INFO( "Reading packed config file '%s' at offset %p...\n",
                           packedIni.Name, offsetToSeek );

            int8_t *pBuffer = new int8_t[packedIni.LengthInBytes];
            memset( pBuffer, 0, sizeof( int8_t ) * packedIni.LengthInBytes );

            fileStream.read( (char *)pBuffer,
                             sizeof( int8_t ) * packedIni.LengthInBytes );
            UnpackIniFromMemory( pBuffer, packedIni.LengthInBytes );

            // Write to disk
            std::ofstream outputFileStream;
            outputFileStream.open( fileOutputPath.c_str(),
                                   std::ios_base::out | std::ios_base::binary );
            outputFileStream.write( (char *)pBuffer,
                                    sizeof( char ) * packedIni.LengthInBytes );
            outputFileStream.close();

            OTDU_LOG_INFO( "Wrote .ini to '%s'\n", fileOutputPath.c_str() );

            delete[] pBuffer;

            iniExtractedCount++;
        }
        fileStream.close();

        OTDU_LOG_ALWAYS( "==== Summary ====\nFound and extracted %u shader "
                         "table(s) and %u .ini file(s) to disk\n",
                         shaderExtractedCount, iniExtractedCount );

        result.bSuccess = true;
        result.ShaderTablesExtracted = shaderExtractedCount;
        result.IniFilesExtracted = iniExtractedCount;
        return result;
    }

} // namespace AssetExtractor
