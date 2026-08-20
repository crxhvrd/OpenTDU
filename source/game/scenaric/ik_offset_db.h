#pragma once

struct SCIKOffset {
    float RotationX;
    float RotationY;
    float RotationZ;
    uint32_t Padding;
    Eigen::Vector4f Offset;
};
OTDU_SIZE_MUST_MATCH( SCIKOffset, 0x20 );

static constexpr int32_t kNumCarIKOffsets = 27;
static constexpr int32_t kNumBikeIKOffsets = 5;
static constexpr int32_t kNumPassengerIKOffsets = 11;
static constexpr int32_t kCarIKOffsetFootRight = 3;
static constexpr int32_t kFirstCarSeatIKOffset = 15;

struct SCIKCarOffsets {
    SCIKOffset PassengerOffsets[kNumPassengerIKOffsets];
    int32_t Id;
    float HandSize;
    float SeatHeight;
    float SeatFrontBack;
    float PassengerSeatHeight;
    float PassengerSeatFrontBack;
    uint32_t __PADDING__[2];
    SCIKOffset Offsets[kNumCarIKOffsets];
};
OTDU_SIZE_MUST_MATCH( SCIKCarOffsets, 0x4e0 );

struct SCIKBikeOffsets {
    SCIKOffset PassengerOffsets[kNumPassengerIKOffsets];
    int32_t Id;
    uint32_t __PADDING__[3];
    SCIKOffset Offsets[kNumBikeIKOffsets];
    float HandSize;
    float BikerAngle;
    float FrontLimit;
    float BackLimit;
};
OTDU_SIZE_MUST_MATCH( SCIKBikeOffsets, 0x220 );

// IK offsets binary blob (named after DAT_00f2a91c)
class SCIKOffsetDB {
public:
    void initialize();
    void terminate();

    SCIKCarOffsets *findCarOffsets( int32_t carID, bool bUseSameIK );
    SCIKBikeOffsets *findBikeOffsets( int32_t carID, bool bUseSameIK );

private:
    bool parse();

private:
    SCIKCarOffsets *pCarIKOffsets = nullptr;
    int32_t NumCarIKOffsets = 0;
    SCIKBikeOffsets *pBikeIKOffsets = nullptr;
    int32_t NumBikeIKOffsets = 0;
    void *pContent = nullptr;
    uint32_t ContentSize = 0u;
};
