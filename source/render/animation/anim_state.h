#pragma once

#include <cstdint>
#include <vector>

class ClipInstance;
class SkeletonInstance;

struct AnimStateClipRef {
    uint32_t PayloadOffset = 0;
    ClipInstance *pClip = nullptr;
};

struct AnimStateBlendEntry {
    ClipInstance *pClip = nullptr;
    float Weight = 0.0f;
    float Ratio = 0.0f;
};
struct AnimStateRecord {
    uint32_t Unknown0;
    uint32_t MatchID;
    uint32_t NumEntries;
    // NumEntries * 0x10 bytes of entries
};

class AnimStateInstance {
public:
    static constexpr uint32_t kChunkBas = 0x21534142;  // "BAS!"
    static constexpr uint32_t kChunkData = 0x41544144; // "DATA"
    static constexpr uint32_t kChunkRefs = 0x53464552; // "REFS"
    static constexpr uint32_t kChunkEof = 0x21464f45;  // "EOF!"

    static constexpr uint32_t kRefTypeAnm = 1;
    static constexpr uint32_t kRefTypeBas = 2;
    static constexpr uint32_t kRecordEntryStride = 0x10;

    static constexpr uint32_t kTrailerFlags = 0x0;
    static constexpr uint32_t kTrailerCurveMode = 0x4;
    static constexpr uint32_t kTrailerClipCount = 0x8;
    static constexpr uint32_t kTrailerClipArray = 0xc;

    static constexpr uint32_t kCurveModePlay = 0;
    static constexpr uint32_t kCurveModeBlendN = 1;
    static constexpr uint32_t kCurveModeBlendN1F = 2;
    static constexpr uint32_t kCurveModeBlendNR = 3;
    static constexpr uint32_t kCurveModeBlendNWR = 4;
    static constexpr uint32_t kCurveModeExpressions = 10;

    using ClipResolver = ClipInstance *(*)( void *pUserData,
                                            uint32_t id,
                                            uint32_t hash );

public:
    inline bool isValid() const { return pRecord != nullptr; }
    inline const void *getPayload() const { return pPayload; }
    inline const AnimStateRecord *getRecord() const { return pRecord; }
    inline uint32_t getMatchID() const {
        return pRecord != nullptr ? pRecord->MatchID : 0;
    }
    inline uint32_t getNumEntries() const {
        return pRecord != nullptr ? pRecord->NumEntries : 0;
    }
    inline const std::vector<AnimStateClipRef> &getClipRefs() const {
        return clipRefs;
    }

public:
    bool initialize( void *pData );
    void resolveReferences( ClipResolver resolver, void *pUserData );

    int32_t getNumDistinctClips() const;
    const uint8_t *getEntry( uint32_t index ) const;
    ClipInstance *getClipAtOffset( uint32_t payloadOffset ) const;
    ClipInstance *getFirstClip() const;
    uint32_t getTrailerOffset() const;
    uint32_t getCurveMode() const;
    int32_t getNumBlendClips() const;
    bool getBlendEntry( int32_t index, AnimStateBlendEntry &out ) const;
    void getBlendSet( std::vector<AnimStateBlendEntry> &outEntries ) const;

private:
    const uint32_t *getTrailer() const;

private:
    const uint8_t *pPayload = nullptr;
    const uint8_t *pRefs = nullptr;
    uint32_t refsSize = 0;
    const AnimStateRecord *pRecord = nullptr;
    std::vector<AnimStateClipRef> clipRefs;
};
