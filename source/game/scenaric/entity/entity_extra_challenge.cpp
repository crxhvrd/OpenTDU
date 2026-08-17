#include "shared.h"
#include "entity_extra_challenge.h"

#include "game/game_mechanism.h"
#include "game/scenaric/gs_scenaric.h"
#include "game/avatar/gs_avatar.h"
#include "database/gs_database.h"
#include "filesystem/binary.h"

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

    pTrigger = gpScenaric->getSCVolume( TriggerHashcode );
}

void EntityExtraChallenge::initializeInstance( void *param_2, void *param_3 ) {
    // FUN_007dcc60
    Entity::initializeInstance( param_2, param_3 );
}

void EntityExtraChallenge::tick() {
    // FUN_007dd8d0
    if ( ( flags & 2 ) == 0 ) {
        return;
    }

    Entity::tick();

    OTDU_UNIMPLEMENTED; // TODO: PLAYERSAVE/GAME
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
