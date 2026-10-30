#pragma once

// TODO: ONLINE: stubs until online is implemented

struct BotNameLabel {
    float X;
    float Y;
    float Width;
    float Height;
    float Distance;
    int32_t Index;
};
OTDU_SIZE_MUST_MATCH( BotNameLabel, 0x18 );

class CP2PFreeRideMgr {
public:
    static constexpr int32_t kNumBotNameLabels = 16;

public:
    CP2PFreeRideMgr();

public:
    bool bUnknownBool1; // (cleared by map screenshots)
    bool bUnknownBool2; // (cleared while a cinematic plays)
    bool bUnknownBool3; // (cleared by the GMMission restart fade)
    std::array<BotNameLabel, kNumBotNameLabels> BotNameLabels;
};

class CP2PGameService {
public:
    CP2PGameService();

public:
    CP2PFreeRideMgr FreeRideMgr;
};

extern CP2PGameService *gpCP2PGameService;
