#include "shared.h"
#include "tutorial_hint.h"

#include "gs_timer.h"
#include "config/gs_config.h"
#include "game/gm_world.h"
#include "game/mng_ingame_menu.h"
#include "game/scenaric/entity/base_entity.h"
#include "game/scenaric/entity/entity_factory.h"
#include "game/scenaric/game_intro_tutorial.h"
#include "game/scenaric/gs_scenaric.h"
#include "game/scenaric/spots.h"
#include "game/vehicle/car_state.h"
#include "input/input_device.h"
#include "player_data/gs_playerdata.h"
#include "render/camera/camera_vehicle.h"
#include "render/camera/gs_camera.h"

static constexpr float kTriggerStartTimer = 100.0f; // DAT_00f41708
static constexpr uint32_t kLookAroundCameraFlags = 0xd0000u;
static constexpr uint32_t kPadConfigNoBackView = 4u;

static bool IsLookAroundInputActive() {
    // FUN_00405410 (inlined)
    PlayerConfiguration *pPlayerConfig = GetPlayerConfig();
    if ( pPlayerConfig == nullptr ) {
        return false;
    }

    const float deadZone = gpCamera->getDeadZone();
    float value = deadZone;
    DeviceAction *pHeadActions[4] = {
        pPlayerConfig->pCarHeadLeft, pPlayerConfig->pCarHeadRight,
        pPlayerConfig->pCarHeadUp, pPlayerConfig->pCarHeadDown };
    for ( DeviceAction *pAction : pHeadActions ) {
        if ( pAction == nullptr ) {
            continue;
        }

        value = pAction->FUN_00434e40( value );
        if ( deadZone < value ) {
            return true;
        }
    }

    return false;
}

TutorialHint::TutorialHint( const uint32_t id )
    : ID( id ),
      DBLine( -1 ),
      CurrentTimer( 0.0f ),
      TimerCondition( kDefaultTimerCondition ),
      TimerOnScreen( kDefaultTimerOnScreen ),
      bSeen( false ),
      bOnScreen( false ),
      bCancel( false ) {
    // FUN_00981280
}

TutorialHint::~TutorialHint() {}

TutorialHintBack::TutorialHintBack() : TutorialHint( TMID_Back ) {
    // FUN_004058b0
}

bool TutorialHintBack::isConditionMet() {
    // FUN_00405380
    return gpScenaric->getLocalPlayer()
               .getCarState()
               ->getBody()
               ->NumWheelsOnRoad == 0;
}

bool TutorialHintBack::canRearm() {
    // FUN_004053a0
    return !isConditionMet();
}

TutorialHintLookAround::TutorialHintLookAround()
    : TutorialHint( TMID_LookAround ) {
    // FUN_004058f0
}

bool TutorialHintLookAround::isConditionMet() {
    // FUN_004053f0
    return ( gpCamera->getActiveVehicleCamera()->getFlags() &
             kLookAroundCameraFlags ) == 0;
}

bool TutorialHintLookAround::isCompleted() {
    // FUN_00405410
    return IsLookAroundInputActive();
}

TutorialHintDPad::TutorialHintDPad() : TutorialHint( TMID_DPad ) {
    // FUN_00405930
}

bool TutorialHintDPad::isCompleted() {
    // FUN_004053c0
    return gpMngInGameMenu != nullptr &&
           gpMngInGameMenu->getState() == IGMS_Open;
}

TutorialHintBackView::TutorialHintBackView() : TutorialHint( TMID_BackView ) {
    // FUN_00405970
}

bool TutorialHintBackView::isConditionMet() {
    // FUN_004054a0
    return gpPlayerData->getLocalPlayerData().PadConfig != kPadConfigNoBackView;
}

bool TutorialHintBackView::isCompleted() {
    // FUN_004054b0
    PlayerConfiguration *pPlayerConfig = GetPlayerConfig();
    return pPlayerConfig != nullptr &&
           IsActionDown( pPlayerConfig->pCarBackview );
}

TutorialHintGearbox::TutorialHintGearbox() : TutorialHint( TMID_Gearbox ) {
    // FUN_004059b0
}

bool TutorialHintGearbox::isConditionMet() {
    // FUN_00405870
    return !gpPlayerData->getLocalPlayerData().bAuto;
}

bool TutorialHintGearbox::isCompleted() {
    // FUN_00405880
    if ( gpPlayerData->getLocalPlayerData().bAuto ) {
        return false;
    }

    PlayerConfiguration *pPlayerConfig = GetPlayerConfig();
    if ( pPlayerConfig == nullptr ) {
        return false;
    }

    return IsActionDown( pPlayerConfig->pCarGearUp ) ||
           IsActionDown( pPlayerConfig->pCarGearDown );
}

TutorialHintCameraView::TutorialHintCameraView()
    : TutorialHint( TMID_CameraView ) {
    // FUN_004059f0
}

bool TutorialHintCameraView::isCompleted() {
    // FUN_004054e0
    PlayerConfiguration *pPlayerConfig = GetPlayerConfig();
    return pPlayerConfig != nullptr &&
           IsActionPressed( pPlayerConfig->pCarCamera );
}

TutorialHintStar::TutorialHintStar() : TutorialHint( TMID_Star ) {
    // FUN_00405a30
}

bool TutorialHintStar::isConditionMet() {
    // FUN_00405710
    return gpScenaric->getIntroTutorial().getStep() != kTutorialStateFirstDrive;
}

bool TutorialHintStar::isCompleted() {
    // FUN_00405720
    return gpActiveGM->isPaused();
}

TutorialHintInstantChallenge::TutorialHintInstantChallenge()
    : TutorialHint( TMID_InstantChallenge ) {
    // FUN_00981840 (inlined)
}

bool TutorialHintInstantChallenge::isConditionMet() {
    // FUN_004055c0
    OTDU_UNIMPLEMENTED_ONCE; // TODO: ONLINE (nearest P2P player in range)
    return false;
}

TutorialHintMap::TutorialHintMap()
    : TutorialHint( TMID_Map ), bMapRequested( false ) {
    // FUN_00405500
}

bool TutorialHintMap::isConditionMet() {
    // FUN_00405550
    return !gpScenaric->getIntroTutorial().isActive();
}

bool TutorialHintMap::isCompleted() {
    // FUN_00405590
    return !gpScenaric->getIntroTutorial().isActive() && bMapRequested;
}

void TutorialHintMap::onMapRequested() {
    // FUN_00405570
    if ( !gpScenaric->getIntroTutorial().isActive() ) {
        bMapRequested = true;
    }
}

TutorialHintTrigger::TutorialHintTrigger( const uint32_t id,
                                          const uint8_t kind,
                                          const uint8_t subKind,
                                          const uint32_t extraChallengeID )
    : TutorialHint( id ),
      pLaunchedSpot( nullptr ),
      pNearestSpot( nullptr ),
      pLaunchedExtraChallenge( nullptr ),
      pNearestExtraChallenge( nullptr ),
      Kind( kind ),
      SubKind( subKind ),
      ExtraChallengeID( extraChallengeID ),
      bExtraChallenge( extraChallengeID != 0u ),
      bRearming( false ),
      RearmTimer( 0.0f ) {
    // LAB_00981ddd + LAB_00981e1c
    CurrentTimer = kTriggerStartTimer;
}

bool TutorialHintTrigger::isConditionMet() {
    // FUN_00405750
    TimerCondition = kTimerCondition;
    if ( gpScenaric->getLocalPlayer().getCarState() == nullptr ) {
        return false;
    }

    if ( bExtraChallenge ) {
        if ( pNearestExtraChallenge == nullptr ) {
            return false;
        }

        const EntityDesc *pDesc = pNearestExtraChallenge->getDesc();
        const uint32_t challengeID = uint32_t(
            ( pDesc != nullptr ) ? pDesc->Type : eEntityType::ET__ENT );
        return challengeID == ExtraChallengeID;
    }

    if ( pNearestSpot == nullptr || pNearestSpot->Type != Kind ) {
        return false;
    }

    if ( pNearestSpot->SubType != SubKind && SubKind != kNoKind ) {
        return false;
    }

    return IsSpotAvailableInCurrentMode( pNearestSpot );
}

bool TutorialHintTrigger::canRearm() {
    // FUN_00405800
    if ( !bRearming ) {
        return false;
    }

    RearmTimer += gpTimer->GameDeltaTime;
    if ( RearmTimer <= kRearmDelay ) {
        return false;
    }

    bRearming = false;
    return true;
}

void TutorialHintTrigger::onSpotLaunched( SpotMap *pSpot ) {
    // FUN_004057c0
    if ( !bExtraChallenge ) {
        pLaunchedSpot = pSpot;
    }
}

void TutorialHintTrigger::onDisplayed() {
    // FUN_00405830
    bRearming = true;
    RearmTimer = 0.0f;
}

void TutorialHintTrigger::update( SpotMap *pSpot, Entity *pExtraChallenge ) {
    // FUN_00405840
    pNearestSpot = pSpot;
    pNearestExtraChallenge = pExtraChallenge;
}

void TutorialHintTrigger::setLaunchedExtraChallenge( Entity *pExtraChallenge ) {
    // FUN_004057e0
    if ( bExtraChallenge ) {
        pLaunchedExtraChallenge = pExtraChallenge;
    }
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
        return new TutorialHintBack();
    case TMID_BackView:
        return new TutorialHintBackView();
    case TMID_CameraView:
        return new TutorialHintCameraView();
    case TMID_DPad:
        return new TutorialHintDPad();
    case TMID_Gearbox:
        return new TutorialHintGearbox();
    case TMID_InstantChallenge:
        return new TutorialHintInstantChallenge();
    case TMID_LookAround:
        return new TutorialHintLookAround();
    case TMID_Star:
        return new TutorialHintStar();

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
                                        TutorialHintTrigger::kNoKind,
                                        uint32_t( eEntityType::ET_SHOG ) );
    case TMID_HitchHiker:
        return new TutorialHintTrigger( id, TutorialHintTrigger::kNoKind,
                                        TutorialHintTrigger::kNoKind,
                                        uint32_t( eEntityType::ET_HIHI ) );
    case TMID_Transport:
        return new TutorialHintTrigger( id, TutorialHintTrigger::kNoKind,
                                        TutorialHintTrigger::kNoKind,
                                        uint32_t( eEntityType::ET_TRAN ) );
    case TMID_Convoy:
        return new TutorialHintTrigger( id, TutorialHintTrigger::kNoKind,
                                        TutorialHintTrigger::kNoKind,
                                        uint32_t( eEntityType::ET_CVOY ) );

    default:
        break;
    }

    return nullptr;
}
