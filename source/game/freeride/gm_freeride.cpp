#include "shared.h"
#include "gm_freeride.h"

#include "gs_timer.h"
#include "tdu_instance.h"

#include "database/gs_database.h"
#include "game/gs_tutorial.h"
#include "game/mng_hud.h"
#include "game/scenaric/gs_scenaric.h"
#include "game/scenaric/entity/entity_extra_challenge.h"
#include "game/scenaric/local_player.h"
#include "game/vehicle/car_state.h"
#include "flash/mng_flash_localize.h"
#include "physics/vehicle/physics_vehicle.h"

GMFreeride::GMFreeride()
    : GameModeWorld( 6 ),
      mngBookmark(),
      mngFRMeetingIC(),
      mngPhotoMode(),
      mngCamGPS(),
      mngFontPlayers(),
      mngLogoDisplay(),
      mngMapScreenshot(),
      mngInGameMenu(),
      mngChat(),
      flashListSpots(),
      bResumedFromPauseMenu( false ),
      bEnterSpotClipVisible( false ),
      enterSpotClipTimer( 0.0f ),
      spotBank(),
      pExtraChallenges{},
      numExtraChallenges( 0 ) {
    // FUN_00793880
    if ( gpMngMapScreenshot == nullptr ) {
        gpMngMapScreenshot = &mngMapScreenshot;
    }

    registerManager( &mngBookmark );
    registerManager( &mngFRMeetingIC );
    registerManager( &mngPhotoMode );
    registerManager( &mngFontPlayers );
    registerManager( &mngLogoDisplay );

    if ( gbMapScreenshotsMngEnabled ) {
        registerManagerFirst( &mngMapScreenshot );
    }

    registerManager( &mngInGameMenu );
    registerManager( &mngCamGPS );
    registerManager( &mngChat );

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
}

void GMFreeride::initialize() {
    // FUN_00797340
    OTDU_UNIMPLEMENTED; // TODO: SCENARIC / FLASH (ad banners)

    if ( gpScenaric != nullptr ) {
        gpScenaric->setICEnabled( false );
    }

    GameModeWorld::initialize();
}

void GMFreeride::reset() {
    // FUN_00796830
    OTDU_UNIMPLEMENTED; // TODO: ONLINE (P2P free-ride manager) / GSAI (cop and
                        // bot pools, infringement transfer) / MngActor (player
                        // spawn placement)

    GameModeWorld::reset();

    OTDU_UNIMPLEMENTED; // TODO: SCENARIC/TUTORIAL
    gatherExtraChallenges();

    OTDU_UNIMPLEMENTED; // TODO: CAMERA (map-exit hand-over) / PHYSICS
                        // (postponed pause) / WORLD (sector draw toggle)
}

void GMFreeride::tick( float deltaTime, float totalTime ) {
    // FUN_00796260
    GameModeWorld::tick( deltaTime, totalTime );

    OTDU_UNIMPLEMENTED_ONCE; // TODO: SCENARIC (pending spot warp / spot cine
                             // launch) / CAMERA (map return) / MngFade
}

void GMFreeride::draw( float /*deltaTime*/, float /*totalTime*/ ) {
    // FUN_007941f0
    OTDU_UNIMPLEMENTED_ONCE; // TODO: GameLogic / FLASH
}

void GMFreeride::pause() {
    // FUN_00794900
    OTDU_UNIMPLEMENTED; // TODO: GSAI (bot/cop pools) / PLAYER_DATA (rental
                        // fee) / SCENARIC

    if ( gpTutorial != nullptr ) {
        gpTutorial->bEnabled = false;
    }

    GameModeWorld::pause();
}

void GMFreeride::destroy() {
    // FUN_00794860
    GameModeWorld::destroy();

    OTDU_UNIMPLEMENTED; // TODO: GSAI (bots/cops) / SCENARIC (spot cine slot
                        // release) / FLASH (ad banners)

    const ScenaricBank::eBankState bankState = spotBank.getState();
    if ( bankState != ScenaricBank::BBS_Unused &&
         bankState != ScenaricBank::BBS_LoadFailed && gpScenaric != nullptr ) {
        gpScenaric->unloadBank( &spotBank );
    }
}

void GMFreeride::onResourceStreamed() {
    // FUN_007940f0
    GameModeWorld::onResourceStreamed();

    FlashPlayer *pPlayer = mngHudChallenge.getHudPlayer();
    if ( pPlayer == nullptr ) {
        return;
    }

    mngBookmark.setFlashPlayer( pPlayer );
    mngFRMeetingIC.setFlashPlayer( pPlayer );
    mngInGameMenu.setFlashPlayer( pPlayer );

    if ( gpMngFlashLocalize != nullptr ) {
        OTDU_UNIMPLEMENTED; // TODO: PLAYER_DATA
    }

    OTDU_UNIMPLEMENTED; // TODO: PLAYER_DATA

    mngFlash.addWidget( "spot", &flashListSpots );
    flashListSpots.setPlayer( pPlayer, "/menus/ss_menus/array/spots" );
    flashListSpots.setFlashPlayer( pPlayer );

    if ( gpTutorial != nullptr ) {
        gpTutorial->bEnabled = true;
    }

    mngPhotoMode.setFlashPlayer( pPlayer );
}

bool GMFreeride::executeAction( int32_t param_2, void *param_3 ) {
    // FUN_00794ff0
    OTDU_UNIMPLEMENTED; // TODO: SCENARIC / MngFade / PHYSICS / DATABASE

    return false;
}

void GMFreeride::gatherExtraChallenges() {
    // FUN_00792e60
    numExtraChallenges = 0;

    if ( gpScenaric == nullptr ) {
        return;
    }

    ScenaricBank *pBank = gpScenaric->getWorldExtraChallengesBank();
    for ( int32_t i = 0; i < pBank->getEntityCount(); i++ ) {
        Entity *pEntity = pBank->getEntity( i );
        if ( pEntity == nullptr ) {
            continue;
        }

        const eEntityType type = pEntity->getType();
        if ( type != eEntityType::ET_SHOG && type != eEntityType::ET_HIHI &&
             type != eEntityType::ET_TRAN && type != eEntityType::ET_CVOY ) {
            continue;
        }

        OTDU_UNIMPLEMENTED_ONCE; // TODO: PLAYER_DATA — that per-challenge state
                                 // array is not ported.

        if ( !gbSimuModeEnabled ) {
            mngActor.registerActor( pEntity );
        }

        if ( numExtraChallenges < kMaxNumExtraChallenges ) {
            pExtraChallenges[numExtraChallenges] = pEntity;
            numExtraChallenges++;
        }
    }
}

bool GMFreeride::onMessage( FlashMessage *pMessage ) {
    // FUN_00796f40
    if ( GameModeWorld::onMessage( pMessage ) ||
         mngPhotoMode.onMessage( pMessage ) ||
         mngBookmark.onMessage( pMessage ) ) {
        return true;
    }

    if ( gpMngInGameMenu != nullptr &&
         gpMngInGameMenu->onMessage( pMessage ) ) {
        return true;
    }

    switch ( *pMessage ) {
    case FlashMessage( "inic" ):
    case FlashMessage( "goic" ):
    case FlashMessage( "icbo" ):
    case FlashMessage( "noic" ):
        return mngFRMeetingIC.onMessage( pMessage );
    default:
        break;
    }

    OTDU_UNIMPLEMENTED; // TODO: the "gogp"/"goho"/"home"/"gosh"/"gosp"/"goid"/
                        // "goch"/"acex"/"acti"/"caex"/"info"/"noch"/"ssmn"
                        // handlers (spot entry + extra challenges)

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

    bResumedFromPauseMenu = true;

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

        if ( ImGui::Button( "Over lighthouse car park" ) ) {
            target[0] = 548.4f;
            target[1] = 38.7f;
            target[2] = 17767.62f;
        }
        ImGui::SameLine();
        if ( ImGui::Button( "Pearl City" ) ) {
            target[0] = 31846.57f;
            target[1] = 20.22f;
            target[2] = 37179.66f;
        }
        ImGui::SameLine();
        if ( ImGui::Button( "HMap LOD Bug Repro" ) ) {
            target[0] = 48413.344f;
            target[1] = 309.431f;
            target[2] = 41670.789f;
        }

        ImGui::SetNextItemWidth( 300.0f );
        ImGui::DragFloat3( "World XYZ", target, 1000.0f );
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
