#include "shared.h"
#include "mng_chat.h"

#include "flash/gs_flash.h"
#include "flash/mng_flash_localize.h"
#include "flash/modal_message_box.h"
#include "gs_timer.h"

#include "config/gs_config.h"
#include "game/gm_base.h"
#include "input/ed_dev.h"
#include "input/gs_device.h"
#include "input/input_device.h"

MngChat *gpMngChat = nullptr;

static inline FlashPlayer *GetGeneralMoviePlayer() {
    uint32_t levelIndex = 0u;
    uint32_t movieIndex = 0u;
    if ( !gpFlash->getFlashMovieIndex( &levelIndex, &movieIndex, "GENERAL" ) ) {
        return nullptr;
    }

    MoviePlayer *pMoviePlayer =
        gpFlash->getMoviePlayer( levelIndex, movieIndex );
    if ( pMoviePlayer == nullptr || !pMoviePlayer->MovieBank.isLoaded() ) {
        return nullptr;
    }

    return pMoviePlayer->MovieBank.getFlashPlayer();
}

// Builds the three paths the movie exposes for a given chat line
// (e.g. "pop_chat/log/line1", "pop_chat/log/line1/color" and
// "pop_chat/log/line1/color/:txt")
static inline void BuildChatLinePaths( const int32_t lineIndex,
                                       char *pLinePath,
                                       char *pColorPath,
                                       char *pTextPath ) {
    sprintf( pLinePath, "pop_chat/log/line%d", lineIndex + 1 );
    sprintf( pColorPath, "%s/color", pLinePath );
    sprintf( pTextPath, "%s/:txt", pColorPath );
}

MngChat::MngChat()
    : Manager(),
      mutex(),
      time( 0.0f ),
      bOnlineAvailable( false ),
      field700_0x331( false ) {
    // FUN_006b1bb0
    pTextInput[0] = '\0';
    resetLines();
    OTDU_ASSERT( gpMngChat == nullptr );
    gpMngChat = this;
}

MngChat::~MngChat() {
    // FUN_004327e0
    mutex.terminate();
    gpMngChat = nullptr;
}

void MngChat::tick( float, float ) {
    // FUN_006b2590
    if ( gpTimer->GameSpeed <= 0.0f ) {
        closeInputPopup();
        bOnlineAvailable = false;
        return;
    }
    time += gpTimer->SystemDeltaTime;
    tickLines();

    if ( gpConfig->pPlayerConfig == nullptr || bOnlineAvailable ||
         field700_0x331 == false ) {
        return;
    }

    DeviceAction *pChatAction = gpConfig->pPlayerConfig->pChat;
    if ( pChatAction == nullptr || pChatAction->NumAddedChannels == 0 ||
         !IsActionPressed( pChatAction ) ) {
        return;
    }

    if ( getLocalPlayerIndex() == -1 ) {
        gpFlash->getFlashSound()->playFrontEndSound( "wrong", 0, false );
        return;
    }

    FlashPlayer *pPlayer = gpFlash->getFlashPlayer( "GENERAL" );
    if ( pPlayer != nullptr ) {
        memset( pTextInput, 0, sizeof( pTextInput ) );
        // The edit text writes straight into our buffer
        pPlayer->setVariableValue( "/:TxtInput", pTextInput );
        pPlayer->showFrame( "open", "/pop_chat", true );
        bOnlineAvailable = true;
    }
}

void MngChat::reset() {
    // FUN_006b1af0
    if ( field700_0x331 == false ) {
        field700_0x331 = true;
    }
}

void MngChat::pause() {
    // FUN_006b2720
    FlashPlayer *pPlayer = GetGeneralMoviePlayer();
    if ( pPlayer != nullptr ) {
        pPlayer->setVariableValueNonLinked( "pop_chat/log/line1/color/:txt",
                                            " " );
        pPlayer->setVariableValueNonLinked( "pop_chat/log/line2/color/:txt",
                                            " " );
        pPlayer->setVariableValueNonLinked( "pop_chat/log/line3/color/:txt",
                                            " " );
        pPlayer->setVariableValueNonLinked( "pop_chat/log/line4/color/:txt",
                                            " " );
    }

    closeInputPopupIfOpen();
}

void MngChat::closeInputPopupIfOpen() {
    // FUN_006b2720 (inlined)
    if ( field700_0x331 != false ) {
        closeInputPopup();
        field700_0x331 = false;
    }
}

bool MngChat::onMessage( FlashMessage *pMessage ) {
    // FUN_006b2330
    OTDU_ASSERT( pMessage );

    switch ( *pMessage ) {
    case FlashMessage( "chva" ): { // the local player validated its input
        char pInput[128];
        strncpy( pInput, pTextInput, 0x80 );
        pInput[sizeof( pInput ) - 1] = '\0';

        if ( strlen( pInput ) != 0 ) {
            if ( getLocalPlayerIndex() != -1 ) {
                // TODO: ONLINE/P2P
                OTDU_UNIMPLEMENTED_ONCE;
                bOnlineAvailable = false;
                return true;
            }

            addLine( "No online available", "System", 0x10 );
        }

        bOnlineAvailable = false;
        return true;
    }
    case FlashMessage( "clli" ): { // a line finished its close animation
        ScopedMutexLock lock( &mutex );

        if ( lines[0].State == ChatLine::CLS_Closing ) {
            // Scroll the log up by one
            for ( int32_t i = 0; i < kNumChatLines - 1; i++ ) {
                lines[i] = lines[i + 1];
            }

            ChatLine &lastLine = lines[kNumChatLines - 1];
            lastLine.pText[0] = ' ';
            lastLine.pText[1] = '\0';

            const ChatLine::eState firstLineState = lines[0].State;
            lastLine.ColorIndex = -1;
            lastLine.DisplayUntil = kChatLineUnused;
            lastLine.State = ChatLine::CLS_Unused;

            if ( firstLineState == ChatLine::CLS_Visible ) {
                FlashPlayer *pPlayer = gpFlash->getFlashPlayer( "GENERAL" );
                if ( pPlayer != nullptr ) {
                    pPlayer->showFrame( "open", "pop_chat/log/line1", true );
                }
            }

            refreshLineVariables();
        }

        return true;
    }
    default:
        break;
    }

    return false;
}

void MngChat::addLine( const char *pText,
                       const char *pAuthor,
                       const int32_t colorIndex ) {
    // FUN_006b20e0
    ScopedMutexLock lock( &mutex );

    int32_t lineIndex = 0;
    for ( ; lineIndex < kNumChatLines; lineIndex++ ) {
        if ( lines[lineIndex].DisplayUntil == kChatLineUnused ) {
            break;
        }
    }

    if ( lineIndex < kNumChatLines ) {
        // Found a free slot: simply play its open animation
        lines[lineIndex].State = ChatLine::CLS_Visible;

        char pLinePath[32];
        sprintf( pLinePath, "pop_chat/log/line%d", lineIndex + 1 );

        FlashPlayer *pPlayer = GetGeneralMoviePlayer();
        if ( pPlayer != nullptr ) {
            pPlayer->showFrame( "open", pLinePath, true );
        }
    } else {
        // The log is full: recycle the oldest line
        const ChatLine::eState firstLineState = lines[0].State;
        if ( firstLineState == ChatLine::CLS_Closing ||
             firstLineState == ChatLine::CLS_Unused ) {
            FlashPlayer *pPlayer = GetGeneralMoviePlayer();
            if ( pPlayer != nullptr ) {
                pPlayer->showFrame( "open", "pop_chat/log/line1", true );
            }
        }

        for ( int32_t i = 0; i < kNumChatLines - 1; i++ ) {
            lines[i] = lines[i + 1];
        }

        lineIndex = kNumChatLines - 1;
    }

    ChatLine &line = lines[lineIndex];
    if ( gpMngFlashLocalize == nullptr ) {
        sprintf( line.pText, "%s : %s", pAuthor, pText );
    } else {
        gpMngFlashLocalize->setSysVar( "AUTHOR", pAuthor );
        gpMngFlashLocalize->setSysVar( "CHATTEXT", pText );
        gpMngFlashLocalize->makeSentenceFromHashcode(
            line.pText, sizeof( line.pText ), kChatLineSentenceHashcode );
        gpMngFlashLocalize->clearAllSysVar();
    }

    line.ColorIndex = colorIndex;
    line.DisplayUntil = time + kChatLineDuration;
    line.State = ChatLine::CLS_Visible;

    refreshLineVariables();
}

void MngChat::FlashClose() {
    // FUN_006b1c00
    FlashPlayer *peVar1 = gpFlash->getFlashPlayer( "GENERAL" );
    if ( peVar1 != nullptr ) {
        peVar1->showFrame( "close", "/pop_chat", true );
    }
}

void MngChat::resetLines() {
    // FUN_006b1b10
    ScopedMutexLock lock( &mutex );

    for ( ChatLine &line : lines ) {
        memset( line.pText, 0, 0x80 );
        line.ColorIndex = -1;
        line.DisplayUntil = kChatLineUnused;
        line.State = ChatLine::CLS_Unused;
    }
}

void MngChat::closeInputPopup() {
    // FUN_006b1d40
    if ( bOnlineAvailable == false ) {
        return;
    }

    gpDevice->setKeyboardInputEnabled( true );
    Device *pKeyboard = gEdDev.getDevice( eDeviceType::DT_Keyboard, 0 );
    if ( pKeyboard != nullptr ) {
        pKeyboard->setEnabled();
    }

    FlashPlayer *pPlayer = gpFlash->getFlashPlayer( "GENERAL" );
    if ( pPlayer != nullptr ) {
        pPlayer->showFrame( "default", "/pop_chat", true );
        pPlayer->releaseTextInputFocus();
    }

    // FUN_0099fcc0 (inlined)
    if ( gpFlashMessageBox != nullptr ) {
        gpFlashMessageBox->FUN_0099fcc0();
    }

    if ( gpActiveGameMode != nullptr ) {
        gpActiveGameMode->setsMessageBoxVisible( false );
    }
}

void MngChat::tickLines() {
    // FUN_006b1c70
    ScopedMutexLock lock( &mutex );

    for ( int32_t i = 0; i < kNumChatLines; i++ ) {
        ChatLine &line = lines[i];
        if ( line.DisplayUntil == kChatLineUnused ||
             line.DisplayUntil >= time ||
             line.State == ChatLine::CLS_Closing ) {
            continue;
        }

        line.State = ChatLine::CLS_Closing;

        char pLinePath[32];
        sprintf( pLinePath, "pop_chat/log/line%d", i + 1 );

        FlashPlayer *pPlayer = gpFlash->getFlashPlayer( "GENERAL" );
        if ( pPlayer != nullptr ) {
            pPlayer->showFrame( "close", pLinePath, true );
        }
    }
}

void MngChat::refreshLineVariables() {
    // FUN_006b1df0
    FlashPlayer *pPlayer = GetGeneralMoviePlayer();
    if ( pPlayer == nullptr ) {
        return;
    }

    char pLinePath[32];
    char pColorPath[32];
    char pTextPath[32];

    for ( int32_t i = 0; i < kNumChatLines; i++ ) {
        BuildChatLinePaths( i, pLinePath, pColorPath, pTextPath );
        pPlayer->setVariableValueNonLinked( pTextPath, "" );
    }

    for ( int32_t i = 0; i < kNumChatLines; i++ ) {
        ChatLine &line = lines[i];
        if ( line.DisplayUntil == kChatLineUnused ) {
            continue;
        }

        BuildChatLinePaths( i, pLinePath, pColorPath, pTextPath );

        pPlayer->goToFrameAndPlay( line.ColorIndex, pColorPath, false );
        pPlayer->setVariableValue( pTextPath, line.pText );
    }
}

int32_t MngChat::getLocalPlayerIndex() const {
    // FUN_006b2590 (inlined)
    // TODO: ONLINE/P2P
    OTDU_UNIMPLEMENTED_ONCE;
    return -1;
}
