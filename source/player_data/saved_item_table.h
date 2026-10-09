#pragma once

#include "core/notifier.h"

class edStore;

class SavedItemTable : public Notifiable {
public:
    static constexpr int32_t kNumEntries = 100;
    static constexpr uint16_t kDefaultKind = 1u;
    static constexpr uint16_t kExtraChallengeGoldFlag = 0x1000u;
    static constexpr uint16_t kExtraChallengePerfectFlag = 0x2000u;

    // Low byte of an entry
    enum eExtraChallengeState : uint8_t {
        ECS_Available = 1,
        ECS_Done = 2,
    };

public:
    SavedItemTable();
    ~SavedItemTable();

    void clear();
    void deserialize( edStore &stream );
    void serialize( edStore &stream ) const;

    inline uint8_t getExtraChallengeState( const int32_t index ) const {
        return static_cast<uint8_t>( Kinds[index] );
    }

    inline bool isExtraChallengeGold( const int32_t index ) const {
        return ( Kinds[index] & kExtraChallengeGoldFlag ) != 0u;
    }
    inline bool isExtraChallengePerfect( const int32_t index ) const {
        return ( Kinds[index] & kExtraChallengePerfectFlag ) != 0u;
    }

public:
    uint16_t Kinds[kNumEntries];
    uint32_t Values[kNumEntries];
    uint32_t Count = 0u;
};
