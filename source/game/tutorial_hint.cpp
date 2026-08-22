#include "shared.h"
#include "tutorial_hint.h"

static constexpr float kTriggerStartTimer = 100.0f; // DAT_00f41708

TutorialHint::TutorialHint( const uint32_t id )
    : ID( id ),
      DBLine( -1 ),
      CurrentTimer( 0.0f ),
      TimerCondition( kDefaultTimerCondition ),
      TimerOnScreen( kDefaultTimerOnScreen ),
      bSeen( false ),
      bOnScreen( false ),
      bCancel( false ) {
    // FUN_004058b0 / FUN_004058f0 / FUN_00405930 / FUN_00405970 /
    // FUN_004059b0 / FUN_004059f0 / FUN_00405a30
}

TutorialHint::~TutorialHint() {}

void TutorialHint::cancel() {
    OTDU_UNIMPLEMENTED_ONCE; // TODO: TUTORIAL 
}

TutorialHintMap::TutorialHintMap() : TutorialHint( TMID_Map ), bField_0x1c( false ) {
    // FUN_00405500
}

TutorialHintTrigger::TutorialHintTrigger( const uint32_t id,
                                const uint8_t kind,
                                const uint8_t subKind,
                                const uint32_t extraChallengeID )
    : TutorialHint( id ),
      Field_0x1c( 0 ),
      Field_0x20( 0 ),
      Field_0x24( 0 ),
      Field_0x28( 0 ),
      Kind( kind ),
      SubKind( subKind ),
      ExtraChallengeID( extraChallengeID ),
      bExtraChallenge( extraChallengeID != 0u ),
      bField_0x35( false ) {
    // LAB_00981ddd + LAB_00981e1c
    CurrentTimer = kTriggerStartTimer;
}

uint32_t GetTutorialHintID( const char *pTag ) {
    // FUN_00982080
    if ( pTag == nullptr ) {
        return 0u;
    }

    uint32_t id = 0u;
    for ( int32_t i = 0; i < 4; i++ ) {
        const uint8_t character = uint8_t( pTag[i] );
        id = ( id << 8 ) | character;
        if ( character == 0u ) {
            id <<= ( 8 * ( 3 - i ) );
            break;
        }
    }

    return id;
}

TutorialHint *CreateTutorialHint( const uint32_t id ) {
    // FUN_00981840
    switch ( id ) {
    case TMID_Back:
    case TMID_BackView:
    case TMID_CameraView:
    case TMID_DPad:
    case TMID_Gearbox:
    case TMID_InstantChallenge:
    case TMID_LookAround:
    case TMID_Star:
        return new TutorialHint( id );

    case TMID_Map:
        return new TutorialHintMap();

    case TMID_Dealer:
        return new TutorialHintTrigger( id, 1u, TutorialHintTrigger::kNoKind );
    case TMID_DealerBike:
        return new TutorialHintTrigger( id, 2u, TutorialHintTrigger::kNoKind );
    case TMID_Clothes:
        return new TutorialHintTrigger( id, 3u, TutorialHintTrigger::kNoKind );
    case TMID_AfterMarket:
        return new TutorialHintTrigger( id, 4u, TutorialHintTrigger::kNoKind );
    case TMID_Club:
        return new TutorialHintTrigger( id, 5u, TutorialHintTrigger::kNoKind );
    case TMID_DrivingSchool:
        return new TutorialHintTrigger( id, 6u, TutorialHintTrigger::kNoKind );

    case TMID_TimeSolo:
        return new TutorialHintTrigger( id, 7u, 0x00u );
    case TMID_SpeedSolo:
        return new TutorialHintTrigger( id, 7u, 0x10u );
    case TMID_SpeedMulti:
        return new TutorialHintTrigger( id, 7u, 0x11u );
    case TMID_RaceSolo:
        return new TutorialHintTrigger( id, 7u, 0x30u );
    case TMID_RaceMulti:
        return new TutorialHintTrigger( id, 7u, 0x31u );

    case TMID_PeiSmall:
        return new TutorialHintTrigger( id, 8u, 1u );
    case TMID_Rental:
        return new TutorialHintTrigger( id, 8u, 2u );
    case TMID_Estate:
        return new TutorialHintTrigger( id, 8u, 4u );
    case TMID_PeiLarge:
        return new TutorialHintTrigger( id, 8u, 9u );

    case TMID_Shopping:
        return new TutorialHintTrigger( id, TutorialHintTrigger::kNoKind,
                                   TutorialHintTrigger::kNoKind, 0x53484f47u );
    case TMID_HitchHiker:
        return new TutorialHintTrigger( id, TutorialHintTrigger::kNoKind,
                                   TutorialHintTrigger::kNoKind, 0x48494849u );
    case TMID_Transport:
        return new TutorialHintTrigger( id, TutorialHintTrigger::kNoKind,
                                   TutorialHintTrigger::kNoKind, 0x5452414eu );
    case TMID_Convoy:
        return new TutorialHintTrigger( id, TutorialHintTrigger::kNoKind,
                                   TutorialHintTrigger::kNoKind, 0x43564f59u );

    default:
        break;
    }

    return nullptr;
}
