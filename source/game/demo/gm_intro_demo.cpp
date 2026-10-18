#include "shared.h"
#include "gm_intro_demo.h"

#include "tdu_instance.h"
#include "flash/gs_flash.h"
#include "flash/implementation/flash_player.h"
#include "player_data/gs_playerdata.h"

GMIntroDemo::GMIntroDemo() : GMDemoBase() {
    // FUN_00406590
}

GMIntroDemo::~GMIntroDemo() {}

void GMIntroDemo::reset() {
    // FUN_009bb460
    GameModeScene::reset();

    pDemoPlayer = gpFlash->getFlashPlayer( "DEMO_END" );
    if ( pDemoPlayer != nullptr ) {
        pDemoPlayer->showFrame( "intro", "/", true );
    }

    setPicturePrefix( "intr" );

    if ( pDemoPlayer != nullptr ) {
        pDemoPlayer->setVariableValue(
            "/:PadConfig",
            uint32_t( gpPlayerData->getLocalPlayerData().PadConfig ) );
    }
}

void GMIntroDemo::changeGameMode() {
    // FUN_009bb430
    gpTestDriveInstance->setNextGameMode( GM_Living );
    requestExit();
}
