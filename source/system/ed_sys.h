#pragma once

#include "core/ed_base.h"

enum class eFullscreenMode;

#if defined( OTDU_USE_SDL )
typedef void ( *edSysInputCallback )( uint32_t codepoint );
#endif

struct DisplayMode {
    uint32_t Width = 0u;
    uint32_t Height = 0u;
    uint32_t RefreshRate = 0u;
};

class edSys : public edBase {
public:
    inline bool isWindowActivated() const { return bWindowActivated; }
    inline void *getWindow() const { return pWindow; }

public:
    edSys();
    ~edSys();

    int32_t initialize() override;
    void destroy() override;

    void pollEvents();

    void resizeGameWindow( const uint32_t width,
                           const uint32_t height,
                           const eFullscreenMode fullscreenMode,
                           const bool bWindowed );
    bool isWindowStateApplied( const eFullscreenMode fullscreenMode,
                               const bool bWindowed ) const;
    void showCursor( bool param_1 );
    void setRelativeMouseMode( bool bEnabled );

    void retrieveCursorClientPos( double &cursorX, double &cursorY );
    void bindCharInputCallback( edSysInputCallback param_1 );

    int32_t getMonitorRefreshRate();
    void enumerateDisplayModes( std::vector<DisplayMode> &outModes );
    float getDPIScale();

public:
#ifdef OTDU_USE_SDL
    struct SDL_Window *pWindow;
    edSysInputCallback textInputCallback;
#endif

    uint64_t startTimeMonotonic;

    uint32_t state;
    uint8_t bCreateWindow : 1;
    uint8_t bWindowActivated : 1;

private:
    int32_t appliedFullscreenMode;
    bool bAppliedWindowed;

private:
    void centerWindow();
};

extern edSys gEdSys;

extern uint32_t gWindowWidth;
extern uint32_t gWindowHeight;
extern int32_t gPickedRefreshRate;
extern DisplayMode gBootDisplayMode;
extern bool gWindowSizeDirty;
// OTDU_ADDITION: display HDR headroom changed (brightness, display move)
extern bool gWindowHDRDirty;
extern bool gbIgnoreFocusLoss;

static inline float GetScreenAspectRatio() {
    return static_cast<float>( gWindowWidth ) /
           static_cast<float>( gWindowHeight );
}
