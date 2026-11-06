#include "asset_extractor.h"

#include "shared.h"

#include "core/logger.h"
#include "filesystem/bank.h"
#include "world/far_impostor_pack.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <unordered_map>
#include <vector>

#ifdef OTDU_UNIX
#include "filesystem/virtual_file_system.h"
#endif

namespace {
    constexpr const char *kLevelName = "Hawai";
    constexpr const char *kLevelBankFolder = "EURO/BNK/Level/";

    constexpr uint32_t kPMIMagic = 0x494d502e; // ".PMI"
    constexpr float kCellMetricFactor = 1.0f / 65536.0f;
    constexpr uint32_t kPMIMinSize = 0x1d0;
    constexpr uint32_t kPMITypeHeaderSize = 0x20;
    constexpr uint32_t kPMIInstanceMinSize = 0x16;
    constexpr int16_t kImpostorGroup = 5;
    constexpr int16_t kImpostorPMISubType = 10;

    // Bank file = 8 bytes prefix + BankHeader; section offsets are relative
    // to the file start and point to an 8 bytes section header.
    constexpr uint32_t kBankPrefixSize = 8;
    constexpr uint32_t kSectionHeaderSize = 8;

    template <typename T>
    T ReadAt( const std::vector<uint8_t> &data, const size_t offset ) {
        T value;
        std::memcpy( &value, data.data() + offset, sizeof( T ) );
        return value;
    }

    bool ReadFileRange( std::ifstream &file,
                        const uint64_t offset,
                        const uint64_t size,
                        std::vector<uint8_t> &outData ) {
        outData.resize( size );
        file.seekg( static_cast<std::streamoff>( offset ) );
        file.read( reinterpret_cast<char *>( outData.data() ),
                   static_cast<std::streamsize>( size ) );
        return static_cast<uint64_t>( file.gcount() ) == size;
    }

    // Reads only the bank tables and the impostor .PMI entry.
    bool ReadImpostorPMI( const std::filesystem::path &bankPath,
                          std::vector<uint8_t> &outPMI ) {
        std::ifstream file( bankPath, std::ios::binary );
        if ( !file ) {
            return false;
        }

        std::vector<uint8_t> headerBytes;
        if ( !ReadFileRange( file, kBankPrefixSize, sizeof( BankHeader ),
                             headerBytes ) ) {
            return false;
        }
        const BankHeader header = ReadAt<BankHeader>( headerBytes, 0 );
        if ( !header.isVersionValid() || header.NumEntries <= 0 ||
             header.SectionOffsetTable == 0 ) {
            return false;
        }
        const uint32_t numEntries = static_cast<uint32_t>( header.NumEntries );

        std::vector<uint8_t> sections;
        if ( !ReadFileRange(
                 file,
                 static_cast<uint64_t>( header.SectionOffsetTable ) +
                     kSectionHeaderSize,
                 numEntries * sizeof( BankSection ), sections ) ) {
            return false;
        }

        std::vector<uint8_t> types;
        if ( header.SectionOffsetTypes != 0 &&
             !ReadFileRange(
                 file,
                 static_cast<uint64_t>( header.SectionOffsetTypes ) +
                     kSectionHeaderSize,
                 numEntries * sizeof( BankType ), types ) ) {
            return false;
        }

        for ( uint32_t i = 0; i < numEntries; i++ ) {
            // Same Type/SubType mapping as BankInstance::getEntry.
            int16_t group = header.TypeUnknown;
            int16_t subType = header.Type;
            if ( !types.empty() ) {
                const BankType type =
                    ReadAt<BankType>( types, i * sizeof( BankType ) );
                group = type.TypeUnknown;
                subType = type.Type;
            }
            if ( group != kImpostorGroup || subType != kImpostorPMISubType ) {
                continue;
            }

            const BankSection section =
                ReadAt<BankSection>( sections, i * sizeof( BankSection ) );
            if ( section.Offset <= 0 || section.Size <= 0 ) {
                return false;
            }
            return ReadFileRange( file, static_cast<uint64_t>( section.Offset ),
                                  static_cast<uint64_t>( section.Size ),
                                  outPMI );
        }
        return false;
    }

    struct SectorCoords {
        int32_t AreaX = 0;
        int32_t AreaY = 0;
        int32_t SectorX = 0;
        int32_t SectorY = 0;
    };

    bool ParseSectorFilename( const std::string &filename,
                              SectorCoords &outCoords ) {
        char extension[8] = {};
        return sscanf( filename.c_str(), "Sector-%d-%d-%d-%d.%3s",
                       &outCoords.AreaX, &outCoords.AreaY, &outCoords.SectorX,
                       &outCoords.SectorY, extension ) == 5 &&
               ( strcmp( extension, "bnk" ) == 0 ||
                 strcmp( extension, "BNK" ) == 0 );
    }

    struct PackBuilder {
        std::vector<uint64_t> typeHashcodes;
        std::unordered_map<uint64_t, uint16_t> typeIndices;
        std::vector<FarImpostorPack::Sector> sectors;
        std::vector<FarImpostorPack::Run> runs;
        std::vector<FarImpostorPack::Record> records;

        uint16_t getTypeIndex( const uint64_t hashcode ) {
            auto it = typeIndices.find( hashcode );
            if ( it != typeIndices.end() ) {
                return it->second;
            }
            const uint16_t index =
                static_cast<uint16_t>( typeHashcodes.size() );
            typeHashcodes.push_back( hashcode );
            typeIndices.emplace( hashcode, index );
            return index;
        }

        // Mirrors the .PMI walk of BuildImpostorGrid (FUN_00700c80).
        void addSector( const SectorCoords &coords,
                        const std::vector<uint8_t> &pmi ) {
            if ( pmi.size() < kPMIMinSize || ReadAt<uint16_t>( pmi, 0 ) != 0 ||
                 ReadAt<uint16_t>( pmi, 2 ) != 6 ||
                 ReadAt<uint32_t>( pmi, 0xc ) != kPMIMagic ) {
                return;
            }

            FarImpostorPack::Sector sector;
            sector.AreaX = static_cast<int16_t>( coords.AreaX );
            sector.AreaY = static_cast<int16_t>( coords.AreaY );
            sector.SectorX = static_cast<int16_t>( coords.SectorX );
            sector.SectorY = static_cast<int16_t>( coords.SectorY );
            sector.OriginY = ReadAt<float>( pmi, 0x24 );
            sector.CellSizeY = ( ReadAt<float>( pmi, 0x28 ) - sector.OriginY ) *
                               kCellMetricFactor;
            sector.FirstRun = static_cast<uint32_t>( runs.size() );
            sector.NumRuns = 0;

            const uint32_t numTypes = ReadAt<uint32_t>( pmi, 0x20 );
            size_t typeOffset = kPMIMinSize;
            for ( uint32_t t = 0; t < numTypes; t++ ) {
                if ( typeOffset + kPMITypeHeaderSize > pmi.size() ) {
                    break;
                }
                const uint64_t hashcode =
                    static_cast<uint64_t>(
                        ReadAt<uint32_t>( pmi, typeOffset + 0x10 ) ) |
                    ( static_cast<uint64_t>(
                          ReadAt<uint32_t>( pmi, typeOffset + 0x14 ) )
                      << 32 );
                const uint16_t instanceCount =
                    ReadAt<uint16_t>( pmi, typeOffset + 0x18 );
                const uint32_t instanceStride =
                    Align( static_cast<uint32_t>(
                               ReadAt<uint16_t>( pmi, typeOffset + 0x1a ) ) +
                               0x20,
                           16 );
                const uint8_t kind = pmi[typeOffset + 0x1e];
                const size_t instancesOffset = typeOffset + kPMITypeHeaderSize;
                typeOffset =
                    instancesOffset +
                    static_cast<size_t>( instanceStride ) * instanceCount;

                // Kinds 0/2 are skipped by the runtime; kind 4 carries panel
                // text (drawn through MngPanel, not as a plain sprite).
                if ( kind != 1 && kind != 3 ) {
                    continue;
                }

                FarImpostorPack::Run run;
                run.TypeIndex = getTypeIndex( hashcode );
                run.MaxScale = 0;
                run.FirstRecord = static_cast<uint32_t>( records.size() );
                run.NumRecords = 0;
                for ( uint16_t n = 0; n < instanceCount; n++ ) {
                    const size_t instanceOffset =
                        instancesOffset +
                        static_cast<size_t>( instanceStride ) * n;
                    if ( instanceOffset + kPMIInstanceMinSize > pmi.size() ) {
                        break;
                    }
                    auto word = [&]( const uint32_t index ) {
                        return ReadAt<uint16_t>( pmi,
                                                 instanceOffset + index * 2 );
                    };
                    FarImpostorPack::Record record;
                    record.X = word( 0 );
                    record.Y = word( 1 );
                    record.Z = word( 2 );
                    record.Scale = word( 4 );
                    record.Angle = word( 7 );
                    record.Color = static_cast<uint8_t>( word( 10 ) );
                    record.Padding = 0;
                    records.push_back( record );
                    run.MaxScale = std::max( run.MaxScale, record.Scale );
                    run.NumRecords++;
                }
                if ( run.NumRecords != 0 ) {
                    runs.push_back( run );
                    sector.NumRuns++;
                }
            }

            if ( sector.NumRuns != 0 ) {
                sectors.push_back( sector );
            }
        }

        bool write( const std::filesystem::path &outputPath ) const {
            FarImpostorPack::Header header;
            header.Magic = FarImpostorPack::kMagic;
            header.Version = FarImpostorPack::kVersion;
            header.NumTypes = static_cast<uint32_t>( typeHashcodes.size() );
            header.NumSectors = static_cast<uint32_t>( sectors.size() );
            header.NumRuns = static_cast<uint32_t>( runs.size() );
            header.NumRecords = static_cast<uint32_t>( records.size() );

            std::error_code errorCode;
            std::filesystem::create_directories( outputPath.parent_path(),
                                                 errorCode );

            // Write to a temporary file first so an interrupted bake never
            // leaves a truncated pack behind.
            std::filesystem::path tempPath = outputPath;
            tempPath += ".tmp";
            {
                std::ofstream file( tempPath, std::ios::binary );
                if ( !file ) {
                    return false;
                }
                auto writeArray = [&file]( const auto &array ) {
                    file.write( reinterpret_cast<const char *>( array.data() ),
                                static_cast<std::streamsize>(
                                    array.size() * sizeof( array[0] ) ) );
                };
                file.write( reinterpret_cast<const char *>( &header ),
                            sizeof( header ) );
                writeArray( typeHashcodes );
                writeArray( sectors );
                writeArray( runs );
                writeArray( records );
                if ( !file ) {
                    return false;
                }
            }
            std::filesystem::rename( tempPath, outputPath, errorCode );
            return !errorCode;
        }
    };

    std::filesystem::path GetPackPath( const char *pOutputPath ) {
        return std::filesystem::path( pOutputPath ) /
               FarImpostorPack::kLevelsFolder / kLevelName /
               FarImpostorPack::kFileName;
    }
} // namespace

namespace AssetExtractor {
    bool BuildFarImpostors( const char *pOutputPath ) {
        std::filesystem::path levelFolder =
            std::filesystem::path( pOutputPath ) / kLevelBankFolder /
            kLevelName;
#ifdef OTDU_UNIX
        const std::filesystem::path insensitivePath =
            ResolvePathInsensitive( levelFolder );
        if ( !insensitivePath.empty() ) {
            levelFolder = insensitivePath;
        }
#endif
        std::error_code errorCode;
        if ( !std::filesystem::is_directory( levelFolder, errorCode ) ) {
            OTDU_LOG_ALWAYS( "Far impostors: level folder '%s' not found\n",
                             levelFolder.string().c_str() );
            return false;
        }

        // Sorted for a deterministic pack.
        std::vector<std::pair<std::filesystem::path, SectorCoords>> banks;
        for ( const std::filesystem::directory_entry &entry :
              std::filesystem::recursive_directory_iterator( levelFolder,
                                                             errorCode ) ) {
            SectorCoords coords;
            if ( entry.is_regular_file() &&
                 ParseSectorFilename( entry.path().filename().string(),
                                      coords ) ) {
                banks.emplace_back( entry.path(), coords );
            }
        }
        std::sort( banks.begin(), banks.end(),
                   []( const auto &lhs, const auto &rhs ) {
                       return lhs.first < rhs.first;
                   } );

        PackBuilder builder;
        std::vector<uint8_t> pmi;
        for ( const auto &bank : banks ) {
            if ( ReadImpostorPMI( bank.first, pmi ) ) {
                builder.addSector( bank.second, pmi );
            }
        }

        if ( !builder.write( GetPackPath( pOutputPath ) ) ) {
            OTDU_LOG_ALWAYS( "Far impostors: failed to write '%s'\n",
                             GetPackPath( pOutputPath ).string().c_str() );
            return false;
        }

        OTDU_LOG_ALWAYS( "Far impostors: %zu sector(s), %zu type(s), %zu "
                         "instance(s) baked from %zu bank(s)\n",
                         builder.sectors.size(), builder.typeHashcodes.size(),
                         builder.records.size(), banks.size() );
        return true;
    }

    bool AreFarImpostorsBuilt( const char *pOutputPath ) {
        std::ifstream file( GetPackPath( pOutputPath ), std::ios::binary );
        FarImpostorPack::Header header = {};
        file.read( reinterpret_cast<char *>( &header ), sizeof( header ) );
        return file && header.Magic == FarImpostorPack::kMagic &&
               header.Version == FarImpostorPack::kVersion;
    }
} // namespace AssetExtractor
