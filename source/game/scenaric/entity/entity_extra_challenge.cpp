#include "shared.h"
#include "entity_extra_challenge.h"

#include "entity_volume.h"
#include "entity_hitch_hiker.h"
#include "entity_shopping_girl.h"
#include "entity_transporter.h"
#include "game/game_mechanism.h"
#include "game/game_mechanism_player.h"
#include "game/scenaric/gs_scenaric.h"
#include "game/scenaric/spots.h"
#include "game/scenaric/local_player.h"
#include "game/vehicle/car_state.h"
#include "game/avatar/gs_avatar.h"
#include "database/gs_database.h"
#include "filesystem/binary.h"
#include "physics/vehicle/physics_vehicle.h"
#include "player_data/gs_playerdata.h"
#include "player_data/player_data_interface.h"
#include "player_data/saved_item_table.h"

// Index of TDU_PNJ in GameDatabase::pNumItems
static constexpr int32_t kPNJDatabaseIndex = 0x10;

EntityExtraChallenge::EntityExtraChallenge()
    : Entity(),
      Index( 0 ),
      TimeLimit( 0.0f ),
      pGoalMatrix( nullptr ),
      pGoodiesBinMatrix( nullptr ),
      GoodiesMatrix( Matrix4x4f_t::Identity() ),
      TriggerHashcode( 0 ),
      pTrigger( nullptr ),
      AvatarVoiceType( 0 ),
      bInZone( false ),
      RoadID( -1 ),
      RoadEntityID( -1 ) {
    // FUN_007dd170
}

EntityExtraChallenge::~EntityExtraChallenge() {
    // FUN_00424120
}

void EntityExtraChallenge::initialize() {
    // FUN_007dd9e0
    setTransformFromDesc();

    if ( pMechanism != nullptr ) {
        pMechanism->setEntity( this );
    }
}

void EntityExtraChallenge::loadResources( ScenaricBank *param_2 ) {
    // FUN_007dd7e0
    if ( pMechanism != nullptr ) {
        pMechanism->loadResources( param_2 );
    }

    resolveSoundReference();

    pTrigger = static_cast<EntityVolume *>(
        gpScenaric->getSCVolume( TriggerHashcode ) );
}

void EntityExtraChallenge::initializeInstance( void *param_2, void *param_3 ) {
    // FUN_007dcc60
    Entity::initializeInstance( param_2, param_3 );
}

void EntityExtraChallenge::tick( PlayerGameMech *pGameMech,
                                 float deltaTime,
                                 float totalTime ) {
    // FUN_007dd8d0
    if ( ( flags & 2 ) == 0 ) {
        return;
    }

    Entity::tick( pGameMech, deltaTime, totalTime );

    if ( !isAvailable() ) {
        return;
    }

    const CarState *pLocalCar = gpScenaric->getLocalPlayer().getCarState();
    if ( pLocalCar == nullptr ) {
        return;
    }

    const CarBodyState *pBody = pLocalCar->getBody();
    const Eigen::Vector4f worldPosition = pBody->WorldMatrix.row( 3 );
    const Eigen::Vector4f previousWorldPosition =
        pBody->PreviousWorldMatrix.row( 3 );

    if ( pTrigger != nullptr &&
         pTrigger->isPointInside( worldPosition, &previousWorldPosition ) ) {
        bInZone = true;
        pGameMech->onMessage( this, PlayerGameMech::ME_ExtraChallengeEntered,
                              this );
        return;
    }

    if ( bInZone ) {
        pGameMech->onMessage( this, PlayerGameMech::ME_SpotLeft, this );
        bInZone = false;
    }
}

bool EntityExtraChallenge::isAvailable() const {
    // FUN_007dd8d0 (inlined)
    const SavedItemTable *pChallengeStates =
        gpPlayerData->getLoadedSave()->getItemTable();
    return !gbSimuModeEnabled && pChallengeStates->getExtraChallengeState(
                                     Index ) != SavedItemTable::ECS_Done;
}

void EntityExtraChallenge::getIconInfo( SpotIconInfo &outInfo ) const {
    // FUN_007dc6d0
    outInfo.Kind = 1;
    outInfo.ChallengeFlags = 0x390u;

    const SavedItemTable *pChallengeStates =
        gpPlayerData->getLoadedSave()->getItemTable();
    outInfo.ChallengeFlags |=
        pChallengeStates->isExtraChallengeGold( Index ) ? 0x20u : 0x40u;

    uint32_t conditionType = 0u;
    uint32_t conditionValue = 0xffffffffu;
    switch ( getType() ) {
    case eEntityType::ET_SHOG: {
        const EntityShoppingGirl *pShoppingGirl =
            static_cast<const EntityShoppingGirl *>( this );
        conditionType = pShoppingGirl->getConditionType();
        conditionValue = pShoppingGirl->getConditionValue();
    } break;
    case eEntityType::ET_HIHI: {
        const EntityHitchHiker *pHitchHiker =
            static_cast<const EntityHitchHiker *>( this );
        conditionType = pHitchHiker->getConditionType();
        conditionValue = pHitchHiker->getConditionValue();
    } break;
    case eEntityType::ET_TRAN: {
        const EntityTransporter *pTransporter =
            static_cast<const EntityTransporter *>( this );
        conditionType = pTransporter->getConditionType();
        conditionValue = pTransporter->getConditionValue();
    } break;
    case eEntityType::ET_CVOY:
        outInfo.ChallengeFlags |= 0x1ff000u;
        break;
    default:
        break;
    }

    outInfo.SpotFlags = 0u;
    outInfo.Unknown0xC = 0u;
    outInfo.Unknown0x10 = 0u;

    switch ( conditionType ) {
    case 1: {
        // Vehicle class index
        static constexpr uint32_t kClassFlags[9] = {
            0x1000u,  0x2000u,  0x4000u,  0x8000u,   0x10000u,
            0x20000u, 0x40000u, 0x80000u, 0x100000u,
        };
        outInfo.ChallengeFlags |=
            ( conditionValue < 9 ) ? kClassFlags[conditionValue] : 0x7f000u;
    } break;
    case 3: {
        // Vehicle model ID
        DBEntryCarPhysicsData *pCar =
            gpDatabase->getCarDBEntry( int32_t( conditionValue ) );
        if ( pCar == nullptr ) {
            break;
        }
        const uint32_t classFlag = GetCarGroupIconFlag( pCar );
        outInfo.ChallengeFlags |= ( classFlag != 0u ) ? classFlag : 0x7f000u;
    } break;
    default:
        outInfo.ChallengeFlags |= 0x7f000u;
        break;
    }
}

void EntityExtraChallenge::computeRoadLocation() {
    // FUN_007dda00
    OTDU_UNIMPLEMENTED_ONCE; // TODO: AI
}

void EntityExtraChallenge::deserialize( BinaryReader *param_2 ) {
    // FUN_007dce20
    Entity::deserialize( param_2 );

    TimeLimit = *reinterpret_cast<float *>( param_2->pCursor );
    param_2->pCursor += sizeof( float );

    pGoalMatrix = reinterpret_cast<SerializedTransform *>( param_2->pCursor );
    param_2->pCursor += sizeof( SerializedTransform );

    pGoodiesBinMatrix =
        reinterpret_cast<SerializedTransform *>( param_2->pCursor );
    param_2->pCursor += sizeof( SerializedTransform );

    Eigen::Quaternionf goodiesRotation(
        pGoodiesBinMatrix->RotationW, pGoodiesBinMatrix->RotationX,
        pGoodiesBinMatrix->RotationY, pGoodiesBinMatrix->RotationZ );
    Eigen::Matrix3f goodiesRotationMatrix = goodiesRotation.toRotationMatrix();

    GoodiesMatrix = Matrix4x4f_t::Zero();
    GoodiesMatrix.row( 0 ).head( 3 ) = goodiesRotationMatrix.row( 0 );
    GoodiesMatrix.row( 1 ).head( 3 ) = goodiesRotationMatrix.row( 1 );
    GoodiesMatrix.row( 2 ).head( 3 ) = goodiesRotationMatrix.row( 2 );
    GoodiesMatrix.row( 3 ) = Eigen::Vector4f(
        pGoodiesBinMatrix->PositionX, pGoodiesBinMatrix->PositionY,
        pGoodiesBinMatrix->PositionZ, 1.0f );

    TriggerHashcode = *reinterpret_cast<uint64_t *>( param_2->pCursor );
    param_2->pCursor += sizeof( uint64_t );

    AvatarVoiceType = *reinterpret_cast<uint32_t *>( param_2->pCursor );
    param_2->pCursor += sizeof( uint32_t );

    readAvatarConfigFromPNJ( param_2, AvatarConfig );
}

void EntityExtraChallenge::readAvatarConfigFromPNJ(
    BinaryReader *pReader, AvatarConfiguration &outConfig ) {
    // FUN_007dd1c0 (inlined)
    const char *pPNJName = ReadStreamString( pReader );

    const GameDatabase &gameDB = gpDatabase->getGameDB();
    const int32_t numPNJ =
        static_cast<int32_t>( gameDB.pNumItems[kPNJDatabaseIndex] );

    for ( int32_t i = 0; i < numPNJ; i++ ) {
        const DBEntryPNJ &pnjEntry = gameDB.DBPNJ[i];
        if ( strcmp( pnjEntry.ConfigID.pAsString, pPNJName ) != 0 ) {
            continue;
        }

        if ( gpAvatar->applyPNJConfiguration( outConfig, pnjEntry ) ) {
            return;
        }

        break;
    }

    outConfig = gpAvatar->getDefaultConfiguration( 0 );
}
