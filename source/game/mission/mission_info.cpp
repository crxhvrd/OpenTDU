#include "shared.h"
#include "gm_mission.h"

#include "ai/ai_bot_manager.h"
#include "ai/ai_config.h"
#include "ai/ai_game_session.h"
#include "ai/ai_pool_traffic.h"
#include "ai/ai_vehicle.h"
#include "ai/gs_ai.h"
#include "database/gs_database.h"
#include "game/scenaric/event_parameters.h"
#include "game/scenaric/gs_scenaric.h"
#include "game/scenaric/parameters/game_parameter.h"
#include "game/vehicle/car_desc.h"
#include "gs_timer.h"
#include "player_data/achievements_data.h"
#include "player_data/gs_playerdata.h"
#include "player_data/player_data_interface.h"

static constexpr uint32_t kRandomSeed = 0x3039;
static constexpr float kDefaultRating = 100.0f;     // DAT_00f41708
static constexpr float kInvalidRating = -1.0f;      // DAT_00f41664
static constexpr float kReferenceDefault = 1000.0f; // DAT_00f41718
static constexpr float kRatingSpeed = 27.0f;
static constexpr float kRatingScale = 0.2857143f; // DAT_00f41b90
static constexpr int32_t kNumCarGroups = 9;
static constexpr int32_t kGroupA = 0;
static constexpr int32_t kGroupB = 1;
static constexpr int32_t kGroupG = 6;
static constexpr int32_t kGroupMA = 7;
static constexpr int32_t kGroupMB = 8;
static constexpr int32_t kMaxRangeGroups = 3;
static constexpr int32_t kVehicleTypeCars = 1;
static constexpr int32_t kVehicleTypeBikes = 2;

// DAT_00f941d8
static constexpr uint64_t kCarGroupHashcodes[kNumCarGroups] = {
    0x467a0e8, 0x476e328, 0x4862568, 0x49567a8, 0x4a4a9e8,
    0x4b3ec28, 0x4c32e68, 0x493e108, 0x4a32348 };

enum eOpponentRestriction : int32_t {
    OR_None = 0,
    OR_Brand = 1,
    OR_Model = 2,
    OR_Category = 3,
    OR_Range = 4,
};

struct RaceTier {
    float MinDistance;
    float MaxDistance;
    float SlowDownFactor;
};

// FUN_0073bc70 (jump table 0x0073be40)
static constexpr RaceTier kRaceTiers[] = {
    {100.0f, 300.0f,  0.8f},
    {150.0f, 350.0f,  0.7f},
    {200.0f, 400.0f,  0.6f},
    {250.0f, 500.0f,  0.5f},
    {300.0f, 600.0f,  0.3f},
    {500.0f, 1000.0f, 0.3f},
};

struct CarGroupList {
    static constexpr int32_t kCapacity = 50;

    int32_t Cars[kCapacity];
    int32_t Count;

    CarGroupList() : Count( 0 ) {
        for ( int32_t &car : Cars ) {
            car = -1;
        }
    }

    void push( const int32_t carID ) {
        if ( Count != kCapacity ) {
            Cars[Count++] = carID;
        }
    }

    void removeAt( const int32_t index ) {
        for ( int32_t i = index; i < Count - 1; i++ ) {
            Cars[i] = Cars[i + 1];
        }
        Cars[--Count] = -1;
    }

    void removeValue( const int32_t carID ) {
        // FUN_0042df70
        for ( int32_t i = 0; i < Count; i++ ) {
            if ( Cars[i] == carID ) {
                removeAt( i );
                return;
            }
        }
    }
};

static int32_t RandomIndex( RandomGen &random, const int32_t count ) {
    return std::clamp( static_cast<int32_t>( RandomBelow(
                           random, static_cast<uint32_t>( count ) ) ),
                       0, count - 1 );
}

static int32_t GetCarGroupIndex( const uint64_t hashcode ) {
    // FUN_0073a970
    for ( int32_t i = 0; i < kNumCarGroups; i++ ) {
        if ( kCarGroupHashcodes[i] == hashcode ) {
            return i;
        }
    }
    return kGroupA;
}

static int32_t GetCarGroupIndex( const DBEntryCarPhysicsData *pCar ) {
    return GetCarGroupIndex(
        gpDatabase->getGameDB().Databases[1].getHashcodeForPointer(
            pCar->CarGroup.pAsRawPointer ) );
}

static int32_t GetChoiceGroupIndex( GameParameterChoice &choice ) {
    const int32_t value = int32_t( choice.getValue() );
    if ( value < 0 || value >= kNumCarGroups ) {
        return kGroupA;
    }
    return GetCarGroupIndex( kCarGroupHashcodes[value] );
}

static int32_t
FindIndex( const int32_t *pValues, const int32_t count, const int32_t value ) {
    for ( int32_t i = 0; i < count; i++ ) {
        if ( pValues[i] == value ) {
            return i;
        }
    }
    return -1;
}

static void RemoveAt( int32_t *pValues, int32_t &count, const int32_t index ) {
    for ( int32_t i = index; i < count - 1; i++ ) {
        pValues[i] = pValues[i + 1];
    }
    count--;
}

// Iterates the cars driven by at least one DB bot, starting from (and
// excluding the wrap back to) firstCarID.
template <typename Func>
static void ForEachBotCar( const int32_t firstCarID, Func func ) {
    int32_t carID = firstCarID;
    do {
        DBEntryCarPhysicsData *pCar = gpDatabase->getCarDBEntry( carID );
        if ( pCar != nullptr && FindBotOwningCar( carID ) != -1 ) {
            func( carID, pCar );
        }
        carID = GetNextAvailableCarID( carID );
    } while ( carID != -1 && carID != firstCarID );
}

MissionInfo::MissionInfo( GMMission *pOwnerGM )
    : pOwner( pOwnerGM ),
      Random(),
      Participants{},
      NumEntries( 0 ),
      NumSlots( 0 ),
      pRestriction( nullptr ),
      pVehicleType( nullptr ),
      PlayerCarID( 0 ),
      PlayerCarRawID( 0 ),
      NumCandidateCars( 0 ),
      CandidateCars{},
      NumCandidateBots( 0 ),
      CandidateBots{},
      NumBikeBots( 0 ),
      BikeBots{},
      RefAcceleration( 0.0f ),
      RefBrakingDistance( 0.0f ),
      RefTopSpeed( 0.0f ),
      RefTurningRadius( 0.0f ) {
    // FUN_0073eb10 (inlined)
    Random.setSeed( kRandomSeed );
}

bool MissionInfo::isInstantChallengeBotSlot( const int32_t index ) const {
    return index == 1 && gpScenaric->isInstantChallenge() &&
           gpScenaric->getICEChallengers()[0].bBot;
}

void MissionInfo::selectOpponents(
    GameParameterChoice *pOpponentVehicleType,
    GameParameterVehicleRestriction *pOpponentRestriction ) {
    // FUN_0073cd90
    Random.setSeed( static_cast<uint32_t>( gpTimer->SystemTotalTime ) );
    int32_t numOpponents = GetNumOpponents( this );
    NumSlots = numOpponents + 1;
    NumEntries = 0;
    NumBikeBots = 0;
    if ( numOpponents == 0 && gAIConfig.NumOpponentsOverride != 0 ) {
        return;
    }

    const CarDesc *pCurrentCar = gpPlayerData->getCurrentCar();
    const int32_t playerCarID =
        ( pCurrentCar != nullptr )
            ? static_cast<int32_t>( pCurrentCar->getCarConfigID() )
            : -1;
    Participant &player = Participants[0];
    player.BotID = -1;
    player.CarDBID = playerCarID;
    player.PaintIndex =
        ( pCurrentCar != nullptr ) ? pCurrentCar->paintIndex : -1;
    player.Rating = kDefaultRating;
    NumEntries++;

    pRestriction = pOpponentRestriction;
    pVehicleType = pOpponentVehicleType;
    PlayerCarRawID = playerCarID;
    PlayerCarID = playerCarID;
    const DBEntryCarPhysicsData *pPlayerCar =
        gpDatabase->getCarDBEntry( PlayerCarID );
    if ( pPlayerCar == nullptr || ( pPlayerCar->BitField & 1 ) == 0 ||
         pPlayerCar->Brand == nullptr ||
         ( pPlayerCar->Brand->Flags & 1 ) == 0 ) {
        PlayerCarID = GetNextAvailableCarID( PlayerCarID );
    }

    if ( numOpponents >= 1 ) {
        if ( gpScenaric->isInstantChallenge() &&
             gpScenaric->getICEChallengers()[0].bBot ) {
            const int32_t botID =
                static_cast<int32_t>( gpScenaric->getICEChallengers()[0].ID );
            Participant &opponent = Participants[NumEntries];
            opponent.CarDBID = GetBotCarID( botID );
            opponent.PaintIndex = GetBotColorIndex( botID );
            opponent.BotID = botID;
            opponent.Rating = kDefaultRating;
            NumEntries++;
            numOpponents--;
        }

        if ( numOpponents >= 1 ) {
            buildCandidateCars( numOpponents );
            pickOpponentCars( numOpponents );
            computeRatingsAndTier();
            if ( hasBikeOpponent() ) {
                buildBikeBotList();
            }

            if ( int32_t( pRestriction->getValue() ) == OR_None ) {
                buildBotListRange( 0, 2 );
            } else {
                AchievementsData *pAchievements =
                    gpPlayerData->getLoadedSave()->getAchievements();
                switch ( pAchievements->getPlayerLevel() ) {
                case 0:
                case 1:
                    buildBotListExact( 0 );
                    break;
                case 2:
                    buildBotListRange( 0, 1 );
                    break;
                case 3:
                    buildBotListRange( 1, 2 );
                    break;
                case 4:
                case 5:
                    buildBotListExact( 2 );
                    break;
                default:
                    break;
                }
            }
            assignBots();
            assignPaints();
        }
    }

    pRestriction = nullptr;
    pVehicleType = nullptr;
    PlayerCarID = 0;
}

void MissionInfo::addCandidateCar( const int32_t carID ) {
    if ( NumCandidateCars < kMaxNumCandidates ) {
        CandidateCars[NumCandidateCars++] = carID;
    }
}

void MissionInfo::buildCandidateCars( const int32_t numOpponents ) {
    // FUN_0073ccb0
    NumCandidateCars = 0;
    if ( pRestriction == nullptr ) {
        addCandidateCar( PlayerCarID );
        return;
    }
    if ( gAIConfig.bSameCarAsPlayer ) {
        addCandidateCar( PlayerCarRawID );
        return;
    }

    switch ( int32_t( pRestriction->getValue() ) ) {
    case OR_Brand:
        buildCandidateCarsBrand();
        break;
    case OR_Model:
        addCandidateCar( int32_t( pRestriction->getModel().getValue() ) );
        break;
    case OR_Category:
        buildCandidateCarsCategory();
        break;
    case OR_Range:
        buildCandidateCarsRange( numOpponents );
        break;
    default:
        buildCandidateCarsNone();
        break;
    }
}

void MissionInfo::buildCandidateCarsNone() {
    // FUN_0073c580
    CarGroupList groups[kNumCarGroups];
    NumCandidateCars = 0;
    ForEachBotCar( PlayerCarID,
                   [&]( const int32_t carID, DBEntryCarPhysicsData *pCar ) {
                       groups[GetCarGroupIndex( pCar )].push( carID );
                   } );

    const int32_t vehicleType = int32_t( pVehicleType->getValue() );
    const int32_t firstGroup =
        ( vehicleType == kVehicleTypeBikes ) ? kGroupMA : kGroupA;
    const int32_t lastGroup =
        ( vehicleType == kVehicleTypeCars ) ? kGroupG : kGroupMB;
    const uint32_t numPicks = ( vehicleType == kVehicleTypeBikes ) ? 4u : 2u;
    for ( int32_t groupIndex = firstGroup; groupIndex <= lastGroup;
          groupIndex++ ) {
        CarGroupList &group = groups[groupIndex];
        int32_t lastPick = -1;
        for ( uint32_t pick = 0; pick < numPicks; pick++ ) {
            if ( static_cast<uint32_t>( group.Count ) > numPicks - pick &&
                 lastPick != -1 ) {
                group.removeAt( lastPick );
            }
            if ( group.Count <= 0 ) {
                break;
            }
            lastPick = RandomIndex( Random, group.Count );
            addCandidateCar( group.Cars[lastPick] );
        }
    }
}

void MissionInfo::buildCandidateCarsBrand() {
    // FUN_0073cb20
    const uint64_t brandHashcode = pRestriction->getBrand().getBrandHashcode();
    const int32_t vehicleType = int32_t( pVehicleType->getValue() );
    NumCandidateCars = 0;
    ForEachBotCar( PlayerCarID,
                   [&]( const int32_t carID, DBEntryCarPhysicsData *pCar ) {
                       if ( pCar->Brand == nullptr ||
                            pCar->Brand->Hashcode != brandHashcode ) {
                           return;
                       }
                       const bool bBike = gpDatabase->isBike( pCar );
                       if ( ( vehicleType == kVehicleTypeCars && bBike ) ||
                            ( vehicleType == kVehicleTypeBikes && !bBike ) ) {
                           return;
                       }
                       addCandidateCar( carID );
                   } );
}

void MissionInfo::buildCandidateCarsCategory() {
    // FUN_0073c9a0
    const int32_t selectedGroup =
        GetCarGroupIndex( pRestriction->getCategory().getSelectedOption() );
    const bool bWithBikes =
        int32_t( pVehicleType->getValue() ) != kVehicleTypeCars;
    NumCandidateCars = 0;
    ForEachBotCar( PlayerCarID, [&]( const int32_t carID,
                                     DBEntryCarPhysicsData *pCar ) {
        const int32_t group = GetCarGroupIndex( pCar );
        if ( group == selectedGroup ||
             ( bWithBikes && selectedGroup == kGroupA && group == kGroupMA ) ||
             ( bWithBikes && selectedGroup == kGroupB && group == kGroupMB ) ) {
            addCandidateCar( carID );
        }
    } );
}

void MissionInfo::buildCandidateCarsRange( const int32_t numOpponents ) {
    // FUN_0073c100
    const bool bCarsOnly =
        int32_t( pVehicleType->getValue() ) == kVehicleTypeCars;
    const int32_t minGroup =
        GetChoiceGroupIndex( pRestriction->getCategoryTo() );
    const int32_t maxGroup =
        GetChoiceGroupIndex( pRestriction->getCategoryFrom() );
    const bool bWithMA =
        !bCarsOnly && maxGroup >= kGroupA && minGroup <= kGroupA;
    const bool bWithMB =
        !bCarsOnly && maxGroup >= kGroupB && minGroup <= kGroupB;

    CarGroupList groups[kNumCarGroups];
    NumCandidateCars = 0;
    ForEachBotCar( PlayerCarID,
                   [&]( const int32_t carID, DBEntryCarPhysicsData *pCar ) {
                       if ( carID == PlayerCarRawID ) {
                           return;
                       }
                       const int32_t group = GetCarGroupIndex( pCar );
                       if ( group >= minGroup && group <= maxGroup ) {
                           groups[group].push( carID );
                       } else if ( ( group == kGroupMA && bWithMA ) ||
                                   ( group == kGroupMB && bWithMB ) ) {
                           groups[group].push( carID );
                       }
                   } );
    if ( minGroup > maxGroup ) {
        return;
    }

    const int32_t numGroups =
        std::min( maxGroup - minGroup + 1, kMaxRangeGroups );
    int32_t counts[kNumCarGroups] = {};
    counts[minGroup] = static_cast<int32_t>(
        static_cast<float>( numOpponents ) / static_cast<float>( numGroups ) +
        0.5f );
    if ( numGroups > 1 ) {
        counts[minGroup + 1] = static_cast<int32_t>(
            static_cast<float>( numOpponents - counts[minGroup] ) /
                static_cast<float>( numGroups - 1 ) +
            0.5f );
    }
    if ( numGroups > 2 ) {
        counts[minGroup + 2] =
            ( numOpponents - counts[minGroup + 1] - counts[minGroup] ) /
            ( numGroups - 2 );
    }

    RandomGen &random = gpAI->getRandom();
    for ( int32_t i = 0; i < numGroups; i++ ) {
        const int32_t groupIndex = minGroup + i;
        while ( counts[groupIndex] > 0 ) {
            int32_t pickGroup = groupIndex;
            if ( bWithMA && pickGroup == kGroupA &&
                 groups[kGroupMA].Count > 0 &&
                 random.getRandomUint32() < 0x7fffffffu ) {
                pickGroup = kGroupMA;
            }
            if ( bWithMB && pickGroup == kGroupB &&
                 groups[kGroupMB].Count > 0 &&
                 random.getRandomUint32() < 0x7fffffffu ) {
                pickGroup = kGroupMB;
            }

            CarGroupList &group = groups[pickGroup];
            // NOTE: Retail reads Cars[-1] when the group is empty.
            if ( group.Count <= 0 ) {
                counts[groupIndex]--;
                continue;
            }
            const int32_t carID =
                group.Cars[RandomIndex( random, group.Count )];
            addCandidateCar( carID );
            group.removeValue( carID );
            counts[groupIndex]--;
        }
    }
}

void MissionInfo::pickOpponentCars( const int32_t numOpponents ) {
    // FUN_0073ab60
    int32_t lastPick =
        FindIndex( CandidateCars, NumCandidateCars, Participants[0].CarDBID );
    for ( int32_t remaining = numOpponents; remaining > 0; remaining-- ) {
        if ( NumCandidateCars > remaining && lastPick != -1 ) {
            RemoveAt( CandidateCars, NumCandidateCars, lastPick );
        }
        Participant &opponent = Participants[NumEntries];
        opponent.BotID = -1;
        opponent.PaintIndex = -1;
        if ( NumCandidateCars < 1 ) {
            opponent.CarDBID = PlayerCarID;
        } else {
            lastPick = RandomIndex( Random, NumCandidateCars );
            opponent.CarDBID = CandidateCars[lastPick];
        }
        NumEntries++;
    }
}

void MissionInfo::computeFieldReference() {
    // FUN_0073b200
    RefAcceleration = kReferenceDefault;
    RefBrakingDistance = kReferenceDefault;
    RefTurningRadius = kReferenceDefault;
    RefTopSpeed = 0.0f;
    // NOTE: Retail scans every slot (including stale ones from a previous
    // mission), not only the NumEntries filled this time.
    for ( const Participant &participant : Participants ) {
        if ( participant.CarDBID == 0 ) {
            continue;
        }
        const DBEntryCarPhysicsData *pCar =
            gpDatabase->getCarDBEntry( participant.CarDBID );
        if ( pCar == nullptr ) {
            continue;
        }
        const AIVehiclePhysics *pPhysics =
            gAIConfig.findVehiclePhysics( pCar->CarID );
        if ( pPhysics == nullptr || pPhysics->TurningSpeedC1 == -1.0f ) {
            continue;
        }

        const float turningRadius = ComputeTurningRadius(
            pPhysics->TurningSpeedC1, pPhysics->TurningSpeedC2, kRatingSpeed );
        const float brakingDistance = ComputeBrakingDistance(
            pPhysics->BrakingDistC1, pPhysics->BrakingDistC2, kRatingSpeed,
            0.0f, 0.0f );
        if ( brakingDistance != 0.0f ) {
            RefBrakingDistance =
                std::min( RefBrakingDistance, brakingDistance );
        }
        if ( turningRadius != 0.0f ) {
            RefTurningRadius = std::min( RefTurningRadius, turningRadius );
        }
        if ( pCar->AccelerationKMH != 0.0f ) {
            RefAcceleration =
                std::min( RefAcceleration, pCar->AccelerationKMH );
        }
        RefTopSpeed = std::max( RefTopSpeed, pCar->TopSpeedKMH );
    }
}

float MissionInfo::computeCarRating( const int32_t carID ) const {
    // FUN_0073afe0
    const DBEntryCarPhysicsData *pCar = gpDatabase->getCarDBEntry( carID );
    if ( pCar == nullptr ) {
        return kInvalidRating;
    }
    const AIVehiclePhysics *pPhysics =
        gAIConfig.findVehiclePhysics( pCar->CarID );
    if ( pPhysics == nullptr || pPhysics->TurningSpeedC1 == -1.0f ) {
        return kInvalidRating;
    }

    const float turningRadius = ComputeTurningRadius(
        pPhysics->TurningSpeedC1, pPhysics->TurningSpeedC2, kRatingSpeed );
    const float brakingDistance = ComputeBrakingDistance(
        pPhysics->BrakingDistC1, pPhysics->BrakingDistC2, kRatingSpeed, 0.0f,
        0.0f );
    const float acceleration = pCar->AccelerationKMH;
    if ( acceleration == 0.0f || brakingDistance == 0.0f ||
         turningRadius == 0.0f ) {
        return kInvalidRating;
    }

    const float topSpeed = pCar->TopSpeedKMH;
    return kDefaultRating * kRatingScale *
           ( 1.0f / ( ( acceleration * acceleration ) /
                      ( RefAcceleration * RefAcceleration ) ) +
             0.5f / ( ( brakingDistance * brakingDistance ) /
                      ( RefBrakingDistance * RefBrakingDistance ) ) +
             ( topSpeed * topSpeed ) / ( RefTopSpeed * RefTopSpeed ) +
             1.0f / ( ( turningRadius * turningRadius ) /
                      ( RefTurningRadius * RefTurningRadius ) ) );
}

void MissionInfo::computeRatingsAndTier() {
    // FUN_0073bc70
    computeFieldReference();
    for ( int32_t i = 0; i < NumEntries; i++ ) {
        Participants[i].Rating = computeCarRating( Participants[i].CarDBID );
    }

    AchievementsData *pAchievements =
        gpPlayerData->getLoadedSave()->getAchievements();
    const int32_t level = std::clamp( pAchievements->getPlayerLevel(), 0,
                                      int32_t( std::size( kRaceTiers ) ) - 1 );
    gAIConfig.RubberBandMinDistance = kRaceTiers[level].MinDistance;
    gAIConfig.RubberBandMaxDistance = kRaceTiers[level].MaxDistance;
    gAIConfig.RubberBandMaxSlowDown = kRaceTiers[level].SlowDownFactor;
}

bool MissionInfo::hasBikeOpponent() const {
    // FUN_0073af30
    for ( int32_t i = 1; i < NumSlots; i++ ) {
        if ( isInstantChallengeBotSlot( i ) ) {
            continue;
        }
        if ( gpDatabase->isBike(
                 gpDatabase->getCarDBEntry( Participants[i].CarDBID ) ) ) {
            return true;
        }
    }
    return false;
}

void MissionInfo::buildBikeBotList() {
    // FUN_0073ae10
    NumBikeBots = 0;
    const int32_t numBots =
        static_cast<int32_t>( gpDatabase->getGameDB().pNumItems[0xe] );
    for ( int32_t i = 0; i < numBots && NumBikeBots < kMaxNumBikeBots; i++ ) {
        if ( gpDatabase->isBike(
                 gpDatabase->getCarDBEntry( GetBotCarID( i ) ) ) ) {
            BikeBots[NumBikeBots++] = i;
        }
    }
}

void MissionInfo::buildBotListRange( const int32_t minTier,
                                     const int32_t maxTier ) {
    // FUN_0073ba20
    NumCandidateBots = 0;
    const int32_t numBots =
        static_cast<int32_t>( gpDatabase->getGameDB().pNumItems[0xe] );
    for ( int32_t i = 0; i < numBots && NumCandidateBots < kMaxNumCandidates;
          i++ ) {
        if ( gpDatabase->isBike(
                 gpDatabase->getCarDBEntry( GetBotCarID( i ) ) ) ) {
            continue;
        }
        const int32_t tier = GetBotTier( i );
        if ( tier >= minTier && tier <= maxTier ) {
            CandidateBots[NumCandidateBots++] = i;
        }
    }
}

void MissionInfo::buildBotListExact( const int32_t tier ) {
    // FUN_0073bb50
    buildBotListRange( tier, tier );
}

int32_t MissionInfo::pickBikeBot( const int32_t carID ) {
    // FUN_0073ad10
    int32_t index =
        FindIndex( BikeBots, NumBikeBots, FindBotOwningCar( carID ) );
    if ( index == -1 ) {
        index = RandomIndex( gpAI->getRandom(), NumBikeBots );
    }
    const int32_t botID = BikeBots[index];
    RemoveAt( BikeBots, NumBikeBots, index );
    return botID;
}

void MissionInfo::assignBots() {
    // FUN_0073be60
    int32_t lastPick = -1;
    for ( int32_t i = 1; i < NumSlots; i++ ) {
        const bool bInstantChallengeSlot = isInstantChallengeBotSlot( i );
        if ( bInstantChallengeSlot ) {
            lastPick = FindIndex( CandidateBots, NumCandidateBots,
                                  Participants[1].BotID );
        }
        if ( lastPick != -1 ) {
            RemoveAt( CandidateBots, NumCandidateBots, lastPick );
        }

        Participant &opponent = Participants[i];
        if ( gpDatabase->isBike(
                 gpDatabase->getCarDBEntry( opponent.CarDBID ) ) ) {
            if ( NumBikeBots <= 0 ) {
                opponent.BotID = -1;
            } else if ( !bInstantChallengeSlot ) {
                opponent.BotID = pickBikeBot( opponent.CarDBID );
                lastPick = -1;
            }
        } else if ( NumCandidateBots <= 0 ) {
            opponent.BotID = -1;
        } else if ( !bInstantChallengeSlot ) {
            const int32_t ownerBot = FindBotOwningCar( opponent.CarDBID );
            lastPick =
                ( ownerBot != -1 )
                    ? FindIndex( CandidateBots, NumCandidateBots, ownerBot )
                    : -1;
            if ( lastPick == -1 ) {
                lastPick = RandomIndex( Random, NumCandidateBots );
            }
            opponent.BotID = CandidateBots[lastPick];
        }
    }
}

void MissionInfo::assignPaints() {
    // FUN_0073b6a0
    for ( int32_t i = 1; i < NumSlots; i++ ) {
        if ( isInstantChallengeBotSlot( i ) ) {
            continue;
        }
        Participant &opponent = Participants[i];
        if ( FindBotOwningCar( opponent.CarDBID ) == opponent.BotID ) {
            opponent.PaintIndex = GetBotColorIndex( opponent.BotID );
            continue;
        }
        const int32_t numColors = gpDatabase->getNumColors( opponent.CarDBID );
        opponent.PaintIndex =
            ( numColors != 0 )
                ? RandomRange( gpAI->getRandom(), 0, numColors - 1 )
                : 0;
    }
}
