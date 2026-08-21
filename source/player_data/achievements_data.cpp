#include "shared.h"
#include "achievements_data.h"

AchievementsData::AchievementsData()
    : achievements(),
      bMustLevelUp( false ),
      currentGameMode( -1 ),
      field516_0x204( false ),
      bReady( false ),
      flagAsync( 0x80000000u ),
      numCustomUploaded( 0 ),
      numCustomWon( 0 ),
      numSoldBoughtEbay( 0 ),
      exploredRoadsLength( 0 ),
      totalRoadsLength( 0 ),
      rewardCarsBought{},
      bDisplayLevelUp( false ),
      bQuiet( false ),
      pointsNeeded{} {
    // FUN_0089eb80
    reset();

    pointsNeeded[0] = 40u;
    pointsNeeded[1] = 130u;
    pointsNeeded[2] = 300u;
    pointsNeeded[3] = 500u;
    pointsNeeded[4] = 1000u;
}

AchievementsData::~AchievementsData() {}

void AchievementsData::reset() {
    // FUN_0089e980
    field516_0x204 = false;
    bMustLevelUp = false;
    currentGameMode = -1;

    numCustomUploaded = 0;
    numCustomWon = 0;
    numSoldBoughtEbay = 0;
    exploredRoadsLength = 0;
    totalRoadsLength = 0;

    memset( rewardCarsBought, 0, sizeof( rewardCarsBought ) );

    for ( AchievementCheck *pAchievement : achievements ) {
        if ( pAchievement != nullptr ) {
            pAchievement->bLock = true;
        }
    }

    OTDU_UNIMPLEMENTED_ONCE; // TODO: PLAYERSAVE
}

bool AchievementsData::isRewardLocked( const uint64_t rewardHashcode ) const {
    // FUN_0089bd30
    for ( size_t i = 1; i < achievements.size(); i++ ) {
        const AchievementCheck *pAchievement = achievements[i];
        if ( pAchievement != nullptr &&
             pAchievement->RewardHashcode == rewardHashcode ) {
            return achievements[i]->bLock;
        }
    }

    return true;
}

uint32_t AchievementsData::getAchievedPoints() const {
    // FUN_0089bd90
    uint32_t achievedPoints = 0u;
    for ( const AchievementCheck *pAchievement : achievements ) {
        if ( pAchievement != nullptr && !pAchievement->bLock ) {
            achievedPoints += pAchievement->NumPoints;
        }
    }

    return achievedPoints;
}

int32_t AchievementsData::getPlayerLevel() const {
    // FUN_0089c210
    const uint32_t achievedPoints = getAchievedPoints();

    if ( pointsNeeded[4] <= achievedPoints ) {
        return 5;
    }
    if ( pointsNeeded[3] <= achievedPoints ) {
        return 4;
    }
    if ( pointsNeeded[2] <= achievedPoints ) {
        return 3;
    }
    if ( pointsNeeded[1] <= achievedPoints ) {
        return 2;
    }

    return int32_t( pointsNeeded[0] <= achievedPoints );
}
