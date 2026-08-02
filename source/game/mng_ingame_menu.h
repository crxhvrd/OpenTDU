#pragma once

#include <cstdint>

#include "game/mng_base.h"

class FlashPlayer;
class TestDriveGameInstance;
struct FlashMessage;

// State of the in-game menu open/close animation
enum eInGameMenuState : int32_t {
    IGMS_Closed = 0,
    IGMS_Opening = 1,
    IGMS_Open = 2,
    IGMS_Closing = 3,
};

// Which in-game menu page is currently open
enum eInGameMenu : int32_t {
    IGM_None = 0,
    IGM_Players = 1, // "players"
    IGM_Radio = 2,   // "radio"
    IGM_Driving = 3, // "driving"
    IGM_Windows = 4, // "windows"
    IGM_Chrono = 5,  // "chrono"
};

// MngInGameMenu — the in-game (pause / interaction) menu manager.
class MngInGameMenu : public Manager {
public:
    inline int32_t getWindowsDirection() const { return WindowsDirection; }

public:
    MngInGameMenu();
    ~MngInGameMenu();

    void tick( float, float ) override;
    void reset() override;
    void pause() override;
    bool onMessage( FlashMessage *pMessage, FlashPlayer *pPlayer = nullptr );

private:
    FlashPlayer *pFlashVariable;
    bool bEnabled;
    eInGameMenuState StateMenu;
    eInGameMenu OpenedMenu;
    float MenuCloseTimeLeft;
    int32_t CurrentPlayer;
    eInGameMenuState LockStateMenu;
    bool pbAskingToLockWith[16];
    float pTimeoutAskingToLockWith[16];
    int32_t CurrentAskingLockPlayer;
    float TimeoutAskingLockPlayer;
    bool pbAcceptedToIsolateWith[16];
    float TimeoutAskingToIsolate;
    int32_t CurrentAskingToIsolate;
    float TimeoutAskingIsolate;
    bool bMustSaveOnClose;
    int32_t WindowsDirection;
    bool bHasWindows;
    float MaxSpeedImperial;
    float Counter;
    bool bDrivingModeEnabled;

private:
    void updateInput();
    bool canOpenMenu();
    bool changeMenu( eInGameMenu menu );
    void close();
    void toggleWindows( bool bPlayAnim );
    void changeDrivingMode( bool bNext, bool bPlayAnim );
    void updateMenuTimeout();
    void refreshHasWindows();
    void refreshDrivingHelpLabel();
    void resetSpeedChrono( bool bArm );
};

extern MngInGameMenu *gpMngInGameMenu;
