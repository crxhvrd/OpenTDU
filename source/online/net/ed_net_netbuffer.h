#pragma once

#include <functional>
#include <vector>

#include "ed_net_socket.h"

class edNetProxy;
class edStore;

class edNetNetBufferClient {
public:
    using Callback_t = std::function<void( const bool bSuccess )>;

    static constexpr uint32_t kChunkSize = 1200;
    static constexpr uint32_t kRetryIntervalMs = 500;
    static constexpr uint32_t kMaxTries = 10;

public:
    explicit edNetNetBufferClient( edNetProxy &transport );
    ~edNetNetBufferClient();

    void getNetBuffer( const edNetAddress &source,
                       const uint32_t bufferID,
                       uint8_t *pDest,
                       const uint32_t size,
                       Callback_t callback,
                       const uint64_t nowMs );

    bool onMessage( const edNetAddress &sender, edStore &message );
    void tick( const uint64_t nowMs );

    void cancelAll();
    inline bool hasPendingDownloads() const { return !downloads.empty(); }

private:
    struct Download {
        edNetAddress Source;
        uint32_t BufferID = 0u;
        uint8_t *pDest = nullptr;
        uint32_t Size = 0u;
        uint32_t Received = 0u;
        uint32_t NumTries = 0u;
        uint64_t LastRequestMs = 0ull;
        bool bRequestNow = true;
        Callback_t Callback;
    };

private:
    edNetProxy &transport;
    std::vector<Download> downloads;

private:
    void sendRequest( Download &download, const uint64_t nowMs );
    void finish( const size_t index, const bool bSuccess );
};
