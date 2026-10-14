#include "shared.h"
#include "scene_shop.h"

#include "gm_shop.h"

#include "config/gs_config.h"
#include "filesystem/gs_file.h"
#include "flash/flash_callbacks_register.h"
#include "flash/gs_flash.h"
#include "flash/implementation/flash_player.h"
#include "flash/mng_fade.h"
#include "game/car_showcase/mng_showcase_car_control.h"
#include "game/gm_scene.h"
#include "game/scenaric/bank_scene_arrays.h"
#include "game/scenaric/entity/entity_cinematic.h"
#include "game/scenaric/entity/entity_shop_data.h"
#include "game/scenaric/gs_scenaric.h"
#include "game/scenaric/spots.h"
#include "database/gs_database.h"
#include "game/vehicle/car_state.h"
#include "physics/vehicle/physics_vehicle.h"
#include "render/camera/camera_path.h"
#include "world/gs_world_streaming.h"
#include "render/camera/camera_game.h"
#include "render/camera/gs_camera.h"

static constexpr const char *kDefaultShopBank = "Interior/ICD_15B.bnk";
static constexpr int32_t kInteriorCameraMode = 7;
static constexpr int32_t kShowcaseCameraMode = 6;
static constexpr float kCarFadeDuration = 0.5f; // FLOAT_00f41674

SceneShop::SceneShop()
    : pOwner( nullptr ),
      pDetailsCinematic( nullptr ),
      pCinematic( nullptr ),
      pPendingDetailsCinematic( nullptr ),
      pPendingCinematic( nullptr ),
      bank(),
      content(),
      carPark(),
      interiorBank(),
      selectedCar( 0 ),
      lureTimer( -1.0f ),
      skipCineTimer( -1.0f ),
      pSkipCinePlayer( nullptr ) {
    // FUN_00772840
    OTDU_UNIMPLEMENTED; // TODO: RENDER
    OTDU_UNIMPLEMENTED; // TODO: GMSHOP
}

SceneShop::~SceneShop() {
    // FUN_00772750
}

void SceneShop::loadResources( const bool bRental ) {
    // FUN_007729f0
    char bankPath[512] = {};
    if ( !BuildInteriorBankPath( GM_Invalid, bankPath ) ) {
        snprintf( bankPath, sizeof( bankPath ), "%s%s",
                  gpConfig->getResRootPath(), kDefaultShopBank );
    }

    if ( !interiorBank.isLoaded() ) {
        if ( interiorBank.loadBank( gpFile, bankPath ) ) {
            interiorBank.loadContentAsync();
        }
    }

    if ( !interiorBank.isLoaded() ) {
        return;
    }

    if ( bRental ) {
        content.setOverrideFovVsLod( false );
    }

    content.enter( &interiorBank, false );

    OTDU_UNIMPLEMENTED; // TODO: GMSHOP

    gpCamera->setMouseCam( false );

    OTDU_UNIMPLEMENTED; // TODO: RENDER ("viewport_details" 256x256 RT)
}

void SceneShop::terminate() {
    // FUN_00772620
    if ( pDetailsCinematic != nullptr ) {
        pDetailsCinematic->stop( true );
        pDetailsCinematic = nullptr;
    }

    if ( pCinematic != nullptr ) {
        pCinematic->stop( true );
        pCinematic = nullptr;
    }

    OTDU_UNIMPLEMENTED; // TODO: RENDER (details cameras terminate)

    const ScenaricBank::eBankState bankState = bank.getState();
    if ( bankState != ScenaricBank::BBS_Unused &&
         bankState != ScenaricBank::BBS_LoadFailed ) {
        gpScenaric->unloadBank( &bank );
    }

    OTDU_UNIMPLEMENTED; // TODO: RENDER (details render scene, render target
                        // and material release)
    OTDU_UNIMPLEMENTED; // TODO: GMSHOP

    carPark.releaseAll();
    content.leave();
    interiorBank.unload();

    gpCamera->setMouseCam( true );
}

void SceneShop::tick( float deltaTime ) {
    // FUN_00773030
    content.tick();
    content.draw();

    carPark.tick( deltaTime );

    if ( lureTimer < 0.0f ) {
        if ( skipCineTimer >= 0.0f ) {
            if ( pSkipCinePlayer != nullptr ) {
                pSkipCinePlayer->setVariableValue( "/:skipCine", 0 );
            }
            skipCineTimer -= deltaTime;
        }
    } else {
        lureTimer -= deltaTime;
    }

    OTDU_UNIMPLEMENTED_ONCE; // TODO: RENDER

    if ( content.getState() == SceneInterior::SIS_Ready ) {
        onInteriorReady();
    }

    if ( pPendingCinematic != nullptr ) {
        if ( pCinematic != nullptr ) {
            pCinematic->stop( true );
            pCinematic = nullptr;
        }
        pCinematic = pPendingCinematic;
        pPendingCinematic = nullptr;
        pCinematic->start( true );
    }

    if ( pPendingDetailsCinematic != nullptr ) {
        if ( pDetailsCinematic != nullptr ) {
            pDetailsCinematic->stop( true );
            pDetailsCinematic = nullptr;
        }
        pDetailsCinematic = pPendingDetailsCinematic;
        pPendingDetailsCinematic = nullptr;
        pDetailsCinematic->start( true );
    }

    if ( pDetailsCinematic != nullptr ) {
        OTDU_UNIMPLEMENTED_ONCE; // TODO: RENDER (details cinematic camera)
    }
}

void SceneShop::onInteriorReady() {
    // FUN_00772170
    carPark.setEnvMapTargets(
        content.getBankBitmap( GetIdentifier64bit( "CUBECAR" ) ),
        content.getBankBitmap( GetIdentifier64bit( "CUBECARB" ) ) );

    EntityShopData *pShop = content.getShopData();
    const int32_t numPositions =
        ( pShop != nullptr ) ? pShop->getNumPositions() : 0;

    if ( pOwner->getShopLogic()->usesClassicCarStock() ) {
        OTDU_UNIMPLEMENTED; // TODO: GMSHOP (classic car stock)
    } else {
        for ( int32_t i = 0; i < numPositions && i < ShopLogic::kNumCarSlots;
              i++ ) {
            carPark.pushConfig( pOwner->getCarID( i ) );
        }
    }

    for ( int32_t i = 0; i < carPark.getNumUsedEntries(); i++ ) {
        const int32_t carIndex = carPark.getUsedEntryIndex( i );
        if ( carIndex >= numPositions ) {
            continue;
        }

        Matrix4x4f_t transform = Matrix4x4f_t::Identity();
        Entity *pPosition = pShop->getPosition( carIndex );
        if ( pPosition != nullptr ) {
            transform = pPosition->getModelMatrix();
        }
        transform( 3, 3 ) = 1.0f;

        if ( carIndex < carPark.getNumEntries() ) {
            SceneCarPark::Entry &entry = carPark.getEntry( carIndex );
            entry.Transform = transform;
            entry.field_0x60 = 0.0f;
            entry.PlacementState = SceneCarPark::CPPS_Pending;
        }
    }

    carPark.commit();

    Path *pCameraPath = ( pShop != nullptr ) ? pShop->getCameraPath() : nullptr;
    if ( pCameraPath == nullptr ) {
        content.focusCamera( 0 );
        selectedCar = 0;
    } else if ( gpCamera != nullptr ) {
        content.startCameraPath( pCameraPath, false, true );
    }

    CameraGame *pCamera =
        gpCamera->findCameraWithMode( 0, gpCamera->getBoundCarState() );
    pCamera->setTargetOffseted( Eigen::Vector4f( 1.0f, 0.0f, 0.0f, 1.0f ) );
    pCamera->setPositionOffseted( Eigen::Vector4f( 0.0f, 0.0f, 0.0f, 1.0f ) );
    gpCamera->setCameraMode( 0, 0, 1.0f );
}

void SceneShop::focusCar( const int32_t carIndex ) {
    // FUN_00771ae0
    content.focusCamera( carIndex );
    selectedCar = carIndex;
}

bool SceneShop::playCarCinematic( const char *pName,
                                  const int32_t carIndex,
                                  const bool bDetails ) {
    // FUN_00771b30
    EntityCinematic *&pPending =
        bDetails ? pPendingDetailsCinematic : pPendingCinematic;

    EntityCinematic *pFound = static_cast<EntityCinematic *>(
        carPark.getDealerBank().findEntity( eEntityType::ET_CINE, pName ) );
    if ( pFound == pPending ) {
        return true;
    }

    pPending = pFound;
    if ( pFound == nullptr ) {
        return false;
    }

    CarState *pCarState = carPark.getCarState( carIndex );
    pFound->bindPlayerCar( pCarState );

    if ( bDetails ) {
        OTDU_UNIMPLEMENTED_ONCE; // TODO: RENDER (details viewport camera)
        pFound->setDontSwitchCamera( true );
    }

    Matrix4x4f_t offset = Matrix4x4f_t::Identity();
    PhysicsVehicle *pPhysicsVehicle =
        ( pCarState != nullptr ) ? pCarState->getPhysicsVehicle() : nullptr;
    if ( pPhysicsVehicle != nullptr ) {
        offset = pPhysicsVehicle->getChassisWorldMatrix();
    }
    const Eigen::Vector4f shift = gpWorldStreaming->isEnabled()
                                      ? gpWorldStreaming->getPosition()
                                      : Eigen::Vector4f::Zero();
    offset.row( 3 ) -= shift.transpose();
    pFound->setTransformOffset( offset );
    return true;
}

void SceneShop::playOptionDetailsCinematic( const int32_t option,
                                            const int32_t carIndex ) {
    // FUN_00771c60
    CarState *pCarState = carPark.getCarState( carIndex );
    if ( pCarState == nullptr ) {
        return;
    }

    DBEntryCarPhysicsData *pCarEntry =
        gpDatabase->getCarDBEntry( int32_t( pCarState->getCarID() ) );

    switch ( option ) {
    case 1:
        playCarCinematic( "C_WHEELS_V", carIndex, true );
        break;
    case 2:
        playCarCinematic( gpDatabase->isBike( pCarEntry ) ? "C_PAINT_V_BIKE"
                                                          : "C_PAINT_V",
                          carIndex, true );
        break;
    case 3:
        playCarCinematic( "C_INTERIOR_V", carIndex, true );
        break;
    default:
        if ( pDetailsCinematic != nullptr ) {
            pDetailsCinematic->stop( true );
            pDetailsCinematic = nullptr;
        }
        break;
    }
}

void SceneShop::playOptionCinematic( const int32_t option,
                                     const int32_t carIndex ) {
    // FUN_00771d90
    CarState *pCarState = carPark.getCarState( carIndex );
    if ( pCarState == nullptr ) {
        return;
    }

    DBEntryCarPhysicsData *pCarEntry =
        gpDatabase->getCarDBEntry( int32_t( pCarState->getCarID() ) );

    OTDU_UNIMPLEMENTED_ONCE; // TODO: VEHICLE (showcase player vehicle
                             // force-cockpit flag)

    MngShowCaseCarControl *pShowcase =
        ( gpActiveGMScene != nullptr )
            ? &gpActiveGMScene->getShowcaseCarControl()
            : nullptr;

    switch ( option ) {
    case 0: {
        if ( pCinematic != nullptr ) {
            pCinematic->stop( true );
            pCinematic = nullptr;
        }
        if ( pShowcase != nullptr ) {
            pShowcase->closeLeftDoor();
        }

        gpCamera->setCameraMode( kInteriorCameraMode, 0, 1.0f );
        CameraPath *pInteriorCamera =
            static_cast<CameraPath *>( gpCamera->findCameraWithMode(
                kInteriorCameraMode, gpCamera->getBoundCarState() ) );
        pInteriorCamera->setTime( 1.0f );
        break;
    }
    case 1:
        if ( pShowcase != nullptr ) {
            pShowcase->closeLeftDoor();
        }
        playCarCinematic( "C_BUY", carIndex, false );
        break;
    case 2:
        if ( pShowcase != nullptr ) {
            pShowcase->closeLeftDoor();
        }
        playCarCinematic( gpDatabase->isBike( pCarEntry ) ? "C_BUY_BIKE"
                                                          : "C_BUY",
                          carIndex, false );
        break;
    case 3:
        if ( pShowcase != nullptr ) {
            pShowcase->openLeftDoor( carPark.getCarVehicle( carIndex ) );
        }
        playCarCinematic( "C_INTERIOR", carIndex, false );
        break;
    default:
        if ( pCinematic != nullptr ) {
            pCinematic->stop( true );
            pCinematic = nullptr;
        }
        break;
    }
}

void SceneShop::moveToCar( const int32_t fromCar, int32_t toCar ) {
    // FUN_00772d90
    SpotMap *pSpot = gpScenaric->getSpotList().getWorldSpotChecked(
        gpScenaric->getCurrentSpotIndex() );
    const bool bRental = pSpot != nullptr && pSpot->Type == ST_Unknown8 &&
                         pSpot->SubType >= 2 && pSpot->SubType <= 3;

    if ( bRental ) {
        pSkipCinePlayer = gpFlash->getFlashPlayer( "SHOPS" );
        if ( pSkipCinePlayer != nullptr ) {
            pSkipCinePlayer->setVariableValue( "/:skipCine", 1 );
        }
        lureTimer = 1.0f;
        skipCineTimer = 0.0f;
        content.moveCameraTo( toCar );
    } else {
        const int32_t lastSlot = carPark.getLastAvailableShopSlot();
        const bool bForward = toCar > fromCar;

        int32_t pathCar = toCar;
        if ( !bForward ) {
            pathCar = ( toCar + 1 > lastSlot ) ? 0 : toCar + 1;
        }

        Path *pPath = content.getPositionPath( pathCar );
        const bool bWrapsAround = bForward
                                      ? ( toCar == lastSlot && fromCar == 0 )
                                      : ( toCar == 0 && fromCar == lastSlot );

        if ( pPath == nullptr ||
             ( bWrapsAround && pOwner->getNumShopCars() > 2 ) ) {
            FlashPlayer *pPlayer = gpFlash->getFlashPlayer( "SHOPS" );
            if ( pPlayer != nullptr ) {
                pPlayer->setVariableValue( "/:lockChca", 1 );
                gpMngFade->startTransition(
                    ( gpMngFade->getTransitionFlags() & ~0xcu ) | 1u,
                    kCarFadeDuration, &GMShop::OnCarFadeOut,
                    reinterpret_cast<void *>( intptr_t( toCar ) ), pOwner );
            }
        } else {
            const float duration = pPath->getDuration();
            pSkipCinePlayer = gpFlash->getFlashPlayer( "SHOPS" );
            if ( duration > 0.0f && pSkipCinePlayer != nullptr ) {
                pSkipCinePlayer->setVariableValue( "/:skipCine", 1 );
            }

            lureTimer = duration;
            skipCineTimer = 0.0f;
            content.startCameraPath( content.getPositionPath( pathCar ),
                                     !bForward, false );
        }
    }

    selectedCar = toCar;
    carPark.streamAround( toCar );
}

void SceneShop::setupCamera() {
    // FUN_007713d0
    CameraGame *pCamera = gpCamera->findCameraWithMode(
        kInteriorCameraMode, gpCamera->getBoundCarState() );

    const Eigen::Vector4f position = content.getWorldPosition();
    pCamera->setPosition( position );

    const Eigen::Vector4f target =
        position + Eigen::Vector4f( 1.0f, 0.0f, 0.0f, 0.0f );
    pCamera->setTarget( target );

    const Eigen::Vector4f up( 0.0f, 1.0f, 0.0f, 0.0f );
    pCamera->setUp( up );
    pCamera->updateMatrix( position.head<3>(), up.head<3>(), target.head<3>() );

    gFlashCallbacksRegister.add( "lure", &SceneShop::LureCallback, this );
}

void SceneShop::suspend() {
    // FUN_007713b0
    content.suspend();
    gFlashCallbacksRegister.removeAllSynonyms( &SceneShop::LureCallback );
}

void SceneShop::LureCallback( FlashRECT *pRect,
                              float deltaTime,
                              DrawList *pDrawList,
                              void *pUserData,
                              FlashResource *pResource,
                              const char *pClipName ) {
    // FUN_00771290
    OTDU_UNIMPLEMENTED_ONCE; // TODO: RENDER (draws the details render target
                             // in the "buy/lure" clip)
}

void SceneShop::draw() {
    // FUN_00771530
    OTDU_UNIMPLEMENTED_ONCE; // TODO: RENDER (displayed cars render lists in
                             // the details render scene)
}

void SceneShop::enterShowcase() {
    // FUN_00771f70
    CarState *pCarState = carPark.getCarState( selectedCar );
    if ( pCarState == nullptr ) {
        return;
    }

    if ( gpActiveGMScene != nullptr ) {
        gpActiveGMScene->getShowcaseCarControl().focusOn(
            carPark.getVehicle( selectedCar ) );
    }

    Matrix4x4f_t boundingBox;
    pCarState->retrieveBoundingBox( boundingBox );
    content.setupShowcaseCamera( selectedCar, pCarState->getPhysicsVehicle(),
                                 boundingBox, false );
}

void SceneShop::leaveShowcase() {
    // FUN_00771a50
    if ( gpActiveGMScene != nullptr ) {
        MngShowCaseCarControl &showcase =
            gpActiveGMScene->getShowcaseCarControl();
        showcase.setEnabled( false );

        CameraGame *pShowcaseCamera = gpCamera->findCameraWithMode(
            kShowcaseCameraMode, gpCamera->getBoundCarState() );
        pShowcaseCamera->setfield_0x26c( false );

        showcase.closeShowcase();
    }

    content.restoreInteriorCamera();
}

void SceneShop::enterCar() {
    // FUN_00772120
    if ( gpActiveGMScene != nullptr ) {
        gpActiveGMScene->getShowcaseCarControl().enterCar();
    }

    if ( carPark.getVehicle( selectedCar ) != nullptr ) {
        carPark.enterCar( selectedCar );
    }
}

void SceneShop::leaveCar() {
    // FUN_00772040
    CarState *pCarState = carPark.getCarState( selectedCar );
    if ( pCarState == nullptr ) {
        return;
    }

    if ( gpActiveGMScene != nullptr ) {
        gpActiveGMScene->getShowcaseCarControl().leaveCar();
    }

    Matrix4x4f_t boundingBox;
    pCarState->retrieveBoundingBox( boundingBox );
    content.setupShowcaseCamera( selectedCar, pCarState->getPhysicsVehicle(),
                                 boundingBox, true );

    if ( carPark.getVehicle( selectedCar ) != nullptr ) {
        carPark.leaveCar( selectedCar );
    }
}
