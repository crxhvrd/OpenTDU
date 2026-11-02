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
};
