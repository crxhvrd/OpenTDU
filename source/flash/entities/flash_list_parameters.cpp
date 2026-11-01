#include "shared.h"
#include "flash_list_parameters.h"

#include "database/gs_database.h"
#include "flash/gs_flash.h"
#include "flash/implementation/ed_flash.h"
#include "flash/implementation/flash_player.h"
#include "flash/implementation/flash_sprite.h"
#include "game/scenaric/parameters/game_parameter.h"
#include "input/gs_device.h"
#include "input/input_device.h"

static constexpr FlashMessage kMsgCheckBox = FlashMessage( "ckbo" );
static constexpr FlashMessage kMsgArrowLeft = FlashMessage( "arwl" );
static constexpr FlashMessage kMsgArrowRight = FlashMessage( "arwr" );
static constexpr FlashMessage kMsgNextValue = FlashMessage( "neva" );
static constexpr FlashMessage kMsgPreviousValue = FlashMessage( "prva" );

static constexpr float kArrowInitialDelay = 0.5f;  // FLOAT_00f41674
static constexpr float kArrowMinDelay = 0.01f;     // DAT_00f416d4
static constexpr float kArrowDelayFactor = 0.5f;   // FLOAT_00f41674
static constexpr float kArrowNoStepTime = -1.0f;   // FLOAT_00f41664
static constexpr uint64_t kUnknownParameterNameHashcode = 0x3ab89bf;

static inline bool IsLabel( const char *pLabel,
                            const char *pReference,
                            const size_t length ) {
    return strncmp( pLabel, pReference, length ) == 0;
}

FlashListParameters::FlashListParameters()
    : FlashList(),
      pWidgetPlayer( nullptr ),
      bArrowRepeatActive( false ),
      arrowInitialDelay( kArrowInitialDelay ),
      arrowMinDelay( kArrowMinDelay ),
      arrowLastStepTime( kArrowNoStepTime ),
      arrowDelay( 0.0f ),
      bArrowLeftHeld( false ),
      bArrowRightHeld( false ),
      bArrowLeftClicked( false ),
      bArrowRightClicked( false ) {
    // FUN_0071bfc0
    field16_0x67 = true;
}

bool FlashListParameters::onMessage( FlashMessage *pMessage,
                                     FlashPlayer *pPlayer ) {
    // FUN_0071c490
    const FlashMessage message = *pMessage;
    if ( message == kMsgArrowLeft ) {
        bArrowRightClicked = false;
        bArrowLeftClicked = true;
        return true;
    }
    if ( message == kMsgArrowRight ) {
        bArrowLeftClicked = false;
        bArrowRightClicked = true;
        return true;
    }
    if ( message == kMsgNextValue || message == kMsgPreviousValue ) {
        return true;
    }
    if ( message == kMsgCheckBox ) {
        GameParameter *pParameter =
            getParameter( selectedLineIndex + firstLine );
        if ( pParameter != nullptr && isParameterEditable( pParameter ) ) {
            int32_t checkBoxState = 0;
            const char *pFrame = applyClick( pParameter, &checkBoxState );
            pPlayer->setVariableValue( "/:IsCheckBox", checkBoxState );
            showLineSubClipFrame( selectedLineIndex, "text/paraValue",
                                  pFrame );
            reset();
            onParameterChanged();
        }
        return true;
    }

    return FlashList::onMessage( pMessage, pPlayer );
}

void FlashListParameters::tick( float param_2, float param_3 ) {
    // FUN_0071c780
    FlashList::tick( param_2, param_3 );
    tickArrowRepeat( param_3 );
}

void FlashListParameters::update( const int32_t index,
                                  const int32_t lineIndex ) {
    // FUN_0071c0a0
    if ( index >= getListLength() ) {
        setLineState( eLineState::FLLS_Empty, lineIndex );
        return;
    }

    bool bArrowsVisible = false;
    const bool bNextStep = isNextStepLine( index );
    GameParameter *pParameter = getParameter( index );

    if ( index == selectedLineIndex + firstLine ) {
        int32_t checkBoxState = 0;
        if ( pParameter != nullptr ) {
            const char *pLabel = pParameter->getTypeDesc()->pLabel;
            if ( IsLabel( pLabel, "input_txt", 5 ) ||
                 IsLabel( pLabel, "chkbox_off", 6 ) ||
                 pParameter->getFileFlags() == 1 ) {
                checkBoxState = 1;
            } else if ( pParameter->getFileFlags() == 2 ) {
                checkBoxState = 2;
            } else {
                bArrowsVisible = true;
            }
        }
        if ( pWidgetPlayer != nullptr ) {
            pWidgetPlayer->setVariableValue( "/:IsNextStep",
                                             int32_t( bNextStep ) );
            pWidgetPlayer->setVariableValue( "/:IsCheckBox", checkBoxState );
        }
    }

    char lineClip[0x80];
    char lineValid[0x80];
    snprintf( lineClip, sizeof( lineClip ), "%s/line%d", movieName.c_str(),
              lineIndex + 1 );
    snprintf( lineValid, sizeof( lineValid ), "%s/line%d/:IsValid",
              movieName.c_str(), lineIndex + 1 );

    if ( bNextStep ) {
        pMovie->setVariableValue( lineValid, 0 );
        pMovie->showFrame( "nextstep", lineClip, false );
        return;
    }
    if ( pParameter == nullptr ) {
        return;
    }

    const bool bEditable = isParameterEditable( pParameter );
    setLineSubClipVisible( lineIndex, "text/arr_l", bArrowsVisible );
    setLineSubClipVisible( lineIndex, "text/arr_r", bArrowsVisible );

    const char *pLineFrame = "normal";
    if ( !bEditable ) {
        pMovie->setVariableValue( lineValid, 0 );
        pLineFrame = "disabled";
    } else if ( pParameter->isTrackingParent() ) {
        pMovie->setVariableValue( lineValid, 0 );
        pLineFrame = "ss_menu";
    } else if ( pParameter->getFileFlags() == 1 ) {
        pMovie->setVariableValue( lineValid, 0 );
        pLineFrame = "disabled";
    } else {
        pMovie->setVariableValue( lineValid, 1 );
    }
    pMovie->showFrame( pLineFrame, lineClip, false );

    const char *pName = gpDatabase->getStringByHashcode(
        0, pParameter->getDescriptionHashcode() );
    if ( pName == nullptr ) {
        pName =
            gpDatabase->getStringByHashcode( 0, kUnknownParameterNameHashcode );
    }
    setLineField( lineIndex, "paraName", pName );

    const char *pLabel = pParameter->getTypeDesc()->pLabel;
    if ( pLabel != nullptr ) {
        showLineSubClipFrame( lineIndex, "text/paraValue", pLabel );
    }
    if ( !IsLabel( pLabel, "input_txt", 5 ) &&
         pParameter->getFileFlags() != 0 ) {
        setLineSubClipVisible( lineIndex, "text/paraValue/chkbox", true );
        showLineSubClipFrame( lineIndex, "text/paraValue/chkbox",
                              ( pParameter->getFileFlags() == 2 ) ? "on"
                                                                  : "off" );
    } else {
        setLineSubClipVisible( lineIndex, "text/paraValue/chkbox", false );
    }

    const char *pDisplayText = pParameter->getDisplayText();
    if ( pDisplayText != nullptr ) {
        setLineField( lineIndex, "textField", pDisplayText );
    }
}

void FlashListParameters::selectedLineChanged() {
    // FUN_0071bee0
    update( selectedLineIndex + firstLine, selectedLineIndex );
    arrowLastStepTime = kArrowNoStepTime;
    bArrowLeftHeld = false;
    bArrowRightHeld = false;

    const bool bNextStep = isNextStepLine( selectedLineIndex + firstLine );
    GameParameter *pParameter = getParameter( selectedLineIndex + firstLine );
    const bool bEditable = isParameterEditable( pParameter );
    bool bCheckBox = false;
    bool bTextInput = false;
    if ( pParameter != nullptr ) {
        const char *pLabel = pParameter->getTypeDesc()->pLabel;
        bCheckBox = IsLabel( pLabel, "chkbox_off", 6 );
        bTextInput = IsLabel( pLabel, "input_txt", 5 );
    }

    if ( !bNextStep && !bCheckBox && !bTextInput && bEditable ) {
        activateArrowRepeat();
        return;
    }
    bArrowRepeatActive = false;
}

void FlashListParameters::setLineSubClipVisible( const int32_t lineIndex,
                                                 const char *pSubClip,
                                                 const bool bVisible ) {
    // FUN_0071ca00
    char clipPath[0x80];
    snprintf( clipPath, sizeof( clipPath ), "%s/line%d/%s", movieName.c_str(),
              lineIndex + 1, pSubClip );

    FlashClip clip;
    if ( !pMovie->getClipPlayer( clipPath, &clip ) ) {
        return;
    }

    ScopedMutexLock lock = gpFlash->scopedLock();
    gpPreviousActiveFlashResource = gpActiveFlashResource;
    uint32_t &flags = clip.pSprite->getDisplayList()->getFlagsRW();
    if ( bVisible ) {
        flags |= 1;
    } else {
        flags &= ~1u;
    }
}

void FlashListParameters::activateArrowRepeat() {
    // FUN_0071beb0
    if ( bArrowRepeatActive ) {
        return;
    }
    arrowLastStepTime = kArrowNoStepTime;
    bArrowLeftHeld = false;
    bArrowRightHeld = false;
    bArrowRepeatActive = true;
}

void FlashListParameters::stepSelectedParameter( const bool bIncrement ) {
    // FUN_0071c580
    GameParameter *pParameter = getParameter( selectedLineIndex + firstLine );
    if ( pParameter->getFileFlags() == 1 ) {
        return;
    }
    onParameterChanged();
    if ( bIncrement ) {
        pParameter->increment( 0 );
    } else {
        pParameter->decrement( 0 );
    }
    reset();
}

void FlashListParameters::tickArrowRepeat( const float totalTime ) {
    // FUN_0071c5d0
    if ( !bArrowRepeatActive ) {
        return;
    }

    if ( bArrowLeftClicked || bArrowRightClicked ) {
        const InputChannel *pButton0 =
            gpDevice->getMouseState().pChannelButton0;
        if ( pButton0 == nullptr || ( pButton0->Flags & 0x10 ) == 0 ) {
            bArrowLeftClicked = false;
            bArrowRightClicked = false;
        }
    }

    bool bLeft = false;
    bool bRight = false;
    gpDevice->retrieveFlashNavKeyStates( &bLeft, &bRight, true, true, 1 );
    if ( bArrowLeftClicked ) {
        bLeft = true;
        bRight = false;
    } else if ( bArrowRightClicked ) {
        bLeft = false;
        bRight = true;
    } else if ( bLeft ) {
        bRight = false;
    }

    const bool bFirstLeft = bLeft && !bArrowLeftHeld;
    const bool bFirstRight = !bLeft && bRight && !bArrowRightHeld;
    if ( bFirstLeft || bFirstRight ) {
        arrowLastStepTime = totalTime;
        arrowDelay = arrowInitialDelay;
        GameParameter *pParameter =
            getParameter( selectedLineIndex + firstLine );
        if ( pParameter->getFileFlags() != 1 ) {
            onParameterChanged();
            if ( bFirstLeft ) {
                pParameter->decrement( 0 );
            } else {
                pParameter->increment( 0 );
            }
            reset();
        }
    } else if ( ( bLeft || bRight ) &&
                arrowDelay <= totalTime - arrowLastStepTime ) {
        arrowLastStepTime = totalTime;
        arrowDelay = std::max( arrowDelay * kArrowDelayFactor, arrowMinDelay );
        stepSelectedParameter( bRight );
    }

    bArrowRightHeld = bRight;
    bArrowLeftHeld = bLeft;
}
