#include "shared.h"
#include "gs_debug.h"

#include "gs_timer.h"
#include "render/text_renderer.h"
#include "gs_imgui.h"
#include "game/gm_base.h"
#include "tdu_instance.h"

GSDebug::GSDebug() {}

GSDebug::~GSDebug() {}

bool GSDebug::initialize( TestDriveGameInstance * ) { return true; }

void GSDebug::tick( float totalTime, float deltaTime ) {
    const bool *pKeyStates = SDL_GetKeyboardState( nullptr );

    const bool bF2Down =
        ( pKeyStates != nullptr ) && pKeyStates[SDL_SCANCODE_F2];
    if ( bF2Down && !bPauseTriggered ) {
        bGamePaused = !bGamePaused;
        gpTimer->GameSpeed = bGamePaused ? 0.0f : 1.0f;
    }
    bPauseTriggered = bF2Down;

    const bool bF1Down =
        ( pKeyStates != nullptr ) && pKeyStates[SDL_SCANCODE_F1];
    if ( bF1Down && !bFocusHasChanged ) {
        bVisible = !bVisible;
        gpImgui->setFocus( bVisible );
    }
    bFocusHasChanged = bF1Down;

    const bool bF3Down =
        ( pKeyStates != nullptr ) && pKeyStates[SDL_SCANCODE_F3];
    if ( bF3Down && !bGMDebugMenuVisibleTriggered ) {
        bGMDebugMenuVisible = !bGMDebugMenuVisible;
        gpImgui->setFocus( bGMDebugMenuVisible );
    }
    bGMDebugMenuVisibleTriggered = bF3Down;

    if ( bGamePaused ) {
        gTextRenderer.setColor( gColorRed );
        gTextRenderer.pushCommand( 256, 5, "[PAUSED]" );
    }

#ifdef OTDU_USE_IMGUI
    if ( bGMDebugMenuVisible && gpActiveGameMode != nullptr ) {
        if ( ImGui::Begin( "Game Mode Debug Menu" ) ) {
            gpActiveGameMode->drawDebug( deltaTime );
        }
        ImGui::End();
    }

    if ( bVisible ) {
        gpTestDriveInstance->drawDebugMenu( deltaTime );
    }
#endif
}

void GSDebug::terminate() {}
