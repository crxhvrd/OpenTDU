#pragma once

#include <functional>
#include <vector>

#include "ed_net_socket.h"

class edNetProxy;
class edStore;

class edNetActionClient {
public:
    enum eCallResult : int32_t {
        CR_Success = 0,
        CR_Timeout = 1,
    };

    struct Reply {
        eCallResult Result = CR_Timeout;
        edStore *pReply = nullptr;
        uint32_t BufferID = 0u;
        uint32_t BufferSize = 0u;
    };
    using Callback_t = std::function<void( const Reply &reply )>;

    static uint32_t DefaultIntervalMs;
    static uint32_t DefaultRetries;

public:
    explicit edNetActionClient( edNetProxy &transport );
    ~edNetActionClient();

    uint32_t callAction( const edNetAddress &destination,
                         const edStore &request,
                         Callback_t callback,
                         const uint64_t nowMs,
                         const uint32_t intervalMs = DefaultIntervalMs,
                         const uint32_t retries = DefaultRetries );
    void callActionUnreliable( const edNetAddress &destination,
                               const edStore &request );

    bool onMessage( const edNetAddress &sender, edStore &message );
    void tick( const uint64_t nowMs );

    void cancelAll();
    inline bool hasPendingCalls() const { return !pendingCalls.empty(); }

private:
    struct PendingCall {
        uint32_t ReqID = 0u;
        edNetAddress Destination;
        std::vector<uint8_t> Datagram;
        Callback_t Callback;
        uint64_t StartMs = 0ull;
        uint32_t IntervalMs = 0u;
        uint32_t Retries = 0u;
        uint32_t NumResends = 0u;
    };

private:
    edNetProxy &transport;
    std::vector<PendingCall> pendingCalls;
    uint32_t nextReqID;

private:
    void send( const edNetAddress &destination,
               const uint8_t *pData,
               const uint32_t size );
};
