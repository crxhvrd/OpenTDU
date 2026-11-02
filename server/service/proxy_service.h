#pragma once

#include <map>
#include <memory>
#include <random>

#include "core/crypto/blowfish.h"
#include "online/net/ed_net_socket.h"

class Server;
class edStore;
class ActionService;

// A client that completed (or is completing) the edNetProxy handshake.
struct ClientSession {
    edNetAddress Address;
    uint32_t SessionID = 0u;
    uint32_t Challenges[3] = {};
    bool bConnected = false;
    BlowFishCrypto Cipher;
    uint64_t LastSeenMs = 0ull;
    std::string PlayerName;
    std::string Version;
    // Set by OpenTDU::Identity::r_login (0 = not logged in).
    uint32_t AccountID = 0u;
};

class ProxyService {
public:
    explicit ProxyService( Server &server );
    ~ProxyService();

    void onDatagram( const edNetAddress &sender,
                     const uint8_t *pData,
                     const uint32_t size );
    void tick();

    bool sendToClient( ClientSession &session,
                       const edNetAddress &sourceEndpoint,
                       const edStore &message );
    bool sendToClient( ClientSession &session,
                       const edNetAddress &sourceEndpoint,
                       const uint8_t *pPayload,
                       const uint32_t length );

    inline ActionService &getActionService() { return *pActionService; }

private:
    static constexpr uint64_t kSessionTimeoutMs = 30ull * 60ull * 1000ull;

private:
    Server &server;
    std::unique_ptr<ActionService> pActionService;
    std::map<uint64_t, std::unique_ptr<ClientSession>> sessions;
    std::mt19937 random;
    uint32_t nextSessionID;

private:
    static uint64_t GetSessionKey( const edNetAddress &address );
    ClientSession *findSession( const edNetAddress &address );

    void onHello( const edNetAddress &sender,
                  const uint8_t *pData,
                  const uint32_t size );
    void onChallengeResponse( const edNetAddress &sender,
                              const uint8_t *pData,
                              const uint32_t size );
    void onRelay( const edNetAddress &sender,
                  const uint8_t *pData,
                  const uint32_t size );
};
