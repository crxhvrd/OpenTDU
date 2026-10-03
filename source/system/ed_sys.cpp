#include "shared.h"
#include "ed_sys.h"

#include "config/gs_config.h"
#include "core/time.h"
#include "tdu_instance.h"
#include "audio/implementation/ed_audio.h"

#if defined( OTDU_USE_SDL )
#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#include "input/ed_dev.h"
#include "input/devices/mouse.h"
#endif

#ifdef OTDU_USE_IMGUI
#include "debug/gs_imgui.h"
#ifndef OTDU_USE_SDL
#error "Only SDL3 is currently supported"
#endif
#include "backends/imgui_impl_sdl3.h"
#endif

static constexpr const char *kedSysName = "edSys"; // DAT_00f3e924

edSys gEdSys = {}; // DAT_00fa9d30

uint32_t gWindowWidth = 0u;     // DAT_00fac8d4
uint32_t gWindowHeight = 0u;    // DAT_00fac8d8
int32_t gPickedRefreshRate = 0; // DAT_00fac8ec

// DAT_01041a34 / DAT_01041a38 / DAT_01041a30
DisplayMode gBootDisplayMode = {};

bool gWindowSizeDirty = false;

edSys::edSys()
    : edBase( kedSysName, 1, 0xd, 0 ),
      pWindow( nullptr )
#if defined( OTDU_USE_SDL )
      ,
      textInputCallback( nullptr )
#endif
      ,
      state( 0 ),
      bCreateWindow( true ),
      bWindowActivated( true ) {
    // FUN_00432f30
}

edSys::~edSys() {}

int32_t edSys::initialize() {
    // FUN_00432fb0
#if defined( OTDU_USE_SDL )
    if ( !SDL_Init( SDL_INIT_VIDEO | SDL_INIT_GAMEPAD | SDL_INIT_AUDIO ) ) {
        OTDU_LOG_ERROR( "Failed to initialize SDL: '%s'!\n", SDL_GetError() );
        state = 4;
        return state;
    }
    if ( bCreateWindow ) {
        pWindow = SDL_CreateWindow( "OpenTDU", 1280, 720,
                                    SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE );
        if ( pWindow == nullptr ) {
            OTDU_LOG_ERROR( "Failed to create SDL window: '%s'!\n",
                            SDL_GetError() );
            state = 4;
            return state;
        }
    }
#endif

    centerWindow();

    gWindowWidth = 1280u;
    gWindowHeight = 720u;

    startTimeMonotonic = GetTimeMonotonic();
    bEnabled = true;
    return 0;
}

void edSys::destroy() {
    // FUN_00433080
#if defined( OTDU_USE_SDL )
    SDL_DestroyWindow( pWindow );
    SDL_Quit();
#endif
    bEnabled = false;
}

void edSys::pollEvents() {
    // FUN_00433260
#if defined( OTDU_USE_SDL )
    SDL_Event event;
    while ( SDL_PollEvent( &event ) ) {
#ifdef OTDU_USE_IMGUI
        if ( gpImgui->hasFocus() ) {
            ImGui_ImplSDL3_ProcessEvent( &event );
        }
#endif
        switch ( event.type ) {
        case SDL_EVENT_QUIT: {
            gbRequestedExit = true;
            break;
        }
        case SDL_EVENT_WINDOW_FOCUS_GAINED: {
            gEdSys.bWindowActivated = true;
            gEdAudio.setOutputMuted( false );
            break;
        }
        case SDL_EVENT_WINDOW_FOCUS_LOST: {
            gEdSys.bWindowActivated = false;
            gEdAudio.setOutputMuted( true );
            break;
        }
        case SDL_EVENT_TEXT_INPUT: {
            if ( textInputCallback != nullptr ) {
                int32_t i = 0;
                while ( event.text.text[i] != '\0' ) {
                    textInputCallback( event.text.text[i] );
                    i++;
                }
            }
            break;
        }
        case SDL_EVENT_MOUSE_WHEEL: {
            DeviceMouse *pMouse = static_cast<DeviceMouse *>(
                gEdDev.getDevice( eDeviceType::DT_Mouse, 0 ) );
            if ( pMouse != nullptr ) {
                pMouse->addScrollDelta( event.wheel.y );
            }
            break;
        }
        case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
        case SDL_EVENT_WINDOW_RESIZED:
        case SDL_EVENT_WINDOW_MAXIMIZED:
        case SDL_EVENT_WINDOW_RESTORED: {
            int32_t pixelWidth = 0;
            int32_t pixelHeight = 0;
            SDL_GetWindowSizeInPixels( pWindow, &pixelWidth, &pixelHeight );
            if ( pixelWidth > 0 && pixelHeight > 0 ) {
                gWindowWidth = static_cast<uint32_t>( pixelWidth );
                gWindowHeight = static_cast<uint32_t>( pixelHeight );
                gWindowSizeDirty = true;
            }
            break;
        }
        // These are handled via SDL state polling (SDL_GetKeyboardState /
        // SDL_GetMouseState) and do not require event-based processing.
        case SDL_EVENT_KEY_DOWN: {
            if ( textInputCallback != nullptr ) {
                switch ( event.key.key ) {
                case SDLK_BACKSPACE:
                    textInputCallback( '\b' );
                    break;
                case SDLK_TAB:
                    textInputCallback( '\t' );
                    break;
                case SDLK_RETURN:
                case SDLK_KP_ENTER:
                    textInputCallback( '\r' );
                    break;
                default:
                    break;
                }
            }
            break;
        }
        case SDL_EVENT_MOUSE_MOTION:
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
        case SDL_EVENT_MOUSE_BUTTON_UP:
        case SDL_EVENT_KEY_UP:
            break;
        default:
            break;
        }
    }
#endif
}

void edSys::resizeGameWindow( const uint32_t width,
                              const uint32_t height,
                              const eFullscreenMode fullscreenMode,
                              const bool bWindowed ) {
    gWindowWidth = width;
    gWindowHeight = height;

#if defined( OTDU_USE_SDL )
    SDL_SetWindowFullscreen( pWindow, !bWindowed );

    if ( bWindowed ) {
        // SDL ignores size requests on a maximized window
        if ( SDL_GetWindowFlags( pWindow ) & SDL_WINDOW_MAXIMIZED ) {
            SDL_RestoreWindow( pWindow );
        }
        SDL_SetWindowSize( pWindow, width, height );
        centerWindow();
    } else {
        SDL_DisplayID mainDisplay = SDL_GetPrimaryDisplay();
        switch ( fullscreenMode ) {
        case eFullscreenMode::FM_Manual: {
            SDL_DisplayMode displayMode = {};

            bool bFoundDisplayMode = SDL_GetClosestFullscreenDisplayMode(
                mainDisplay, width, height,
                static_cast<float>( gPickedRefreshRate ), true, &displayMode );
            OTDU_ASSERT( bFoundDisplayMode );

            SDL_SetWindowFullscreenMode( pWindow, &displayMode );
            break;
        }
        case eFullscreenMode::FM_Auto:
        case eFullscreenMode::FM_Desktop:
            SDL_SetWindowFullscreenMode(
                pWindow, SDL_GetWindowFullscreenMode( pWindow ) );
            break;
        default:
            OTDU_UNIMPLEMENTED;
            break;
        };
    }

    SDL_SyncWindow( pWindow );

    int32_t pixelWidth = 0;
    int32_t pixelHeight = 0;
    SDL_GetWindowSizeInPixels( pWindow, &pixelWidth, &pixelHeight );
    if ( pixelWidth > 0 && pixelHeight > 0 &&
         ( uint32_t( pixelWidth ) != width ||
           uint32_t( pixelHeight ) != height ) ) {
        gWindowWidth = static_cast<uint32_t>( pixelWidth );
        gWindowHeight = static_cast<uint32_t>( pixelHeight );
        gWindowSizeDirty = true;
    }
#endif
}

void edSys::showCursor( bool param_1 ) {
#if defined( OTDU_USE_SDL )
    if ( param_1 ) {
        SDL_ShowCursor();
    } else {
        SDL_HideCursor();
    }
#endif
}

void edSys::setRelativeMouseMode( bool bEnabled ) {
#if defined( OTDU_USE_SDL )
    if ( pWindow != nullptr ) {
        SDL_SetWindowRelativeMouseMode( pWindow, bEnabled );
    }
#endif
}

void edSys::retrieveCursorClientPos( double &cursorX, double &cursorY ) {
#if defined( OTDU_USE_SDL )
    float screenX, screenY;
    SDL_GetGlobalMouseState( &screenX, &screenY );

    int32_t windowX, windowY;
    SDL_GetWindowPosition( pWindow, &windowX, &windowY );

    cursorX = screenX - windowX;
    cursorY = screenY - windowY;
#endif
}

void edSys::bindCharInputCallback( edSysInputCallback param_1 ) {
#if defined( OTDU_USE_SDL )
    textInputCallback = param_1;
#endif
}

void edSys::enumerateDisplayModes( std::vector<DisplayMode> &outModes ) {
    outModes.clear();

#if defined( OTDU_USE_SDL )
    const SDL_DisplayID mainDisplay = SDL_GetPrimaryDisplay();

    int32_t numModes = 0;
    SDL_DisplayMode **ppModes =
        SDL_GetFullscreenDisplayModes( mainDisplay, &numModes );
    if ( ppModes == nullptr ) {
        return;
    }

    for ( int32_t i = 0; i < numModes; i++ ) {
        const SDL_DisplayMode *pMode = ppModes[i];
        if ( pMode == nullptr ) {
            continue;
        }

        // The original rejects anything below 640x480.
        if ( pMode->w < 640 || pMode->h < 480 ) {
            continue;
        }

        DisplayMode mode = {};
        mode.Width = uint32_t( pMode->w );
        mode.Height = uint32_t( pMode->h );
        mode.RefreshRate = uint32_t( pMode->refresh_rate + 0.5f );

        const bool bAlreadyListed =
            std::find_if( outModes.begin(), outModes.end(),
                          [&mode]( const DisplayMode &other ) {
                              return other.Width == mode.Width &&
                                     other.Height == mode.Height &&
                                     other.RefreshRate == mode.RefreshRate;
                          } ) != outModes.end();
        if ( !bAlreadyListed ) {
            outModes.push_back( mode );
        }
    }

    SDL_free( ppModes );

    std::sort( outModes.begin(), outModes.end(),
               []( const DisplayMode &l, const DisplayMode &r ) {
                   if ( l.Width != r.Width ) {
                       return l.Width < r.Width;
                   }
                   if ( l.Height != r.Height ) {
                       return l.Height < r.Height;
                   }
                   return l.RefreshRate < r.RefreshRate;
               } );
#endif
}

int32_t edSys::getMonitorRefreshRate() {
#if defined( OTDU_USE_SDL )
    SDL_DisplayID mainDisplay = SDL_GetPrimaryDisplay();
    const SDL_DisplayMode *pCurDisplayMode =
        SDL_GetCurrentDisplayMode( mainDisplay );

    return static_cast<int32_t>( pCurDisplayMode->refresh_rate );
#endif
}

float edSys::getDPIScale() {
#if defined( OTDU_USE_SDL )
    return SDL_GetDisplayContentScale( SDL_GetPrimaryDisplay() );
#else
    return 1.0f;
#endif
}

void edSys::centerWindow() {
#if defined( OTDU_USE_SDL )
    SDL_SetWindowPosition( pWindow, SDL_WINDOWPOS_CENTERED,
                           SDL_WINDOWPOS_CENTERED );
#endif
}
