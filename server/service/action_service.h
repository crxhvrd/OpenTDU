#pragma once

#include <functional>
#include <map>
#include <unordered_map>
#include <vector>

#include "online/net/ed_net_socket.h"

class Server;
class ProxyService;
class edStore;
struct ClientSession;

// One incoming RPC. The handler writes the a_ reply edStore (its own CRC
// header included) into Reply, and/or fills NetBuffer to have the payload
// pulled by the client through edNetServiceNetBuffer
// (a_callaction_with_netbufferid).
struct RPCCall {
    ClientSession &Session;
    edStore &Request;
    edStore &Reply;
    std::vector<uint8_t> NetBuffer;
    bool bUseNetBuffer = false;
};

// Server side of edNetServiceHandlerAction, Echo and NetBuffer
// (docs/online_wire_spec.md §3-5). r_callaction carries the actual RPC as a
// nested edStore whose own header CRC is the RPC signature.
class ActionService {
public:
    // Returns true to answer, false to leave the call unanswered (the client
    // then times out, like an unimplemented retail service).
    using RPCHandler_t = std::function<bool( RPCCall &call )>;

    static constexpr uint32_t kNetBufferChunkSize = 1200;

public:
    ActionService( Server &server, ProxyService &proxy );
    ~ActionService();

    void registerRPC( const uint16_t messageID,
                      const char *pName,
                      RPCHandler_t handler );
    void registerRPC( const char *pSignature, RPCHandler_t handler );

    void onMessage( ClientSession &session,
                    const edNetAddress &endpoint,
                    edStore &message );
    void onSessionClosed( const ClientSession &session );
    void tick();

private:
    // Longer than the slowest client retry window (1500 ms x 7 retries), so
    // a late duplicate request still finds its reply.
    static constexpr uint64_t kReplyCacheLifetimeMs = 60ull * 1000ull;
    static constexpr uint64_t kNetBufferLifetimeMs = 5ull * 60ull * 1000ull;

    struct RPCEntry {
        std::string Name;
        RPCHandler_t Handler;
    };

    struct CachedReply {
        edNetAddress Endpoint;
        std::vector<uint8_t> Payload;
        uint64_t TimeMs = 0ull;
    };

    struct NetBuffer {
        std::vector<uint8_t> Data;
        uint64_t TimeMs = 0ull;
    };

private:
    Server &server;
    ProxyService &proxy;
    std::unordered_map<uint16_t, RPCEntry> rpcs;
    // Keyed by ( session id << 32 ) | reqId. Retail retries resend the same
    // reqId, so a duplicate gets the cached reply instead of re-running the
    // RPC.
    std::map<uint64_t, CachedReply> replyCache;
    // Keyed by ( session id << 32 ) | buffer id.
    std::map<uint64_t, NetBuffer> netBuffers;
    uint32_t nextNetBufferID;

private:
    static uint64_t GetSessionKey( const ClientSession &session,
                                   const uint32_t id );

    void onCallAction( ClientSession &session,
                       const edNetAddress &endpoint,
                       edStore &message,
                       const bool bReliable );
    void onGetNetBuffer( ClientSession &session,
                         const edNetAddress &endpoint,
                         edStore &message );
    bool dispatchRPC( RPCCall &call );
};
