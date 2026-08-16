#pragma once

#include "entity_sound_emitter.h"

// SCSoundDensity (ET_SDEN) - a sound emitter whose sample comes from a bank
class EntitySoundDensity : public EntitySoundEmitter {
public:
    enum eState : int32_t {
        SDS_Idle = 0,
        SDS_Installing = 1,
        SDS_Finished = 2,
    };

    struct DensitySlot {
        int32_t State = SDS_Idle;
        EntitySoundDensity *pOwner = nullptr;
        void *pBankData = nullptr;
    };

    static constexpr int32_t kMaxDensitySlots = 16; // DAT_010d6f60

public:
    static DensitySlot Slots[kMaxDensitySlots];

public:
    EntitySoundDensity();
    ~EntitySoundDensity();

    void loadResources( ScenaricBank *param_2 ) override;
    void unloadResources() override;
    void deserialize( BinaryReader *param_2 ) override;

    virtual void installSoundBank();
    virtual void uninstallSoundBank();
    virtual int32_t getState() const { return State; /* FUN_004268f0 */ }

    void onSoundBankInstalled();

protected:
    const char *pBankName;
    void *pBankData;
    int32_t SlotIndex;
    int32_t State;
};
