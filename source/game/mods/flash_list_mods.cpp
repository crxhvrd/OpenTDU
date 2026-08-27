#include "shared.h"
#include "flash_list_mods.h"

#include "flash/gs_flash.h"
#include "flash/implementation/flash_player.h"
#include "flash/implementation/flash_sprite.h"
#include "flash/implementation/flash_transform.h"

static CXForm GetLineStateColorTransform( FlashList::eLineState state ) {
    CXForm color;

    switch ( state ) {
    case FlashList::eLineState::FLLS_Green:
        color.MulTerm[0] = 0x50;
        color.MulTerm[1] = 0x160;
        color.MulTerm[2] = 0x70;
        break;
    case FlashList::eLineState::FLLS_Red:
        color.MulTerm[0] = 0x160;
        color.MulTerm[1] = 0x50;
        color.MulTerm[2] = 0x50;
        break;
    case FlashList::eLineState::FLLS_Gold:
    case FlashList::eLineState::FLLS_GoldSelected:
        color.MulTerm[0] = 0x150;
        color.MulTerm[1] = 0x110;
        color.MulTerm[2] = 0x40;
        break;
    case FlashList::eLineState::FLLS_Disabled:
        color.MulTerm[0] = 0x80;
        color.MulTerm[1] = 0x80;
        color.MulTerm[2] = 0x80;
        break;
    default:
        break;
    }

    return color;
}

static void SetClipColorTransform( FlashPlayer *pMovie,
                                   const char *pPath,
                                   const CXForm &color ) {
    FlashClip clip;
    if ( !pMovie->getClipPlayer( pPath, &clip ) || clip.pSprite == nullptr ) {
        static bool bReported = false;
        if ( !bReported ) {
            bReported = true;
            OTDU_LOG_WARN( "Mod manager: no clip at '%s', row colours are "
                           "off for this pane\n",
                           pPath );
        }
        return;
    }

    ScopedMutexLock lock = gpFlash->scopedLock();
    FlashResource *pPreviousResource = gpActiveFlashResource;
    gpActiveFlashResource = clip.pParent;
    gpPreviousActiveFlashResource = gpActiveFlashResource;

    FlashTransform &transform = clip.pSprite->getTransformRW();
    transform.Flags |= 1;
    transform.Transform = color;
    clip.pSprite->updateTransform();

    gpPreviousActiveFlashResource = pPreviousResource;
    gpActiveFlashResource = pPreviousResource;
}

FlashListMods::FlashListMods( const std::vector<ModsListRow> *pList )
    : FlashList(), pRows( pList ), lineText{} {}

void FlashListMods::setRowText( const int32_t lineIndex, const char *pText ) {
    if ( pMovie == nullptr || lineIndex < 0 || lineIndex >= kMaxLines ) {
        return;
    }

    char *pSlot = lineText[lineIndex];
    strncpy( pSlot, pText, kLineTextLength - 1 );
    pSlot[kLineTextLength - 1] = '\0';

    char varName[192];
    snprintf( varName, sizeof( varName ), "%s/line%d:name", movieName.c_str(),
              lineIndex + 1 );
    pMovie->setVariableValue( varName, pSlot );
}

int32_t FlashListMods::getListLength() const {
    return ( pRows != nullptr ) ? (int32_t)pRows->size() : 0;
}

void FlashListMods::update( const int32_t entryIndex,
                            const int32_t lineIndex ) {
    if ( pMovie == nullptr || pRows == nullptr || entryIndex < 0 ||
         entryIndex >= (int32_t)pRows->size() ) {
        return;
    }

    const ModsListRow &row = ( *pRows )[entryIndex];

    setRowText( lineIndex, row.Name.c_str() );

    applyRowState( row, lineIndex );
}

void FlashListMods::applyRowState( const ModsListRow &row,
                                   const int32_t lineIndex ) {
    if ( pMovie == nullptr ) {
        return;
    }

    char linePath[160];
    snprintf( linePath, sizeof( linePath ), "%s/line%d", movieName.c_str(),
              lineIndex + 1 );

    char validPath[192];
    snprintf( validPath, sizeof( validPath ), "%s/:IsValid", linePath );
    pMovie->setVariableValue( validPath, 1 );

    const eLineState frameState = ( row.State == eLineState::FLLS_Green )
                                      ? eLineState::FLLS_Normal
                                      : row.State;
    pMovie->showFrame( getLineStateFrameName( frameState ), linePath, false );

    setRowColor( lineIndex, row.State );
}

void FlashListMods::clearRow( const int32_t lineIndex ) {
    if ( pMovie == nullptr ) {
        return;
    }

    char linePath[160];
    snprintf( linePath, sizeof( linePath ), "%s/line%d", movieName.c_str(),
              lineIndex + 1 );

    setRowText( lineIndex, "" );

    char validPath[192];
    snprintf( validPath, sizeof( validPath ), "%s/:IsValid", linePath );
    pMovie->setVariableValue( validPath, 0 );

    pMovie->showFrame( getLineStateFrameName( eLineState::FLLS_Empty ),
                       linePath, false );

    setRowColor( lineIndex, eLineState::FLLS_Normal );
}

void FlashListMods::setRowColor( const int32_t lineIndex,
                                 const eLineState state ) {
    if ( pMovie == nullptr ) {
        return;
    }

    char linePath[160];
    snprintf( linePath, sizeof( linePath ), "%s/line%d", movieName.c_str(),
              lineIndex + 1 );

    SetClipColorTransform( pMovie, linePath,
                           GetLineStateColorTransform( state ) );
}

void FlashListMods::refresh() {
    if ( pMovie == nullptr || !pMovie->spriteExists( movieName.c_str() ) ) {
        numLines = 0;
        return;
    }

    if ( numLines == 0 ) {
        open();
        return;
    }

    reset();
    repaintRows();
}

void FlashListMods::selectedLineChanged() {
    if ( onSelectionChanged != nullptr ) {
        onSelectionChanged();
    }
}

void FlashListMods::repaintRows() {
    if ( pRows == nullptr ) {
        return;
    }

    const int32_t rowCount = (int32_t)pRows->size();
    for ( int32_t line = 0; line < numLines; line++ ) {
        const int32_t entry = firstLine + line;
        if ( entry >= 0 && entry < rowCount ) {
            applyRowState( ( *pRows )[entry], line );
        } else {
            clearRow( line );
        }
    }
}

int32_t FlashListMods::getSelectedIndex() const {
    if ( pRows == nullptr ) {
        return -1;
    }

    const int32_t index = firstLine + selectedLineIndex;
    return ( index >= 0 && index < (int32_t)pRows->size() ) ? index : -1;
}
