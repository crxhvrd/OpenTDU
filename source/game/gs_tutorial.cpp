#include "shared.h"
#include "gs_tutorial.h"

#include "core/ed_store.h"
#include "database/gs_database.h"
#include "tutorial_hint.h"

GSTutorial *gpTutorial = nullptr;
bool gbTutorialEnabled = true;         // DAT_00f77da8
bool gbScenarioTutorialEnabled = true; // DAT_00f8b208

GSTutorial::GSTutorial()
    : GameSystem(), bEnabled( false ), bPopupDisplayed( false ) {
    gpTutorial = this;
}

GSTutorial::~GSTutorial() { terminate(); }

bool GSTutorial::initialize( TestDriveGameInstance * ) {
    // FUN_00982080
    terminate();

    if ( gpDatabase == nullptr ) {
        return true;
    }

    const GameDatabase &gameDB = gpDatabase->getGameDB();
    const uint32_t numEntries = gameDB.pNumItems[kTutorialDatabaseIndex];
    if ( gameDB.DBTutorial != nullptr ) {
        for ( uint32_t i = 0u; i < numEntries; i++ ) {
            const DBEntryTutorial &entry = gameDB.DBTutorial[i];

            TutorialHint *pMessage = CreateTutorialHint(
                GetTutorialHintID( entry.TextID.pAsString ) );
            if ( pMessage == nullptr ) {
                continue;
            }

            pMessage->TimerCondition =
                ( entry.TimeCent > 0 )
                    ? float( entry.TimeCent ) * 0.01f // DAT_00f416d4
                    : 60.0f;                          // FLOAT_00f41744
            pMessage->DBLine = int32_t( i );

            if ( numTutorialMsgs >= kMaxTutorialHints ) {
                OTDU_LOG_WARN( "More than %d tutorial message(s) in the "
                               "database; the rest is dropped\n",
                               kMaxTutorialHints );
                delete pMessage;
                break;
            }

            pTutorialHints[numTutorialMsgs] = pMessage;
            numTutorialMsgs++;
        }
    }

    OTDU_LOG_DEBUG( "Built %d tutorial message(s) out of %u database row(s)\n",
                    numTutorialMsgs, numEntries );

    OTDU_UNIMPLEMENTED_ONCE; // TODO: TUTORIAL

    return true;
}

void GSTutorial::terminate() {
    for ( int32_t i = 0; i < numTutorialMsgs; i++ ) {
        delete pTutorialHints[i];
        pTutorialHints[i] = nullptr;
    }

    numTutorialMsgs = 0;
}

void GSTutorial::resetProgress() {
    // FUN_009810e0
    for ( int32_t i = 0; i < numTutorialMsgs; i++ ) {
        TutorialHint *pMessage = pTutorialHints[i];
        if ( pMessage != nullptr ) {
            pMessage->bCancel = false;
            pMessage->bSeen = false;
            pMessage->CurrentTimer = 0.0f;
        }
    }

    memset( bStepDone, 0, sizeof( bStepDone ) );
}

void GSTutorial::cancelDisplayedMessages() {
    // FUN_00981140
    for ( int32_t i = 0; i < numTutorialMsgs; i++ ) {
        if ( pTutorialHints[i] != nullptr ) {
            pTutorialHints[i]->cancel();
        }
    }
}

TutorialHint *GSTutorial::findMessage( const uint32_t id ) {
    for ( int32_t i = 0; i < numTutorialMsgs; i++ ) {
        TutorialHint *pMessage = pTutorialHints[i];
        if ( pMessage != nullptr && pMessage->ID == id ) {
            return pMessage;
        }
    }

    return nullptr;
}

void GSTutorial::deserialize( edStore &stream ) {
    // FUN_00981630
    for ( int32_t i = 0; i < kNumTutorialSteps; i++ ) {
        const uint32_t id = uint32_t( stream.readInteger() );

        TutorialHint *pMessage = findMessage( id );
        if ( pMessage == nullptr ) {
            stream.readInteger();
            stream.readInteger();
            continue;
        }

        pMessage->bCancel = stream.readInteger() != 0;
        pMessage->bSeen = stream.readInteger() != 0;
    }

    for ( bool &bDone : bStepDone ) {
        bDone = stream.readByte() != 0u;
    }
}

void GSTutorial::serialize( edStore &stream ) const {
    // FUN_009815b0
    for ( int32_t i = 0; i < kMaxTutorialHints; i++ ) {
        const TutorialHint *pMessage = pTutorialHints[i];
        if ( pMessage == nullptr ) {
            stream.writeInteger( 0 );
            stream.writeInteger( 0 );
            stream.writeInteger( 0 );
            continue;
        }

        stream.writeInteger( int32_t( pMessage->ID ) );
        stream.writeInteger( pMessage->bCancel ? 1 : 0 );
        stream.writeInteger( pMessage->bSeen ? 1 : 0 );
    }

    for ( const bool bDone : bStepDone ) {
        stream.writeByte( bDone ? 1u : 0u );
    }
}
