#include "shared.h"
#include "gs_brand_car.h"

#include "car_state.h"
#include "gs_car_display.h"
#include "gs_car_state.h"
#include "vehicle_base.h"

#include "bank_vehicle.h"

#include "database/db_aftermarket_pack.h"
#include "database/gs_database.h"
#include "filesystem/gs_file.h"
#include "game/scenaric/entity/entity_car_player.h"
#include "physics/gs_physics.h"
#include "physics/vehicle/physics_vehicle.h"
#include "world/gs_world_streaming.h"
#include "tdu_instance.h"

GSBrandCar *gpBrandCar = nullptr; // DAT_00fa7130

GSBrandCar::GSBrandCar() : GameSystem(), bSuspended( false ) {
    // FUN_009de770
    gpBrandCar = this;
}

GSBrandCar::~GSBrandCar() {}

bool GSBrandCar::initialize( TestDriveGameInstance * ) {
    // FUN_008b56a0
    bSuspended = false;
    return true;
}

void GSBrandCar::terminate() {
    // FUN_008b5630
}

void GSBrandCar::pause() {
    // FUN_008b5960
    for ( BrandCar &car : cars ) {
        car.destroy();
    }
}

void GSBrandCar::tick( float, float ) {
    // FUN_008b5640
    for ( BrandCar &car : cars ) {
        if ( car.state == BrandCar::IS_Done ) {
            if ( car.pCarState != nullptr &&
                 car.pCarState->getPhysicsVehicle() == nullptr ) {
                car.refreshTransform();
            }
            continue;
        }
        if ( car.state != BrandCar::IS_ToDo || car.pResource == nullptr ) {
            continue;
        }
        if ( !car.pResource->bInitialized ) {
            continue;
        }
        car.state = BrandCar::IS_WaitingCallback;
        if ( gpTestDriveInstance != nullptr ) {
            gpTestDriveInstance->pushResourceToStream( &car );
        }
    }
}

void GSBrandCar::addCar( EntityCarPlayer *pSpawnPoint,
                         int32_t carID,
                         const Matrix4x4f_t &spawn ) {
    // FUN_008b59b0
    DBEntryCarPhysicsData *pDiag =
        gpDatabase != nullptr ? gpDatabase->getCarDBEntry( carID ) : nullptr;
    OTDU_LOG_DEBUG( "addCar %d: suspended=%d db=%p entry=%p bitField=%08X "
                    "brand=%p brandFlags=%08X\n",
                    carID, (int32_t)bSuspended, (void *)gpDatabase,
                    (void *)pDiag, pDiag != nullptr ? pDiag->BitField : 0,
                    pDiag != nullptr ? (void *)pDiag->Brand : nullptr,
                    ( pDiag != nullptr && pDiag->Brand != nullptr )
                        ? pDiag->Brand->Flags
                        : 0 );

    if ( bSuspended || pSpawnPoint == nullptr || gpDatabase == nullptr ) {
        return;
    }

    // Only cars that are both in the database and sold by a shipped brand get a
    // showcase model.
    DBEntryCarPhysicsData *pCarEntry = gpDatabase->getCarDBEntry( carID );
    if ( pCarEntry == nullptr || ( pCarEntry->BitField & 1 ) == 0 ||
         pCarEntry->Brand == nullptr || ( pCarEntry->Brand->Flags & 1 ) == 0 ) {
        return;
    }

    BrandCar *pFreeSlot = nullptr;
    for ( BrandCar &car : cars ) {
        if ( car.pSpawnPoint == pSpawnPoint ) {
            return; // already spawned for this spawn point
        }
        if ( pFreeSlot == nullptr && car.pSpawnPoint == nullptr ) {
            pFreeSlot = &car;
        }
    }

    if ( pFreeSlot == nullptr ) {
        OTDU_LOG_WARN( "No free brand car slot for car %d (%d in flight)\n",
                       carID, kMaxNumBrandCar );
        return;
    }

    OTDU_LOG_DEBUG( "Brand car %d requested at (%.1f, %.1f, %.1f)\n", carID,
                    spawn( 3, 0 ), spawn( 3, 1 ), spawn( 3, 2 ) );

    pFreeSlot->load( pSpawnPoint, carID, 0, spawn );
}

void GSBrandCar::removeCar( EntityCarPlayer *pSpawnPoint ) {
    // FUN_008b5920
    for ( BrandCar &car : cars ) {
        if ( car.pSpawnPoint == pSpawnPoint ) {
            car.destroy();
            return;
        }
    }
}

bool BrandCar::load( EntityCarPlayer *pSpawnPoint_,
                     int32_t carID,
                     int32_t tuneID,
                     const Matrix4x4f_t &spawn ) {
    // FUN_008b2970
    if ( gpCarState == nullptr ) {
        return false;
    }

    VehicleBaseResources *pRes = gpCarState->grabFreeResourceEntry();
    const bool bLoaded =
        pRes != nullptr && pRes->load( carID, -1, 1, 0x10, 0, tuneID,
                                       eVehicleResourceType::VRT_Traffic );
    if ( !bLoaded ) {
        if ( pRes != nullptr ) {
            gpCarState->releaseResourceEntry( pRes );
        }
        pRes = nullptr;
    }

    pResource = pRes;
    pCarState = nullptr;
    spawnMatrix = spawn;
    if ( pRes != nullptr ) {
        pSpawnPoint = pSpawnPoint_;
    }
    state = IS_ToDo;

    return pRes != nullptr;
}

void BrandCar::instanciate() {
    // FUN_008b2610
    markAsInstanciated();

    VehicleBaseResources *pRes = pResource;
    CarState *pSpawned = nullptr;

    if ( pRes != nullptr && gpCarState != nullptr && gpCarDisplay != nullptr &&
         gpDatabase != nullptr ) {
        CarState &carState = gpCarState->grabNextFreeCarState();
        carState.reset();
        carState.owner = CarState::eOwner::CSO_Bot;
        carState.simulationType = CarState::eSimulationType::CSST_Local;
        carState.bUseShowcaseControl = false;
        carState.carID = pRes->CarID;
        carState.tireID = pRes->TireID;
        carState.rimID = pRes->RimID;
        carState.body.BackOnTrackState = 0;
        carState.body.resetBackOnTrack();

        DBEntryCarPhysicsData *pCarEntry =
            gpDatabase->getCarDBEntry( pRes->CarID );
        if ( pCarEntry == nullptr ) {
            pCarEntry = gpDatabase->getCarDBEntry( 999 );
        } else if ( ( pCarEntry->BitField & 1 ) == 0 ||
                    pCarEntry->Brand == nullptr ||
                    ( pCarEntry->Brand->Flags & 1 ) == 0 ) {
            const char *pBikeCategory =
                gpDatabase->getStringByHashcode( 1, 0x35da5b5 );
            const uint32_t fallbackID =
                ( pBikeCategory == pCarEntry->Category.pAsString ) ? 666 : 999;
            pCarEntry = gpDatabase->getCarDBEntry( fallbackID );
        }

        VehicleController *pVehicle = nullptr;
        if ( gpDatabase->isBike( pCarEntry ) ) {
            pVehicle = gpCarDisplay->instantiateBike(
                &carState, &pRes->Exterior, &pRes->Interior, &pRes->FrontRims,
                &pRes->RearRims );
        } else {
            pVehicle = gpCarDisplay->instantiateCar(
                &carState, &pRes->Exterior, &pRes->Interior, &pRes->FrontRims,
                &pRes->RearRims );
        }

        if ( pVehicle != nullptr ) {
            PhysicsVehicle *pPhysicsVehicle = nullptr;
            if ( pRes->bUseGamePhysics != 0 && gpPhysics != nullptr ) {
                DBEntryCarPhysicsData *pPhysEntry = pCarEntry;
                const DBEntryAftermarketPack *pPack =
                    gpDatabase->getAftermarketPack( pRes->PackHashcode );
                if ( carState.carID == 999 ) {
                    pPhysEntry = gpDatabase->getCarDBEntry( 223 );
                    pPack = nullptr;
                } else if ( carState.carID == 666 ) {
                    pPhysEntry = gpDatabase->getCarDBEntry( 1502 );
                    pPack = nullptr;
                }

                BankVehicle *pBank = pRes->Exterior.pBankInstance;
                PhysFactory *pFactory =
                    ( pBank != nullptr ) ? pBank->getPhysFactory() : nullptr;
                const void *pTorqueBinary =
                    ( pBank != nullptr ) ? pBank->getTorqueBinary() : nullptr;
                if ( pPhysEntry != nullptr ) {
                    pPhysicsVehicle = gpPhysics->createVehicle(
                        pFactory, spawnMatrix, *pPhysEntry, pTorqueBinary,
                        pPack, true, false );
                }
            }

            carState.pVehicleBase = pVehicle;
            carState.setPhysicsVehicle( pPhysicsVehicle );
            carState.bIsIndoor = false;
            carState.pResource = pRes;
            pRes->RefCount++;

            if ( pPhysicsVehicle != nullptr ) {
                PhysicsLock lock;
                pPhysicsVehicle->resetToTransform( spawnMatrix, true );
            }

            pSpawned = &carState;
        }
    }

    pCarState = pSpawned;

    if ( pSpawned != nullptr ) {
        refreshTransform();
        gpCarState->registerCallback( pSpawned, pSpawned->pVehicleBase,
                                      nullptr );
    }

    OTDU_LOG_DEBUG( "Brand car %d %s\n", pRes != nullptr ? pRes->CarID : -1,
                    pSpawned != nullptr ? "instanciated"
                                        : "FAILED to instanciate" );

    if ( pSpawned == nullptr ) {
        if ( pResource != nullptr ) {
            pResource->unload();
            gpCarState->releaseResourceEntry( pResource );
            pResource = nullptr;
        }
        pSpawnPoint = nullptr;
        state = IS_None;
        return;
    }

    state = IS_Done;
}

void BrandCar::refreshTransform() {
    if ( pCarState == nullptr ) {
        return;
    }

    Eigen::Vector4f shift = gpWorldStreaming->isEnabled()
                                ? gpWorldStreaming->getPosition()
                                : Eigen::Vector4f::Zero();
    shift.w() = 0.0f;

    Matrix4x4f_t renderMatrix = spawnMatrix;
    renderMatrix.row( 3 ) -= shift;
    renderMatrix( 3, 3 ) = 1.0f;

    pCarState->body.WorldMatrix = spawnMatrix;
    pCarState->body.OffsetedWorldMatrix = renderMatrix;
}

void BrandCar::destroy() {
    // FUN_008b2570
    if ( state == IS_WaitingCallback && gpFile != nullptr ) {
        gpFile->flushPendingAsync();
    }

    if ( pCarState != nullptr ) {
        VehicleBaseResources *pRes = pCarState->getResource();
        if ( gpCarState != nullptr ) {
            gpCarState->releaseCarState( pCarState, false );
            if ( pRes != nullptr ) {
                pRes->unload();
                gpCarState->releaseResourceEntry( pRes );
            }
        }
        pCarState = nullptr;
        pResource = nullptr;
        pSpawnPoint = nullptr;
        state = IS_None;
        return;
    }

    if ( pResource != nullptr ) {
        // The load may still be in flight; drain it before tearing down.
        if ( !pResource->bInitialized && gpFile != nullptr ) {
            gpFile->flushPendingAsync();
        }
        pResource->unload();
        if ( gpCarState != nullptr ) {
            gpCarState->releaseResourceEntry( pResource );
        }
        pResource = nullptr;
    }

    pSpawnPoint = nullptr;
    state = IS_None;
}
