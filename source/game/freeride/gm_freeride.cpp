#include "shared.h"
#include "gm_freeride.h"

#include "gs_timer.h"
#include "tdu_instance.h"

#include "database/gs_database.h"
#include "game/scenaric/gs_scenaric.h"
#include "game/scenaric/local_player.h"
#include "game/vehicle/car_state.h"

GMFreeride::GMFreeride()
    : GameModeWorld( 6 ),
      mngBookmark(),
      // mngFRMeetingIC(),
      mngPhotoMode(),
      mngCamGPS(),
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
    registerManager( &mngCamGPS );
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

    ImGui::SeparatorText( "Teleport" );
    CarState *pCar = ( gpScenaric != nullptr )
                         ? gpScenaric->getLocalPlayer().getCarState()
                         : nullptr;
    if ( pCar == nullptr ) {
        ImGui::TextDisabled( "No local-player car spawned." );
    } else {
        // Absolute world position (row 3 of the body's world matrix).
        const Eigen::Vector4f cur = pCar->getBody()->WorldMatrix.row( 3 );
        ImGui::Text( "Current: (%.1f, %.1f, %.1f)", cur.x(), cur.y(), cur.z() );

        // Seed the input fields from the current position on first display.
        static float target[3] = { 0.0f, 0.0f, 0.0f };
        static bool bSeeded = false;
        if ( !bSeeded ) {
            target[0] = cur.x();
            target[1] = cur.y();
            target[2] = cur.z();
            bSeeded = true;
        }

        ImGui::SeparatorText( "Bookmarks" );
        if ( ImGui::Button( "Manoa Falls" ) ) {
            target[0] = 41464.73f;
            target[1] = 88.72f;
            target[2] = 25543.82f;
        }
        ImGui::SameLine();
        if ( ImGui::Button( "Secret Island Teleporter" ) ) {
            target[0] = 63519.45f;
            target[1] = 223.50f;
            target[2] = 46305.20f;
        }
        ImGui::SameLine();
        if ( ImGui::Button( "Oahu Raceway" ) ) {
            target[0] = 34569.15f;
            target[1] = 3.06f;
            target[2] = 45154.43f;
        }

        if ( ImGui::Button( "Secret Island" ) ) {
            target[0] = 64285.047f;
            target[1] = 37.758f;
            target[2] = 44133.129f;
        }
        ImGui::SameLine();
        if ( ImGui::Button( "Japanese Temple" ) ) {
            target[0] = 44802.07f;
            target[1] = 88.87f;
            target[2] = 32675.04f;
        }
        ImGui::SameLine();
        if ( ImGui::Button( "Observatory" ) ) {
            target[0] = 10643.63f;
            target[1] = 864.36f;
            target[2] = 23641.65f;
        }

        if ( ImGui::Button( "AI Trainer Sector" ) ) {
            target[0] = 9122.41f;
            target[1] = 0.65f;
            target[2] = 9847.77f;
        }
        ImGui::SameLine();
        if ( ImGui::Button( "Tunnel" ) ) {
            target[0] = 50037.879f;
            target[1] = 356.729f;
            target[2] = 40454.828f;
        }
        ImGui::SameLine();
        if ( ImGui::Button( "Diamond Head" ) ) {
            target[0] = 48442.840f;
            target[1] = 190.627f;
            target[2] = 52137.379f;
        }

        ImGui::SetNextItemWidth( 300.0f );
        ImGui::InputFloat3( "World XYZ", target );
        if ( ImGui::Button( "Use current" ) ) {
            target[0] = cur.x();
            target[1] = cur.y();
            target[2] = cur.z();
        }
        ImGui::SameLine();
        if ( ImGui::Button( "Teleport" ) ) {
            gRequestedPlayerTeleportPos =
                Eigen::Vector3f( target[0], target[1], target[2] );
            gRequestedPlayerTeleport = true;
        }
    }
}
#endif
