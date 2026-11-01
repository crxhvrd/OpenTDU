#include "shared.h"
#include "modal_message_box.h"

#include "database/gs_database.h"
#include "game/gs_tutorial.h"
#include "game/scenaric/gs_scenaric.h"
#include "gs_flash.h"
#include "gs_timer.h"
#include "implementation/flash_sprite.h"
#include "mng_flash_localize.h"
#include "online/ed_net_gamespy_manager.h"
#include "system/gs_keyboard_pc.h"

ModalMessageBox *gpFlashMessageBox = nullptr;
ModalMessageBox gFlashMessageBox = {};

ModalMessageBox::ModalMessageBox()
    : pFlashPlayer( nullptr ),
      inputState( eInputState::MMBIS_None ),
      displayState( eDisplayState::MMBDS_Closed ),
      promptType( MPT_Warning ),
      bHasCancel( false ),
      messageHashcode( 0ull ),
      messageDatabaseIndex( 0 ),
      pDefaultMessage( nullptr ),
      pAnswerCallback( nullptr ),
      pUserCallback( nullptr ),
      pUserCallbackData( nullptr ),
      pWaitingCallback( nullptr ),
      pWaitingCallbackData( nullptr ),
      displayTime( 0.0f ),
      closeAfterDuration( 0.0f ),
      answer( ePopupAnswer::PA_Unknown ),
      userData( 0 ),
      numOpenMessageBox( 0 ),
      bUnknownFlag( false ),
      bVisible( false ),
      bUnknownFlag2( false ) {
    gpFlashMessageBox = this;
}

ModalMessageBox::~ModalMessageBox() {}

void ModalMessageBox::tick() {
    // FUN_0099ff50
    if ( displayState == eDisplayState::MMBDS_Closing ) {
        displayState = eDisplayState::MMBDS_Closed;

        if ( pAnswerCallback != nullptr ) {
            pAnswerCallback( answer, pUserCallbackData, userData );
        }

        if ( pUserCallback != nullptr ) {
            pUserCallback->onClose( answer, pUserCallbackData, userData );
        }
    }

    GameMode *pGVar3 = gpActiveGameMode;
    if ( bUnknownFlag != false ) {
        if ( gpActiveGameMode != nullptr ) {
            gpActiveGameMode->setfield7_0x1c( true );
            gpActiveGameMode->setsMessageBoxVisible( bVisible );
        }

        for ( int32_t movieIndex = 0; movieIndex < gpFlash->getNumMovies( 1 );
              movieIndex++ ) {
            MoviePlayer *pMVar6 = gpFlash->getMoviePlayer( 1, movieIndex );
            if ( pMVar6 != nullptr ) {
                pMVar6->Flags = pMVar6->Flags & 0xffffffef;
            }
        }

        uint32_t uStack_c = 0;
        uint32_t FStack_8 = 0;
        bool bVar4 =
            gpFlash->getFlashMovieIndex( &uStack_c, &FStack_8, "CONFIGPC" );
        if ( bVar4 ) {
            MoviePlayer *pMVar6 = gpFlash->getMoviePlayer( uStack_c, FStack_8 );
            if ( pMVar6 != nullptr ) {
                pMVar6->Flags = pMVar6->Flags & 0xffffffef;
            }
        }

        if ( bMouseActive != false ) {
            gpFlash->getMouse()->field12_0x30 = true;
        }

        bUnknownFlag = false;
    }

    eInputState FVar1 = inputState;
    if ( eInputState::MMBIS_None < FVar1 ) {
        if ( eInputState::MMBIS_CloseRequested < FVar1 ) {
            goto LAB_009a0071;
        }
        displayTime += gpTimer->SystemDeltaTime;
    }
    if ( ( FVar1 == eInputState::MMBIS_CloseRequested ) &&
         ( closeAfterDuration <= displayTime ) ) {
        close();
    }

LAB_009a0071:
    FlashPlayer *pFVar2 = pFlashPlayer;

    bool bVar4 = false;
    if ( inputState == eInputState::MMBIS_None ) {
        if ( ( pFVar2 != nullptr ) &&
             ( bVar4 = pFVar2->spriteExists( "/load_scr_popup" ), bVar4 ) ) {
            FlashClip FStack_8;
            bVar4 = pFlashPlayer->getClipPlayer( "/load_scr_popup", &FStack_8 );
            if ( bVar4 ) {
                FStack_8.setProperty( eFlashProperty::FP_Visible, true );
                return;
            }
        }
    } else if ( ( pFVar2 != nullptr ) &&
                ( bVar4 = pFVar2->spriteExists( "/load_scr_popup" ), bVar4 ) ) {
        FlashClip FStack_8;
        bVar4 = pFlashPlayer->getClipPlayer( "/load_scr_popup", &FStack_8 );
        if ( bVar4 ) {
            FStack_8.setProperty( eFlashProperty::FP_Visible, false );
            return;
        }
    }
}

bool ModalMessageBox::display( uint64_t hashcode,
                               float param_2,
                               int32_t param_5,
                               void ( *pCallback )( void * ),
                               void *pCallbackData,
                               bool param_7 ) {
    // FUN_009a0210
    if ( inputState != eInputState::MMBIS_None ) {
        return false;
    }

    displayTime = 0.0f;
    pWaitingCallback = pCallback;
    pWaitingCallbackData = pCallbackData;
    bUnknownFlag2 = param_7;
    if ( param_7 ) {
        int32_t iVar1 = gpFlash->getNumMovies( 1 );
        for ( int32_t i = 0; i < iVar1; i++ ) {
            MoviePlayer *pMVar2 = gpFlash->getMoviePlayer( 1, i );
            if ( pMVar2 != nullptr ) {
                pMVar2->Flags |= 0x10;
            }
        }
        gpFlash->enablePlayer( "CONFIGPC",
                               ModalMessageBox::eDisplayState::MMBDS_Closed,
                               false, 4 );
    }

    FlashPlayer *pPlayer = gpFlash->getFlashPlayer( "GENERAL" );
    const char *pFrameToDisplay = getFrameToDisplay( param_2 );

    if ( pPlayer != nullptr ) {
        const char *pcVar4 =
            gpDatabase->getStringByHashcode( param_5, hashcode );
        pPlayer->setVariableValue( "/:msg", pcVar4 );
        pPlayer->showFrame( pFrameToDisplay, "/waiting", true );
    }

    updateInputState( param_2 );
    return true;
}

ModalMessageBox::eDisplayState ModalMessageBox::getDisplayState() const {
    return displayState;
}

ModalMessageBox::eInputState ModalMessageBox::getInputState() const {
    return inputState;
}

bool ModalMessageBox::close() {
    // FUN_0099fdc0
    if ( inputState == eInputState::MMBIS_None ) {
        return false;
    }

    FlashPlayer *peVar1 = gpFlash->getFlashPlayer( "GENERAL" );
    peVar1->showFrame( "w_out", "/waiting", true );

    inputState = eInputState::MMBIS_Closing;
    displayTime = 0.0f;
    closeAfterDuration = 0.0f;

    return true;
}

bool ModalMessageBox::FUN_0099fbe0() {
    // FUN_0099fbe0
    // Force a currently-displayed box to start closing, guaranteeing it stays
    // up for at least the minimum on-screen time (3.3s).
    if ( inputState != eInputState::MMBIS_Displayed ) {
        return false;
    }

    if ( displayTime < 3.3f ) {
        closeAfterDuration = 3.3f;
    } else {
        closeAfterDuration = displayTime;
    }
    inputState = eInputState::MMBIS_CloseRequested;

    return true;
}

void ModalMessageBox::openTutorialPrompt() {
    // FUN_0099fc40
    displayState = eDisplayState::MMBDS_Opening;
    bHasCancel = true;
    promptType = MPT_Tutorial;
    messageHashcode = 0ull;
    messageDatabaseIndex = 0;
    pAnswerCallback = nullptr;
    pDefaultMessage = nullptr;
    pUserCallback = nullptr;
    pUserCallbackData = nullptr;
    userData = 0;
    answer = ePopupAnswer::PA_Unknown;
}

void ModalMessageBox::setPlayer( FlashPlayer *pPlayer ) {
    pFlashPlayer = pPlayer;
}

void ModalMessageBox::FUN_0099feb0() {
    // FUN_0099feb0
    inputState = eInputState::MMBIS_None;
    if ( pWaitingCallback != nullptr ) {
        pWaitingCallback( pWaitingCallbackData );
    }

    pWaitingCallback = nullptr;
    pWaitingCallbackData = nullptr;

    if ( bUnknownFlag2 != false ) {
        for ( int32_t i = 0; i < gpFlash->getNumMovies( 1 ); i++ ) {
            MoviePlayer *pMVar3 = gpFlash->getMoviePlayer( 1, i );
            if ( pMVar3 != nullptr ) {
                pMVar3->Flags = pMVar3->Flags & 0xffffffef;
            }
        }

        uint32_t uStack_8;
        uint32_t uStack_4;
        bool bVar1 =
            gpFlash->getFlashMovieIndex( &uStack_8, &uStack_4, "CONFIGPC" );
        if ( bVar1 ) {
            MoviePlayer *pMVar3 = gpFlash->getMoviePlayer( uStack_8, uStack_4 );
            if ( pMVar3 != nullptr ) {
                pMVar3->Flags = pMVar3->Flags & 0xffffffef;
            }
        }
    }
}

bool ModalMessageBox::FUN_00403100() {
    // FUN_00403100
    if ( ( numOpenMessageBox == 0 ) && ( bUnknownFlag == false ) ) {
        return false;
    }
    return true;
}

const char *ModalMessageBox::getFrameToDisplay( const float param_1 ) const {
    if ( param_1 <= 1.0 )
        return "waiting_1";
    else if ( param_1 <= 3.0 )
        return "waiting_3";
    else
        return "waiting_more";
}

void ModalMessageBox::updateInputState( const float param_1 ) {
    if ( param_1 <= 1.0 )
        inputState = eInputState::MMBIS_Displayed1Sec;
    else if ( param_1 <= 3.0 )
        inputState = eInputState::MMBIS_Displayed3Sec;
    else
        inputState = eInputState::MMBIS_Displayed;
}

void ModalMessageBox::FUN_0099fcc0() {
    // FUN_0099fcc0
    int32_t iVar1 = numOpenMessageBox + -1;
    numOpenMessageBox = iVar1;
    if ( iVar1 == 0 ) {
        bUnknownFlag = true;
        if ( promptType == MPT_Tutorial &&
             displayState == eDisplayState::MMBDS_Opened ) {
            displayState = eDisplayState::MMBDS_Closing;
            return;
        }
    } else if ( iVar1 < 0 ) {
        numOpenMessageBox = 0;
    }
}

void ModalMessageBox::FUN_009a0110() {
    // FUN_009a0110
    // FUN_016adf1c (inlined)
    int32_t iVar4 = numOpenMessageBox + 1;
    numOpenMessageBox = iVar4;

    GameMode *pGVar1 = gpActiveGameMode;
    if ( iVar4 == 1 ) {
        iVar4 = 0;
        bool bVar5 = gpActiveGameMode != nullptr;
        bUnknownFlag = false;
        if ( bVar5 ) {
            bVisible = pGVar1->isMessageBoxVisible();
            pGVar1->setfield7_0x1c( false );
            pGVar1->setsMessageBoxVisible( true );
        }

        for ( int32_t iVar4 = 0; iVar4 < gpFlash->getNumMovies( 1 ); iVar4++ ) {
            MoviePlayer *pMVar3 = gpFlash->getMoviePlayer( 1, iVar4 );
            if ( pMVar3 != nullptr ) {
                pMVar3->Flags = pMVar3->Flags | 0x10;
            }
        }

        uint32_t param_1 = 0;
        uint32_t param_2 = 0;
        bVar5 = gpFlash->getFlashMovieIndex( &param_1, &param_2, "CONFIGPC" );
        if ( bVar5 ) {
            MoviePlayer *pMVar3 = gpFlash->getMoviePlayer( param_1, param_2 );
            if ( pMVar3 != nullptr ) {
                pMVar3->Flags = pMVar3->Flags | 0x10;
            }
        }
        bMouseActive = gpFlash->getMouse()->field12_0x30;
        gpFlash->getMouse()->field12_0x30 = false;
        gpFlash->getMouse()->field13_0x31 = false;
    }

    if ( displayState == eDisplayState::MMBDS_Opening ) {
        displayState = eDisplayState::MMBDS_Opened;
    }
}

void ModalMessageBox::FUN_0099fc80() {
    // FUN_0099fc80
    answer = ePopupAnswer::PA_Cancel;
    if ( displayState == eDisplayState::MMBDS_Opened ) {
        displayState = eDisplayState::MMBDS_Closing;
    }
}

void ModalMessageBox::forceClose() {
    // FUN_0099fe10
    if ( gpActiveGameMode != nullptr ) {
        gpActiveGameMode->setfield7_0x1c( true );
        gpActiveGameMode->setsMessageBoxVisible( bVisible );
    }

    FlashPlayer *pPlayer = pFlashPlayer;
    displayState = eDisplayState::MMBDS_Closed;
    numOpenMessageBox = 0;
    bUnknownFlag = true;

    if ( pPlayer == nullptr ) {
        return;
    }

    if ( promptType == MPT_Tutorial ) {
        pPlayer->showFrame( "close", "/tutorial", false );
        return;
    }

    pPlayer->showFrame( "default", "/avert", false );
}

void TestDrive::DisplayModalPrompt( bool bHasCancel,
                                    uint64_t messageHashcode,
                                    int32_t messageDatabaseIndex,
                                    PopupAnswerCallback_t pAnswerCallback,
                                    PopupCallback *pUserCallback,
                                    void *pUserCallbackData,
                                    int32_t userData,
                                    const char *pDefaultMessage,
                                    bool bHasValidButton,
                                    bool bNoData,
                                    eModalPromptType promptType,
                                    ModalMessageBox *pMessageBox ) {
    // FUN_009a03e0
    if ( pMessageBox->displayState != ModalMessageBox::MMBDS_Closed ) {
        if ( pMessageBox->pAnswerCallback != nullptr ) {
            pMessageBox->pAnswerCallback( ePopupAnswer::PA_Unknown,
                                          pMessageBox->pUserCallbackData,
                                          pMessageBox->userData );
        }

        if ( pMessageBox->pUserCallback != nullptr ) {
            pMessageBox->pUserCallback->onClose( ePopupAnswer::PA_Unknown,
                                                 pMessageBox->pUserCallbackData,
                                                 pMessageBox->userData );
        }
    }

    if ( !bHasValidButton && !bHasCancel ) {
        bHasValidButton = true;
    }

    pMessageBox->messageHashcode = messageHashcode;
    pMessageBox->messageDatabaseIndex = messageDatabaseIndex;
    pMessageBox->pUserCallback = pUserCallback;
    pMessageBox->bHasCancel = bHasCancel;
    pMessageBox->pAnswerCallback = pAnswerCallback;
    pMessageBox->displayState = ModalMessageBox::MMBDS_Closed;
    pMessageBox->promptType = promptType;
    pMessageBox->pDefaultMessage = pDefaultMessage;
    pMessageBox->pUserCallbackData = pUserCallbackData;
    pMessageBox->userData = userData;
    pMessageBox->answer = ePopupAnswer::PA_Unknown;

    FlashPlayer *pPlayer = pMessageBox->pFlashPlayer;
    if ( pPlayer == nullptr ) {
        return;
    }

    FlashClip clip;
    const char *pClipPath =
        ( promptType == MPT_Tutorial ) ? "/tutorial" : "/avert";
    if ( !pPlayer->getClipPlayer( pClipPath, &clip ) ) {
        return;
    }

    if ( gpActiveGameMode != nullptr ) {
        gpActiveGameMode->setfield7_0x1c( false );
    }
    pMessageBox->displayState = ModalMessageBox::MMBDS_Opening;

    const char *pFlashVarName = nullptr;
    if ( pMessageBox->promptType == MPT_Warning ) {
        const char *pLabel = "on";
        if ( pMessageBox->bHasCancel ) {
            pLabel = bNoData ? "no_data" : "on_choice";
        }

        clip.goToLabelAndPlay( pLabel, true );
        pFlashVarName = "/:m_AvertText";
    } else if ( pMessageBox->promptType == MPT_Default ) {
        clip.goToLabelAndPlay( "msg", true );
        pFlashVarName = "/:m_MsgText";

        pPlayer->setVariableValue( "/:IsCancel",
                                   int32_t( pMessageBox->bHasCancel ) );
        pPlayer->setVariableValue( "/:NoValidButton",
                                   int32_t( !bHasValidButton ) );
    } else {
        const char *pLabel = "on";
        if ( userData == 1 ) {
            pLabel = "on_group";
        } else if ( userData == 2 ) {
            pLabel = "on_group_moto";
        }

        clip.goToLabelAndPlay( pLabel, true );
        pFlashVarName = "/:TutorialText";

        pPlayer->setVariableValueNonLinked( "/:WhatButton", "empty" );
        pPlayer->setVariableValueNonLinked( "/:TutorialTitle", "" );
    }

    if ( pMessageBox->promptType == MPT_Tutorial ) {
        pPlayer->setVariableValue( pFlashVarName, pDefaultMessage );
        return;
    }

    if ( pMessageBox->messageHashcode == 0ull ) {
        pPlayer->setVariableValueNonLinked(
            pFlashVarName,
            pDefaultMessage != nullptr ? pDefaultMessage : "#ERR: no message" );
        return;
    }

    if ( gpMngFlashLocalize != nullptr ) {
        gpMngFlashLocalize->setDatabaseContext(
            pMessageBox->messageDatabaseIndex );
        gpMngFlashLocalize->setVarLocalized( pPlayer, pFlashVarName,
                                             pMessageBox->messageHashcode );
        gpMngFlashLocalize->setDatabaseContext( 0 ); // BDD_Menus
        return;
    }

    const char *pValue = gpDatabase->getStringByHashcode(
        pMessageBox->messageDatabaseIndex, pMessageBox->messageHashcode );
    if ( pValue != nullptr ) {
        pPlayer->setVariableValue( pFlashVarName, pValue );
        return;
    }

    if ( pDefaultMessage != nullptr ) {
        pPlayer->setVariableValueNonLinked( pFlashVarName, pDefaultMessage );
        return;
    }

    pPlayer->setVariableValueNonLinked( pFlashVarName,
                                        "#ERR: no localized message" );
}
