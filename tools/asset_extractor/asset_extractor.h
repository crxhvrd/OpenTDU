#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

// Shared asset-extraction core. Unpacks the packed .ini files and, on request,
// the retail shader tables from TestDriveUnlimited.exe (MC 1.66A)
namespace AssetExtractor {
    struct ExtractionParams {
        const char *pExecutablePath = nullptr; // Path to TestDriveUnlimited.exe
        const char *pOutputPath = nullptr; // Base output path (parent of EURO/)
        bool bForceExtraction =
            false; // Re-extract everything even if assets already exist
        bool bExtractShaders =
            false; // Extract the retail shader tables (shader lifter tools
                   // only; the client runs EURO/Shaders/rebuilt.bin)
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
    // True when every generated movie exists (built by an earlier run)
    bool AreGeneratedMoviesBuilt( const char *pOutputPath );

    // Far impostor pack (world/far_impostor_pack.h), baked from the sector
    // banks of the level found under pOutputPath.
    bool BuildFarImpostors( const char *pOutputPath );
    // True when the pack exists and matches the current format version
    bool AreFarImpostorsBuilt( const char *pOutputPath );
} // namespace AssetExtractor
