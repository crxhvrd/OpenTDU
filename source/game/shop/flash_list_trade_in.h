#pragma once

#include "flash/entities/flash_list.h"

class ShopLogicDealer;

class FlashListTradeIn : public FlashList {
public:
    static constexpr int32_t kNumSlots = 0x280;
    static constexpr int32_t kColumnLabelLength = 0x200;

public:
    inline void setOwner( ShopLogicDealer *pDealer ) { pOwner = pDealer; }
    inline int32_t getSelectedSlot() const {
        return slots[firstLine + selectedLineIndex];
    }
    inline bool isMissingConfig( const int32_t slot ) const {
        return bMissingConfig[slot];
    }

public:
    FlashListTradeIn();

    int32_t getListLength() const override;
    void open() override;
    void sort( int32_t param_1 ) override;
    void update( const int32_t param_2, const int32_t param_3 ) override;

private:
    void cacheGarageCars();
    void refreshSlots();
    void rebuild();
    bool compareSlots( const int32_t slotA, const int32_t slotB ) const;

private:
    int32_t slots[kNumSlots];
    ShopLogicDealer *pOwner;
    char columnLabel1[kColumnLabelLength];
    char columnLabel2[kColumnLabelLength];
    float prices[kNumSlots];
    const char *pNames[kNumSlots];
    bool bMissingConfig[kNumSlots];
};
