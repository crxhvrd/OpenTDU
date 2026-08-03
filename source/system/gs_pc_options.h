#pragma once

#include "core/popup_callback.h"
#include "flash/entities/flash_entity.h"
#include "game/gs_base.h"

#include "mng_options.h"

class FlashPlayer;
struct PlayerConfiguration;

// Entries of the CONFIGPC main menu ('/options_root/cursor:choice'); the same
// enum drives GSPCOptions::onMainMenuSelection.
enum eMainMenuOption : int32_t {
    MMO_Continue = 0,      // resume / "clear current game ?" prompt
    MMO_QuitToDesktop = 1, // "return to Windows ?" prompt
    MMO_ChangeProfile = 2,
    MMO_Options = 3, // hand over to the options screen (MngOptions)
    MMO_Quit = 4,    // quit to the main menu (settles police fines first)
    MMO_HardcoreMode = 5,
};

class GSPCOptions : public GameSystem,
                    public FlashEntity,
                    public PopupCallback {
public:
    const char *getName() const override { /* FUN_0042b370 */
        return "PC_Options";
    }

    inline bool isMainMenuOpened() const { return bMainMenuOpened; }
    inline void setMainMenuAllowed( const bool bAllowed ) {
        bCanOpenMainMenu = bAllowed;
    }

public:
    GSPCOptions();
    ~GSPCOptions();

    bool initialize( TestDriveGameInstance * ) override;
    void tick( float totalTime, float deltaTime ) override;
    void terminate() override;
    void pause() override;
    void reset() override;

    bool onMessage( FlashMessage *pMessage, FlashPlayer *pPlayer ) override;
    void openMainMenu( bool bInGameOverlay );
    void closeMainMenu();

    void onMainMenuChoice( eMainMenuOption choice );
    void onMainMenuSelection( eMainMenuOption selection );

protected:
    void
    onClose( ePopupAnswer answer, void *param_2, int32_t param_3 ) override;

private:
    void showOptionsRoot();
    void hideOptionsRoot();
    void closeOptionsMenu();
    void setNotificationClipsVisible( bool bVisible );
    void handlePendingPopupAnswer();
    void settlePoliceFine();

private:
    MngOptions mngOptions;

    FlashPlayer *pFlashPlayer;
    PlayerConfiguration *pInputActions;

    bool bCanOpenMainMenu;
    bool bMainMenuOpened;
    bool bOptionsRootHidden;
    bool bMoviesNeedReenable;
    bool bSavedMouseEnabled;
    bool bWasGameAlreadyPaused;
    bool bCloseRequested;
    bool field23_0x35; //  close animation in flight
    bool bPendingPopupAnswer;

    uint32_t savedAudioMixState;
    int32_t pendingMenuAction; // 0 none, 1 resume, 2 profile, 3 quit, 4 quit to
                               // desktop
    int32_t exitCountdown;     // frames left before acting on it

    ePopupAnswer pendingPopupAnswer;
    int32_t pendingPopupUserData;
};

extern GSPCOptions *gpPCOptions;
