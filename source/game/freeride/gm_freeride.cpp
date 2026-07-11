#include "shared.h"
#include "gm_freeride.h"

#include "gs_timer.h"
#include "tdu_instance.h"

#include "database/gs_database.h"

GMFreeride::GMFreeride()
    : GameModeWorld( 6 ),
      mngBookmark(),
      // mngFRMeetingIC(),
      mngPhotoMode(),
      // mngCamGPS(),
      // mngFontPlayers(),
      mngLogoDisplay(),
      mngMapScreenshot(),
      mngInGameMenu(),
      mngChat() {
    // FUN_00793880
    registerManager( &mngBookmark );
    // registerManager( &mngFRMeetingIC );
    registerManager( &mngPhotoMode );
    // registerManager( &mngFontPlayers );
    registerManager( &mngLogoDisplay );

    // The map-screenshot tool manager is only ticked when explicitly enabled.
    if ( gbMapScreenshotsMngEnabled ) {
        registerManager( &mngMapScreenshot );
    }

    registerManager( &mngInGameMenu );
    // registerManager( &mngCamGPS );
    registerManager( &mngChat );

    OTDU_UNIMPLEMENTED; // TODO: SCENARICS

    mngFlash.setWantsAsyncInstall( true );
    mngBookmark.setMngFlash( &mngFlash );

    // Player-car game mechanic (overrides the GameModeWorld defaults).
    gameMechType = 4;    // player car
    gameMechID = 0xc0c1; // GMFreeRide-specific mechanic id

    OTDU_UNIMPLEMENTED; // TODO: SCENARIC

    // When returning from the map screen, use a fast single-tick transition.
    if ( gpTestDriveInstance != nullptr &&
         gpTestDriveInstance->getPreviousGameMode() == GM_Map ) {
        transitionFlags = 0x11;
        numTicksPerTransition = 1;
    }
}

GMFreeride::~GMFreeride() {
    // FUN_00793750
    // Restore the game speed that was saved when the pause menu was opened.
    if ( gpTimer != nullptr ) {
        gpTimer->GameSpeed = gpTimer->PreviousGameSpeed;
    }

    OTDU_UNIMPLEMENTED;
}

bool GMFreeride::onMessage( FlashMessage *pMessage ) {
    // FUN_00796f40
    if ( GameModeWorld::onMessage( pMessage ) ) {
        return true;
    }

    OTDU_UNIMPLEMENTED;

    return false;
}

void GMFreeride::onPauseMenuRequested( bool bOpened ) {
    // FUN_007934f0
    GameModeWorld::onPauseMenuRequested( bOpened );

    OTDU_UNIMPLEMENTED; // TODO: ONLINE
}

void GMFreeride::onMenuPauseExit( bool bRestoreState ) {
    // FUN_00793460
    GameModeWorld::onMenuPauseExit( bRestoreState );

    OTDU_UNIMPLEMENTED; // TODO: ONLINE/PHYSICS
}

#ifdef OTDU_USE_IMGUI
void GMFreeride::drawDebug( float deltaTime ) {
    if ( gpDatabase != nullptr ) {
        const GameDatabase &gdb = gpDatabase->getGameDB();
        const int32_t numCars = static_cast<int32_t>( gdb.pNumItems[1] );
        if ( numCars > 0 && gdb.DBCarData != nullptr ) {
            static int32_t carSlot = 0;
            carSlot = Clamp( carSlot, 0, numCars - 1 );
            ImGui::Text( "Test vehicle: slot %d/%d  ->  CarID %d", carSlot,
                         numCars - 1, gdb.DBCarData[carSlot].CarID );
            if ( ImGui::Button( "< Prev car" ) ) {
                carSlot = ( carSlot + numCars - 1 ) % numCars;
                gRequestedPlayerCarID = gdb.DBCarData[carSlot].CarID;
            }
            ImGui::SameLine();
            if ( ImGui::Button( "Next car >" ) ) {
                carSlot = ( carSlot + 1 ) % numCars;
                gRequestedPlayerCarID = gdb.DBCarData[carSlot].CarID;
            }
            ImGui::SameLine();
            ImGui::SetNextItemWidth( 140.0f );
            ImGui::SliderInt( "##carslot", &carSlot, 0, numCars - 1 );
            ImGui::SameLine();
            if ( ImGui::Button( "Spawn" ) ) {
                gRequestedPlayerCarID = gdb.DBCarData[carSlot].CarID;
            }
        }
    }
}
#endif
