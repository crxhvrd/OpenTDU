#include "shared.h"
#include "flash_list_bookmark.h"

#include "mng_bookmark.h"
#include "flash/flash_callbacks_register.h"
#include "flash/implementation/flash_player.h"

// TODO: Move to right cpp
static bool FUN_00914cf0( const FlashMessage message,
                          FlashListBookmark *pList ) {
    // FUN_00914cf0
    OTDU_UNIMPLEMENTED; // TODO: AI (deferred bookmark action)
    return false;
}

// TODO: Move to right cpp
static void EditorGoToBookmark( const Eigen::Vector3f &position ) {
    // FUN_0078bfe0
    OTDU_UNIMPLEMENTED; // TODO: GAME (GME is not ported yet)
}

FlashListBookmark::FlashListBookmark() : FlashList(), pParent( nullptr ) {
    // FUN_0070b430 (inlined)
    levelIndex = 1;
    bIsEmpty = true;
    field16_0x67 = true;
}

bool FlashListBookmark::onMessage( FlashMessage *pMessage,
                                   FlashPlayer *pPlayer ) {
    // FUN_0070cfa0
    const int32_t bookmarkIndex = firstLine + selectedLineIndex;

    switch ( *pMessage ) {
    case kDelete:
        pParent->onDelete( bookmarkIndex );
        return true;

    case kGoto:
        if ( !FUN_00914cf0( kGoto, this ) ) {
            pParent->goTo( bookmarkIndex );
        }
        return true;

    case kImportBookmark:
        if ( pParent->isGME() ) {
            EditorGoToBookmark(
                pParent->getBookmark( bookmarkIndex ).Position );
        }
        return true;

    default:
        return FlashList::onMessage( pMessage, pPlayer );
    }
}

int32_t FlashListBookmark::getListLength() const {
    // FUN_0070b040
    return pParent->getNumBookmarks();
}

void FlashListBookmark::close() {
    // FUN_0070bf80
    gFlashCallbacksRegister.removeAllSynonyms(
        &MngBookmark::PictureDrawCallback );
    bOpened = false;
}

void FlashListBookmark::update( const int32_t param_2, const int32_t param_3 ) {
    // FUN_0070b990
    if ( getListLength() <= param_2 ) {
        setLineState( eLineState::FLLS_Empty, param_3 );
        return;
    }

    const int32_t lineIndex = param_3 + 1;

    char acStack_100[128];
    sprintf( acStack_100, "%s/line%d", movieName.c_str(), lineIndex );

    char acStack_80[128];
    sprintf( acStack_80, "%s/line%d/:IsValid", movieName.c_str(), lineIndex );

    pMovie->setVariableValue( acStack_80, 1 );
    pMovie->showFrame( "normal", acStack_100, false );

    sprintf( acStack_100, "%s/line%d:%s", movieName.c_str(), lineIndex,
             "name" );
    pMovie->setVariableValue( acStack_100,
                              pParent->getBookmark( param_2 ).pName );
}

void FlashListBookmark::selectedLineChanged() {
    // FUN_0070bf60
    pParent->onSelectionChanged( firstLine + selectedLineIndex );
}
