#pragma once

#include "core/crypto/blowfish.h"
#include "ed_net_proxy_protocol.h"
#include "ed_net_socket.h"

class edStore;

class edNetProxyListener {
public:
    virtual ~edNetProxyListener() {}

    virtual void onProxyState( const int32_t state ) {}
    virtual void onProxyConnected( const edNetAddress &coreAddress ) {}
    virtual void onProxyError( const int32_t errorCode, const char *pMessage ) {
    }
    virtual void buildHello( edNetProxyProtocol::Hello &outHello ) = 0;
};

class edNetProxy {
public:
    enum eResult : int32_t { R_Connecting = 0, R_Connected = 1, R_Failed = 2 };

    static constexpr uint32_t kMaxRoutes = 64;

public:
    edNetProxy();

    void start( edNetSocket &socket,
                const edNetAddress &proxyAddress,
                edNetProxyListener *pListener,
                const uint64_t nowMs );
    void stop();

    void tick( const uint64_t nowMs );

    bool onDatagram( const edNetAddress &sender,
                     const uint8_t *pData,
                     const uint32_t size,
                     edNetAddress &outSource,
                     uint8_t *pOutPayload,
                     uint32_t &outPayloadLength );

    bool send( const edNetAddress &destination, const edStore &message );

    void registerRoute( const edNetAddress &destination );
    bool isRouted( const edNetAddress &destination ) const;

    inline eResult getResult() const { return result; }
    inline const edNetAddress &getCoreAddress() const { return coreAddress; }
    inline const edNetAddress &getPublicAddress() const {
        return publicAddress;
    }
    inline uint32_t getSessionID() const { return sessionID; }

private:
    enum ePhase : int32_t { P_Hello = 0, P_Challenge = 1, P_Done = -1 };

private:
    edNetSocket *pSocket;
    edNetProxyListener *pListener;
    edNetAddress proxyAddress;
    edNetAddress publicAddress;
    edNetAddress coreAddress;
    eResult result;
    ePhase phase;
    uint64_t lastSendMs;
    uint64_t lastTickMs;
    bool bSendNow;
    uint32_t sendCount;
    uint32_t sessionID;
    bool bEncrypt;
    edNetProxyProtocol::Hello hello;
    uint32_t challenges[edNetProxyProtocol::kNumChallenges];
    uint32_t responses[edNetProxyProtocol::kNumChallenges];
    BlowFishCrypto cipher;
    edNetAddress routes[kMaxRoutes];
    uint32_t numRoutes;

private:
    void fail( const int32_t errorCode, const char *pMessage );
    void sendHandshakeFrame();
    void onChallenge( const uint8_t *pData,
                      const uint32_t size,
                      const uint64_t nowMs );
    void onResult( const uint8_t *pData, const uint32_t size );
};
