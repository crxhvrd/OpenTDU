#include "shared.h"
#include "ai_spawn_request.h"

#include "game/vehicle/vehicle_traffic.h"

#include "ai/ai_oriented_box.h"
#include "ai/ai_pool.h"
#include "ai/ai_vehicle.h"
#include "ai/gs_ai.h"
#include "database/gs_database.h"
#include "game/game_object.h"
#include "game/vehicle/car_state.h"
#include "game/vehicle/paint.h"
#include "game/vehicle/vehicle_base.h"

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
      colorIndex( 0 ) {
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
    case ASK_Network:
    case ASK_Parked:
    case ASK_CopDam:
    case ASK_Bot:
        OTDU_UNIMPLEMENTED_ONCE; // TODO: FUN_00922540 / 009224f0 / 00922490 /
                                 // 00922160
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
        OTDU_UNIMPLEMENTED_ONCE; // TODO: bot resource (kind 4)
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
            OTDU_UNIMPLEMENTED_ONCE; // TODO: bot resource streaming wait
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
    default:
        OTDU_UNIMPLEMENTED_ONCE; // TODO: FUN_00922b50 / 009228d0 / 009227d0 /
                                 // 00922c00
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
