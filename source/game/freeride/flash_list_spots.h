#pragma once

#include "flash/entities/flash_list.h"

class FlashPlayer;

class FlashListSpots : public FlashList {
public:
    inline void setFlashPlayer( FlashPlayer *pFlashPlayer ) {
        pPlayer = pFlashPlayer;
    }

public:
    FlashListSpots();
    ~FlashListSpots();

    int32_t getListLength() const override;
    void open() override;
    void close() override;
    void update( const int32_t param_2, const int32_t param_3 ) override;
    void selectedLineChanged() override;

private:
    FlashPlayer *pPlayer;
};
