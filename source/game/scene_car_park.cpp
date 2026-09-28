#include "shared.h"
#include "scene_car_park.h"

#include "audio/vehicle/gs_vehicle_sound.h"
#include "config/gs_config.h"
#include "database/gs_database.h"
#include "filesystem/gs_file.h"
#include "flash/gs_flash.h"
#include "flash/implementation/flash_player.h"
#include "game/gm_scene.h"
#include "game/scenaric/gs_scenaric.h"
#include "game/vehicle/bank_vehicle.h"
#include "game/vehicle/car_desc.h"
#include "game/vehicle/car_state.h"
#include "game/vehicle/gs_car_display.h"
#include "game/vehicle/gs_car_state.h"
#include "game/vehicle/vehicle_base.h"
#include "game/vehicle/vehicle_controller.h"
#include "physics/gs_physics.h"
#include "physics/vehicle/physics_vehicle.h"
#include "render/camera/camera_vehicle.h"
#include "render/camera/gs_camera.h"

static constexpr const char *kCarDealerBank =
    "scenarics/Cinematics/car_dealer.bnk";
static constexpr float kFadeInSpeed = 4.0f;            // FLOAT_00fab924
static constexpr float kEnterCarTransitionTime = 1.0f; // FLOAT_00f4165c
static constexpr float kSteerColumnRake = 0.26f;
static constexpr float kWheelOffsetScale = 0.0007874016f; // DAT_00f41758
static constexpr float kWheelOffsetBias = 0.1f;           // FLOAT_00f41668

static void SetInnerViewLocked( const bool bLocked ) {
    MoviePlayer *pMoviePlayer = gpFlash->getMoviePlayer( 1, 0 );
    if ( pMoviePlayer == nullptr || !pMoviePlayer->MovieBank.isLoaded() ||
         pMoviePlayer->MovieBank.getFlashPlayer() == nullptr ) {
        return;
    }

    pMoviePlayer->MovieBank.getFlashPlayer()->setVariableValue(
        "/:lockInnerView", bLocked ? 1 : 0 );
}

static inline bool ListContains( const int32_t *pList, const int32_t value ) {
    // FUN_00776140
    for ( int32_t i = 0; i < SceneCarPark::kNumStreamedCars; i++ ) {
        if ( pList[i] == value ) {
            return true;
        }
    }
    return false;
}

static inline bool ListIsFull( const int32_t *pList ) {
    // FUN_007760e0
    for ( int32_t i = 0; i < SceneCarPark::kNumStreamedCars; i++ ) {
        if ( pList[i] == -1 ) {
            return false;
        }
    }
    return true;
}

static inline void ListAdd( int32_t *pList, const int32_t value ) {
    // FUN_00776160
    if ( ListContains( pList, value ) ) {
        return;
    }

    for ( int32_t i = 0; i < SceneCarPark::kNumStreamedCars; i++ ) {
        if ( pList[i] == -1 ) {
            pList[i] = value;
            return;
        }
    }
}

static inline VehicleController *GetDisplayedVehicle( VehicleBase *pVehicle ) {
    if ( pVehicle == nullptr ||
         ( pVehicle->getType() != VehicleBase::eType::VBT_Car &&
           pVehicle->getType() != VehicleBase::eType::VBT_Bike ) ) {
        return nullptr;
    }
    return static_cast<VehicleController *>( pVehicle );
}

static inline HiearchyNode *FindSteeringWheel( VehicleController *pVehicle ) {
    if ( pVehicle == nullptr ) {
        return nullptr;
    }

    HiearchyNode *pInterior = pVehicle->getInstantiatedInteriorNode();
    return ( pInterior != nullptr ) ? pInterior->findChild( "Steer_HR" )
                                    : nullptr;
}

static inline float GetSteeringWheelOffset( CameraGame *pCamera ) {
    return float( static_cast<CameraVehicle *>( pCamera )->getWheelOffset() ) *
               kWheelOffsetScale -
           kWheelOffsetBias;
}

static void OffsetSteeringWheel( HiearchyNode *pSteer, const float offset ) {
    Matrix4x4f_t steerMatrix = MakeRotationMatX( kSteerColumnRake );
    steerMatrix( 3, 0 ) = pSteer->TransformMatrix( 3, 0 );
    steerMatrix( 3, 1 ) = pSteer->TransformMatrix( 3, 1 ) + offset;
    steerMatrix( 3, 2 ) = pSteer->TransformMatrix( 3, 2 );
    pSteer->TransformMatrix = steerMatrix;

    HiearchyNode *pRoot = pSteer;
    while ( pRoot->pParent != nullptr ) {
        pRoot = pRoot->pParent;
    }

    pRoot->pInstance->setModelMatrix( pRoot->TransformMatrix );
    pRoot->pInstance->calculateBiggestScale();
    pRoot->pInstance->calculateDeterminant();
    pRoot->propagateTransformMatrices();
}

SceneCarPark::SceneCarPark()
    : capacity( kMaxNumCars ),
      numEntries( 0 ),
      head( kMaxNumCars - 1 ),
      tail( 0 ),
      pEntries( nullptr ),
      pendingStreamIndex( -1 ),
      pPendingCar( nullptr ),
      pendingCarIndex( -1 ),
      pendingInnerViewState( CPIV_Ready ),
      pCarStates{},
      pResources{},
      fadeIn{},
      bEnabled( false ),
      bLoaded( false ),
      bBankLoaded( false ),
      pEnvMapFront( nullptr ),
      pEnvMapBack( nullptr ),
      dealerBankFile(),
      dealerBank(),
      enteredCarState( 0 ),
      enteredCarIndex( 0 ),
      steeringWheelOffset( FLT_MIN ) {
    // FUN_00776c80
    pEntries = static_cast<Entry *>(
        TestDrive::AllocAligned( sizeof( Entry ) * kMaxNumCars, 16 ) );
}

SceneCarPark::~SceneCarPark() {
    // FUN_007777e0
    if ( bLoaded ) {
        releaseAll();
    }

    unloadBank();

    TestDrive::FreeAligned( pEntries );
    pEntries = nullptr;
    numEntries = 0;
    capacity = 0;
}

void SceneCarPark::setEnabled( const bool bEnable ) {
    // FUN_007764d0
    if ( bEnabled == bEnable ) {
        return;
    }

    bEnabled = bEnable;
    for ( int32_t i = 0; i < numEntries; i++ ) {
        if ( pCarStates[i] != nullptr ) {
            OTDU_UNIMPLEMENTED; // TODO: CAR PARK
        }
    }
}

VehicleBase *SceneCarPark::getVehicle( const int32_t index ) const {
    // FUN_00776480
    CarState *pCarState = getCarState( index );
    return ( pCarState != nullptr ) ? pCarState->getVehicleInstance() : nullptr;
}

void SceneCarPark::push( const Entry &entry ) {
    // FUN_00408bd0
    if ( numEntries >= capacity ) {
        return;
    }

    pEntries[tail] = entry;
    numEntries++;
    tail++;
    if ( tail >= capacity ) {
        tail = 0;
    }
}

void SceneCarPark::pushConfig( const int32_t configID ) {
    // FUN_007765c0
    if ( bLoaded || numEntries == capacity ) {
        return;
    }

    int32_t rimID = -1;
    if ( configID != -1 ) {
        OTDU_UNIMPLEMENTED; // TODO: CAR PARK
    }

    Entry entry;
    entry.pCar = nullptr;
    entry.ConfigID = configID;
    entry.PaintIndex = -1;
    entry.RimID = rimID;
    entry.InteriorID = -1;
    entry.bStreamed = false;
    entry.Transform = Matrix4x4f_t::Identity();
    entry.field_0x60 = 0.0f;
    entry.PlacementState = CPPS_None;
    entry.InnerViewState = CPIV_Unloaded;
    push( entry );
}

void SceneCarPark::pushCar( CarDesc *pCar ) {
    // FUN_00776be0
    if ( bLoaded || numEntries == capacity ) {
        return;
    }

    Entry entry;
    entry.pCar = pCar;
    entry.ConfigID = int32_t( pCar->getCarConfigID() );
    entry.PaintIndex = pCar->paintIndex;
    entry.RimID = pCar->rimID;
    entry.InteriorID = pCar->interiorID;
    entry.bStreamed = false;
    entry.Transform = Matrix4x4f_t::Identity();
    entry.field_0x60 = 0.0f;
    entry.PlacementState = CPPS_None;
    entry.InnerViewState = CPIV_Unloaded;
    push( entry );
}

void SceneCarPark::releaseResource( const int32_t index ) {
    VehicleBaseResources *pResource = pResources[index];
    pResource->unload();
    gpCarState->releaseResourceEntry( pResource );

    if ( !pResource->isLoaded() ) {
        pResources[index] = nullptr;
    }
}

void SceneCarPark::releaseAll() {
    // FUN_007776e0
    if ( !bLoaded ) {
        return;
    }

    for ( int32_t i = 0; i < numEntries; i++ ) {
        if ( pCarStates[i] != nullptr ) {
            OTDU_UNIMPLEMENTED_ONCE; // TODO: PHYSICS
            gpCarState->releaseCarState( pCarStates[i], false );
            pCarStates[i] = nullptr;
        }

        if ( pResources[i] != nullptr && pResources[i]->isLoaded() ) {
            releaseResource( i );
        }
    }

    numEntries = 0;
    head = capacity - 1;
    tail = 0;
    bLoaded = false;

    OTDU_UNIMPLEMENTED_ONCE; // TODO: PHYSICS

    pEnvMapFront = nullptr;
    pEnvMapBack = nullptr;

    unloadBank();
}

void SceneCarPark::loadBank() {
    // FUN_007778a0
    if ( bBankLoaded ) {
        return;
    }

    if ( !dealerBankFile.isLoaded() ) {
        char bankPath[512] = {};
        snprintf( bankPath, sizeof( bankPath ), "%s%s",
                  gpConfig->getResRootPath(), kCarDealerBank );

        if ( dealerBankFile.loadBank( gpFile, bankPath ) ) {
            dealerBankFile.loadContentAsync();
        }
    }

    if ( !dealerBankFile.isLoaded() ) {
        return;
    }

    MngActor *pMngActor = ( gpActiveGMScene != nullptr )
                              ? &gpActiveGMScene->getActorManager()
                              : nullptr;

    gpScenaric->registerBank( &dealerBank );
    dealerBank.load( dealerBankFile.getBankFile(), 0, 0, pMngActor,
                     pMngActor != nullptr );
    dealerBank.forceLoad();

    bBankLoaded = true;
}

void SceneCarPark::unloadBank() {
    // FUN_007774f0
    if ( !bBankLoaded ) {
        return;
    }

    gpScenaric->unloadBank( &dealerBank );
    dealerBankFile.unload();

    bBankLoaded = false;
}

bool SceneCarPark::streamList( const int32_t *pList ) {
    // FUN_00778460
    int32_t numPending = 0;
    for ( int32_t i = 0; i < numEntries; i++ ) {
        Entry &entry = getEntry( i );
        if ( entry.ConfigID == -1 || ListContains( pList, i ) ||
             !entry.bStreamed ) {
            continue;
        }

        if ( pCarStates[i] != nullptr ) {
            gpCarState->releaseCarState( pCarStates[i], false );
            pCarStates[i] = nullptr;
        }

        if ( pResources[i] == nullptr || !pResources[i]->isLoaded() ) {
            numPending++;
            continue;
        }

        releaseResource( i );

        if ( entry.PlacementState == CPPS_Placed ) {
            entry.PlacementState = CPPS_Pending;
        }
        entry.InnerViewState = CPIV_Unloaded;
        entry.bStreamed = false;
    }

    if ( numPending != 0 ) {
        return true;
    }

    for ( int32_t i = 0; i < numEntries; i++ ) {
        Entry &entry = getEntry( i );
        if ( entry.ConfigID == -1 || !ListContains( pList, i ) ||
             entry.bStreamed ) {
            continue;
        }

        const int32_t rimID =
            ( entry.pCar != nullptr ) ? entry.pCar->rimID : entry.RimID;

        VehicleBaseResources *pResource = gpCarState->grabFreeResourceEntry();
        if ( pResource != nullptr &&
             !pResource->load( entry.ConfigID, rimID, 0, 0x10,
                               0xffffffffffffffffull, 1,
                               eVehicleResourceType::VRT_Traffic ) ) {
            gpCarState->releaseResourceEntry( pResource );
            pResource = nullptr;
        }

        pResources[i] = pResource;
        entry.bStreamed = true;
    }

    return false;
}

void SceneCarPark::streamAround( const int32_t index ) {
    // FUN_00778730
    int32_t streamedCars[kNumStreamedCars] = { -1, -1, -1, -1, -1, -1, -1, -1 };
    ListAdd( streamedCars, index );

    int32_t forward = index;
    int32_t backward = index;
    while ( true ) {
        forward++;
        if ( forward >= numEntries ) {
            forward = 0;
        }
        if ( getEntry( forward ).ConfigID != -1 ) {
            ListAdd( streamedCars, forward );
        }
        if ( backward == forward || ListIsFull( streamedCars ) ) {
            break;
        }

        backward--;
        if ( backward < 0 ) {
            backward = numEntries - 1;
        }
        if ( getEntry( backward ).ConfigID != -1 ) {
            ListAdd( streamedCars, backward );
        }
        if ( backward == forward || ListIsFull( streamedCars ) ) {
            break;
        }
    }

    pendingStreamIndex = streamList( streamedCars ) ? index : -1;
    commit();
}

void SceneCarPark::commit() {
    // FUN_00777590
    for ( int32_t i = 0; i < numEntries; i++ ) {
        Entry &entry = getEntry( i );
        if ( !entry.bStreamed || entry.PlacementState != CPPS_Pending ) {
            continue;
        }

        CarState *pCarState = pCarStates[i];
        if ( pCarState == nullptr ) {
            entry.PlacementState = CPPS_Replaced;
            continue;
        }

        PhysicsVehicle *pPhysicsVehicle = pCarState->getPhysicsVehicle();
        if ( pPhysicsVehicle != nullptr ) {
            {
                PhysicsLock lock;
                pPhysicsVehicle->resetToTransform( entry.Transform, true );
                pPhysicsVehicle->setDrivingMode( PhysicsVehicle::DM_Parked );
            }
            pPhysicsVehicle->setKeyframed( true, true );
        }

        entry.PlacementState = CPPS_Placed;
    }
}

void SceneCarPark::instantiateCar( const int32_t index, Entry &entry ) {
    // FUN_00777cd0
    const bool bReplaced = entry.PlacementState == CPPS_Replaced;

    VehicleBaseResources *pResource = pResources[index];
    CarState &carState = gpCarState->grabNextFreeCarState();
    carState.reset();
    carState.simulationType = CarState::eSimulationType::CSST_Local;
    carState.owner = CarState::eOwner::CSO_Bot;
    carState.bUseShowcaseControl = true;
    carState.carID = pResource->CarID;
    carState.tireID = pResource->TireID;
    carState.rimID = pResource->RimID;
    carState.body.BackOnTrackState = 0;
    carState.body.resetBackOnTrack();

    DBEntryCarPhysicsData *pCarEntry =
        gpDatabase->getCarDBEntry( pResource->CarID );
    if ( pCarEntry == nullptr ) {
        pCarEntry = gpDatabase->getCarDBEntry( 999 );
    } else if ( ( pCarEntry->BitField & 1 ) == 0 ||
                pCarEntry->Brand == nullptr ||
                ( pCarEntry->Brand->Flags & 1 ) == 0 ) {
        const char *pBikeCategory =
            gpDatabase->getStringByHashcode( 1, 0x35da5b5 );
        const int32_t fallbackID =
            ( pBikeCategory == pCarEntry->Category.pAsString ) ? 666 : 999;
        pCarEntry = gpDatabase->getCarDBEntry( fallbackID );
    }

    VehicleController *pVehicle = nullptr;
    if ( gpDatabase->isBike( pCarEntry ) ) {
        pVehicle = gpCarDisplay->instantiateBike(
            &carState, &pResource->Exterior, &pResource->Interior,
            &pResource->FrontRims, &pResource->RearRims );
    } else {
        pVehicle = gpCarDisplay->instantiateCar(
            &carState, &pResource->Exterior, &pResource->Interior,
            &pResource->FrontRims, &pResource->RearRims );
    }

    PhysicsVehicle *pPhysicsVehicle = nullptr;
    if ( pResource->bUseGamePhysics != 0 && gpPhysics != nullptr ) {
        DBEntryCarPhysicsData *pPhysicsEntry = pCarEntry;
        const DBEntryAftermarketPack *pPack =
            gpDatabase->getAftermarketPack( pResource->PackHashcode );
        if ( carState.carID == 999 ) {
            pPhysicsEntry = gpDatabase->getCarDBEntry( 223 );
            pPack = nullptr;
        } else if ( carState.carID == 666 ) {
            pPhysicsEntry = gpDatabase->getCarDBEntry( 1502 );
            pPack = nullptr;
        }

        BankVehicle *pBank = pResource->Exterior.pBankInstance;
        PhysFactory *pFactory =
            ( pBank != nullptr ) ? pBank->getPhysFactory() : nullptr;
        const void *pTorqueBinary =
            ( pBank != nullptr ) ? pBank->getTorqueBinary() : nullptr;
        const Matrix4x4f_t spawnMatrix =
            bReplaced ? entry.Transform : Matrix4x4f_t::Identity();
        if ( pPhysicsEntry != nullptr ) {
            pPhysicsVehicle =
                gpPhysics->createVehicle( pFactory, spawnMatrix, *pPhysicsEntry,
                                          pTorqueBinary, pPack, true, false );
        }

        if ( pPhysicsVehicle != nullptr && bReplaced ) {
            {
                PhysicsLock lock;
                pPhysicsVehicle->resetToTransform( entry.Transform, true );
            }
            pPhysicsVehicle->setKeyframed( true, true );
        }
    }

    VehicleSoundInstance *pVehicleSound = nullptr;
    if ( pResource->bSoundResource && gpVehicleSound != nullptr ) {
        pVehicleSound = gpVehicleSound->createInstance( &carState );
    }

    carState.setPhysicsVehicle( pPhysicsVehicle );
    carState.pVehicleBase = pVehicle;
    carState.pVehicleSound = pVehicleSound;
    carState.pResource = pResource;
    pResource->RefCount++;

    pCarStates[index] = &carState;
    fadeIn[index] = 0.0f;

    OTDU_UNIMPLEMENTED_ONCE; // TODO: PHYSICS

    carState.bIsIndoor = ( gpActiveGMScene != nullptr ) &&
                         gpActiveGMScene->isStaticFrontEndScene();

    OTDU_UNIMPLEMENTED_ONCE; // TODO: PHYSICS

    if ( pVehicle == nullptr ) {
        return;
    }

    if ( entry.pCar != nullptr ) {
        pVehicle->applyGaragePaint( *entry.pCar );
    } else {
        int32_t paintIndex = entry.PaintIndex;
        if ( paintIndex == -1 &&
             gpDatabase->getNumColors( entry.ConfigID ) > 0 ) {
            paintIndex = 0;
        }

        pVehicle->applyPaint(
            uint32_t( entry.ConfigID ), ePaintType::PT_Default, paintIndex,
            entry.RimID, entry.InteriorID, nullptr, nullptr, nullptr, nullptr );
    }

    carState.openConvertibleWindows();

    if ( pEnvMapFront != nullptr ) {
        pVehicle->setRealTimeEnvMap( pEnvMapFront, false, pEnvMapBack );
    }

    OTDU_UNIMPLEMENTED_ONCE; // TODO: CAR PARK
}

bool SceneCarPark::applyPaint( const int32_t index ) {
    // FUN_007779a0
    if ( index < 0 ) {
        return false;
    }

    Entry &entry = getEntry( index );
    if ( gpDatabase->getColorByIndex( entry.ConfigID, entry.PaintIndex ) ==
             nullptr ||
         index >= kMaxNumCars || pCarStates[index] == nullptr ) {
        return false;
    }

    VehicleBase *pVehicle = getVehicle( index );
    if ( pVehicle == nullptr ) {
        return false;
    }

    if ( entry.pCar != nullptr ) {
        pVehicle->applyGaragePaint( *entry.pCar );
        return true;
    }

    pVehicle->applyPaint( uint32_t( entry.ConfigID ), ePaintType::PT_Default,
                          entry.PaintIndex, entry.RimID, entry.InteriorID,
                          nullptr, nullptr, nullptr, nullptr );
    return true;
}

bool SceneCarPark::isAnyInnerViewUnloading() const {
    // FUN_00776260
    for ( int32_t i = 0; i < numEntries; i++ ) {
        if ( pEntries[getRingIndex( i )].InnerViewState == CPIV_Unloading ) {
            return true;
        }
    }
    return false;
}

void SceneCarPark::tick( const float deltaTime ) {
    // FUN_00778b70
    const bool bCinematicPlaying =
        gpActiveGMScene != nullptr &&
        gpActiveGMScene->getIntroExteriorCinematic() != nullptr;

    if ( pendingStreamIndex != -1 ) {
        streamAround( pendingStreamIndex );
    }

    if ( pPendingCar != nullptr && pResources[pendingCarIndex] != nullptr &&
         pResources[pendingCarIndex]->isLoaded() ) {
        releaseResource( pendingCarIndex );

        Entry &entry = getEntry( pendingCarIndex );
        entry.pCar = pPendingCar;
        entry.ConfigID = int32_t( pPendingCar->getCarConfigID() );
        entry.PaintIndex = pPendingCar->paintIndex;
        entry.RimID = pPendingCar->rimID;
        entry.InteriorID = pPendingCar->interiorID;
        entry.bStreamed = false;
        entry.field_0x60 = 0.0f;
        entry.PlacementState = CPPS_Replaced;
        entry.InnerViewState = pendingInnerViewState;

        streamAround( pendingCarIndex );
        pPendingCar = nullptr;
        pendingCarIndex = -1;
    }

    for ( int32_t i = 0; i < numEntries; i++ ) {
        Entry &entry = getEntry( i );

        if ( pCarStates[i] == nullptr && pResources[i] != nullptr &&
             pResources[i]->isLoaded() ) {
            instantiateCar( i, entry );
        }

        CarState *pCarState = pCarStates[i];
        if ( pCarState == nullptr ) {
            continue;
        }

        VehicleBase *pVehicle = pCarState->getVehicleInstance();

        fadeIn[i] =
            std::clamp( fadeIn[i] + kFadeInSpeed * deltaTime, 0.0f, 1.0f );
        if ( pVehicle != nullptr ) {
            pVehicle->setAlpha( fadeIn[i] );
        }

        if ( bCinematicPlaying && ( entry.InnerViewState == CPIV_Loaded ||
                                    entry.InnerViewState == CPIV_Ready ) ) {
            if ( pVehicle != nullptr && pVehicle->unloadInterior() ) {
                SetInnerViewLocked( true );
                entry.InnerViewState = CPIV_Loading;
            }
        }

        if ( entry.InnerViewState == CPIV_Unloading ) {
            const bool bUnloaded =
                pVehicle != nullptr && pVehicle->unloadInterior();
            entry.InnerViewState = bUnloaded ? CPIV_Unloaded : CPIV_Unloading;
            SetInnerViewLocked( true );
        }

        if ( entry.InnerViewState == CPIV_Loading && !bCinematicPlaying &&
             !isAnyInnerViewUnloading() && pVehicle != nullptr ) {
            if ( !pVehicle->isInteriorLoading() &&
                 !pVehicle->isInteriorLoaded() ) {
                pVehicle->loadInterior();
            }

            if ( pVehicle->hasInteriorInstance() ) {
                entry.InnerViewState = CPIV_Loaded;
            }
        }

        if ( entry.InnerViewState == CPIV_Loaded && applyPaint( i ) ) {
            if ( pVehicle != nullptr ) {
                pVehicle->setRealTimeEnvMap( pEnvMapFront, false, pEnvMapBack );
            }
            entry.InnerViewState = CPIV_Ready;
            SetInnerViewLocked( false );
        }

        PhysicsVehicle *pPhysicsVehicle = pCarState->getPhysicsVehicle();
        if ( entry.PlacementState == CPPS_Replaced &&
             pPhysicsVehicle != nullptr ) {
            {
                PhysicsLock lock;
                pPhysicsVehicle->resetToTransform( entry.Transform, true );
                pPhysicsVehicle->setDrivingMode( PhysicsVehicle::DM_Parked );
            }

            OTDU_UNIMPLEMENTED_ONCE; // TODO: PHYSICS

            pPhysicsVehicle->setKeyframed( true, true );

            entry.PlacementState = CPPS_Placed;
        }
    }

    if ( enteredCarState == 1 ) {
        enterCar( enteredCarIndex );
    }
}

void SceneCarPark::releaseCar( const int32_t index ) {
    // FUN_00777380
    if ( index < 0 ) {
        return;
    }

    SetInnerViewLocked( true );

    Entry &entry = getEntry( index );
    VehicleBase *pVehicle = getVehicle( index );
    if ( pVehicle != nullptr && entry.InnerViewState != CPIV_Unloaded &&
         !pVehicle->unloadInterior() ) {
        entry.InnerViewState = CPIV_Unloading;
        return;
    }

    entry.InnerViewState = CPIV_Unloaded;
}

void SceneCarPark::replaceCar( CarDesc *pCar, const int32_t index ) {
    // FUN_007788c0
    Entry &entry = getEntry( index );
    pendingInnerViewState = ( entry.InnerViewState == CPIV_Loaded ||
                              entry.InnerViewState == CPIV_Ready )
                                ? CPIV_Loading
                                : CPIV_Unloaded;

    if ( entry.bStreamed ) {
        if ( pCarStates[index] != nullptr ) {
            gpCarState->releaseCarState( pCarStates[index], false );
            pCarStates[index] = nullptr;
        }

        if ( pResources[index] == nullptr || !pResources[index]->isLoaded() ) {
            pPendingCar = pCar;
            pendingCarIndex = index;

            entry.pCar = nullptr;
            entry.ConfigID = -1;
            entry.PaintIndex = -1;
            entry.RimID = -1;
            entry.InteriorID = -1;
            entry.bStreamed = true;
            entry.field_0x60 = 0.0f;
            entry.PlacementState = CPPS_Pending;
            entry.InnerViewState = pendingInnerViewState;
        } else {
            releaseResource( index );

            if ( entry.PlacementState == CPPS_Placed ) {
                entry.PlacementState = CPPS_Pending;
            }
            entry.bStreamed = false;
            pPendingCar = nullptr;
            pendingCarIndex = -1;
        }
    }

    if ( pPendingCar != nullptr ) {
        return;
    }

    entry.pCar = pCar;
    entry.ConfigID = int32_t( pCar->getCarConfigID() );
    entry.PaintIndex = pCar->paintIndex;
    entry.RimID = pCar->rimID;
    entry.InteriorID = pCar->interiorID;
    entry.bStreamed = false;
    entry.field_0x60 = 0.0f;
    entry.PlacementState = CPPS_Replaced;
    entry.InnerViewState = pendingInnerViewState;

    streamAround( index );
}

void SceneCarPark::openInnerView( const int32_t index ) {
    // FUN_007782a0
    if ( index < 0 ) {
        return;
    }

    Entry &entry = getEntry( index );
    const bool bCinematicPlaying =
        gpActiveGMScene != nullptr &&
        gpActiveGMScene->getIntroExteriorCinematic() != nullptr;

    VehicleBase *pVehicle = getVehicle( index );
    if ( pVehicle == nullptr || bCinematicPlaying ) {
        entry.InnerViewState = CPIV_Loading;
        return;
    }

    if ( entry.InnerViewState == CPIV_Unloading ) {
        entry.InnerViewState = CPIV_Loading;
    }

    if ( entry.InnerViewState == CPIV_Loading ||
         entry.InnerViewState == CPIV_Unloaded ) {
        if ( !isAnyInnerViewUnloading() ) {
            if ( !pVehicle->isInteriorLoading() &&
                 !pVehicle->isInteriorLoaded() ) {
                pVehicle->loadInterior();
            }

            entry.InnerViewState =
                pVehicle->hasInteriorInstance() ? CPIV_Loaded : CPIV_Loading;
        } else {
            entry.InnerViewState = CPIV_Loading;
        }
    }

    if ( entry.InnerViewState == CPIV_Loaded && applyPaint( index ) ) {
        pVehicle->setRealTimeEnvMap( pEnvMapFront, false, pEnvMapBack );
        entry.InnerViewState = CPIV_Ready;
        SetInnerViewLocked( false );
    }
}

void SceneCarPark::enterCar( const int32_t index ) {
    // FUN_00776fb0
    enteredCarIndex = index;

    const bool bIsBike = gpDatabase->isBike(
        gpDatabase->getCarDBEntry( getEntry( index ).ConfigID ) );

    VehicleController *pVehicle = GetDisplayedVehicle( getVehicle( index ) );
    CarState *pCarState =
        ( pVehicle != nullptr ) ? getCarState( index ) : nullptr;
    if ( pCarState != nullptr ) {
        gpCamera->setTargetPhysEntity( pCarState->getPhysicsVehicle() );
    }
    gpCamera->setBoundCarState( pCarState );

    CameraGame *pCamera =
        bIsBike ? gpCamera->findCameraWithMode(
                      gpCamera->findFirstHoodTransitionMode(), pCarState )
                : gpCamera->findCamera( pCarState );
    if ( pCamera == nullptr ) {
        return;
    }

    gpCamera->setNextCamera( pCamera, 0, kEnterCarTransitionTime );
    if ( bIsBike ) {
        return;
    }

    HiearchyNode *pSteer = FindSteeringWheel( pVehicle );
    if ( pSteer == nullptr ) {
        enteredCarState = 1;
        return;
    }

    const float offset = GetSteeringWheelOffset( pCamera );
    OffsetSteeringWheel( pSteer, offset );
    enteredCarState = 2;
    steeringWheelOffset = offset;
}

void SceneCarPark::leaveCar( const int32_t index ) {
    // FUN_00776d80
    VehicleController *pVehicle = GetDisplayedVehicle( getVehicle( index ) );
    CarState *pCarState =
        ( pVehicle != nullptr ) ? getCarState( index ) : nullptr;

    CameraGame *pCamera = gpCamera->findCamera( pCarState );
    if ( pCamera != nullptr && !gpDatabase->isBike( gpDatabase->getCarDBEntry(
                                   getEntry( index ).ConfigID ) ) ) {
        HiearchyNode *pSteer = FindSteeringWheel( pVehicle );
        if ( pSteer == nullptr ) {
            enteredCarState = 0;
            return;
        }

        OffsetSteeringWheel( pSteer, -GetSteeringWheelOffset( pCamera ) );
        steeringWheelOffset = FLT_MIN;
    }

    enteredCarState = 0;
    enteredCarIndex = -1;
}
