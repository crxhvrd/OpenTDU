#pragma once

#include "game/gs_base.h"

class GSDebug : public GameSystem {
public:
    const char *getName() const override { return "Service : Debug"; }

public:
    GSDebug();
    ~GSDebug();

    bool initialize( TestDriveGameInstance * ) override;
    void tick( float totalTime, float deltaTime ) override;
    void terminate() override;

private:
    bool bGamePaused = false;
    bool bPauseTriggered = false;

    bool bVisible = false;
    bool bFocusHasChanged = false;

    bool bGMDebugMenuVisible = false;
    bool bGMDebugMenuVisibleTriggered = false;
};
