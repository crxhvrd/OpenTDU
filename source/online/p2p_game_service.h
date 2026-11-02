#pragma once

#include "online/net/ed_net_action.h"
#include "online/net/ed_net_netbuffer.h"
#include "online/net/ed_net_proxy.h"
#include "online/net/ed_net_socket.h"

// TODO: ONLINE: stubs until online is implemented

struct BotNameLabel {
    float X;
    float Y;
    float Width;
    float Height;
    float Distance;
    int32_t Index;
};
OTDU_SIZE_MUST_MATCH( BotNameLabel, 0x18 );

class CP2PFreeRideMgr {
public:
    static constexpr int32_t kNumBotNameLabels = 16;

public:
    CP2PFreeRideMgr();

public:
    bool bUnknownBool1; // (cleared by map screenshots)
    bool bUnknownBool2; // (cleared while a cinematic plays)
    bool bUnknownBool3; // (cleared by the GMMission restart fade)
    std::array<BotNameLabel, kNumBotNameLabels> BotNameLabels;
};

class CP2PGameService {
public:
    static constexpr uint16_t kDefaultPort = 8889;

public:
    inline edNetSocket &getSocket() { return socket; }
    inline edNetProxy &getProxy() { return proxy; }
    inline edNetActionClient &getActionClient() { return actionClient; }
    inline edNetNetBufferClient &getNetBufferClient() {
        return netBufferClient;
    }
    inline void setProxyHostInterface( edNetProxyListener *pListener ) {
        pProxyHostInterface = pListener;
    }
    inline edNetProxyListener *getProxyHostInterface() const {
        return pProxyHostInterface;
    }

public:
    CP2PGameService();

    bool init();
    void term();
    void refresh();

    void setNetClock( const uint32_t serverTimeSeconds );
    void startProxy( const edNetAddress &orbAddress );

public:
    CP2PFreeRideMgr FreeRideMgr;

private:
    edNetSocket socket;
    edNetProxy proxy;
    edNetProxyListener *pProxyHostInterface;
    edNetActionClient actionClient;
    edNetNetBufferClient netBufferClient;
    uint32_t netClockLocalMs;
    uint32_t netClockServerMs;
};

extern CP2PGameService *gpCP2PGameService;
