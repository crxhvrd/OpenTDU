#include "shared.h"
#include "options_widget_page.h"

#include "config/gs_config.h"
#include "flash/gs_flash.h"
#include "flash/implementation/ed_flash.h"
#include "flash/implementation/flash_player.h"
#include "flash/implementation/flash_sprite.h"
#include "input/input_device.h"

static constexpr const char *kRootPath = "/keymap";
static constexpr const char *kListPath = "/keymap/list";
static constexpr const char *kHelpClip = "/help";
// Sent by the scrollbar arrows as 'widg:<list msg>:scru' / ':scrd'
static constexpr const char *kListMessage = "opts";
static constexpr FlashMessage kWidgetMessage = FlashMessage( "widg" );
static constexpr FlashMessage kScrollUp = FlashMessage( "scru" );
static constexpr FlashMessage kScrollDown = FlashMessage( "scrd" );

// Tabs placed by the controls page
static constexpr int32_t kMaxTabs = 6;
// Widgets per pool (AssetExtractor kSettingsWidgetPoolSize)
static constexpr int32_t kPoolSize = 10;
// Read-only arrow widgets of the split layout's right column
// (AssetExtractor kSettingsDetailPoolSize)
static constexpr const char *kDetailPoolPrefix = "inf";
static constexpr int32_t kDetailPoolSize = 8;
static constexpr const char *kListBackgroundClip = "bg_list";
static constexpr const char *kSplitBackgroundClip = "bg_split";

struct WidgetDesc {
    const char *pPoolPrefix;
    // Twips taken by the row in the list, and where the widget's origin sits
    // below the row top (the checkbox draws above its origin)
    float Height;
    float OriginOffset;
};

static constexpr WidgetDesc kWidgets[] = {
    {"arr", 1600.0f, 0.0f   }, // ORT_Arrows
    {"chk", 850.0f,  2023.0f}, // ORT_Checkbox
    {"gau", 1400.0f, 0.0f   }, // ORT_Gauge
};

// Row tops (twips, list space) that fit in the panel
static constexpr float kRowsTop = -5539.0f;
static constexpr float kRowsBottom = 2706.0f;

static constexpr int32_t kGaugeSteps = 16;
static constexpr float kRepeatDelay = 0.4f;
static constexpr float kRepeatInterval = 0.05f;

static const WidgetDesc &GetWidget( const eOptionsRowType type ) {
    return kWidgets[int32_t( type )];
}

static std::string GetPoolPath( const eOptionsRowType type,
                                const int32_t index ) {
    return std::string( kListPath ) + "/" + GetWidget( type ).pPoolPrefix +
           std::to_string( index + 1 );
}

// The arrows of an arrow widget send these, with the slot index
static void GetArrowMessages( const int32_t slot,
                              char ( &outPrevious )[5],
                              char ( &outNext )[5] ) {
    snprintf( outPrevious, sizeof( outPrevious ), "op%02d", slot );
    snprintf( outNext, sizeof( outNext ), "on%02d", slot );
}

static bool IsInClip( FlashPlayer *pPlayer, const std::string &path ) {
    FlashClip clip;
    if ( !pPlayer->getClipPlayer( path.c_str(), &clip ) ||
         clip.pSprite == nullptr ) {
        return false;
    }

    FlashRECT viewport;
    clip.getPropertyViewport( eFlashProperty::FP_Viewport, viewport );
    const float mouseX = gEdFlash.Input.MouseCoordinates[0];
    const float mouseY = gEdFlash.Input.MouseCoordinates[1];
    return viewport.X <= mouseX && mouseX <= viewport.X + viewport.Width &&
           viewport.Y <= mouseY && mouseY <= viewport.Y + viewport.Height;
}

static float GetMouseRatio( FlashPlayer *pPlayer, const std::string &path ) {
    FlashClip clip;
    if ( !pPlayer->getClipPlayer( path.c_str(), &clip ) ||
         clip.pSprite == nullptr ) {
        return -1.0f;
    }

    FlashRECT viewport;
    clip.getPropertyViewport( eFlashProperty::FP_Viewport, viewport );
    if ( viewport.Width <= 0.0f ) {
        return -1.0f;
    }
    return Clamp( ( gEdFlash.Input.MouseCoordinates[0] - viewport.X ) /
                      viewport.Width,
                  0.0f, 1.0f );
}

// Read by FlashMouse, which shows the hand while a button is hovered
static void ShowHandCursor() { gEdFlash.Input.field6_0x10 = 1; }

static void SetClipVisible( FlashPlayer *pPlayer,
                            const std::string &path,
                            const bool bVisible ) {
    FlashClip clip;
    if ( pPlayer->getClipPlayer( path.c_str(), &clip ) &&
         clip.pSprite != nullptr ) {
        clip.setProperty( eFlashProperty::FP_Visible, bVisible );
    }
}

static void SetText( FlashPlayer *pPlayer,
                     const std::string &path,
                     const std::string &text ) {
    pPlayer->setVariableValueNonLinked( path.c_str(), text.c_str() );
}

OptionsWidgetPage::OptionsWidgetPage()
    : pPlayer( nullptr ),
      state( PS_Closed ),
      currentTab( 0 ),
      firstRow( 0 ),
      focusedRow( 0 ),
      hoveredSlot( -1 ),
      draggedSlot( -1 ),
      lastInput( 0u ),
      repeatTimer( 0.0f ),
      bMouseDown( false ) {}

void OptionsWidgetPage::bind( FlashPlayer *pNewPlayer ) {
    pPlayer = pNewPlayer;
    state = PS_Closed;
}

void OptionsWidgetPage::open( std::vector<OptionsTab> &&newTabs,
                              std::function<void()> &&newOnConfirm,
                              std::function<void()> &&newOnCancel ) {
    tabs = std::move( newTabs );
    onConfirm = std::move( newOnConfirm );
    onCancel = std::move( newOnCancel );
    currentTab = 0;
    state = PS_Opening;
}

void OptionsWidgetPage::onListRestored() {
    if ( state == PS_Closed ) {
        return;
    }

    // Set by those scripts, 1 for APPLY
    const std::string valid = std::string( kListPath ) + ":valid";
    finish( pPlayer->getVariableAsInt( valid.c_str() ) == 1 );
}

void OptionsWidgetPage::close() {
    state = PS_Closed;
    slots.clear();
}

const OptionsRow &OptionsWidgetPage::getRow( const int32_t row ) const {
    return tabs[currentTab].Rows[row];
}

int32_t OptionsWidgetPage::getNumRows() const {
    return int32_t( tabs[currentTab].Rows.size() );
}

bool OptionsWidgetPage::isRowEnabled( const int32_t row ) const {
    const OptionsRow &desc = getRow( row );
    return !desc.IsEnabled || desc.IsEnabled();
}

float OptionsWidgetPage::getRowHeight( const int32_t row ) const {
    return GetWidget( getRow( row ).Type ).Height;
}

int32_t OptionsWidgetPage::findSlot( const int32_t row ) const {
    for ( int32_t i = 0; i < int32_t( slots.size() ); i++ ) {
        if ( slots[i].Row == row ) {
            return i;
        }
    }
    return -1;
}

void OptionsWidgetPage::showTabs() {
    for ( int32_t i = 0; i < kMaxTabs; i++ ) {
        const std::string path =
            std::string( kListPath ) + "/onglet" + std::to_string( i + 1 );
        const bool bUsed = i < int32_t( tabs.size() );
        SetClipVisible( pPlayer, path, bUsed );
        if ( !bUsed ) {
            continue;
        }

        SetText( pPlayer, path + ":m_name", tabs[i].Label );
        pPlayer->showFrame( i == currentTab ? "selected" : "normal",
                            path.c_str(), false );
    }
}

void OptionsWidgetPage::selectTab( const int32_t tab ) {
    if ( tab == currentTab || tab < 0 || tab >= int32_t( tabs.size() ) ) {
        return;
    }

    currentTab = tab;
    firstRow = 0;
    focusedRow = 0;
    while ( focusedRow < getNumRows() - 1 && !isRowEnabled( focusedRow ) ) {
        focusedRow++;
    }

    showTabs();
    layout();
    showHelp();
    gpFlash->getFlashSound()->playFrontEndSound( "move", 0, false );
}

void OptionsWidgetPage::showLayout() {
    const std::string list = std::string( kListPath ) + "/";
    const bool bSplit = tabs[currentTab].bSplitLayout;
    SetClipVisible( pPlayer, list + kListBackgroundClip, !bSplit );
    SetClipVisible( pPlayer, list + kSplitBackgroundClip, bSplit );
}

void OptionsWidgetPage::showDetails() {
    const OptionsTab &tab = tabs[currentTab];
    std::vector<OptionsDetail> details;
    if ( tab.bSplitLayout && tab.GetDetails && focusedRow < getNumRows() ) {
        details = tab.GetDetails( focusedRow );
    }

    const float height = GetWidget( eOptionsRowType::ORT_Arrows ).Height;
    for ( int32_t i = 0; i < kDetailPoolSize; i++ ) {
        const std::string path = std::string( kListPath ) + "/" +
                                 kDetailPoolPrefix + std::to_string( i + 1 );
        const bool bUsed = i < int32_t( details.size() ) &&
                           kRowsTop + float( i + 1 ) * height <= kRowsBottom;
        SetClipVisible( pPlayer, path, bUsed );
        if ( !bUsed ) {
            continue;
        }

        pPlayer->setClipProperty( path.c_str(), eFlashProperty::FP_Y,
                                  ( kRowsTop + float( i ) * height ) / 20.0f );
        SetText( pPlayer, path + ":param_name", details[i].Label );
        SetText( pPlayer, path + "/title/s_name:s_name", details[i].Label );
        SetText( pPlayer, path + ":param", details[i].Value );
        SetText( pPlayer, path + ":msg1", "" );
        SetText( pPlayer, path + ":msg2", "" );
        pPlayer->setVariableValue( ( path + ":display" ).c_str(), 1 );
        pPlayer->showFrame( "set", path.c_str(), false );
    }
}

void OptionsWidgetPage::layout() {
    // Scrolled so the focused row is in view
    firstRow = Min( firstRow, focusedRow );
    for ( ; getNumRows() != 0; ) {
        float height = 0.0f;
        for ( int32_t row = firstRow; row <= focusedRow; row++ ) {
            height += getRowHeight( row );
        }
        if ( height <= kRowsBottom - kRowsTop || firstRow == focusedRow ) {
            break;
        }
        firstRow++;
    }

    slots.clear();
    int32_t poolUsage[std::size( kWidgets )] = {};
    float top = kRowsTop;
    for ( int32_t row = firstRow; row < getNumRows(); row++ ) {
        const eOptionsRowType type = getRow( row ).Type;
        int32_t &used = poolUsage[int32_t( type )];
        if ( top + getRowHeight( row ) > kRowsBottom || used == kPoolSize ) {
            break;
        }

        Slot slot;
        slot.Row = row;
        slot.Path = GetPoolPath( type, used++ );
        pPlayer->setClipProperty( slot.Path.c_str(), eFlashProperty::FP_Y,
                                  ( top + GetWidget( type ).OriginOffset ) /
                                      20.0f );
        SetClipVisible( pPlayer, slot.Path, true );
        slots.push_back( slot );
        top += getRowHeight( row );
    }

    for ( size_t type = 0; type < std::size( kWidgets ); type++ ) {
        for ( int32_t i = poolUsage[type]; i < kPoolSize; i++ ) {
            SetClipVisible( pPlayer, GetPoolPath( eOptionsRowType( type ), i ),
                            false );
        }
    }

    for ( int32_t i = 0; i < int32_t( slots.size() ); i++ ) {
        bindSlot( i, slots[i].Row == focusedRow );
    }

    // Read by the scrollbar every frame
    const std::string list = kListPath;
    pPlayer->setVariableValue( ( list + ":nb_lines" ).c_str(),
                               int32_t( slots.size() ) );
    pPlayer->setVariableValue( ( list + ":total_lines" ).c_str(),
                               getNumRows() );
    pPlayer->setVariableValue( ( list + ":cur_list_line" ).c_str(), firstRow );

    showLayout();
    showDetails();
}

void OptionsWidgetPage::showSlotValue( const int32_t slot ) {
    const Slot &entry = slots[slot];
    const OptionsRow &row = getRow( entry.Row );
    const std::string label = row.GetLabel ? row.GetLabel() : std::string();

    switch ( row.Type ) {
    case eOptionsRowType::ORT_Arrows: {
        const std::string value = row.GetValue ? row.GetValue() : std::string();
        SetText( pPlayer, entry.Path + ":param_name", label );
        SetText( pPlayer, entry.Path + "/title/s_name:s_name", label );
        SetText( pPlayer, entry.Path + ":param", value );
        // What the focused arrow row shows once its value changed
        if ( entry.Row == focusedRow ) {
            pPlayer->setVariableValueNonLinked( "/:NameTemp", value.c_str() );
        }
        break;
    }
    case eOptionsRowType::ORT_Checkbox:
        SetText( pPlayer, entry.Path + ":s_name", label );
        SetText( pPlayer, entry.Path + "/sm_name:s_name", label );
        break;
    case eOptionsRowType::ORT_Gauge: {
        SetText( pPlayer, entry.Path + ":param_name", label );
        SetText( pPlayer, entry.Path + "/title/s_name:s_name", label );
        const float ratio = row.GetRatio ? row.GetRatio() : 0.0f;
        const int32_t frame =
            1 +
            int32_t( Clamp( ratio, 0.0f, 1.0f ) * ( kGaugeSteps - 1 ) + 0.5f );
        // Read again by the widget's state frames
        pPlayer->setVariableValue( ( entry.Path + ":param" ).c_str(), frame );
        pPlayer->goToFrameAndPlay( frame, ( entry.Path + "/slider" ).c_str(),
                                   false );
        break;
    }
    }
}

void OptionsWidgetPage::bindSlot( const int32_t slot, const bool bFocused ) {
    const Slot &entry = slots[slot];
    const OptionsRow &row = getRow( entry.Row );
    const bool bEnabled = isRowEnabled( entry.Row );

    if ( row.Type == eOptionsRowType::ORT_Arrows ) {
        char previous[5];
        char next[5];
        GetArrowMessages( slot, previous, next );
        SetText( pPlayer, entry.Path + ":msg1", previous );
        SetText( pPlayer, entry.Path + ":msg2", next );
    }
    showSlotValue( slot );

    pPlayer->setClipProperty( entry.Path.c_str(), eFlashProperty::FP_Alpha,
                              bEnabled ? 100.0f : 20.0f );

    if ( row.Type == eOptionsRowType::ORT_Checkbox ) {
        const bool bChecked = row.IsChecked && row.IsChecked();
        if ( bFocused ) {
            pPlayer->showFrame( bChecked ? "change" : "change_lock",
                                entry.Path.c_str(), true );
        } else {
            pPlayer->showFrame( bChecked ? "set_on" : "off_lock",
                                entry.Path.c_str(), false );
        }
        return;
    }

    pPlayer->setVariableValue( ( entry.Path + ":display" ).c_str(),
                               bEnabled ? 1 : 0 );
    if ( bFocused ) {
        pPlayer->showFrame( "in", entry.Path.c_str(), true );
        return;
    }

    pPlayer->showFrame( bEnabled ? "set" : "lock", entry.Path.c_str(), false );
    pPlayer->showFrame( "default", ( entry.Path + "/title" ).c_str(), false );
    pPlayer->showFrame( "default", ( entry.Path + "/cart" ).c_str(), false );
}

void OptionsWidgetPage::showHelp() {
    if ( focusedRow >= getNumRows() ) {
        return;
    }

    pPlayer->showFrame( getRow( focusedRow ).Type ==
                                eOptionsRowType::ORT_Checkbox
                            ? "apply_checkbox"
                            : "apply",
                        kHelpClip, true );
}

void OptionsWidgetPage::focusRow( const int32_t row, const bool bPlaySound ) {
    if ( row == focusedRow || row < 0 || row >= getNumRows() ) {
        return;
    }

    const int32_t previousSlot = findSlot( focusedRow );
    focusedRow = row;

    const int32_t slot = findSlot( row );
    if ( slot < 0 ) {
        layout();
    } else {
        if ( previousSlot >= 0 ) {
            const Slot &previous = slots[previousSlot];
            const OptionsRow &previousRow = getRow( previous.Row );
            const bool bDisplayed =
                previousRow.Type == eOptionsRowType::ORT_Checkbox
                    ? ( previousRow.IsChecked && previousRow.IsChecked() )
                    : isRowEnabled( previous.Row );
            pPlayer->showFrame( bDisplayed ? "out" : "out_lock",
                                previous.Path.c_str(), true );
        }
        bindSlot( slot, true );
    }

    showHelp();
    showDetails();
    if ( bPlaySound ) {
        gpFlash->getFlashSound()->playFrontEndSound( "move", 0, false );
    }
}

void OptionsWidgetPage::scroll( const int32_t direction ) {
    if ( getNumRows() == 0 ) {
        return;
    }

    int32_t lastFirstRow = getNumRows() - 1;
    float height = 0.0f;
    for ( ; lastFirstRow > 0; lastFirstRow-- ) {
        height += getRowHeight( lastFirstRow );
        if ( height + getRowHeight( lastFirstRow - 1 ) >
             kRowsBottom - kRowsTop ) {
            break;
        }
    }

    const int32_t newFirstRow = Clamp( firstRow + direction, 0, lastFirstRow );
    if ( newFirstRow == firstRow ) {
        return;
    }
    firstRow = newFirstRow;

    // Keeps the focus in view, on the row nearest to where it was
    int32_t lastVisibleRow = firstRow;
    height = 0.0f;
    while ( lastVisibleRow < getNumRows() &&
            height + getRowHeight( lastVisibleRow ) <=
                kRowsBottom - kRowsTop ) {
        height += getRowHeight( lastVisibleRow++ );
    }
    int32_t row = Clamp( focusedRow, firstRow, lastVisibleRow - 1 );
    const int32_t step = ( row == firstRow ) ? 1 : -1;
    while ( row >= firstRow && row < lastVisibleRow && !isRowEnabled( row ) ) {
        row += step;
    }
    if ( row >= firstRow && row < lastVisibleRow ) {
        focusedRow = row;
    }

    layout();
    showHelp();
}

void OptionsWidgetPage::moveFocus( const int32_t direction ) {
    const int32_t numRows = getNumRows();
    if ( numRows == 0 ) {
        return;
    }
    int32_t row = focusedRow;
    for ( int32_t i = 0; i < numRows; i++ ) {
        row = ( row + direction + numRows ) % numRows;
        if ( isRowEnabled( row ) ) {
            focusRow( row, true );
            return;
        }
    }
}

void OptionsWidgetPage::stepFocusedRow( const int32_t direction ) {
    if ( focusedRow >= getNumRows() || !isRowEnabled( focusedRow ) ) {
        return;
    }

    const OptionsRow &row = getRow( focusedRow );
    if ( !row.Step ) {
        return;
    }

    row.Step( direction );

    const int32_t slot = findSlot( focusedRow );
    if ( slot < 0 ) {
        return;
    }

    if ( row.Type == eOptionsRowType::ORT_Checkbox ) {
        const bool bChecked = row.IsChecked && row.IsChecked();
        pPlayer->showFrame( bChecked ? "enable" : "disable",
                            slots[slot].Path.c_str(), true );
        showDetails();
        return;
    }

    showSlotValue( slot );
    gpFlash->getFlashSound()->playFrontEndSound( "move", 0, false );

    // A value may lock or unlock other rows
    for ( int32_t i = 0; i < int32_t( slots.size() ); i++ ) {
        if ( i != slot ) {
            bindSlot( i, false );
        }
    }
    showDetails();
}

void OptionsWidgetPage::finish( const bool bConfirmed ) {
    state = PS_Closed;
    slots.clear();

    if ( bConfirmed ) {
        gpFlash->getFlashSound()->playFrontEndSound( "valid", 0, false );
        if ( onConfirm ) {
            onConfirm();
        }
    } else {
        gpFlash->getFlashSound()->playFrontEndSound( "back", 0, false );
        if ( onCancel ) {
            onCancel();
        }
    }

    // The page's close frame reports it and goes back to the options list
    pPlayer->setVariableValue(
        ( std::string( kRootPath ) + ":isvalid" ).c_str(), bConfirmed ? 1 : 0 );
    pPlayer->showFrame( "close", kRootPath, true );
}

void OptionsWidgetPage::tickKeys( float deltaTime ) {
    const uint32_t input = gpFlash->getInputMask();
    const bool bPressed = input != lastInput;
    lastInput = input;

    if ( bPressed ) {
        switch ( input ) {
        case FK_Up:
        case FK_WheelUp:
            moveFocus( -1 );
            return;
        case FK_Down:
        case FK_WheelDown:
            moveFocus( 1 );
            return;
        case FK_SortLeft:
            selectTab( currentTab - 1 );
            return;
        case FK_SortRight:
            selectTab( currentTab + 1 );
            return;
        case FK_Option1:
            if ( focusedRow < getNumRows() &&
                 getRow( focusedRow ).Type == eOptionsRowType::ORT_Checkbox ) {
                stepFocusedRow( 1 );
            }
            return;
        case FK_Validate:
            finish( true );
            return;
        case FK_Back:
            finish( false );
            return;
        default:
            break;
        }
    }

    // The arrow widgets send their own messages; the others step here, with
    // a repeat for the gauges
    if ( focusedRow >= getNumRows() ||
         getRow( focusedRow ).Type == eOptionsRowType::ORT_Arrows ) {
        return;
    }

    PlayerConfiguration *pInput =
        ( gpConfig != nullptr ) ? gpConfig->pPlayerConfig : nullptr;
    if ( pInput == nullptr ) {
        return;
    }

    const bool bLeft = IsActionDown( pInput->pMenuLeft );
    const bool bRight = IsActionDown( pInput->pMenuRight );
    if ( !bLeft && !bRight ) {
        repeatTimer = 0.0f;
        return;
    }

    const int32_t direction = bLeft ? -1 : 1;
    if ( repeatTimer == 0.0f ) {
        stepFocusedRow( direction );
        repeatTimer = kRepeatDelay;
        return;
    }

    if ( getRow( focusedRow ).Type != eOptionsRowType::ORT_Gauge ) {
        return;
    }

    repeatTimer -= deltaTime;
    if ( repeatTimer <= 0.0f ) {
        stepFocusedRow( direction );
        repeatTimer = kRepeatInterval;
    }
}

void OptionsWidgetPage::tickMouse() {
    const bool bButtonDown = gEdFlash.Input.bMouseButton0Down;
    const bool bClicked = bButtonDown && !bMouseDown;
    bMouseDown = bButtonDown;

    if ( draggedSlot >= 0 ) {
        if ( !bButtonDown || draggedSlot >= int32_t( slots.size() ) ) {
            draggedSlot = -1;
            return;
        }

        const OptionsRow &row = getRow( slots[draggedSlot].Row );
        const float ratio =
            GetMouseRatio( pPlayer, slots[draggedSlot].Path + "/gauge_length" );
        if ( ratio >= 0.0f && row.SetRatio ) {
            row.SetRatio( ratio );
            showSlotValue( draggedSlot );
        }
        return;
    }

    for ( int32_t i = 0; i < int32_t( tabs.size() ); i++ ) {
        if ( IsInClip( pPlayer, std::string( kListPath ) + "/onglet" +
                                    std::to_string( i + 1 ) ) ) {
            ShowHandCursor();
            if ( bClicked ) {
                selectTab( i );
                return;
            }
        }
    }

    int32_t slot = -1;
    for ( int32_t i = 0; i < int32_t( slots.size() ); i++ ) {
        if ( IsInClip( pPlayer, slots[i].Path ) ) {
            slot = i;
            break;
        }
    }

    if ( slot >= 0 && isRowEnabled( slots[slot].Row ) ) {
        ShowHandCursor();
    }

    if ( slot != hoveredSlot ) {
        hoveredSlot = slot;
        if ( slot >= 0 && isRowEnabled( slots[slot].Row ) ) {
            focusRow( slots[slot].Row, true );
            slot = findSlot( focusedRow );
            hoveredSlot = slot;
        }
    }

    if ( !bClicked || slot < 0 || slots[slot].Row != focusedRow ) {
        return;
    }

    switch ( getRow( focusedRow ).Type ) {
    case eOptionsRowType::ORT_Checkbox:
        stepFocusedRow( 1 );
        break;
    case eOptionsRowType::ORT_Gauge:
        if ( IsInClip( pPlayer, slots[slot].Path + "/gauge_length" ) ) {
            draggedSlot = slot;
        }
        break;
    default:
        break;
    }
}

void OptionsWidgetPage::tick( float deltaTime ) {
    if ( pPlayer == nullptr || state == PS_Closed ) {
        return;
    }

    if ( state == PS_Opening ) {
        const std::string firstWidget =
            GetPoolPath( eOptionsRowType::ORT_Gauge, 0 );
        if ( !pPlayer->spriteExists( firstWidget.c_str() ) ) {
            return;
        }

        firstRow = 0;
        focusedRow = 0;
        hoveredSlot = -1;
        draggedSlot = -1;
        lastInput = gpFlash->getInputMask();
        repeatTimer = 0.0f;
        while ( focusedRow < getNumRows() - 1 && !isRowEnabled( focusedRow ) ) {
            focusedRow++;
        }

        pPlayer->setVariableValueNonLinked(
            ( std::string( kListPath ) + ":msg" ).c_str(), kListMessage );
        pPlayer->setVariableValue(
            ( std::string( kListPath ) + ":valid" ).c_str(), 0 );
        showTabs();
        layout();
        showHelp();
        state = PS_Open;
        return;
    }

    tickKeys( deltaTime );
    if ( state == PS_Open ) {
        tickMouse();
    }
}

bool OptionsWidgetPage::onMessage( FlashMessage *pMessage ) {
    if ( state != PS_Open ) {
        return false;
    }

    if ( pMessage[0] == kWidgetMessage &&
         pMessage[1] == FlashMessage( kListMessage ) ) {
        if ( pMessage[2] == kScrollUp || pMessage[2] == kScrollDown ) {
            scroll( pMessage[2] == kScrollUp ? -1 : 1 );
        }
        return true;
    }

    const char *pTag = pMessage->pAsChar;
    if ( pTag[0] != 'o' || ( pTag[1] != 'p' && pTag[1] != 'n' ) ||
         !isdigit( uint8_t( pTag[2] ) ) || !isdigit( uint8_t( pTag[3] ) ) ) {
        return false;
    }

    const int32_t slot = ( pTag[2] - '0' ) * 10 + ( pTag[3] - '0' );
    if ( slot >= int32_t( slots.size() ) ) {
        return true;
    }

    if ( slots[slot].Row != focusedRow ) {
        focusRow( slots[slot].Row, false );
    }
    stepFocusedRow( pTag[1] == 'p' ? -1 : 1 );
    return true;
}
