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
#include "flash/mng_flash_localize.h"
#include "flash/implementation/flash_player.h"
#include "game/vehicle/car_desc.h"
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

Eigen::Vector4f EntityExtraChallenge::getGoalPosition() const {
    // FUN_00427b50
    return Eigen::Vector4f( pGoalMatrix->PositionX, pGoalMatrix->PositionY,
                            pGoalMatrix->PositionZ, 1.0f );
}

void EntityExtraChallenge::buildVehicleRestrictionText(
    const uint32_t conditionType,
    const ChallengeCondition &condition,
    char *pOutText,
    const size_t maxLength,
    const bool bShowGroup ) const {
    // FUN_007dc9c0
    static constexpr uint64_t kNoRestrictionHashcode = 0x580fef;
    static constexpr uint64_t kConvoyRestrictionHashcode = 0x38ff432;
    static constexpr uint64_t kGroupRestrictionHashcode = 0x3b44512;

    pOutText[0] = '\0';
    switch ( conditionType ) {
    case 0: {
        const uint64_t hashcode = ( getType() == eEntityType::ET_CVOY )
                                      ? kConvoyRestrictionHashcode
                                      : kNoRestrictionHashcode;
        const char *pText = gpDatabase->getStringByHashcode( 0, hashcode );
        if ( pText != nullptr ) {
            strncpy( pOutText, pText, maxLength );
        } else {
            strncpy( pOutText, "TEXT NOT FOUND IN BDD OR INVALID!!!",
                     maxLength );
        }
    } break;
    case 1: {
        if ( !bShowGroup ) {
            break;
        }
        char groupLetter[4];
        snprintf( groupLetter, sizeof( groupLetter ), "%c",
                  'A' + condition.Value );
        gpMngFlashLocalize->setSysVar( "GROUP", groupLetter );
        gpMngFlashLocalize->makeSentenceFromHashcode(
            pOutText, maxLength, kGroupRestrictionHashcode );
        gpMngFlashLocalize->clearAllSysVar();
    } break;
    case 2: {
        const GameDatabase &gameDB = gpDatabase->getGameDB();
        for ( uint32_t i = 0; i < gameDB.pNumItems[6]; i++ ) {
            const DBEntryBrand &brand = gameDB.DBBrand[i];
            if ( strcmp( condition.pValue, brand.BrandID.pAsString ) != 0 ) {
                continue;
            }
            if ( brand.BrandNameID.Hashcode != 0 ) {
                strncpy( pOutText, brand.BrandNameID.pAsString, maxLength );
            } else {
                strncpy( pOutText, "No brank mark, BUG!!!!", maxLength );
            }
            break;
        }
    } break;
    case 3: {
        DBEntryCarPhysicsData *pCar =
            gpDatabase->getCarDBEntry( condition.Value );
        if ( pCar != nullptr ) {
            strncpy( pOutText, GetFullCarName( pCar ).c_str(), maxLength );
        }
    } break;
    default:
        break;
    }
    pOutText[maxLength - 1] = '\0';
}

void EntityExtraChallenge::SetGroupRestrictionVars(
    FlashPlayer *pPlayer,
    const uint32_t conditionType,
    const ChallengeCondition &condition ) {
    // FUN_007dd750
    if ( pPlayer == nullptr ) {
        return;
    }

    switch ( conditionType ) {
    case 0:
    case 2:
    case 3:
        pPlayer->setVariableValueNonLinked( "/:GroupRestriction01", "empty" );
        pPlayer->setVariableValueNonLinked( "/:GroupRestriction02", "empty" );
        break;
    case 1: {
        pPlayer->setVariableValueNonLinked( "/:GroupRestriction02", "empty" );
        char groupLetter[4];
        snprintf( groupLetter, sizeof( groupLetter ), "%c",
                  'A' + condition.Value );
        pPlayer->setVariableValueNonLinked( "/:GroupRestriction01",
                                            groupLetter );
    } break;
    default:
        break;
    }
}

bool EntityExtraChallenge::isCarAllowed(
    const uint32_t conditionType, const ChallengeCondition &condition ) const {
    // FUN_007dcfe0
    // DAT_00f941d8
    static constexpr uint64_t kCarGroupHashcodes[] = {
        0x467a0e8, 0x476e328, 0x4862568, 0x49567a8,
        0x4a4a9e8, 0x4b3ec28, 0x4c32e68 };

    const CarDesc *pCurrentCar = gpPlayerData->getCurrentCar();
    switch ( conditionType ) {
    case 0: {
        if ( getType() == eEntityType::ET_CVOY ) {
            return true;
        }
        CarState *pCarState = gpScenaric->getLocalPlayer().getCarState();
        if ( pCarState == nullptr ) {
            return true;
        }
        bool bRejected = gpDatabase->isBike(
            gpDatabase->getCarDBEntry( pCarState->getCarID() ) );
        if ( !bRejected && pCurrentCar != nullptr ) {
            const DBEntryCarPhysicsData *pCar =
                gpDatabase->getCarDBEntry( pCurrentCar->getCarConfigID() );
            if ( pCar != nullptr ) {
                const char *pNumSeats = pCar->NumSeats.pAsString;
                if ( pNumSeats[0] == '1' || pNumSeats[0] == '3' ) {
                    bRejected = true;
                }
            }
        }
        return !bRejected;
    }
    case 1: {
        if ( pCurrentCar == nullptr ) {
            return true;
        }
        const DBEntryCarPhysicsData *pCar =
            gpDatabase->getCarDBEntry( pCurrentCar->getCarConfigID() );
        if ( pCar == nullptr ) {
            return true;
        }
        const uint64_t groupHashcode =
            gpDatabase->getGameDB().Databases[1].getHashcodeForPointer(
                pCar->CarGroup.pAsRawPointer );
        for ( uint32_t i = 0; i < sizeof( kCarGroupHashcodes ) /
                                      sizeof( kCarGroupHashcodes[0] );
              i++ ) {
            if ( groupHashcode == kCarGroupHashcodes[i] ) {
                return i == condition.Value;
            }
        }
        return false;
    }
    case 2:
        return pCurrentCar != nullptr && pCurrentCar->getConfig() != nullptr &&
               StringCmpI( pCurrentCar->getConfig()->Name.c_str(),
                           condition.pValue ) == 0;
    case 3:
        return pCurrentCar != nullptr &&
               pCurrentCar->getCarConfigID() == condition.Value;
    default:
        return true;
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
