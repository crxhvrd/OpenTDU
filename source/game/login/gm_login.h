#pragma once

#include "core/popup_callback.h"
#include "game/gm_base.h"

#include "flash/mng_fade.h"
#include "flash/mng_flash_counter.h"
#include "flash/mng_flash_localize.h"
#include "mng_flash_login.h"

class FlashPanelOnline;
class LoginWorldScene;

typedef enum eGMLoginState : int32_t {
    GLS_LegalDisplay = 0,
    GLS_SelectPad = 1,
    GLS_Signing = 2,
    GLS_ConnectLSP = 3,
    GLS_LoginToServer = 4,
    GLS_WaitStartAnim = 5,
    GLS_WaitForLoad = 6,
    GLS_DeletingBadSaves = 7,
    GLS_WaitForSave = 8,
    GLS_NewContinueQuestion = 9,
    GLS_WaitExit = 10,
    GLS_MainMenu = 11,
    GLS_LeaveGameMode = 12,
    GLS_LeaveApplication = 13,
    GLS_WaitSaveLoaded = 14,
    GLS_WaitFileMap = 15,
} eGMLoginState;

class GMLogin : public GameMode, public PopupCallback {
public:
    GMLogin();
    ~GMLogin();

    void reset() override;
    void tick( float deltaTime, float totalTime ) override;
    void pause() override;

    void onClose( enum ePopupAnswer answer,
                  void *param_2,
                  int32_t param_3 ) override;
    bool onMessage( FlashMessage *pMessage, FlashPlayer *pPlayer ) override;

    static void RequestNewGame();
    static void ChangeProfile( const uint64_t profileHashcode );

private:
    bool isMenuClipVisible();
    static bool HasInputActivity();

    void resumeSavedGame();
    void startNewGame();

    void onConnectionFailed();
    void onProxyFailed();
    void onLoginFailed( const bool bSuccess );
    void connectToGameServer();

private:
    MngFade fade;
    MngFlashLocalize flashLocalize;
    MngFlashLogin flashLogin;
    MngFlashCounter flashCounter;

    FlashPanelOnline *pPanelOnline;
    LoginWorldScene *pWorldScene; // OTDU_ADDITION

    FlashPlayer *pGeneralFlashPlayer;
    FlashPlayer *pConfigPCFlashPlayer;
    FlashPlayer *pLoginFlashPlayer;

    int32_t menuActionCountdown;
    float idleTime;
    uint32_t saveWaitStartTicks;
    uint32_t waitExitStartTicks;

    eGMLoginState state;
};

extern GMLogin *gpGMLogin;
