#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

// Shared asset-extraction core. Unpacks the packed .ini files and, on request,
// the retail shader tables from TestDriveUnlimited.exe (MC 1.66A)
namespace AssetExtractor {
    // Optional hook used to translate a compiled SPIR-V module into GLSL
    // source. Implemented by the standalone tool (via SPIRV-Cross); the game
    // client passes nullptr since it never needs the OpenGL3.3 shader sources.
    struct ShaderSourceGenerator {
        virtual ~ShaderSourceGenerator() = default;

        // Appends the GLSL translation of one shader to outTableSource.
        virtual void appendShaderSource( uint64_t hashcode,
                                         bool bIsVertexShader,
                                         const uint32_t *pSpirvBytecode,
                                         size_t spirvDwordCount,
                                         std::string &outTableSource ) = 0;
    };

    struct ExtractionParams {
        const char *pExecutablePath = nullptr; // Path to TestDriveUnlimited.exe
        const char *pOutputPath = nullptr; // Base output path (parent of EURO/)
        bool bForceExtraction =
            false; // Re-extract everything even if assets already exist
        bool bExtractShaders =
            false; // Extract the retail shader tables (shader lifter tools
                   // only; the client runs EURO/Shaders/rebuilt.bin)
        ShaderSourceGenerator *pSourceGenerator =
            nullptr; // Optional GLSL generator (nullptr disables
                     // -gen_shader_source)
    };

    struct ExtractionResult {
        bool bSuccess = false;
        uint32_t ShaderTablesExtracted = 0;
        uint32_t IniFilesExtracted = 0;
    };

    // Performs the one-shot asset extraction (packed .ini files + optional
    // shader tables). Both pExecutablePath and pOutputPath must be valid;
    // output folders are created on demand.
    ExtractionResult Run( const ExtractionParams &params );
    bool AreIniFilesExtracted( const char *pOutputPath );

    // OpenTDU front-end movies, generated from the user's own retail banks
    // (see flash/vmf/vmf_movie_builder.h). A bank is only rewritten when its
    // content changed. Returns false if a source bank could not be used.
    bool BuildGeneratedMovies( const char *pOutputPath );
} // namespace AssetExtractor
