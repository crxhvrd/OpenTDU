#include "shared.h"
#include "entity_car_player.h"

#include "ai/ai_pool.h"
#include "ai/ai_vehicle.h"
#include "ai/gs_ai.h"
#include "database/gs_database.h"
#include "filesystem/binary.h"
#include "game/scenaric/gs_scenaric.h"
#include "game/game_mechanism.h"
#include "game/vehicle/car_state.h"
#include "game/vehicle/gs_brand_car.h"
#include "game/freeride/mng_map_screenshot.h"
#include "physics/vehicle/physics_vehicle.h"

// Squared distances the original spawns / despawns a parked vehicle at
static constexpr float kSpawnDistanceSq = 40000.0f;   // DAT_00f41c78
static constexpr float kDespawnDistanceSq = 62500.0f; // DAT_00f41cb4

// A car-position descriptor only marks a spawn point when its second
// serialized dword carries this tag.
static constexpr uint32_t kSpawnPointTag = 5;

EntityCarPlayer::EntityCarPlayer()
    : Entity(),
      Index( 0 ),
      pData( nullptr ),
      Value( 0 ),
      SpawnKind( SK_None ),
      pParkedVehicle( nullptr ),
      bBrandCarSpawned( false ) {
    // FUN_00423f50
}

EntityCarPlayer::~EntityCarPlayer() {
    // FUN_00424120
}

void EntityCarPlayer::deserialize( BinaryReader *param_2 ) {
    // FUN_009c7c00
    Entity::deserialize( param_2 );

    pData = param_2->pCursor;
    param_2->pCursor += 8;
    Value = *reinterpret_cast<uint32_t *>( pData );
}

void EntityCarPlayer::instanciate( MngActor *pMngActor, Entity *param_1 ) {
    // FUN_009c7bd0
    Entity::instanciate( pMngActor, param_1 );
    static_cast<EntityCarPlayer *>( param_1 )->pData = pData;
}

void EntityCarPlayer::initialize() {
    // FUN_009c7cc0
    if ( pData != nullptr &&
         *reinterpret_cast<uint32_t *>( pData + 4 ) == kSpawnPointTag &&
         gpDatabase != nullptr ) {
        DBEntryCarPhysicsData *pCarEntry = gpDatabase->getCarDBEntry( Value );
        if ( pCarEntry == nullptr || pCarEntry->Brand == nullptr ) {
            SpawnKind = SK_ParkedTraffic;
        } else {
            const char *pTrafficBrand =
                gpDatabase->getStringByHashcode( 6, 0x3540681 );
            SpawnKind = ( pTrafficBrand != pCarEntry->Brand->BrandID.pAsString )
                            ? SK_BrandCar
                            : SK_ParkedTraffic;
        }
    }

    OTDU_LOG_DEBUG(
        "CarPos '%s': pData=%p tag=%u value=%u db=%p kind=%d\n", name.c_str(),
        (void *)pData,
        pData != nullptr ? *reinterpret_cast<uint32_t *>( pData + 4 ) : 0u,
        Value, (void *)gpDatabase, (int32_t)SpawnKind );

    setTransformFromDesc();

    if ( pMechanism != nullptr ) {
        pMechanism->setEntity( this );
    }
}

static constexpr float kParkedRemovalDelay = -1.0f; // DAT_00f41664

static bool IsParkingSimulationActive( CarState *pPlayerCar ) {
    // FUN_009c7dc0 (inlined gate)
    const PhysicsVehicle *pPhysics = pPlayerCar->getPhysicsVehicle();
    if ( pPhysics != nullptr && !pPhysics->isPaused() ) {
        return true;
    }
    return gpMngMapScreenshot != nullptr && gpMngMapScreenshot->isCapturing();
}

static float ComputeDistanceSqXZ( const Eigen::Vector4f &position,
                                  const Matrix4x4f_t &matrix ) {
    // FUN_00425a60
    const float dx = position.x() - matrix( 3, 0 );
    const float dz = position.z() - matrix( 3, 2 );
    return dx * dx + dz * dz;
}

void EntityCarPlayer::unloadResources() {
    // FUN_009c7d40
    if ( bBrandCarSpawned ) {
        if ( gpBrandCar != nullptr ) {
            gpBrandCar->removeCar( this );
        }
        bBrandCarSpawned = false;
    }

    if ( pParkedVehicle == nullptr ) {
        return;
    }
    if ( pParkedVehicle->getLocomotionType() == 2 &&
         pParkedVehicle->getLocomotion() != nullptr ) {
        pParkedVehicle->getParkedLocomotion().setOwnerEntity( nullptr );
    }
    pParkedVehicle->setRemovable( true );
    pParkedVehicle->requestRemoval( kParkedRemovalDelay,
                                    "parked vehicle ScCarPos Term" );
    pParkedVehicle = nullptr;
}

void EntityCarPlayer::tick( PlayerGameMech *pGameMech,
                            float deltaTime,
                            float totalTime ) {
    // FUN_009c7dc0
    Entity::tick( pGameMech, deltaTime, totalTime );

    if ( SpawnKind == SK_None || gpScenaric == nullptr ) {
        return;
    }

    CarState *pPlayerCar = gpScenaric->getLocalPlayer().getCarState();
    if ( pPlayerCar == nullptr || !IsParkingSimulationActive( pPlayerCar ) ) {
        return;
    }

    const Matrix4x4f_t spawn = getWorldMatrix();
    const float distanceSq =
        ComputeDistanceSqXZ( pPlayerCar->getWorldPosition(), spawn );

    if ( SpawnKind == SK_ParkedTraffic ) {
        if ( pParkedVehicle == nullptr ) {
            if ( distanceSq < kSpawnDistanceSq && gpAI != nullptr ) {
                pParkedVehicle =
                    gpAI->getAIPool()->requestParked( spawn, -1, this );
            }
        } else if ( distanceSq > kDespawnDistanceSq &&
                    pParkedVehicle->getLocomotion() != nullptr ) {
            pParkedVehicle->setRemovable( true );
            pParkedVehicle->requestRemoval( kParkedRemovalDelay,
                                            "parked vehicle too far" );
            pParkedVehicle = nullptr;
        }
        return;
    }

    if ( gpBrandCar == nullptr ) {
        return;
    }
    if ( !bBrandCarSpawned ) {
        if ( distanceSq < kSpawnDistanceSq ) {
            gpBrandCar->addCar( this, static_cast<int32_t>( Value ), spawn );
            bBrandCarSpawned = true;
        }
    } else if ( distanceSq > kDespawnDistanceSq ) {
        gpBrandCar->removeCar( this );
        bBrandCarSpawned = false;
    }
}
