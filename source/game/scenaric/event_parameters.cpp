#include "shared.h"
#include "event_parameters.h"

#include <ctime>

#include "database/gs_database.h"
#include "flash/implementation/flash_player.h"
#include "game/game_cheats.h"
#include "player_data/achievements_data.h"
#include "player_data/driver.h"
#include "player_data/gs_playerdata.h"
#include "player_data/player_data_interface.h"
#include "player_data/saved_garage.h"
#include "spots.h"

static uint32_t gCategoryGameTypeMasks[4][4]; // DAT_01697b60

static constexpr uint32_t kDefaultDescriptionHashcode = 0x8a9087;
static constexpr uint64_t kModelRestrictionDefaultHashcode = 0x3bd70ba;
static constexpr int32_t kPlayerLevelNovice = 1;

// DAT_00f37494
static constexpr char kDefaultFileName[9] = "UUFF0000";

EventParameters::EventParameters()
    : source( ES_None ),
      pRequestedName{},
      pFileName{},
      field124( 0u ),
      parameterList(),
      version(),
      gameType(),
      spotType(),
      test(),
      vehicleType(),
      vehicleRestriction(),
      level(),
      drivingAid(),
      countdownMode(),
      offRoad(),
      collisions(),
      traffic(),
      cops(),
      startChallenge(),
      gps(),
      noCollision(),
      position(),
      loop(),
      loopCheckpointIndex(),
      name(),
      description(),
      databaseName(),
      databaseDescription(),
      numTeams(),
      numLaps(),
      trafficDensity(),
      goldTime(),
      silverTime(),
      bronzeTime(),
      countdown(),
      checkpointBonus(),
      copsDensity(),
      cockpitView(),
      penaltyCredits(),
      goldSpeed(),
      silverSpeed(),
      bronzeSpeed(),
      trapSpeed(),
      fee(),
      rewardMoney(),
      forc(),
      numOpponents(),
      opponentVehicleType(),
      opponentRestriction(),
      nextChallenge() {
    // FUN_00738d70
    vehicleRestriction.setFixedChildIDs( true );

    GameParameterList &list = parameterList;
    list.add( &version, 0, 0, 0, kDefaultDescriptionHashcode, 0, 0, 0xffff );
    list.add( &gameType, MakeParameterID( "gmth" ), 0x202, 0,
              kDefaultDescriptionHashcode, 0, 0, 0xffff );
    list.add( &spotType, MakeParameterID( "gmpm" ), 0x202, 0,
              kDefaultDescriptionHashcode, 0, 0, 0xffff );
    list.add( &test, MakeParameterID( "TEST" ), 0x216, 0,
              kDefaultDescriptionHashcode, 0, 0, 0xffff );
    list.add( &countdownMode, MakeParameterID( "ChCD" ), 0x204, 1, 0x5552f5, 0,
              2, 0xffff );
    list.add( &offRoad, MakeParameterID( "offR" ), 0x204, 1, 0x538c3b, 0, 4,
              0xffff );
    list.add( &collisions, MakeParameterID( "CCol" ), 0x207, 0, 0x53f365, 0, 0,
              0xffff );
    list.add( &traffic, MakeParameterID( "ChTR" ), 0x204, 1, 0x56d995, 0, 8,
              0xffff );
    list.add( &cops, MakeParameterID( "ChCO" ), 0x204, 1, 0x586035, 0, 16,
              0xffff );
    list.add( &forc, MakeParameterID( "forc" ), 0x203, 1, 0x59e6d5, 0, 32,
              0xffff );
    list.add( &startChallenge, MakeParameterID( "ChST" ), 0x205, 1, 0x5b6d75, 0,
              128, 0xffff );
    list.add( &gps, MakeParameterID( "ChGP" ), 0x210, 1, 0x5d1b25, 0, 512,
              0xffff );
    list.add( &noCollision, MakeParameterID( "ChNC" ), 0x210, 1, 0x5ea1c5, 0,
              1024, 4 );
    list.add( &goldSpeed, MakeParameterID( "SP2R" ), 0x205, 3, 0x5e138b, 0,
              32768, 0xffff );
    list.add( &silverSpeed, MakeParameterID( "SPSi" ), 0x208, 3, 0x557a05, 0,
              65536, 2 );
    list.add( &bronzeSpeed, MakeParameterID( "SPBr" ), 0x208, 3, 0x5700a5, 0,
              131072, 2 );
    list.add( &goldTime, MakeParameterID( "timG" ), 0x202, 3, 0x597fab, 0, 2048,
              0xffff );
    list.add( &silverTime, MakeParameterID( "timS" ), 0x202, 3, 0x630dc2b, 0,
              4096, 0xffff );
    list.add( &bronzeTime, MakeParameterID( "timB" ), 0x202, 3, 0x6401e6b, 0,
              8192, 0xffff );
    list.add( &countdown, MakeParameterID( "CtDw" ), 0x203, 3, 0x53cc55, 0, 0,
              0xffff );
    list.add( &checkpointBonus, MakeParameterID( "ChBo" ), 0x208, 3, 0x588745,
              0, 64, 0xffff );
    list.add( &nextChallenge, MakeParameterID( "next" ), 0x208, 3, 0x5a0de5, 0,
              4, 0xffff );
    list.add( &penaltyCredits, MakeParameterID( "PeCr" ), 0x207, 3, 0x5e53a5, 0,
              0, 0xffff );
    list.add( &trafficDensity, MakeParameterID( "trat" ), 0x202, 3, 0x57f90b, 0,
              0, 0xffff );
    list.add( &numLaps, MakeParameterID( "nblp" ), 0x202, 3, 0x5998b5, 0, 0,
              0xffff );
    list.add( &trapSpeed, MakeParameterID( "tras" ), 0x203, 3, 0x32f3692, 0,
              512, 0xffff );
    list.add( &copsDensity, MakeParameterID( "cops" ), 0x202, 3, 0x53652b, 0, 0,
              0xffff );
    list.add( &numOpponents, MakeParameterID( "NbIA" ), 0x207, 3, 0x526cc5, 0,
              32, 2 );
    list.add( &opponentVehicleType, MakeParameterID( "IAVT" ), 0x215, 3,
              0x55a115, 0, 32, 2 );
    list.add( &opponentRestriction, MakeParameterID( "IAS0" ), 0x215, 3,
              0x5727b5, 0, 32, 2 );
    list.add( &drivingAid, MakeParameterID( "DrAi" ), 0x210, 3, 0x36031a2, 0, 1,
              0xffff );
    list.add( &cockpitView, MakeParameterID( "Cock" ), 0x210, 3, 0x602865, 0,
              16, 0xffff );
    list.add( &level, MakeParameterID( "Plvl" ), 0x209, 3, 0x5b9485, 0, 1,
              0xffff );
    list.add( &vehicleType, MakeParameterID( "VTyp" ), 0x214, 3, 0x5e2c95, 0, 8,
              0xffff );
    list.add( &vehicleRestriction, MakeParameterID( "VSpe" ), 0x206, 3,
              0x56b285, 0, 2, 0xffff );
    list.add( &fee, MakeParameterID( "fee1" ), 0x203, 4, 0x53431b, 0, 4,
              0xffff );
    list.add( &rewardMoney, MakeParameterID( "rewm" ), 0x207, 4, 0x381ee5a, 0,
              8, 2 );
    list.add( &numTeams, MakeParameterID( "nbtm" ), 0x202, 0,
              kDefaultDescriptionHashcode, 0, 0, 0xffff );
    list.add( &position, 0x22, 0x202, 0, kDefaultDescriptionHashcode, 0, 0,
              0xffff );
    list.add( &loop, 0x23, 0x202, 0, kDefaultDescriptionHashcode, 0, 0,
              0xffff );
    list.add( &name, MakeParameterID( "mnme" ), 0x202, 0,
              kDefaultDescriptionHashcode, 0, 0, 0xffff );
    list.add( &description, MakeParameterID( "DesC" ), 0x217, 0,
              kDefaultDescriptionHashcode, 0, 0, 0xffff );
    list.add( &loopCheckpointIndex, MakeParameterID( "lcpi" ), 0x202, 0,
              kDefaultDescriptionHashcode, 0, 0, 0xffff );
    list.add( &databaseName, MakeParameterID( "dbna" ), 0x211, 0,
              kDefaultDescriptionHashcode, 0, 0, 0xffff );
    list.add( &databaseDescription, MakeParameterID( "dbd1" ), 0x211, 0,
              kDefaultDescriptionHashcode, 0, 0, 0xffff );

    countdown.linkParent( &countdownMode );
    checkpointBonus.linkParent( &countdownMode );
    vehicleRestriction.linkParent( &vehicleType );
    opponentRestriction.linkParent( &opponentVehicleType );
    penaltyCredits.linkParent( &offRoad );
    trafficDensity.linkParent( &traffic );
    copsDensity.linkParent( &cops );
    numLaps.linkParent( &loop );
    silverTime.linkParent( &goldTime );
    bronzeTime.linkParent( &silverTime );
    silverSpeed.linkParent( &goldSpeed );
    silverSpeed.setTracksParent( true );
    bronzeSpeed.linkParent( &silverSpeed );
    bronzeSpeed.setTracksParent( true );

    static constexpr uint32_t kTimeAttackMasks[4] = { 0x37f, 0, 0x385f, 0xc };
    static constexpr uint32_t kSpeedMasks[4] = { 0x59f, 0, 0x3821b, 0xc };
    static constexpr uint32_t kRaceMasks[4] = { 0x77d, 0, 0x7b, 0xc };
    memcpy( gCategoryGameTypeMasks[EGT_TimeAttack], kTimeAttackMasks,
            sizeof( kTimeAttackMasks ) );
    memcpy( gCategoryGameTypeMasks[EGT_Speed], kSpeedMasks,
            sizeof( kSpeedMasks ) );
    memcpy( gCategoryGameTypeMasks[EGT_Driving], kTimeAttackMasks,
            sizeof( kTimeAttackMasks ) );
    memcpy( gCategoryGameTypeMasks[EGT_Race], kRaceMasks,
            sizeof( kRaceMasks ) );

    fee.setMoney( true );
    rewardMoney.setMoney( true );
    reset();

    memcpy( pFileName, kDefaultFileName, sizeof( kDefaultFileName ) );
}

void EventParameters::generateFileName() {
    // FUN_00738720
    switch ( getGameType() ) {
    case EGT_TimeAttack:
        pFileName[0] = 'T';
        break;
    case EGT_Speed:
        pFileName[0] = 'S';
        break;
    case EGT_Driving:
        pFileName[0] = 'D';
        break;
    case EGT_Race:
        pFileName[0] = 'R';
        break;
    default:
        pFileName[0] = 'U';
        break;
    }
    switch ( getSpotType() ) {
    case EST_Solo:
        pFileName[1] = 'S';
        break;
    case EST_Multi:
        pFileName[1] = 'M';
        break;
    case 0xc0c2:
        pFileName[1] = 'C';
        break;
    default:
        pFileName[1] = 'U';
        break;
    }
    pFileName[2] = '\0';

    // GetLocalTime
    const std::time_t now = std::time( nullptr );
    const std::tm *pLocalTime = std::localtime( &now );
    const uint32_t stamp =
        ( ( uint32_t( pLocalTime->tm_mday ) * 24 + pLocalTime->tm_hour ) * 60 +
          pLocalTime->tm_min ) *
            60 +
        pLocalTime->tm_sec;
    char prefix[3] = { pFileName[0], pFileName[1], '\0' };
    snprintf( pFileName, sizeof( pFileName ), "%s%06X", prefix, stamp );
    pFileName[8] = '\0';
}

const char *EventParameters::getDisplayName() {
    // FUN_00738850
    if ( int32_t( databaseName.getValue() ) < 0 ) {
        return name.getString();
    }
    return databaseName.getDisplayText();
}

const char *EventParameters::getDisplayDescription() {
    // FUN_00738820
    if ( int32_t( databaseDescription.getValue() ) < 0 ) {
        return description.getString();
    }
    return databaseDescription.getDisplayText();
}

void EventParameters::reset() {
    // FUN_00738880
    gameType.setValue( -1, false );
    spotType.setValue( 0xc0c3, false );
    version.setValue( 0x217, false );
    test.setValue( 0, false );
    penaltyCredits.setup( 10, 9990, 100, 10, true );
    goldSpeed.setRange( 3000, 45000, 13000 );
    silverSpeed.setRange( 3000, 45000, 12000 );
    bronzeSpeed.setRange( 3000, 45000, 11000 );
    numTeams.setRange( 1, 4, 1 );
    numLaps.setRange( 0, 99, 2 );
    vehicleType.setupOptions( 0, 2, 0, kGameParameterVehicleTypeOptions );
    vehicleRestriction.setupOptions( 0, 4, 0,
                                     kGameParameterRestrictionOptions );
    opponentVehicleType.setupOptions( 0, 2, 0,
                                      kGameParameterVehicleTypeOptions );
    opponentRestriction.setupOptions( 0, 4, 0,
                                      kGameParameterRestrictionOptions );
    level.setupOptions( 0, 6, 0, kGameParameterLevelOptions );
    drivingAid.setupOptions( 0, 3, 0, kGameParameterDrivingAidOptions );
    numOpponents.setRange( 1, 7, 3 );

    const int32_t numChallengeNames =
        ( gpDatabase != nullptr ) ? gpDatabase->NbChallengeNames : 0;
    const int32_t numChallengeDescs =
        ( gpDatabase != nullptr ) ? gpDatabase->NbChallengeDesc : 0;
    databaseName.setupDatabase( 1, numChallengeNames, -1, 7 );
    databaseDescription.setupDatabase( 1, numChallengeDescs, -1, 11 );
    goldTime.setup( 10000, 10800000, 160000, 5000, true );
    silverTime.setup( 15000, 10805000, 200000, 5000, true );
    bronzeTime.setup( 20000, 10810000, 240000, 5000, true );
    countdown.setup( 10000, 10800000, 240000, 5000, true );
    checkpointBonus.setup( 0, 10800000, 0, 5000, true );
    loopCheckpointIndex.setValue( 1, false );
    loopCheckpointIndex.setMinMax( 0, 0 );
    loop.setValue( 0, false );
    nextChallenge.setValue( 0, false );
    trafficDensity.setValue( 5, false );
    countdownMode.setValue( 0, true );
    offRoad.setValue( 0, true );
    collisions.setValue( 0, false );
    traffic.setValue( 0, true );
    cops.setValue( 0, true );
    forc.setValue( 0, false );
    startChallenge.setValue( 1, false );
    gps.setValue( 1, false );
    noCollision.setValue( 0, false );
    cockpitView.setValue( 0, false );
    fee.setup( 0, 1000, 50, 10, true );
    rewardMoney.setup( 0, gbGMEDesigner ? 1000000 : 200000, 20000, 1000,
                       false );
    strncpy( name.getStringBuffer(), "Mission Honolulu",
             GameParameterString::kMaxLength );
    strncpy( description.getStringBuffer(), "OAHU",
             GameParameterString::kMaxLength );
}

bool EventParameters::isParameterAvailable( GameParameter *pParameter,
                                            const int32_t parameterCategory ) {
    // FUN_00738cb0
    const int32_t categoryIndex = parameterCategory - 1;
    const int32_t gameTypeIndex = getGameType();
    const uint32_t gameTypeMask =
        ( categoryIndex < 4 )
            ? gCategoryGameTypeMasks[gameTypeIndex][categoryIndex]
            : 0u;

    if ( pParameter->getCategory() != parameterCategory ) {
        return false;
    }
    if ( pParameter->getID() == MakeParameterID( "ChBo" ) &&
         getGameType() == EGT_Speed ) {
        return false;
    }

    bool bAvailable = false;
    if ( ( pParameter->getGameTypeMask() & gameTypeMask ) != 0 ) {
        bAvailable = true;
        switch ( getSpotType() ) {
        case EST_Solo:
            bAvailable = ( ( pParameter->getField68() >> 1 ) & 1 ) != 0;
            break;
        case EST_Multi:
            bAvailable = ( ( pParameter->getField68() >> 2 ) & 1 ) != 0;
            break;
        case EST_Club:
            bAvailable = ( ( pParameter->getField68() >> 3 ) & 1 ) != 0;
            break;
        default:
            break;
        }
    }
    return pParameter->isEnabled() || bAvailable;
}

char *EventParameters::buildVehicleRestrictionText(
    char *pOutText, const uint32_t maxLength, const bool bShowCategories ) {
    // FUN_00738410
    const int32_t vehicleTypeIndex = int32_t( vehicleType.getValue() );
    const int32_t restriction = int32_t( vehicleRestriction.getValue() );
    const uint64_t brandHashcode =
        vehicleRestriction.getBrand().getBrandHashcode();
    const int32_t modelID = int32_t( vehicleRestriction.getModel().getValue() );
    const int32_t category =
        int32_t( vehicleRestriction.getCategory().getValue() );
    const int32_t categoryFrom =
        int32_t( vehicleRestriction.getCategoryFrom().getValue() );
    const int32_t categoryTo =
        int32_t( vehicleRestriction.getCategoryTo().getValue() );

    memset( pOutText, 0, maxLength );
    if ( vehicleTypeIndex != 0 && restriction == 0 ) {
        strncpy( pOutText,
                 gpDatabase->getStringByHashcode(
                     0, kGameParameterVehicleTypeOptions[vehicleTypeIndex] ),
                 maxLength - 1 );
        strncat( pOutText, " ", maxLength - 1 );
        return pOutText;
    }

    switch ( restriction ) {
    case 0:
        strncat( pOutText,
                 gpDatabase->getStringByHashcode(
                     0, kGameParameterRestrictionOptions[0] ),
                 maxLength - 1 );
        return pOutText;
    case 1: {
        const GameDatabase &database = gpDatabase->getGameDB();
        for ( uint32_t i = 0; i < database.pNumItems[6]; i++ ) {
            if ( database.DBBrand[i].Hashcode == brandHashcode ) {
                strncat( pOutText, database.DBBrand[i].BrandNameID.pAsString,
                         maxLength - 1 );
                return pOutText;
            }
        }
        return pOutText;
    }
    case 2: {
        if ( maxLength < 99 ) {
            return pOutText;
        }
        const CarConfig *pCarConfig = gpDatabase->getCarConfig( modelID );
        if ( modelID != 0 && pCarConfig != nullptr ) {
            strcpy( pOutText, pCarConfig->FullName.c_str() );
            return pOutText;
        }
        if ( modelID == 0 && pCarConfig != nullptr ) {
            pOutText[0] = '\0';
            return pOutText;
        }
        const char *pDefaultText = gpDatabase->getStringByHashcode(
            0, kModelRestrictionDefaultHashcode );
        if ( pDefaultText == nullptr ) {
            pOutText[0] = '\0';
            return pOutText;
        }
        strncpy( pOutText, pDefaultText, maxLength - 1 );
        return pOutText;
    }
    case 3: {
        if ( !bShowCategories ) {
            pOutText[0] = '\0';
            return pOutText;
        }
        const char *pCategoryName = gpDatabase->getStringByHashcode(
            1, kGameParameterCarCategoryOptions[category] );
        if ( pCategoryName != nullptr ) {
            strncat( pOutText, pCategoryName, maxLength - 1 );
        }
        return pOutText;
    }
    case 4: {
        if ( !bShowCategories ) {
            pOutText[0] = '\0';
            return pOutText;
        }
        const char *pFromName = gpDatabase->getStringByHashcode(
            1, kGameParameterCarCategoryOptions[categoryFrom] );
        const char *pToName = gpDatabase->getStringByHashcode(
            1, kGameParameterCarCategoryOptions[categoryTo] );
        if ( pFromName != nullptr && pToName != nullptr ) {
            strncat( pOutText, pFromName, maxLength - 1 );
            strncat( pOutText, " - ", maxLength - 1 );
            strncat( pOutText, pToName, maxLength - 1 );
        }
        return pOutText;
    }
    default:
        return pOutText;
    }
}

void EventParameters::setGroupRestrictionVars( FlashPlayer *pPlayer ) {
    // FUN_0073a5b0
    static constexpr const char *kCategoryLetters[7] = { "A", "B", "C", "D",
                                                         "E", "F", "G" };

    if ( pPlayer == nullptr ) {
        return;
    }

    const int32_t vehicleTypeIndex = int32_t( vehicleType.getValue() );
    const int32_t restriction = int32_t( vehicleRestriction.getValue() );
    const int32_t category =
        int32_t( vehicleRestriction.getCategory().getValue() );
    const int32_t categoryFrom =
        int32_t( vehicleRestriction.getCategoryFrom().getValue() );
    const int32_t categoryTo =
        int32_t( vehicleRestriction.getCategoryTo().getValue() );
    if ( restriction < 0 || restriction > 4 ) {
        return;
    }

    pPlayer->setVariableValueNonLinked( "/:GroupRestriction01", "empty" );
    pPlayer->setVariableValueNonLinked( "/:GroupRestriction02", "empty" );
    const bool bCars = vehicleTypeIndex == 0 || vehicleTypeIndex == 1;
    char groupName[8];
    if ( restriction == 3 ) {
        if ( bCars && category >= 0 && category <= 6 ) {
            pPlayer->setVariableValueNonLinked( "/:GroupRestriction01",
                                                kCategoryLetters[category] );
        }
        if ( vehicleTypeIndex == 2 ) {
            if ( category == 0 ) {
                pPlayer->setVariableValueNonLinked( "/:GroupRestriction01",
                                                    "mA" );
            } else if ( category == 1 ) {
                pPlayer->setVariableValueNonLinked( "/:GroupRestriction01",
                                                    "mB" );
            }
        }
    } else if ( restriction == 4 ) {
        if ( bCars ) {
            snprintf( groupName, sizeof( groupName ), "%c", 'A' + categoryTo );
            pPlayer->setVariableValueNonLinked( "/:GroupRestriction01",
                                                groupName );
            snprintf( groupName, sizeof( groupName ), "%c",
                      'A' + categoryFrom );
            pPlayer->setVariableValueNonLinked( "/:GroupRestriction02",
                                                groupName );
        }
        if ( vehicleTypeIndex == 2 && categoryTo <= 1 && categoryFrom <= 1 ) {
            snprintf( groupName, sizeof( groupName ), "m%c", 'A' + categoryTo );
            pPlayer->setVariableValueNonLinked( "/:GroupRestriction01",
                                                groupName );
            snprintf( groupName, sizeof( groupName ), "m%c",
                      'A' + categoryFrom );
            pPlayer->setVariableValueNonLinked( "/:GroupRestriction02",
                                                groupName );
        }
    }
}

void EventParameters::checkRequirements( bool *pOutLevelTooLow,
                                         bool *pOutCurrentCarOK,
                                         bool *pOutOtherCarOK ) {
    // FUN_0073a3c0
    *pOutOtherCarOK = false;
    *pOutCurrentCarOK = false;

    IPlayerData *pSave = gpPlayerData->getLoadedSave();
    const int32_t playerLevel = pSave->getAchievements()->getPlayerLevel();
    const int32_t requiredLevel = int32_t( level.getValue() );
    *pOutLevelTooLow = ( playerLevel + kPlayerLevelNovice ) < requiredLevel &&
                       requiredLevel != 0;

    const int32_t vehicleTypeIndex = int32_t( vehicleType.getValue() );
    const int32_t restriction = int32_t( vehicleRestriction.getValue() );
    const uint64_t brandHashcode =
        vehicleRestriction.getBrand().getBrandHashcode();
    const uint32_t modelID =
        uint32_t( vehicleRestriction.getModel().getValue() );
    const int32_t category =
        int32_t( vehicleRestriction.getCategory().getValue() );
    const int32_t categoryFrom =
        int32_t( vehicleRestriction.getCategoryFrom().getValue() );
    const int32_t categoryTo =
        int32_t( vehicleRestriction.getCategoryTo().getValue() );

    Garage *pGarage = pSave->getGarage();
    const uint16_t currentCar = pSave->getDriver()->CurrentCar;
    if ( IsCarAllowedByRestriction(
             &pGarage->pCars[currentCar], vehicleTypeIndex, restriction,
             brandHashcode, modelID, category, categoryFrom, categoryTo ) ) {
        *pOutCurrentCarOK = true;
        return;
    }

    for ( int32_t carIndex = 0; carIndex < pGarage->getNumCars(); carIndex++ ) {
        int32_t validIndex =
            ( carIndex < 0 || carIndex > Garage::kNumSavedCarSlots ) ? 0
                                                                     : carIndex;

        const CarDesc *pCar = &pGarage->pCars[0];
        for ( int32_t slot = 0; slot < Garage::kNumCarSlots; slot++ ) {
            if ( !pGarage->pCars[slot].isValid() ) {
                continue;
            }
            if ( validIndex == 0 ) {
                pCar = &pGarage->pCars[slot];
                break;
            }
            validIndex--;
        }

        if ( IsCarAllowedByRestriction( pCar, vehicleTypeIndex, restriction,
                                        brandHashcode, modelID, category,
                                        categoryFrom, categoryTo ) ) {
            *pOutOtherCarOK = true;
            return;
        }
    }
}

void EventParameters::setNameVariable( FlashPlayer *pPlayer,
                                       const char *pVariableName ) {
    // FUN_0073a880
    if ( databaseName.getValue() >= 0 ) {
        if ( databaseName.getValue() >= 0 ) {
            pPlayer->setVariableValue( pVariableName,
                                       databaseName.getDisplayText() );
        } else {
            pPlayer->setVariableValue( pVariableName, name.getString() );
        }
        return;
    }

    char nameText[32] = {};
    const char *pText = ( databaseName.getValue() >= 0 )
                            ? databaseName.getDisplayText()
                            : name.getString();
    strncpy( nameText, pText, sizeof( nameText ) - 1 );
    pPlayer->setVariableValueNonLinked( pVariableName, nameText );
}
