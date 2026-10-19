#include "shared.h"
#include "ai_spawn_request.h"

#include "game/vehicle/vehicle_traffic.h"

#include "ai/ai_bot_manager.h"
#include "ai/ai_oriented_box.h"
#include "ai/ai_pool.h"
#include "ai/ai_vehicle.h"
#include "ai/gs_ai.h"
#include "database/gs_database.h"
#include "game/avatar/avatar_configuration.h"
#include "game/avatar/gs_avatar.h"
#include "game/game_object.h"
#include "game/scenaric/entity/entity_avatar.h"
#include "game/scenaric/gs_scenaric.h"
#include "game/vehicle/car_state.h"
#include "game/vehicle/gs_car_state.h"
#include "game/vehicle/paint.h"
#include "game/vehicle/vehicle_base.h"
#include "filesystem/gs_file.h"
#include "physics/gs_physics.h"
#include "physics/vehicle/physics_vehicle.h"

static constexpr int32_t kMinTrafficColor = 1;
static constexpr int32_t kMaxTrafficColor = 11;

AiSpawnRequest::AiSpawnRequest()
    : kind( ASK_Traffic ),
      state( ASS_Idle ),
      pAiVehicle( nullptr ),
      pCarState( nullptr ),
      spawnMatrix( Matrix4x4f_t::Identity() ),
      roadIt(),
      lane( 0 ),
      bPolice( false ),
      botDBIndex( -1 ),
      colorIndex( 0 ),
      pParkedOwner( nullptr ),
      pBotResource( nullptr ) {
    // FUN_00921f80
}

void AiSpawnRequest::fillTraffic( AiVehicle *pVehicle,
                                  const Matrix4x4f_t &matrix,
                                  const AiRoadIt &spawnRoadIt,
                                  const int32_t laneIndex,
                                  const bool bPoliceCar ) {
    // FUN_00921e60
    if ( state != ASS_Idle ) {
        gpAI->getAIPool()->releaseVehicleSlot( pVehicle );
        return;
    }

    pAiVehicle = pVehicle;
    kind = ASK_Traffic;
    state = ASS_Ready;
    spawnMatrix = matrix;
    roadIt = spawnRoadIt;
    lane = laneIndex;
    bPolice = bPoliceCar;
    execute();
}

void AiSpawnRequest::fillParked( AiVehicle *pVehicle,
                                 const Matrix4x4f_t &matrix,
                                 const int32_t newColorIndex,
                                 EntityCarPlayer *pOwner ) {
    // FUN_00921e00
    if ( state != ASS_Idle ) {
        gpAI->getAIPool()->releaseVehicleSlot( pVehicle );
        return;
    }
    kind = ASK_Parked;
    state = ASS_Ready;
    pAiVehicle = pVehicle;
    spawnMatrix = matrix;
    colorIndex = newColorIndex;
    pParkedOwner = pOwner;
    execute();
}

bool AiSpawnRequest::fillBot( AiVehicle *pVehicle,
                              const Matrix4x4f_t &matrix,
                              const AiRoadIt &spawnRoadIt,
                              const int32_t laneIndex,
                              const int32_t botIndex ) {
    // FUN_009232c0
    if ( state != ASS_Idle ) {
        gpAI->getAIPool()->releaseVehicleSlot( pVehicle );
        return false;
    }

    pAiVehicle = pVehicle;
    kind = ASK_Bot;
    state = ASS_WaitResource;
    spawnMatrix = matrix;
    roadIt = spawnRoadIt;
    lane = laneIndex;
    bPolice = false;
    botDBIndex = botIndex;
    if ( !loadBotResource() ) {
        state = ASS_Idle;
        return false;
    }
    return true;
}

bool AiSpawnRequest::loadBotResource() {
    // FUN_009231e0
    const DBEntryBot *pBot = GetBotDBEntry( botDBIndex );
    const DBEntryCarPhysicsData *pBotCar =
        ( pBot != nullptr ) ? static_cast<const DBEntryCarPhysicsData *>(
                                  pBot->BotCar.pAsRawPointer )
                            : nullptr;
    const int32_t carID = ( pBotCar != nullptr ) ? pBotCar->CarID : -1;

    const DBEntryCarPhysicsData *pCarData = gpDatabase->getCarDBEntry( carID );
    if ( pCarData == nullptr || ( pCarData->BitField & 1 ) == 0 ||
         pCarData->Brand == nullptr || ( pCarData->Brand->Flags & 1 ) == 0 ) {
        return false;
    }

    pBotResource = gpCarState->grabFreeResourceEntry();
    if ( pBotResource != nullptr &&
         !pBotResource->load( carID, 0, 0, 0, ~0ull, 1,
                              eVehicleResourceType::VRT_Traffic ) ) {
        gpCarState->releaseResourceEntry( pBotResource );
        pBotResource = nullptr;
    }
    colorIndex = GetBotColorIndex( botDBIndex );
    return true;
}

bool AiSpawnRequest::executeBot() {
    // FUN_00922160
    pCarState = nullptr;
    if ( pBotResource != nullptr ) {
        VehicleSpawnParams params;
        params.bAddPhysicsPaused = true;
        params.PhantomMode = 1;
        pCarState =
            gpCarState->installVehicle( pBotResource, params, VIT_Opponent,
                                        pAiVehicle->getCarStateIndex() );
    }
    pBotResource = nullptr;
    if ( pCarState == nullptr ) {
        gpAI->getAIPool()->releaseVehicleSlot( pAiVehicle );
        return false;
    }
    pCarState->setAIController( pAiVehicle );
    return true;
}

void AiSpawnRequest::finalizeBot() {
    // FUN_00922c00
    pCarState->getWorldMatrix() = spawnMatrix;
    pAiVehicle->assignBotPersonality( botDBIndex );
    pAiVehicle->initialize( eAiVehicleCategory::AVC_Bot, pCarState, roadIt,
                            lane, 3, 7, 1, 1, false );
    pAiVehicle->setRemovable( false );
    pAiVehicle->getSignals().bHeadlightActivated = false;
    if ( pCarState != nullptr ) {
        installBotDriver();
    }

    gpAI->getAIPool()->addVehicle( pAiVehicle );
    if ( pCarState != nullptr && pAiVehicle != nullptr ) {
        pCarState->setAIController( pAiVehicle );
    }

    VehicleBase *pDisplay = pCarState->getVehicleInstance();
    if ( pDisplay != nullptr ) {
        pDisplay->applyPaint( static_cast<uint32_t>( pCarState->getCarID() ),
                              ePaintType::PT_Default, colorIndex, 0, 0, nullptr,
                              nullptr, nullptr, nullptr );
    }
}

void AiSpawnRequest::installBotDriver() {
    // FUN_00922c00 (driver avatar)
    static constexpr int32_t kBotAvatarType = 3;

    int32_t clotheSet = 0;
    const VehicleBaseResources *pResource = pCarState->getResource();
    if ( pResource != nullptr ) {
        DBEntryCarPhysicsData *pCarData =
            gpDatabase->getCarDBEntry( pResource->CarID );
        if ( pCarData != nullptr && gpDatabase->isBike( pCarData ) ) {
            clotheSet = 1;
        }
    }

    AvatarConfiguration config;
    const DBEntryBot *pBot = GetBotDBEntry( botDBIndex );
    const DBEntryPNJ *pPNJ =
        ( pBot != nullptr )
            ? static_cast<const DBEntryPNJ *>( pBot->PNJ.pAsRawPointer )
            : nullptr;
    if ( pPNJ == nullptr ||
         !gpAvatar->applyPNJConfiguration( config, *pPNJ ) ) {
        config = gpAvatar->getDefaultConfiguration( 0 );
    }

    AvatarManager &avatarManager = gpScenaric->getAvatarManager();
    EntityAvatar *pAvatar =
        avatarManager.allocateAvatar( ~0ull, kBotAvatarType );
    if ( pAvatar == nullptr ) {
        return;
    }

    const int32_t activeClotheSet = pAvatar->getPooledEntry()->ActiveClotheSet;
    if ( activeClotheSet == kNoClotheSet ) {
        gpAvatar->bakeAvatar( pAvatar, &config, clotheSet, true, false );
    } else if ( activeClotheSet != clotheSet ) {
        gpAvatar->installClothes( pAvatar, &config, clotheSet, true, false );
    }

    if ( !gpScenaric->placeDriver( pCarState, pAvatar->getDriver() ) ) {
        avatarManager.releaseAvatar( pAvatar );
    }
}

bool AiSpawnRequest::executeParked() {
    // FUN_009224f0
    AiPool *pPool = gpAI->getAIPool();
    AiPoolTraffic::Template *pTemplate =
        pPool->getTraffic().pickTemplateForSpawn( -1, true );
    pCarState =
        ( pTemplate != nullptr )
            ? pPool->installVehicle( pTemplate, pAiVehicle, false, spawnMatrix )
            : nullptr;
    if ( pCarState == nullptr ) {
        pPool->releaseVehicleSlot( pAiVehicle );
        return false;
    }
    return true;
}

void AiSpawnRequest::finalizeParked() {
    // FUN_009228d0
    applyRandomTrafficColor();
    {
        PhysicsLock lock;
        pCarState->getPhysicsVehicle()->resetToTransform( spawnMatrix, true );
    }
    pCarState->getWorldMatrix() = spawnMatrix;
    pAiVehicle->initializeParked( pCarState );
    gpAI->getAIPool()->addVehicle( pAiVehicle );
    if ( pCarState != nullptr && pAiVehicle != nullptr ) {
        pCarState->setAIController( pAiVehicle );
    }
    if ( pAiVehicle != nullptr ) {
        pAiVehicle->getParkedLocomotion().setOwnerEntity( pParkedOwner );
    }
}

void AiSpawnRequest::execute() {
    // FUN_00922740
    if ( state != ASS_Ready ) {
        return;
    }
    state = ASS_Executing;

    bool bSuccess = false;
    switch ( kind ) {
    case ASK_Traffic:
        bSuccess = executeTraffic();
        break;
    case ASK_Parked:
        bSuccess = executeParked();
        break;
    case ASK_Bot:
        bSuccess = executeBot();
        break;
    case ASK_Network:
    case ASK_CopDam:
        OTDU_UNIMPLEMENTED_ONCE; // TODO: FUN_00922540 / 00922490
        break;
    default:
        state = ASS_Idle;
        return;
    }

    if ( !bSuccess ) {
        state = ASS_Idle;
        return;
    }

    if ( pCarState != nullptr && pCarState->getVehicleInstance() != nullptr ) {
        pCarState->getVehicleInstance()->setShouldDraw( false );
    }
    state = ASS_WaitPhysics;
}

bool AiSpawnRequest::executeTraffic() {
    // FUN_009225e0
    AiPool *pPool = gpAI->getAIPool();
    AiPoolTraffic &traffic = pPool->getTraffic();

    pCarState = nullptr;
    if ( bPolice ) {
        AiPoolTraffic::Template *pTemplate =
            traffic.findLoadedTemplate( AiPoolTraffic::kMandatoryCarID );
        if ( pTemplate != nullptr ) {
            pCarState = pPool->installVehicle( pTemplate, pAiVehicle, false,
                                               spawnMatrix );
        }
        bPolice = ( pCarState != nullptr );
        if ( pCarState != nullptr ) {
            OTDU_UNIMPLEMENTED_ONCE;
        }
    }

    if ( pCarState == nullptr ) {
        const int32_t zone = roadIt.getEntity()->DensityZoneIndice;
        AiPoolTraffic::Template *pTemplate =
            traffic.pickTemplateForSpawn( zone, false );
        if ( pTemplate != nullptr ) {
            pCarState = pPool->installVehicle( pTemplate, pAiVehicle, false,
                                               spawnMatrix );
        }
    }

    if ( pCarState == nullptr ) {
        pPool->releaseVehicleSlot( pAiVehicle );
        return false;
    }

    pCarState->getWorldMatrix() = spawnMatrix;
    Matrix4x4f_t boundingBox;
    pCarState->retrieveBoundingBox( boundingBox );

    for ( GameObject *pObject = gGameObjectDico.getFirstObject();
          pObject != nullptr; pObject = pObject->getNextInDico() ) {
        if ( pObject->getID() == pCarState->getID() ||
             pObject->getParentID() == pCarState->getID() ) {
            continue;
        }

        Matrix4x4f_t otherBox;
        pObject->retrieveBoundingBox( otherBox );
        if ( TestOrientedBoxesOverlap( boundingBox, otherBox ) ) {
            if ( pAiVehicle->getTemplate() != nullptr ) {
                pPool->releaseTemplateCar( pAiVehicle );
            }
            pPool->releaseVehicleSlot( pAiVehicle );
            pCarState = nullptr;
            return false;
        }
    }
    return true;
}

void AiSpawnRequest::tick( const bool bCancel ) {
    // FUN_00922ee0
    switch ( state ) {
    case ASS_WaitResource:
        if ( pBotResource != nullptr && pBotResource->isLoaded() ) {
            state = ASS_Ready;
            execute();
        } else if ( pBotResource == nullptr ) {
            gpAI->getAIPool()->releaseVehicleSlot( pAiVehicle );
            state = ASS_Idle;
        }
        break;
    case ASS_WaitPhysics:
        if ( bCancel ) {
            gpAI->getAIPool()->addVehicle( pAiVehicle );
            state = ASS_Idle;
        } else {
            state = ASS_Finalized;
            finalize();
        }
        break;
    case ASS_Finalized:
        if ( pCarState != nullptr &&
             pCarState->getVehicleInstance() != nullptr ) {
            pCarState->getVehicleInstance()->setShouldDraw( true );
        }
        state = ASS_Idle;
        break;
    default:
        break;
    }
}

void AiSpawnRequest::flush() {
    // FUN_00922f80
    while ( state != ASS_Idle ) {
        if ( state == ASS_WaitResource ) {
            if ( pBotResource != nullptr ) {
                while ( !pBotResource->isLoaded() ) {
                    gpFile->tick( 0.0f, 0.0f );
                }
                pBotResource->unload();
                gpCarState->releaseResourceEntry( pBotResource );
                pBotResource = nullptr;
            }
            gpAI->getAIPool()->releaseVehicleSlot( pAiVehicle );
            state = ASS_Idle;
            break;
        }
        tick( true );
    }
}

void AiSpawnRequest::finalize() {
    // FUN_00922e90
    switch ( kind ) {
    case ASK_Traffic:
        finalizeTraffic();
        break;
    case ASK_Parked:
        finalizeParked();
        break;
    case ASK_Bot:
        finalizeBot();
        break;
    default:
        OTDU_UNIMPLEMENTED_ONCE; // TODO: FUN_00922b50 / 009227d0
        break;
    }
}

void AiSpawnRequest::finalizeTraffic() {
    // FUN_009229c0
    if ( !bPolice ) {
        applyRandomTrafficColor();
    }
    pCarState->getWorldMatrix() = spawnMatrix;

    if ( bPolice ) {
        pAiVehicle->initialize( eAiVehicleCategory::AVC_Cop, pCarState, roadIt,
                                lane, 0, 0, 0, 0, false );
        OTDU_UNIMPLEMENTED_ONCE; // TODO: police
    } else {
        pAiVehicle->initialize( eAiVehicleCategory::AVC_Traffic, pCarState,
                                roadIt, lane, 2, 0, 0, 0, false );
    }

    OTDU_UNIMPLEMENTED; // TODO: ONLINE

    gpAI->getAIPool()->addVehicle( pAiVehicle );
    if ( pCarState != nullptr && pAiVehicle != nullptr ) {
        pCarState->setAIController( pAiVehicle );
    }
}

void AiSpawnRequest::applyRandomTrafficColor() {
    // FUN_00921ff0
    RandomGen &random = gpAI->getAIPool()->getRandom();
    const int32_t carID = pCarState->getCarID();

    int32_t color = 0;
    bool bHasColor = false;
    const int32_t numColors = gpDatabase->getNumColors( carID );
    if ( numColors > 0 ) {
        const int32_t index = RandomRange( random, 0, numColors - 1 );
        const DBEntryCarColor *pColor =
            gpDatabase->getColorByIndex( carID, index );
        const char *pName =
            ( pColor != nullptr ) ? pColor->ColorID1.pAsString : nullptr;
        if ( pName != nullptr && strlen( pName ) >= 2 ) {
            color = std::clamp( atoi( pName + strlen( pName ) - 2 ),
                                kMinTrafficColor, kMaxTrafficColor );
            bHasColor = true;
        }
    }
    if ( !bHasColor ) {
        color = RandomRange( random, kMinTrafficColor, kMaxTrafficColor );
    }

    char colorName[32];
    snprintf( colorName, sizeof( colorName ), "TR_%02d", color );
    VehicleBase *pDisplay = pCarState->getVehicleInstance();
    if ( pDisplay != nullptr ) {
        pDisplay->setVehiclePartColor( CCP_BodyColor, colorName );
    }
    pCarState->colorID = color;

    if ( pDisplay != nullptr &&
         pDisplay->getType() == VehicleBase::eType::VBT_TrafficCar ) {
        static_cast<VehicleTraffic *>( pDisplay )
            ->setLogoVariant( RandomRange( random, 0, 2 ) );
    }
}
