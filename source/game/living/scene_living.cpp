#include "shared.h"
#include "scene_living.h"

#include "tdu_instance.h"

#include "config/gs_config.h"
#include "filesystem/gs_file.h"
#include "game/scenaric/gs_scenaric.h"
#include "game/scenaric/spots.h"
#include "render/camera/camera_game.h"
#include "render/camera/gs_camera.h"
#include "render/gs_render.h"

static constexpr const char *kDefaultInteriorBank = "Interior/LPH_L3A.bnk";
static constexpr float kDrawCameraNearPlane = 0.5f;    // FLOAT_00fab928
static constexpr float kDrawCameraFarPlane = 10000.0f; // FLOAT_00fab92c

SceneLiving::SceneLiving() : content(), interiorBank(), spotBank() {
    // FUN_0076a6f0 (inlined)
}

SceneLiving::~SceneLiving() {
    // FUN_0042cde0
}

void SceneLiving::initialize() {
    // FUN_009c39b0
    loadInteriorBank();

    if ( interiorBank.isLoaded() ) {
        content.enter( &interiorBank, false );

        if ( gpCamera != nullptr ) {
            gpCamera->setMouseCam( false );
        }
    }
}

void SceneLiving::loadInteriorBank() {
    if ( interiorBank.isLoaded() ) {
        return;
    }

    char bankPath[512] = {};
    if ( !BuildInteriorBankPath( GM_Living, bankPath ) ) {
        if ( gpScenaric == nullptr ||
             gpScenaric->getSpotList().getNumWorldSpots() == 0 ) {
            return;
        }

        OTDU_LOG_WARN( "No house spot resolved; falling back to '%s'\n",
                       kDefaultInteriorBank );
        snprintf( bankPath, sizeof( bankPath ), "%s%s",
                  gpConfig->getResRootPath(), kDefaultInteriorBank );
    }

    OTDU_LOG_DEBUG( "Loading house interior '%s'\n", bankPath );

    if ( interiorBank.loadBank( gpFile, bankPath ) ) {
        interiorBank.loadContentAsync();
    }
}

void SceneLiving::terminate() {
    // FUN_009c38b0
    if ( gpCamera != nullptr ) {
        gpCamera->setMouseCam( true );
    }

    content.leave();
    interiorBank.unload();
}

void SceneLiving::tick( float /*deltaTime*/, float /*totalTime*/ ) {
    // FUN_00775cf0
    if ( !content.isEntered() ) {
        loadInteriorBank();
        if ( interiorBank.isLoaded() ) {
            content.enter( &interiorBank, false );
        }
    }

    content.tick();
    content.draw();
}

void SceneLiving::draw() {
    // FUN_009c37a0
    if ( gpCamera != nullptr ) {
        CameraGame *pCamera =
            gpCamera->findCameraWithMode( 1, gpCamera->getBoundCarState() );
        if ( pCamera != nullptr ) {
            const Eigen::Vector4f position( 0.0f, 0.0f, 0.0f, 1.0f );
            const Eigen::Vector4f target( 9.0f, 2.0f, -8.0f, 1.0f );
            const Eigen::Vector4f up( 0.0f, 1.0f, 0.0f, 0.0f );

            pCamera->setPosition( position );
            pCamera->setTarget( target );
            pCamera->setUp( up );
            pCamera->updateMatrix( position.head<3>(), up.head<3>(),
                                   target.head<3>() );

            pCamera->setNearPlane( kDrawCameraNearPlane );
            pCamera->setFarPlane( kDrawCameraFarPlane );
        }
    }

    OTDU_UNIMPLEMENTED_ONCE; // TODO: AVATAR (FUN_008d7b70)
}

void SceneLiving::pause() {
    // FUN_009c3790
    suspend();
}

void SceneLiving::resume() {
    // FUN_007744b0
    content.resume();
}

void SceneLiving::suspend() {
    // FUN_007743c0
    content.suspend();
}
