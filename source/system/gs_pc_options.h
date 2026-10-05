#pragma once

#include "core/popup_callback.h"
#include "flash/entities/flash_entity.h"
#include "game/gs_base.h"

#include "flash_list_keymap.h"
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

enum eMenuAction : int32_t {
    MA_None = 0,
    MA_Resume = 1,
    MA_NewGame = 2,       // wipe the current game and restart it
    MA_QuitToDesktop = 3, // leave the process
    MA_ChangeProfile = 4, // drop back to GM_Login
};

class GSPCOptions : public GameSystem,
                    public FlashEntity,
                    public PopupCallback {
public:
    static constexpr int32_t kMaxProfileEntries = 32;
    static constexpr int32_t kMaxProfileNameLength = 32;
    static constexpr int32_t kMaxDisplayedProfiles = 8;

public:
    const char *getName() const override { /* FUN_0042b370 */
        return "PC_Options";
    }

    inline bool isMainMenuOpened() const { return bMainMenuOpened; }
    inline eMenuAction getPendingMenuAction() const {
        return eMenuAction( pendingMenuAction );
    }
    inline void setMainMenuAllowed( const bool bAllowed ) {
        bCanOpenMainMenu = bAllowed;
    }
    inline uint64_t getActiveProfileHashcode() const {
        // FUN_007c47d0
        return activeProfileHashcode;
    }
    inline void setCloseRequested() {
        field23_0x35 = true;
        bCloseRequested = true;
    }
    inline void postPendingPopupAnswer( const ePopupAnswer answer,
                                        const int32_t userData ) {
        bPendingPopupAnswer = true;
        pendingPopupAnswer = answer;
        pendingPopupUserData = userData;
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

    void hideOptionsRoot();
    void showProfileSelection( const int32_t mode );
    void hideProfileSelection();

    void updateMainMenuVisibility();
    void refreshOnlineProfileScreen();
    int32_t refreshProfileList();
    void openNewProfilePopup();

protected:
    void
    onClose( ePopupAnswer answer, void *param_2, int32_t param_3 ) override;

private:
    void showOptionsRoot();
    void closeOptionsMenu();
    void setNotificationClipsVisible( bool bVisible );
    void handlePendingPopupAnswer();
    void settlePoliceFine();
    void gatherProfileEntries();
    void selectProfile();
    void switchToLoginGameMode();
    void beginProfileChange();

private:
    MngOptions mngOptions;
    FlashListKeymap keymapList;

    FlashPlayer *pFlashPlayer;

    char nickname[0x40];
    char email[0x80];
    char password[0x80];

    bool bProfileScreenOpened;
    int32_t profileScreenMode;
    bool field297_0x188;

    bool profileConnected[kMaxProfileEntries];
    char profileNames[kMaxProfileEntries][kMaxProfileNameLength];

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

    uint64_t activeProfileHashcode;
};

extern GSPCOptions *gpPCOptions;
