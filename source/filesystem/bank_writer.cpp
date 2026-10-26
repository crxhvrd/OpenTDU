#include "shared.h"
#include "bank_writer.h"

#include "bank.h"
#include "core/binary_helpers.h"
#include "core/hash/crc32.h"

static constexpr size_t kHeaderOffset = 8; // past {u32 size, u32 CRC}
static constexpr size_t kSectionHeaderSize = 8;
static constexpr uint32_t kBankMagic = 0x42414e4b; // KNAB
static constexpr uint32_t kBankFileAlignment = 0x800;

// Vanilla padding
static constexpr char kPaddingPhrase[] =
    "STNICC2000 RULEZPADDING DATAS...-ORIC AND ATARI--COOL  MACHINES-";
static constexpr size_t kPaddingBlockSize = 4 * 64 + 1;
static_assert( sizeof( kPaddingPhrase ) == 64 + 1 );

static void PadTo( std::vector<uint8_t> &bank, const size_t size ) {
    const size_t start = bank.size();
    bank.resize( size );
    for ( size_t i = start; i < size; i++ ) {
        const size_t blockOffset = ( i - start ) % kPaddingBlockSize;
        bank[i] = ( blockOffset == kPaddingBlockSize - 1 )
                      ? 0
                      : uint8_t( kPaddingPhrase[blockOffset % 64] );
    }
}

// Same as CaclulateBankCRC (bank.cpp): CRC32 without the final inversion
static uint32_t CalculateBankCRC( const uint8_t *pData, const size_t size ) {
    return ~TestDriveCRC32Unfolded( pData, size );
}

static bool ReadBankHeader( const std::vector<uint8_t> &bank,
                            BankHeader &outHeader ) {
    if ( bank.size() < kHeaderOffset + sizeof( BankHeader ) ) {
        return false;
    }
    std::memcpy( &outHeader, bank.data() + kHeaderOffset,
                 sizeof( BankHeader ) );
    return Read<uint32_t>( outHeader.Magic ) == kBankMagic;
}

static bool ReadEntryTable( const std::vector<uint8_t> &bank,
                            const BankHeader &header,
                            std::vector<BankSection> &outTable ) {
    const size_t tableOffset =
        size_t( header.SectionOffsetTable ) + kSectionHeaderSize;
    const size_t tableSize =
        ( size_t( header.NumEntries ) + 1 ) * sizeof( BankSection );
    if ( header.NumEntries < 0 || tableOffset + tableSize > bank.size() ) {
        return false;
    }

    outTable.resize( header.NumEntries + 1 );
    std::memcpy( outTable.data(), bank.data() + tableOffset, tableSize );
    return true;
}

int32_t FindBankEntryByMagic( const std::vector<uint8_t> &bank,
                              const uint32_t magic ) {
    BankHeader header;
    std::vector<BankSection> table;
    if ( !ReadBankHeader( bank, header ) ||
         !ReadEntryTable( bank, header, table ) ) {
        return -1;
    }

    for ( int32_t i = 0; i < header.NumEntries; i++ ) {
        const BankSection &entry = table[i];
        if ( entry.Offset >= 0 && entry.Size >= 4 &&
             size_t( entry.Offset ) + entry.Size <= bank.size() &&
             Read<uint32_t>( bank.data(), entry.Offset ) == magic ) {
            return i;
        }
    }
    return -1;
}

bool ReplaceBankEntry( const std::vector<uint8_t> &bank,
                       const int32_t entryIndex,
                       const std::vector<uint8_t> &content,
                       std::vector<uint8_t> &outBank,
                       std::string &outError ) {
    BankHeader header;
    std::vector<BankSection> table;
    if ( !ReadBankHeader( bank, header ) ||
         !ReadEntryTable( bank, header, table ) ) {
        outError = "not a bank";
        return false;
    }
    if ( entryIndex < 0 || entryIndex >= header.NumEntries ) {
        outError = "entry index out of range";
        return false;
    }

    const size_t dataOffset = header.SectionOffsetData;
    if ( dataOffset > bank.size() || header.DataAlignment <= 0 ) {
        outError = "bad data section";
        return false;
    }

    size_t previousEnd = dataOffset;
    for ( int32_t i = 0; i < header.NumEntries; i++ ) {
        const BankSection &entry = table[i];
        if ( entry.Offset < 0 || size_t( entry.Offset ) < previousEnd ||
             size_t( entry.Offset ) + entry.Size > bank.size() ) {
            outError = "entries are not stored in table order";
            return false;
        }
        previousEnd = size_t( entry.Offset ) + entry.Size;
    }

    // The sentinel entry sits either at the end of the last entry or at the
    // next aligned offset depending on the bank; keep the source's choice
    const bool bAlignedSentinel =
        size_t( table[header.NumEntries].Offset ) != previousEnd;

    // Some banks reserve zeroed room past their content (IGE screens, voices)
    const size_t sourcePaddedEnd =
        Align( Align( previousEnd, size_t( header.DataAlignment ) ),
               size_t( kBankFileAlignment ) );
    const size_t reservedSize =
        size_t( Max( header.Size, 0 ) ) > sourcePaddedEnd
            ? size_t( header.Size ) - sourcePaddedEnd
            : 0;

    outBank.assign( bank.begin(), bank.begin() + dataOffset );
    for ( int32_t i = 0; i < header.NumEntries; i++ ) {
        BankSection &entry = table[i];
        const uint8_t *pContent =
            ( i == entryIndex ) ? content.data() : bank.data() + entry.Offset;
        const size_t size =
            ( i == entryIndex ) ? content.size() : size_t( entry.Size );

        PadTo( outBank,
               Align( outBank.size(), size_t( header.DataAlignment ) ) );
        entry.Offset = int32_t( outBank.size() );
        entry.Size = int32_t( size );
        outBank.insert( outBank.end(), pContent, pContent + size );
    }

    const size_t dataEnd =
        Align( outBank.size(), size_t( header.DataAlignment ) );
    table[header.NumEntries].Offset =
        int32_t( bAlignedSentinel ? dataEnd : outBank.size() );
    header.SizeWithoutPadding = int32_t( dataEnd - dataOffset );
    if ( bAlignedSentinel ) {
        PadTo( outBank, dataEnd );
    }
    PadTo( outBank, Align( dataEnd, size_t( kBankFileAlignment ) ) );

    header.Size = int32_t( outBank.size() + reservedSize );
    outBank.resize( header.Size );

    const size_t tableOffset = header.SectionOffsetTable;
    std::memcpy( outBank.data() + tableOffset + kSectionHeaderSize,
                 table.data(), table.size() * sizeof( BankSection ) );
    const uint32_t tableSectionSize =
        Read<uint32_t>( outBank.data(), uint32_t( tableOffset ) );
    Write<uint32_t>(
        outBank.data(), uint32_t( tableOffset + 4 ),
        CalculateBankCRC( outBank.data() + tableOffset + kSectionHeaderSize,
                          tableSectionSize ) );

    std::memcpy( outBank.data() + kHeaderOffset, &header,
                 sizeof( BankHeader ) );
    const uint32_t headerSize = Read<uint32_t>( outBank.data() );
    Write<uint32_t>(
        outBank.data(), 4,
        CalculateBankCRC( outBank.data() + kHeaderOffset, headerSize ) );
    return true;
}
