#pragma once

// Far impostor binary blob (based on grid-0 .PMI (group 5, subtype 10)).
namespace FarImpostorPack {
    static constexpr uint32_t kMagic = 0x49465444; // "DTFI"
    static constexpr uint32_t kVersion = 1;
    static constexpr const char *kFileName = "far_impostors.bin";
    static constexpr const char *kLevelsFolder = "opentdu_data/levels/";

    struct Header {
        uint32_t Magic;
        uint32_t Version;
        uint32_t NumTypes;
        uint32_t NumSectors;
        uint32_t NumRuns;
        uint32_t NumRecords;
    };
    OTDU_SIZE_MUST_MATCH( Header, 0x18 );

    struct Sector {
        int16_t AreaX;
        int16_t AreaY;
        int16_t SectorX;
        int16_t SectorY;
        float OriginY;   // .PMI header +0x24
        float CellSizeY; // (.PMI header +0x28 - OriginY) * kCellMetricFactor
        uint32_t FirstRun;
        uint32_t NumRuns;
    };
    OTDU_SIZE_MUST_MATCH( Sector, 0x18 );

    // Instances of a single type within a sector.
    struct Run {
        uint16_t TypeIndex;
        uint16_t MaxScale; // Largest raw scale of the run
        uint32_t FirstRecord;
        uint32_t NumRecords;
    };
    OTDU_SIZE_MUST_MATCH( Run, 0xc );

    // Raw .PMI instance words (see DecodeImpostorInstance).
    struct Record {
        uint16_t X;     // pInst[0]
        uint16_t Z;     // pInst[2]
        uint16_t Y;     // pInst[1]
        uint16_t Scale; // pInst[4]
        uint16_t Angle; // pInst[7]
        uint8_t Color;  // pInst[10]
        uint8_t Padding;
    };
    OTDU_SIZE_MUST_MATCH( Record, 0xc );
} // namespace FarImpostorPack
