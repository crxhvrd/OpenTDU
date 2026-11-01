#include "shared.h"
#include "entity_locked_barrier.h"

#include "game/gm_base.h"
#include "game/entity_mechanism.h"
#include "filesystem/binary.h"
#include "player_data/gs_playerdata.h"
#include "player_data/player_data_interface.h"
#include "player_data/achievements_data.h"

enum eLockedBarrierType : int32_t {
    LBT_Unknown = 0,
    LBT_OahuRaceway = 1, // unlocked by achievement byte 0x3551
    LBT_FordIsland = 2,  // unlocked by achievement byte 0x3550
};

static bool IsBarrierZoneUnlocked( const int32_t type ) {
    // FUN_009c80cd
    const AchievementsData *pAchievements =
        gpPlayerData->getLoadedSave()->getAchievements();

    return ( type == LBT_OahuRaceway )
               ? pAchievements->isSimulationModeUnlocked()
               : pAchievements->isReward0x5912a0Unlocked();
}

EntityLockedBarrier::EntityLockedBarrier()
    : EntityActor(),
      Type( LBT_Unknown ),
      bFirstManage( true ),
      bIsLocked( true ),
      OriginalPos( Eigen::Vector4f::Zero() ) {
    // FUN_009c7fe0
}

EntityLockedBarrier::~EntityLockedBarrier() {
    // FUN_00424120
}

void EntityLockedBarrier::tick( GameMechanism *pGameMech,
                                float deltaTime,
                                float totalTime ) {
    // FUN_009c8070
    EntityActor::tick( pGameMech, deltaTime, totalTime );

    if ( bFirstManage ) {
        OriginalPos = modelMatrix.row( 3 );
        bFirstManage = false;
    }

    bool bKeepLocked = false;
    if ( Type == LBT_OahuRaceway || Type == LBT_FordIsland ) {
        const bool bAchievementEarned = IsBarrierZoneUnlocked( Type );
        const bool bInMission =
            gpActiveGameMode != nullptr &&
            gpActiveGameMode->getCurrentGameMode() == GM_Mission;
        bKeepLocked = !bAchievementEarned && !bInMission;
    }

    if ( bKeepLocked ) {
        if ( bIsLocked ) {
            return;
        }
        propagatePosition( OriginalPos, true );
        if ( pMechanism != nullptr ) {
            pMechanism->setInitialTransformMatrix( modelMatrix );
        }
        bIsLocked = true;
        return;
    }

    // Unlocked: move the prop to 100 units below the original position.
    if ( bIsLocked ) {
        Eigen::Vector4f loweredPos =
            OriginalPos + Eigen::Vector4f( 0.0f, -100.0f, 0.0f, 0.0f );
        propagatePosition( loweredPos, true );
        if ( pMechanism != nullptr ) {
            pMechanism->setInitialTransformMatrix( modelMatrix );
        }
        bIsLocked = false;
    }
}

void EntityLockedBarrier::deserialize( BinaryReader *param_2 ) {
    // FUN_009c8040
    EntityActor::deserialize( param_2 );

    Type = *reinterpret_cast<int32_t *>( param_2->pCursor );
    param_2->pCursor += sizeof( int32_t );
}

void EntityLockedBarrier::unknownFunc2( void *param_2 ) {
    // FUN_009c7fd0
    Type = *reinterpret_cast<int32_t *>( param_2 );
}

void EntityLockedBarrier::instanciate( MngActor *pMngActor, Entity *param_1 ) {
    // FUN_009c8010
    EntityActor::instanciate( pMngActor, param_1 );

    static_cast<EntityLockedBarrier *>( param_1 )->Type = Type;
}
