#pragma once

#include "online/net/ed_net_action.h"
#include "online/net/ed_net_resource_orb.h"

#include <memory>
#include "online/net/ed_net_rpc.h"
#include "online/net/ed_net_socket.h"

// coreresquests::r_get_request_handlers_ex request
struct GetRPCHandlersRequest {
    bool bDone = false;
    bool bSuccess = false;
    edNetAddress Results[edNetRPC::kMaxRequestHandlers];
    uint16_t CRCs[edNetRPC::kMaxRequestHandlers] = {};

    void reset();
    inline bool isSuccess() const { return bDone && bSuccess; }
    inline bool isFailed() const { return bDone && !bSuccess; }
    inline bool isDone() const { return bDone; }
};

struct PlayerSaveDescriptor;

// CSysMessageMgr Savegame request status (vtables 0xf1c3f4 / 0xf1c40c)
struct SavegameRequest {
    bool bDone = false;     // +0x4
    bool bSuccess = false;  // +0x5
    bool bInFlight = false; // +0x8
    bool bReplied = false;  // +0xc
    bool bAccepted = false; // +0xd

    void reset();
    inline bool isDone() const { return bDone; }
    void onActionComplete( const edNetActionClient::Reply &reply,
                           const uint16_t replyCRC );
};

// Online services handlers
class OnlineServices {
public:
    static constexpr uint16_t kORBPort = 9000;
    static constexpr uint32_t kNumPUSStorages = 10;

public:
    inline const GetRPCHandlersRequest &getDiscovery() const {
        return discovery;
    }

public:
    OnlineServices();

    void sendDiscovery();
    void onDiscoveryComplete( const edNetActionClient::Reply &reply );
    void refresh();
    void onORBLost();
    void resetConnection();
    void clear();
    void requestNetConfig( const char *pFileName );
    bool sendUserStatistics( edStore &statistics );
    bool sendSavegameCheck( SavegameRequest &request,
                            const uint32_t userID,
                            const PlayerSaveDescriptor &descriptor );
    bool sendSavegameSave( SavegameRequest &request,
                           const uint32_t userID,
                           const PlayerSaveDescriptor &descriptor );

public:
    edNetAddress Core;
    edNetAddress Log;
    edNetAddress GameMaster;
    edNetAddress UserMap;
    edNetAddress GPSTracker;
    edNetAddress UserDatabase;
    edNetAddress Team;
    edNetAddress News;
    edNetAddress Replay;
    edNetAddress CarDealer;
    edNetAddress PUSStorage[kNumPUSStorages];
    edNetAddress PTSStorage;
    edNetAddress Stats;
    edNetAddress Ranking;
    edNetAddress Check;
    edNetAddress Savegame;

    char Username[32];

    bool bGPSTrackerFromIni;
    bool bLobbyFromIni;
    bool bServerFromIni;
    bool bReconnectPending;
    bool bFastDiscovery;

private:
    uint32_t lastDiscoveryTick;      // +0x176c
    GetRPCHandlersRequest discovery; // +0x4db4
    uint32_t lastSuccessTick;        // +0x72d8
    uint32_t discoveryStartTick;     // DAT_010c4a2c
    std::unique_ptr<edNetResourceORB> pResourceORB;
    uint8_t netConfigBuffer[0x1000];

private:
    void onNetConfigDownloaded( const bool bSuccess );
    bool sendGetRPCHandlersEx( const uint32_t userID,
                               const uint32_t sessionID );
    bool sendSavegameRequest( SavegameRequest &request,
                              const char *pName,
                              const uint16_t requestCRC,
                              const uint16_t replyCRC,
                              const uint32_t userID,
                              const PlayerSaveDescriptor &descriptor );
};
