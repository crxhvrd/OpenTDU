#include "shared.h"
#include "ik_offset_db.h"

#include "config/gs_config.h"
#include "database/gs_database.h"
#include "filesystem/gs_file.h"

#include "core/binary_helpers.h"

static constexpr uint32_t kIKOffsetDBHashcode = 0x42444b49; // "IKDB"
static constexpr uint32_t kCarSectionFourCC = 0x4b494143;   // "CAIK"
static constexpr uint32_t kBikeSectionFourCC = 0x4b494942;  // "BIIK"

static constexpr const char *kIKOffsetDBPath = "DataBase/IKOffsetDB.bin";

static constexpr float kDefaultCarFootRightZ = 0.06f; // DAT_00f41e08
static constexpr float kDefaultBikeHandSize = 0.5f;   // FLOAT_00f41674
static constexpr float kDefaultBikeFrontLimit = 1.0f; // FLOAT_00f4165c

struct IKOffsetDBFileHeader {
    uint16_t VersionMajor;
    uint16_t VersionMinor;
    uint32_t Flags;
    uint32_t Size;
    uint32_t Hashcode;
};
OTDU_SIZE_MUST_MATCH( IKOffsetDBFileHeader, 0x10 );

struct IKOffsetDBSectionHeader {
    uint32_t FourCC;
    uint16_t VersionMajor;
    uint16_t VersionMinor;
    uint32_t Size;
    uint32_t DataSize;
};
OTDU_SIZE_MUST_MATCH( IKOffsetDBSectionHeader, 0x10 );

struct SCBufferParser {
    const uint8_t *pBuffer = nullptr;
    bool bReverseEndianness = false;

    uint32_t getValue() {
        uint32_t value;
        std::memcpy( &value, pBuffer, sizeof( value ) );
        pBuffer += sizeof( value );
        return bReverseEndianness ? ReverseEndianness32( value ) : value;
    }

    float getFloat() {
        const uint32_t value = getValue();
        float result;
        std::memcpy( &result, &value, sizeof( result ) );
        return result;
    }

    Eigen::Vector4f getVector3() {
        const float x = getFloat();
        const float y = getFloat();
        const float z = getFloat();
        return Eigen::Vector4f( x, y, z, 0.0f );
    }

    void getSectionHeader( IKOffsetDBSectionHeader &outHeader ) {
        // FUN_00423fe0
        std::memcpy( &outHeader, pBuffer, sizeof( outHeader ) );
        pBuffer += sizeof( outHeader );

        if ( bReverseEndianness ) {
            // FUN_00424030
            outHeader.FourCC = ReverseEndianness32( outHeader.FourCC );
            outHeader.VersionMajor =
                ReverseEndianness16( outHeader.VersionMajor );
            outHeader.VersionMinor =
                ReverseEndianness16( outHeader.VersionMinor );
            outHeader.Size = ReverseEndianness32( outHeader.Size );
            outHeader.DataSize = ReverseEndianness32( outHeader.DataSize );
        }
    }
};

struct IKCarSectionFormat {
    uint16_t VersionMajor;
    uint16_t VersionMinor;
    int32_t NumScalars;    // 3, or 5 once the passenger seat was added
    int32_t NumOffsets;    // how many of the 27 the file supplies
    bool bReadRotations;   // each offset carries its rotation as well
    bool bReadPassengers;  // the 11 passenger offsets follow the car ones
    int32_t CopiedSeatSrc; // Offsets[14] is a copy of this one, or -1
    bool bApplySeatDflts;  // FUN_007fcfd0 rewrites Offsets[15..26] after
    bool bHasBikeSection;  // 1.x files stop after the car section
    bool bVersionedBike;   // the bike section is handed to FUN_007fd270
};

static constexpr IKCarSectionFormat kCarSectionFormats[] = {
    {1, 0, 3, 8,  false, false, -1, true,  false, false},
    {1, 1, 5, 14, false, false, 10, true,  false, false},
    {1, 2, 5, 14, false, false, 10, true,  false, false},
    {1, 3, 5, 15, false, false, 10, true,  false, false},
    {2, 0, 5, 16, false, false, 15, true,  true,  false},
    {2, 1, 5, 16, false, true,  15, true,  true,  false},
    {2, 2, 5, 25, false, true,  -1, true,  true,  false},
    {0, 0, 5, 27, true,  true,  -1, false, true,  true },
};

struct IKBikeSectionFormat {
    int32_t NumScalars; // 1, or 4 once the angle and the two limits landed
    bool bReadRotations;
    bool bReadPassengers;
};

static const IKCarSectionFormat &
GetCarSectionFormat( const IKOffsetDBFileHeader &header ) {
    for ( const IKCarSectionFormat &format : kCarSectionFormats ) {
        if ( format.VersionMajor == header.VersionMajor &&
             format.VersionMinor == header.VersionMinor ) {
            return format;
        }
    }

    return kCarSectionFormats[std::size( kCarSectionFormats ) - 1];
}

static IKBikeSectionFormat
GetBikeSectionFormat( const IKCarSectionFormat &carFormat,
                      const IKOffsetDBSectionHeader &sectionHeader ) {
    // FUN_007fd270
    if ( !carFormat.bVersionedBike ) {
        return { 1, false, carFormat.bReadPassengers };
    }

    const bool bIsInitialVersion =
        ( sectionHeader.VersionMajor == 1 && sectionHeader.VersionMinor == 0 );
    return { bIsInitialVersion ? 1 : 4, true, true };
}

static void ApplyCarSeatDefaults( SCIKCarOffsets *pRecord ) {
    // FUN_007fcfd0
    static constexpr float kSeatDefaults[12][3] = {
        {0.1f,  0.84f, -0.7f}, // 15
        {0.1f,  0.84f, -0.8f}, // 16
        {0.6f,  0.84f, -0.7f}, // 17
        {0.6f,  0.84f, -0.8f}, // 18
        {0.1f,  0.2f,  -0.4f}, // 19
        {0.6f,  0.2f,  -0.4f}, // 20
        {-0.6f, 0.84f, -0.7f}, // 21
        {-0.6f, 0.84f, -0.8f}, // 22
        {-0.1f, 0.84f, -0.7f}, // 23
        {-0.1f, 0.84f, -0.8f}, // 24
        {-0.6f, 0.2f,  -0.4f}, // 25
        {-0.1f, 0.2f,  -0.4f}, // 26
    };

    for ( int32_t i = 0; i < (int32_t)std::size( kSeatDefaults ); i++ ) {
        pRecord->Offsets[kFirstCarSeatIKOffset + i].Offset =
            Eigen::Vector4f( kSeatDefaults[i][0], kSeatDefaults[i][1],
                             kSeatDefaults[i][2], 0.0f );
    }
}

static void ReadIKOffsets( SCBufferParser &parser,
                           SCIKOffset *pOffsets,
                           const int32_t numOffsets,
                           const bool bReadRotations ) {
    for ( int32_t i = 0; i < numOffsets; i++ ) {
        const Eigen::Vector4f offset = parser.getVector3();

        float rotationX = 0.0f;
        float rotationY = 0.0f;
        float rotationZ = 0.0f;
        if ( bReadRotations ) {
            rotationX = parser.getFloat();
            rotationY = parser.getFloat();
            rotationZ = parser.getFloat();
        }

        if ( pOffsets == nullptr ) {
            continue;
        }

        pOffsets[i].Offset = offset;
        if ( bReadRotations ) {
            // Versions that do not carry them leave the defaults alone.
            pOffsets[i].RotationX = rotationX;
            pOffsets[i].RotationY = rotationY;
            pOffsets[i].RotationZ = rotationZ;
        }
    }
}

static void ReadCarRecord( SCBufferParser &parser,
                           const IKCarSectionFormat &format,
                           SCIKCarOffsets *pRecord ) {
    const float handSize = parser.getFloat();
    const float seatFrontBack = parser.getFloat();
    const float seatHeight = parser.getFloat();

    float passengerSeatFrontBack = 0.0f;
    float passengerSeatHeight = 0.0f;
    if ( format.NumScalars == 5 ) {
        passengerSeatFrontBack = parser.getFloat();
        passengerSeatHeight = parser.getFloat();
    }

    if ( pRecord != nullptr ) {
        pRecord->HandSize = handSize;
        pRecord->SeatFrontBack = seatFrontBack;
        pRecord->SeatHeight = seatHeight;
        if ( format.NumScalars == 5 ) {
            pRecord->PassengerSeatFrontBack = passengerSeatFrontBack;
            pRecord->PassengerSeatHeight = passengerSeatHeight;
        }
    }

    ReadIKOffsets( parser, pRecord != nullptr ? pRecord->Offsets : nullptr,
                   format.NumOffsets, format.bReadRotations );

    if ( pRecord != nullptr ) {
        if ( format.CopiedSeatSrc >= 0 ) {
            pRecord->Offsets[14].Offset =
                pRecord->Offsets[format.CopiedSeatSrc].Offset;
        }

        if ( format.bApplySeatDflts ) {
            ApplyCarSeatDefaults( pRecord );
        }
    }

    if ( format.bReadPassengers ) {
        ReadIKOffsets( parser,
                       pRecord != nullptr ? pRecord->PassengerOffsets : nullptr,
                       kNumPassengerIKOffsets, false );
    }
}

static void ReadBikeRecord( SCBufferParser &parser,
                            const IKBikeSectionFormat &format,
                            SCIKBikeOffsets *pRecord ) {
    const float handSize = parser.getFloat();

    float bikerAngle = 0.0f;
    float frontLimit = 0.0f;
    float backLimit = 0.0f;
    if ( format.NumScalars == 4 ) {
        bikerAngle = parser.getFloat();
        frontLimit = parser.getFloat();
        backLimit = parser.getFloat();
    }

    if ( pRecord != nullptr ) {
        pRecord->HandSize = handSize;
        if ( format.NumScalars == 4 ) {
            pRecord->BikerAngle = bikerAngle;
            pRecord->FrontLimit = frontLimit;
            pRecord->BackLimit = backLimit;
        }
    }

    ReadIKOffsets( parser, pRecord != nullptr ? pRecord->Offsets : nullptr,
                   kNumBikeIKOffsets, format.bReadRotations );

    if ( format.bReadPassengers ) {
        ReadIKOffsets( parser,
                       pRecord != nullptr ? pRecord->PassengerOffsets : nullptr,
                       kNumPassengerIKOffsets, false );
    }
}

void SCIKOffsetDB::initialize() {
    // FUN_008007f0
    NumCarIKOffsets = 0;
    NumBikeIKOffsets = 0;

    if ( gpDatabase == nullptr ) {
        return;
    }

    const GameDatabase &gameDB = gpDatabase->getGameDB();
    if ( gameDB.DBCarData == nullptr ) {
        return;
    }

    const uint32_t numRows = gameDB.pNumItems[1];
    for ( uint32_t i = 0; i < numRows; i++ ) {
        if ( gpDatabase->isBike( &gameDB.DBCarData[i] ) ) {
            NumBikeIKOffsets++;
        } else {
            NumCarIKOffsets++;
        }
    }

    pCarIKOffsets = (SCIKCarOffsets *)TestDrive::AllocAligned(
        sizeof( SCIKCarOffsets ) * (size_t)NumCarIKOffsets );
    pBikeIKOffsets = (SCIKBikeOffsets *)TestDrive::AllocAligned(
        sizeof( SCIKBikeOffsets ) * (size_t)NumBikeIKOffsets );

    if ( pCarIKOffsets == nullptr || pBikeIKOffsets == nullptr ) {
        OTDU_LOG_ERROR( "Failed to allocate the IK offset records\n" );
        terminate();
        return;
    }

    int32_t carIndex = 0;
    int32_t bikeIndex = 0;
    for ( uint32_t i = 0; i < numRows; i++ ) {
        DBEntryCarPhysicsData *pRow = &gameDB.DBCarData[i];

        const int32_t recordID = pRow->SameIK;

        if ( gpDatabase->isBike( pRow ) ) {
            SCIKBikeOffsets &record = pBikeIKOffsets[bikeIndex++];
            std::memset( (void *)&record, 0, sizeof( record ) );
            record.Id = recordID;
            record.HandSize = kDefaultBikeHandSize;
            record.FrontLimit = kDefaultBikeFrontLimit;
        } else {
            SCIKCarOffsets &record = pCarIKOffsets[carIndex++];
            std::memset( (void *)&record, 0, sizeof( record ) );
            record.Id = recordID;
            record.Offsets[kCarIKOffsetFootRight].Offset.z() =
                kDefaultCarFootRightZ;
            ApplyCarSeatDefaults( &record );
        }
    }

    char path[128];
    snprintf( path, sizeof( path ), "%s%s", gpConfig->getResRootPath(),
              kIKOffsetDBPath );

    if ( !gpFile->loadFile( path, &pContent, &ContentSize ) ) {
        OTDU_LOG_WARN( "'%s' not found; every vehicle keeps its default IK "
                       "offsets\n",
                       path );
        return;
    }

    if ( parse() ) {
        OTDU_LOG_INFO( "Loaded IK offsets for %d cars and %d bikes\n",
                       NumCarIKOffsets, NumBikeIKOffsets );
    } else {
        OTDU_LOG_WARN( "'%s' could not be parsed; every vehicle keeps its "
                       "default IK offsets\n",
                       path );
    }

    if ( pContent != nullptr ) {
        TestDrive::Free( pContent );
    }
    pContent = nullptr;
    ContentSize = 0u;
}

void SCIKOffsetDB::terminate() {
    // FUN_00805c80
    if ( pCarIKOffsets != nullptr ) {
        TestDrive::FreeAligned( pCarIKOffsets );
        pCarIKOffsets = nullptr;
    }
    if ( pBikeIKOffsets != nullptr ) {
        TestDrive::FreeAligned( pBikeIKOffsets );
        pBikeIKOffsets = nullptr;
    }

    NumCarIKOffsets = 0;
    NumBikeIKOffsets = 0;
}

bool SCIKOffsetDB::parse() {
    // FUN_007fda90
    if ( pContent == nullptr ) {
        return false;
    }

    SCBufferParser parser{ (const uint8_t *)pContent, false };

    IKOffsetDBFileHeader fileHeader;
    std::memcpy( &fileHeader, parser.pBuffer, sizeof( fileHeader ) );
    if ( fileHeader.Hashcode != kIKOffsetDBHashcode ) {
        fileHeader.VersionMajor =
            ReverseEndianness16( fileHeader.VersionMajor );
        fileHeader.VersionMinor =
            ReverseEndianness16( fileHeader.VersionMinor );
        fileHeader.Flags = ReverseEndianness32( fileHeader.Flags );
        fileHeader.Size = ReverseEndianness32( fileHeader.Size );
        fileHeader.Hashcode = ReverseEndianness32( fileHeader.Hashcode );

        if ( fileHeader.Hashcode != kIKOffsetDBHashcode ) {
            return false;
        }

        parser.bReverseEndianness = true;
    }
    parser.pBuffer += sizeof( fileHeader );

    const IKCarSectionFormat &carFormat = GetCarSectionFormat( fileHeader );

    IKOffsetDBSectionHeader sectionHeader;
    parser.getSectionHeader( sectionHeader );
    if ( sectionHeader.FourCC != kCarSectionFourCC ) {
        return false;
    }

    for ( uint32_t i = parser.getValue(); i != 0u; i-- ) {
        const int32_t recordID = (int32_t)parser.getValue();
        ReadCarRecord( parser, carFormat, findCarOffsets( recordID, false ) );
    }

    if ( !carFormat.bHasBikeSection ) {
        return true;
    }

    parser.getSectionHeader( sectionHeader );
    if ( sectionHeader.FourCC != kBikeSectionFourCC ) {
        return false;
    }

    const IKBikeSectionFormat bikeFormat =
        GetBikeSectionFormat( carFormat, sectionHeader );

    for ( uint32_t i = parser.getValue(); i != 0u; i-- ) {
        const int32_t recordID = (int32_t)parser.getValue();
        ReadBikeRecord( parser, bikeFormat,
                        findBikeOffsets( recordID, false ) );
    }

    return true;
}

SCIKCarOffsets *SCIKOffsetDB::findCarOffsets( int32_t carID, bool bUseSameIK ) {
    // FUN_007fcf50
    int32_t recordID = carID;

    if ( gpDatabase != nullptr && bUseSameIK ) {
        const DBEntryCarPhysicsData *pRow = gpDatabase->getCarDBEntry( carID );
        if ( pRow != nullptr ) {
            recordID = pRow->SameIK;
        }
    }

    for ( int32_t i = 0; i < NumCarIKOffsets; i++ ) {
        if ( pCarIKOffsets[i].Id == recordID ) {
            return &pCarIKOffsets[i];
        }
    }

    return nullptr;
}

SCIKBikeOffsets *SCIKOffsetDB::findBikeOffsets( int32_t carID,
                                                bool bUseSameIK ) {
    // FUN_007fced0
    int32_t recordID = carID;

    if ( gpDatabase != nullptr && bUseSameIK ) {
        const DBEntryCarPhysicsData *pRow = gpDatabase->getCarDBEntry( carID );
        if ( pRow != nullptr ) {
            recordID = pRow->SameIK;
        }
    }

    for ( int32_t i = 0; i < NumBikeIKOffsets; i++ ) {
        if ( pBikeIKOffsets[i].Id == recordID ) {
            return &pBikeIKOffsets[i];
        }
    }

    return nullptr;
}
