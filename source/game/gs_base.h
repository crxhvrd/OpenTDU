#pragma once

class TestDriveGameInstance;

#ifdef OTDU_USE_IMGUI
#include "imgui.h"
#endif

class GameSystem {
public:
    inline void setStaticallyAllocated( bool param_1 ) {
        bStaticallyAllocated = param_1;
    }
    inline bool isStaticallyAllocated() const { return bStaticallyAllocated; }

#ifdef OTDU_USE_IMGUI
    inline bool hasDebugMenu() const { return bDebugMenuImplemented; }
#endif

public:
    GameSystem();
    virtual ~GameSystem() {}

    virtual const char *getName() const { return "NoName"; }
    virtual bool initialize( TestDriveGameInstance * );
    virtual void tick( float totalTime, float deltaTime ) {};
    virtual void draw( float deltaTime ) {};
    virtual void terminate() = 0;
    virtual void pause() {};
    virtual void reset();

    bool isPaused() const;
    inline void setPaused( const bool bState ) { bPaused = bState; }
    uint32_t getIndex() const;

#ifdef OTDU_USE_IMGUI
    virtual void drawDebug( float deltaTime ) { bDebugMenuImplemented = false; }
#endif

protected:
    TestDriveGameInstance *pGameInstance;
    uint32_t index;
    uint8_t bPaused : 1;
    uint8_t bStaticallyAllocated : 1;

#ifdef OTDU_USE_IMGUI
    bool bDebugMenuImplemented;
#endif
};
