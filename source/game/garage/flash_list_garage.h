#pragma once

#include "flash/entities/flash_list.h"
#include "flash/entities/flash_panel.h"

class GMGarage;
class FlashListGarage;

// Refreshes the list whenever the player garage changes.
class FlashListGarageNotifier : public FlashPanel {
public:
    FlashListGarageNotifier( FlashListGarage *pOwner );

    void tick() override;

private:
    FlashListGarage *pList;
};

class FlashListGarage : public FlashList {
public:
    static constexpr int32_t kNumSlots = 0x280;

public:
    inline FlashListGarageNotifier &getNotifier() { return notifier; }
    inline int32_t getSelectedSlot() const {
        return slots[firstLine + selectedLineIndex];
    }

public:
    FlashListGarage( GMGarage *pOwner );

    int32_t getListLength() const override;
    void open() override;
    void sort( int32_t param_1 ) override;
    void update( const int32_t param_2, const int32_t param_3 ) override;
    void selectedLineChanged() override;

    void onGarageUpdated();

    using FlashList::reset;

private:
    void refreshSlots();
    void refreshHouseNames();
    void rebuild();
    bool compareSlots( const int32_t slotA, const int32_t slotB ) const;

private:
    FlashListGarageNotifier notifier;
    GMGarage *pGMGarage;
    bool bSuspended;
    int32_t slots[kNumSlots];
    const char *pHouseNames[kNumSlots];
};
