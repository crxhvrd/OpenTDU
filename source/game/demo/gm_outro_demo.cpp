#include "shared.h"
#include "gm_outro_demo.h"

#include "tdu_instance.h"
#include "ai/ai_pool.h"
#include "ai/gs_ai.h"
#include "flash/gs_flash.h"
#include "flash/movie_player.h"
#include "flash/implementation/flash_player.h"
#include "game/scenaric/entity/entity_driver.h"
#include "game/scenaric/gs_scenaric.h"
#include "game/vehicle/car_state.h"
#include "game/vehicle/gs_car_state.h"
#include "physics/gs_physics.h"
#include "physics/vehicle/physics_vehicle.h"

static constexpr float kPictureTimerDisabled = -1.0f; // FLOAT_00f41664
static constexpr float kPictureDuration = 6.0f;       // DAT_00f41840

GMOutroDemo::GMOutroDemo() : GMDemoBase(), pictureTimer( 0.0f ) {
    // FUN_00406560
}

GMOutroDemo::~GMOutroDemo() {}

void GMOutroDemo::reset() {
    // FUN_009bb170
    GameModeScene::reset();

    AiPool *pAIPool = gpAI->getAIPool();
    gpAI->setDisableBots( true );
    gpAI->setBotsActive( false );
    pAIPool->removeBots();
    gpAI->setDisableCops( true );
    gpAI->setCopsActive( false );
    pAIPool->removeCops();

    pDemoPlayer = gpFlash->getFlashPlayer( "DEMO_END" );
    if ( pDemoPlayer != nullptr ) {
        pDemoPlayer->showFrame( "outro", "/", true );
    }

    if ( gpActiveGameMode != nullptr &&
         gpActiveGameMode->isMessageBoxVisible() ) {
        gpActiveGameMode->setsMessageBoxVisible( false );
    }

    FlashPlayer *pGeneralPlayer = gpFlash->getFlashPlayer( "GENERAL" );
    if ( pGeneralPlayer != nullptr ) {
        pGeneralPlayer->showFrame( "w_out", "/waiting", false );
        pGeneralPlayer->showFrame( "off", "avert", false );
        pGeneralPlayer->showFrame( "default", "/tutorial", false );
    }

    const int32_t numHUDMovies = gpFlash->getNumMovies( 1 );
    for ( int32_t movieIndex = 0; movieIndex < numHUDMovies; movieIndex++ ) {
        MoviePlayer *pMovie = gpFlash->getMoviePlayer( 1, movieIndex );
        if ( pMovie != nullptr ) {
            pMovie->Flags = pMovie->Flags & 0xfffffffd;
        }
    }

    setPicturePrefix( "outr" );

    CarState *pLocalCar = gpScenaric->getLocalPlayer().getCarState();
    if ( pLocalCar != nullptr && pLocalCar->getPhysicsVehicle() != nullptr ) {
        PhysicsLock lock;
        pLocalCar->getPhysicsVehicle()->setPostponedPause( 1 );
    }

    pictureTimer = kPictureTimerDisabled;
}

void GMOutroDemo::tick( float deltaTime, float totalTime ) {
    // FUN_009bb010
    GameModeScene::tick( deltaTime, totalTime );

    if ( pictureTimer <= 0.0f || pDemoPlayer == nullptr ) {
        return;
    }

    pictureTimer -= deltaTime;
    if ( pictureTimer > 0.0f ) {
        return;
    }

    const int32_t pictureIndex = pDemoPlayer->getVariableAsInt( "/:num_txt" );
    const int32_t numPictures = pDemoPlayer->getVariableAsInt( "/:nb_txt" );
    pDemoPlayer->showFrame( pictureIndex < numPictures ? "change" : "close",
                            "/", true );
    pictureTimer = kPictureTimerDisabled;
}

void GMOutroDemo::pause() {
    // FUN_009bb0b0
    GameModeScene::pause();

    LocalPlayer &localPlayer = gpScenaric->getLocalPlayer();
    CarState *pLocalCar = localPlayer.getCarState();
    if ( pLocalCar == nullptr ) {
        return;
    }

    EntityDriver *pDriver = pLocalCar->getDriver();
    if ( pDriver != nullptr ) {
        pDriver->removeInstanceReference();
        pDriver->setEnvMap( false, nullptr );
        pLocalCar->unloadDriver();
        gpScenaric->getAvatarManager().releaseAvatar( pDriver );
    }

    if ( pLocalCar->getPhysicsVehicle() != nullptr ) {
        pLocalCar->getPhysicsVehicle()->setSecondaryDrivingControl( nullptr );
    }

    gpCarState->unloadVehicle( pLocalCar, false, false );
    localPlayer.setCarState( nullptr );
}

bool GMOutroDemo::onMessage( FlashMessage *pMessage ) {
    // FUN_009bb3e0
    if ( *pMessage == FlashMessage( "afsc" ) ) {
        pictureTimer = kPictureDuration;
    }

    return GMDemoBase::onMessage( pMessage );
}

void GMOutroDemo::changeGameMode() {
    // FUN_009baff0
    gpActiveGameMode->requestExit();
    gpTestDriveInstance->requestExit();
}
