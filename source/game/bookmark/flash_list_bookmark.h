#pragma once

#include "flash/entities/flash_list.h"

class MngBookmark;

class FlashListBookmark : public FlashList {
public:
    static constexpr FlashMessage kDelete = 0x626c6564u; // delb
    static constexpr FlashMessage kGoto = 0x6f746f67u; // goto
    static constexpr FlashMessage kImportBookmark = 0x6f626d69u; // imbo

public:
    FlashListBookmark();

    bool onMessage( FlashMessage *pMessage,
                    class FlashPlayer *pPlayer ) override;

    int32_t getListLength() const override;
    void close() override;
    void update( const int32_t param_2, const int32_t param_3 ) override;
    void selectedLineChanged() override;

    using FlashList::reset;

    MngBookmark *pParent;
};
