#pragma once

class edStore;

class SavedExtraChallenges {
public:
    static constexpr int32_t kNumCounters = 17; 
    static constexpr int32_t kNumScores = 11;   
    static constexpr int32_t kNumFlags = 16;
    static constexpr int32_t kNumBestPairs = 0x11; 
    static constexpr int32_t kNumTail = 11;        

    static constexpr int32_t kNumFlagBlocks = kNumFlags / 2;
    static constexpr int32_t kMultiGoldCupBlock = 3; 
    static constexpr int32_t kClubGoldCupBlock = 5;  

public:
    SavedExtraChallenges();
    ~SavedExtraChallenges();

    void clear();
    void deserialize( edStore &stream );
    void serialize( edStore &stream ) const;

    uint32_t getFlagBlock( const int32_t blockIndex ) const;
    void setFlagBlock( const int32_t blockIndex, const uint32_t value );

public:
    int32_t PlayerLevel = 0;

    int32_t Counters[kNumCounters] = {};
    double Scores[kNumScores] = {};
    float Field_0xf188 = 0.0f;
    int32_t Field_0xf18c = 0;
    float Field_0xf190 = 0.0f;
    int32_t Field_0xf194 = 0;
    int32_t Field_0xf198 = 0;
    int32_t Field_0xf19c = 0;
    float Field_0xf1a0 = 0.0f;
    float Field_0xf1a4 = 0.0f;
    int32_t Flags[kNumFlags] = {};
    int32_t BestLow[kNumBestPairs] = {}; 
    int32_t BestHigh[kNumBestPairs] = {}; 
    int32_t Tail[kNumTail] = {};
};
