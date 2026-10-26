#pragma once

#include "flash/entities/flash_entity.h"

class FlashPlayer;

// OTDU_ADDITION: a settings page with tabs and any number of rows, built on
// the OPENTDU copy of the controls page
enum class eOptionsRowType : int32_t {
    ORT_Arrows,
    ORT_Checkbox,
    ORT_Gauge,
};

struct OptionsRow {
    eOptionsRowType Type = eOptionsRowType::ORT_Arrows;
    std::function<std::string()> GetLabel;
    // Arrows only
    std::function<std::string()> GetValue;
    // Arrows and gauge: previous/next value. Checkbox: toggles.
    std::function<void( int32_t direction )> Step;
    // Checkbox only
    std::function<bool()> IsChecked;
    // Gauge only, 0..1 (SetRatio is the mouse)
    std::function<float()> GetRatio;
    std::function<void( float ratio )> SetRatio;
    // Optional; a disabled row is dimmed and skipped
    std::function<bool()> IsEnabled;
};

struct OptionsDetail {
    std::string Label;
    std::string Value;
};

struct OptionsTab {
    std::string Label;
    std::vector<OptionsRow> Rows;
    // Split layout: the rows on the left, the focused row's details on the
    // right (read-only)
    bool bSplitLayout = false;
    std::function<std::vector<OptionsDetail>( int32_t row )> GetDetails;
};

class OptionsWidgetPage {
public:
    inline bool isOpen() const { return state != PS_Closed; }

public:
    OptionsWidgetPage();

    void bind( FlashPlayer *pPlayer );
    void open( std::vector<OptionsTab> &&newTabs,
               std::function<void()> &&onConfirm,
               std::function<void()> &&onCancel );
    void close();
    void onListRestored();
    void tick( float deltaTime );
    bool onMessage( FlashMessage *pMessage );

private:
    enum ePageState : uint8_t {
        PS_Closed,
        PS_Opening, // waiting for the widgets to be placed
        PS_Open,
    };

    struct Slot {
        int32_t Row = -1;
        std::string Path;
    };

private:
    const OptionsRow &getRow( const int32_t row ) const;
    int32_t getNumRows() const;
    bool isRowEnabled( const int32_t row ) const;
    float getRowHeight( const int32_t row ) const;
    int32_t findSlot( const int32_t row ) const;

    void showTabs();
    void selectTab( const int32_t tab );
    void layout();
    void bindSlot( const int32_t slot, const bool bFocused );
    void showSlotValue( const int32_t slot );
    void focusRow( const int32_t row, const bool bPlaySound );
    void moveFocus( const int32_t direction );
    void scroll( const int32_t direction );
    void stepFocusedRow( const int32_t direction );
    void showHelp();
    void showLayout();
    void showDetails();
    void finish( const bool bConfirmed );

    void tickKeys( float deltaTime );
    void tickMouse();

private:
    FlashPlayer *pPlayer;
    std::vector<OptionsTab> tabs;
    std::function<void()> onConfirm;
    std::function<void()> onCancel;
    std::vector<Slot> slots;

    ePageState state;
    int32_t currentTab;
    int32_t firstRow;
    int32_t focusedRow;
    int32_t hoveredSlot;
    int32_t draggedSlot;
    uint32_t lastInput;
    float repeatTimer;
    bool bMouseDown;
};
