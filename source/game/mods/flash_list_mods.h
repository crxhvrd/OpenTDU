#pragma once

#include "flash/entities/flash_list.h"

struct ModsListRow {
    std::string Name;
    FlashList::eLineState State = FlashList::eLineState::FLLS_Normal;
};

class FlashListMods : public FlashList {
public:
    static constexpr int32_t kMaxLines = 16;
    static constexpr int32_t kLineTextLength = 96;

public:
    inline int32_t getCapacity() const { return numLines; }

    inline void setOnSelectionChanged( std::function<void()> callback ) {
        onSelectionChanged = std::move( callback );
    }

public:
    explicit FlashListMods( const std::vector<ModsListRow> *pRows );

    int32_t getListLength() const override;
    void update( const int32_t entryIndex, const int32_t lineIndex ) override;
    void selectedLineChanged() override;
    int32_t getSelectedIndex() const;

    void applyRowState( const ModsListRow &row, const int32_t lineIndex );
    void repaintRows();
    void refresh();

private:
    void clearRow( const int32_t lineIndex );
    void setRowColor( const int32_t lineIndex, const eLineState state );
    void setRowText( const int32_t lineIndex, const char *pText );

private:
    const std::vector<ModsListRow> *pRows;
    std::function<void()> onSelectionChanged;
    char lineText[kMaxLines][kLineTextLength];
};
