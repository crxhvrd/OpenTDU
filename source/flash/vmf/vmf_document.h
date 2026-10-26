#pragma once

// OTDU_ADDITION: editable tag tree for .vmf movies
static constexpr uint32_t kVMFMagic = 0x00464D56;     // VMF
static constexpr uint32_t kVMFXIXTMagic = 0x54584958; // XIXT
static constexpr uint32_t kVMFHeaderSize = 0x48;

static constexpr uint32_t kVMFTagEnd = 0x00;
static constexpr uint32_t kVMFTagShowFrame = 0x01;
static constexpr uint32_t kVMFTagDefineSprite = 0x27;

struct VMFAtlasEntry {
    int32_t DictionaryIndex;
    int32_t ID;
    uint32_t BB[4];
    float UV[2];
};
OTDU_SIZE_MUST_MATCH( VMFAtlasEntry, 0x20 );

struct VMFTag {
    uint32_t TagId = kVMFTagEnd;

    // Tag bytes past the 8-byte {TagId, TagSize} header. Empty for sprites.
    std::vector<uint8_t> Payload;

    // DefineSprite only. Children never include the closing End tag.
    uint32_t CharacterIndex = 0;
    uint16_t NumDepths = 0;
    std::vector<VMFTag> Children;

    inline bool isSprite() const { return TagId == kVMFTagDefineSprite; }
    // Dictionary index, for tags that define a character
    bool getCharacterIndex( uint32_t &outIndex ) const;
    uint16_t getNumFrames() const;
};

static constexpr uint32_t kVMFTagPlaceObject2 = 0x1a;

// PlaceObject2 payload. Every float block starts 4-aligned (tags are).
struct VMFPlacement {
    enum eFlags : uint16_t {
        PF_Move = 0x1,
        PF_HasCharacter = 0x2,
        PF_HasMatrix = 0x4,
        PF_HasColorTransform = 0x8,
        PF_HasRatio = 0x10,
        PF_HasName = 0x20,
        PF_HasClipDepth = 0x40,
        PF_HasRotateSkew = 0x80, // matrix parts, only with PF_HasMatrix
        PF_HasScale = 0x100,
        PF_HasTranslate = 0x200,
    };

    uint16_t Depth = 0;
    uint16_t Flags = 0;
    uint16_t CharacterIndex = 0;
    float Scale[2] = { 1.0f, 1.0f };
    float RotateSkew[2] = {};
    float Translate[2] = {};         // twips
    uint16_t ColorTransform[8] = {}; // 4 mul terms, then 4 add terms
    uint16_t Ratio = 0;
    uint16_t ClipDepth = 0;
    std::string Name;
    // Bytes past the name's terminator (padding). Empty = zero pad to 4.
    std::vector<uint8_t> Trailing;

    bool decode( const std::vector<uint8_t> &payload );
    std::vector<uint8_t> encode() const;
};

class VMFDocument {
public:
    inline const std::string &getError() const { return error; }

    // Mismatches between the stored derived fields and the recomputed ones.
    // Retail movies have none; anything listed here will not round-trip.
    inline const std::vector<std::string> &getWarnings() const {
        return warnings;
    }

public:
    bool parse( const uint8_t *pData, size_t size );
    std::vector<uint8_t> serialize() const;

    // Dictionary size needed by the tree (highest character index + 1)
    uint16_t computeNumCharacters() const;

public:
    uint8_t HeaderPadding = 0;
    uint8_t VersionMajor = 2;
    uint16_t VersionMinor = 8;
    // FlashResourceHeader::FileSize. NOT the file size: 0x000e0202 in every
    // retail movie (most likely an exporter version).
    uint32_t ExporterVersion = 0;
    std::array<char, 0x20> AuthorName{};
    float FrameSize[4] = {}; // XMin, XMax, YMin, YMax (twips)
    uint16_t FrameRate = 60;
    // FlashResourceHeader::NumFrames; the runtime sizes its dictionary with it
    uint16_t NumCharacters = 0;
    uint16_t NumDisplayList = 0;
    uint16_t NumSprites = 0;

    VMFTag Root;
    std::vector<VMFAtlasEntry> AtlasEntries;
    std::vector<uint8_t> Trailer; // past the XIXT block (empty in retail)

private:
    std::string error;
    std::vector<std::string> warnings;

private:
    bool parseSprite( const uint8_t *pData,
                      size_t size,
                      size_t offset,
                      bool bRoot,
                      VMFTag &outSprite,
                      size_t &outEnd );
};
