#include "shared.h"
#include "scene_garage.h"

#include "config/gs_config.h"
#include "filesystem/gs_file.h"
#include "flash/mng_fade.h"
#include "game/car_showcase/mng_showcase_car_control.h"
#include "game/gm_scene.h"
#include "game/vehicle/car_state.h"
#include "game/scenaric/entity/entity_shop_data.h"
#include "player_data/driver.h"
#include "player_data/gs_playerdata.h"
#include "player_data/player_data_interface.h"
#include "player_data/saved_garage.h"
#include "render/camera/camera_game.h"
#include "render/camera/gs_camera.h"

static constexpr const char *kDefaultGarageBank = "Interior/GPH_L3A.bnk";
static constexpr int32_t kInteriorCameraMode = 7;
static constexpr int32_t kShowcaseCameraMode = 6;
static constexpr float kPageFadeDuration = 0.5f; // FLOAT_00f41674

static inline int32_t GetNumCarsPerPage() {
    Driver *pDriver = gpPlayerData->getLoadedSave()->getDriver();
    return GetHouseNumGarageSlots( pDriver->getCurrentHouse() ) + 1;
}

SceneGarage::SceneGarage()
    : SceneInterior(),
      carPark(),
      interiorBank(),
      selectedCar( 0 ),
      currentPage( 0 ),
      bBankReentry( false ) {
    // FUN_007739c0
}

SceneGarage::~SceneGarage() {
    // FUN_00773940
}

void SceneGarage::loadBank( const bool bReentry ) {
    // FUN_007739f0
    bBankReentry = bReentry;

    if ( !interiorBank.isLoaded() ) {
        char bankPath[512] = {};
        if ( !BuildInteriorBankPath( GM_Garage, bankPath ) ) {
            snprintf( bankPath, sizeof( bankPath ), "%s%s",
                      gpConfig->getResRootPath(), kDefaultGarageBank );
        }

        if ( interiorBank.loadBank( gpFile, bankPath ) ) {
            interiorBank.loadContentAsync();
        }
    }

    if ( interiorBank.isLoaded() ) {
        enter( &interiorBank, false );

        if ( gpPlayerData->getLoadedSave()->getGarage()->isValid() ) {
            gpCamera->setMouseCam( false );
        }
    }
}

void SceneGarage::terminate() {
    // FUN_007738e0
    carPark.releaseAll();
    leave();
    interiorBank.unload();
    gpCamera->setMouseCam( true );
}

void SceneGarage::tick() {
    // FUN_00775cf0
    // TODO: FIXME: The scene is re-enqueued every frame (see
    // SceneInterior::draw).
    if ( !isEntered() ) {
        loadBank( bBankReentry );
    }

    SceneInterior::tick();
    draw();
}

void SceneGarage::setupCamera() {
    // FUN_00773510
    CameraGame *pCamera = gpCamera->findCameraWithMode(
        kInteriorCameraMode, gpCamera->getBoundCarState() );

    const Eigen::Vector4f position = getWorldPosition();
    pCamera->setPosition( position );

    const Eigen::Vector4f target =
        position + Eigen::Vector4f( 1.0f, 0.0f, 0.0f, 0.0f );
    pCamera->setTarget( target );

    const Eigen::Vector4f up( 0.0f, 1.0f, 0.0f, 0.0f );
    pCamera->setUp( up );
    pCamera->updateMatrix( position.head<3>(), up.head<3>(), target.head<3>() );
}

void SceneGarage::onBankReady() {
    // FUN_00773da0
    carPark.setEnvMapTargets(
        getBankBitmap( GetIdentifier64bit( "CUBECAR" ) ),
        getBankBitmap( GetIdentifier64bit( "CUBECARB" ) ) );

    if ( !bBankReentry ) {
        loadPage( 0 );
    }

    EntityShopData *pShop = getShopData();
    startCameraPath( ( pShop != nullptr ) ? pShop->getCameraPath() : nullptr,
                     false, false );
}

void SceneGarage::loadPage( const int32_t page ) {
    // FUN_00773ae0
    Garage *pGarage = gpPlayerData->getLoadedSave()->getGarage();
    const int32_t numCars = pGarage->getNumHouseCars();
    const int32_t numCarsPerPage = GetNumCarsPerPage();

    const int32_t firstCar = numCarsPerPage * page;
    if ( firstCar > numCars ) {
        return;
    }

    int32_t lastCar = firstCar + numCarsPerPage - 1;
    if ( lastCar >= numCars - 1 ) {
        lastCar = numCars - 1;
    }

    currentPage = page;

    carPark.releaseAll();
    for ( int32_t i = 0; i < firstCar; i++ ) {
        carPark.pushConfig( -1 );
    }
    for ( int32_t i = firstCar; i <= lastCar; i++ ) {
        carPark.pushCar( pGarage->getHouseCarAtIndex( i ) );
    }

    if ( !carPark.isLoaded() ) {
        if ( carPark.getNumEntries() != 0 ) {
            carPark.streamAround( firstCar );
            carPark.loadBank();
        }
        carPark.setLoaded( true );
    }

    EntityShopData *pShop = getShopData();
    const int32_t numPositions =
        ( pShop != nullptr ) ? pShop->getNumPositions() : 0;

    for ( int32_t i = firstCar; i <= lastCar; i++ ) {
        const int32_t positionIndex = getCarPositionInPage( i );
        if ( positionIndex >= numPositions ) {
            continue;
        }

        Matrix4x4f_t transform = Matrix4x4f_t::Identity();
        Entity *pPosition = pShop->getPosition( positionIndex );
        if ( pPosition != nullptr ) {
            transform = pPosition->getModelMatrix();
        }
        transform( 3, 3 ) = 1.0f;

        if ( i < carPark.getNumEntries() ) {
            SceneCarPark::Entry &entry = carPark.getEntry( i );
            entry.Transform = transform;
            entry.field_0x60 = 0.0f;
            entry.PlacementState = SceneCarPark::CPPS_Pending;
        }
    }

    carPark.commit();
}

int32_t SceneGarage::getCarPage( const int32_t carIndex ) const {
    // FUN_00773440
    return int32_t( float( carIndex ) / float( GetNumCarsPerPage() ) );
}

int32_t SceneGarage::getCarPositionInPage( const int32_t carIndex ) const {
    // FUN_007734b0
    return carIndex - GetNumCarsPerPage() * getCarPage( carIndex );
}

bool SceneGarage::isCarOnCurrentPage( const int32_t carIndex ) const {
    // FUN_00773640
    return getCarPage( carIndex ) == currentPage;
}

void SceneGarage::selectCar( const int32_t carIndex ) {
    // FUN_00774070
    if ( getCarPage( carIndex ) != currentPage ) {
        gpMngFade->startTransition(
            ( gpMngFade->getTransitionFlags() & ~0xcu ) | 1u, kPageFadeDuration,
            &SceneGarage::OnPageFadeOut,
            reinterpret_cast<void *>( intptr_t( carIndex ) ), this );
        selectedCar = carIndex;
        return;
    }

    focusCamera( getCarPositionInPage( carIndex ) );
    carPark.streamAround( carIndex );
    selectedCar = carIndex;
}

void SceneGarage::selectCarImmediate( const int32_t carIndex ) {
    // FUN_007740e0
    if ( getCarPage( carIndex ) != currentPage ) {
        gpMngFade->startTransition(
            ( gpMngFade->getTransitionFlags() & ~0xcu ) | 1u, kPageFadeDuration,
            &SceneGarage::OnPageFadeOut,
            reinterpret_cast<void *>( intptr_t( carIndex ) ), this );
        selectedCar = carIndex;
        return;
    }

    moveCameraTo( getCarPositionInPage( carIndex ) );
    carPark.streamAround( carIndex );
    selectedCar = carIndex;
}

void SceneGarage::OnPageFadeOut( void *pData, void *pSource ) {
    // FUN_00773e20
    SceneGarage *pScene = static_cast<SceneGarage *>( pData );
    const int32_t carIndex = int32_t( reinterpret_cast<intptr_t>( pSource ) );

    if ( pScene != nullptr ) {
        pScene->loadPage( pScene->getCarPage( carIndex ) );
        pScene->carPark.openInnerView( carIndex );
        pScene->focusCamera( pScene->getCarPositionInPage( carIndex ) );
    }

    gpMngFade->startTransition( ( gpMngFade->getTransitionFlags() & ~8u ) | 5u,
                                kPageFadeDuration );
}

void SceneGarage::enterShowcase() {
    // FUN_007736f0
    CarState *pCarState = carPark.getCarState( selectedCar );
    VehicleBase *pVehicle = carPark.getVehicle( selectedCar );
    if ( pCarState == nullptr || pVehicle == nullptr ) {
        return;
    }

    if ( gpActiveGMScene != nullptr ) {
        gpActiveGMScene->getShowcaseCarControl().focusOn( pVehicle );
    }

    Matrix4x4f_t boundingBox;
    pCarState->retrieveBoundingBox( boundingBox );
    setupShowcaseCamera( selectedCar, pCarState->getPhysicsVehicle(),
                         boundingBox, false );
}

void SceneGarage::leaveShowcase() {
    // FUN_00773660
    if ( gpActiveGMScene != nullptr ) {
        MngShowCaseCarControl &showcase =
            gpActiveGMScene->getShowcaseCarControl();
        showcase.setEnabled( false );

        CameraGame *pShowcaseCamera = gpCamera->findCameraWithMode(
            kShowcaseCameraMode, gpCamera->getBoundCarState() );
        pShowcaseCamera->setfield_0x26c( false );

        showcase.closeShowcase();
    }

    restoreInteriorCamera();
}

void SceneGarage::enterCar() {
    // FUN_00773890
    if ( gpActiveGMScene != nullptr ) {
        gpActiveGMScene->getShowcaseCarControl().enterCar();
    }

    if ( carPark.getVehicle( selectedCar ) != nullptr ) {
        carPark.enterCar( selectedCar );
    }
}

void SceneGarage::leaveCar() {
    // FUN_007737d0
    if ( gpActiveGMScene != nullptr ) {
        gpActiveGMScene->getShowcaseCarControl().leaveCar();
    }

    CarState *pCarState = carPark.getCarState( selectedCar );
    if ( pCarState != nullptr ) {
        Matrix4x4f_t boundingBox;
        pCarState->retrieveBoundingBox( boundingBox );
        setupShowcaseCamera( selectedCar, pCarState->getPhysicsVehicle(),
                             boundingBox, true );
    }

    if ( carPark.getVehicle( selectedCar ) != nullptr ) {
        carPark.leaveCar( selectedCar );
    }
}
