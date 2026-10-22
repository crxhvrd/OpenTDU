#pragma once

#include "flash/mng_flash.h"

#include "lobby_list_vehicles.h"

class FlashPlayer;
class GMLobby;

// Flash listener of the CHALLENG movie: event setup lists, session browser,
// lobby player list and the commands that leave the lobby.
class MngLobby : public MngFlash {
public:
    const char *getName() const override { return "Lobby"; }

    inline void setOwner( GMLobby *pNewOwner ) { pOwner = pNewOwner; }
    inline GMLobby *getOwner() const { return pOwner; }
    inline FlashPlayer *getFlashPlayer() const { return pFlashPlayer; }
    inline void onVehicleListOpened() {
        bInSession = false;
        bShowPlayerInfo = false;
        bVehicleListOpen = true;
    }
    inline void onVehicleListClosed() { bVehicleListOpen = false; }
    inline bool isVehicleListOpen() const { return bVehicleListOpen; }

public:
    MngLobby();
    ~MngLobby();

    bool initialize( TestDriveGameInstance * ) override;
    void tick( float deltaTime, float totalTime ) override;
    void terminate() override;
    void reset() override;
    void pause() override;

    bool onMessage( FlashMessage *pMessage, FlashPlayer *pPlayer ) override;

private:
    static constexpr int32_t kNumLobbyRows = 8;
    static constexpr uint32_t kJoinResultDelayMs = 3000u;

private:
    bool handleMessage( FlashMessage *pMessage, FlashPlayer *pPlayer );
    void resetSessionParams();
    void showPlayers();
    void hidePlayerInfo();
    void changeVehicle();

private:
    GMLobby *pOwner;
    FlashPlayer *pFlashPlayer;

    uint32_t disconnectAtMs;
    bool bInSession;
    bool bShowPlayerInfo;
    bool bVehicleListOpen;
    bool bGamesListOpen;
    bool bHeadLineSelected;

    uint32_t readyTimerMs;
    uint32_t readyAtMs;
    bool bPendingConnect;
    bool bJoinPending;
    bool bJoinDone;
    uint32_t joinTick;

    LobbyListVehicles vehicleList;
};
