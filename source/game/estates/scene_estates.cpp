#include "shared.h"
#include "scene_estates.h"

#include "config/gs_config.h"
#include "filesystem/gs_file.h"

static constexpr const char *kDefaultRealtorBank = "Interior/ire_a.bnk";

SceneEstates::SceneEstates() : content(), interiorBank() {
    // FUN_0079ac60 (inlined)
}

SceneEstates::~SceneEstates() {
    // FUN_00774220
}

void SceneEstates::initialize() {
    // FUN_007742d0
    char bankPath[512] = {};
    if ( !BuildInteriorBankPath( GM_Invalid, bankPath ) ) {
        snprintf( bankPath, sizeof( bankPath ), "%s%s",
                  gpConfig->getResRootPath(), kDefaultRealtorBank );
    }

    if ( !interiorBank.isLoaded() ) {
        if ( interiorBank.loadBank( gpFile, bankPath ) ) {
            interiorBank.loadContentAsync();
        }
    }

    if ( interiorBank.isLoaded() ) {
        content.enter( &interiorBank, false );
    }
}

void SceneEstates::terminate() {
    // FUN_007741d0
    content.leave();
    interiorBank.unload();
}

void SceneEstates::tick( float /*deltaTime*/, float /*totalTime*/ ) {
    // FUN_0042cdd0
    content.tick();
}

void SceneEstates::pause() {
    // FUN_007741c0
    suspend();
}

void SceneEstates::resume() {
    // FUN_007744b0
    content.resume();
}

void SceneEstates::suspend() {
    // FUN_007743c0
    content.suspend();
}
