#include "shared.h"
#include "gm_aftermarket.h"

#include "flash/gs_flash.h"
#include "flash/implementation/flash_player.h"
#include "game/mng_music.h"

GMAfterMarket::GMAfterMarket()
    : GameModeWorldBase(), sceneAfterMarket(), pFlashPlayer( nullptr ) {
    // FUN_007bbc40
    setTransitionFlags( 0x27 );
}

GMAfterMarket::~GMAfterMarket() {
    // FUN_007bbc00
}

void GMAfterMarket::initialize() {
    // FUN_007bbce0
    GameModeWorldBase::initialize();

    sceneAfterMarket.loadResources();
    sceneAfterMarket.getContent().setModeActive( true );
    sceneAfterMarket.getCarPark().setEnabled( true );

    gpMngMusic->setPlayFlags( 1 );
}

void GMAfterMarket::reset() {
    // FUN_007bbb10
    GameModeScene::reset();

    pFlashPlayer = gpFlash->getFlashPlayer( "SHOPS" );
    if ( pFlashPlayer != nullptr ) {
        pFlashPlayer->showFrame( "after_mkt", "/", true );
        sceneAfterMarket.placeCars();
    }
}

void GMAfterMarket::tick( float deltaTime, float totalTime ) {
    // FUN_007bbcb0
    GameModeScene::tick( deltaTime, totalTime );

    sceneAfterMarket.getContent().tick();

    sceneAfterMarket.getCarPark().tick( deltaTime );
}

void GMAfterMarket::destroy() {
    // FUN_007bbba0
    sceneAfterMarket.getContent().setModeActive( false );
    sceneAfterMarket.getCarPark().setEnabled( false );
    sceneAfterMarket.terminate();

    GameModeWorldBase::destroy();
}
