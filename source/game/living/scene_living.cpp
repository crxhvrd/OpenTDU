#include "shared.h"
#include "scene_living.h"

#include "tdu_instance.h"

#include "config/gs_config.h"
#include "filesystem/gs_file.h"
#include "game/scenaric/gs_scenaric.h"
#include "game/scenaric/spots.h"
#include "render/gs_render.h"
#include "render/render_scene.h"

static constexpr const char *kDefaultInteriorBank = "Interior/LPH_L3A.bnk";

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

        OTDU_UNIMPLEMENTED; // TODO: CAMERA
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
    OTDU_UNIMPLEMENTED; // TODO: CAMERA (restore the debug fly-camera toggle)

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
    OTDU_UNIMPLEMENTED_ONCE; // TODO: CAMERA
}

void SceneLiving::pause() {
    // FUN_009c3790
    suspend();
}

void SceneLiving::resume() {
    // FUN_007744b0
    if ( gpRender == nullptr ) {
        return;
    }

    RenderScene *pNearScene =
        gpRender->getRenderPassAtIndex( eRenderPass::RP_Near ).pScene;
    RenderScene *pDOFScene =
        gpRender->getRenderPassAtIndex( eRenderPass::RP_FakeDOF ).pScene;
    RenderScene *pDOFPrePassScene =
        gpRender->getRenderPassAtIndex( eRenderPass::RP_FakeDOFPrePass ).pScene;

    if ( pNearScene == nullptr || pDOFScene == nullptr ) {
        return;
    }

    if ( content.getRootNode() == nullptr ) {
        return;
    }

    content.setDrawInDOFPass( true );

    OTDU_UNIMPLEMENTED_ONCE; // TODO: POSTFX

    if ( pDOFPrePassScene != nullptr ) {
        pDOFPrePassScene->setEnabled( true );
    }
    pDOFScene->setEnabled( true );
}

void SceneLiving::suspend() {
    // FUN_007743c0
    if ( gpRender == nullptr || content.getRootNode() == nullptr ) {
        return;
    }

    RenderScene *pDOFScene =
        gpRender->getRenderPassAtIndex( eRenderPass::RP_FakeDOF ).pScene;
    RenderScene *pDOFPrePassScene =
        gpRender->getRenderPassAtIndex( eRenderPass::RP_FakeDOFPrePass ).pScene;

    content.setDrawInDOFPass( false );

    OTDU_UNIMPLEMENTED_ONCE; // TODO: POSTFX (glow/radial-blur disable)

    if ( pDOFPrePassScene != nullptr ) {
        pDOFPrePassScene->setEnabled( false );
    }
    if ( pDOFScene != nullptr ) {
        pDOFScene->setEnabled( false );
    }
}
