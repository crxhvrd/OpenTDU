#pragma once

#include <functional>

#include "ed_net_action.h"
#include "ed_net_socket.h"

class edNetResourceORB {
public:
    using Callback_t = std::function<void( const bool bSuccess )>;

    static constexpr uint32_t kOpenIntervalMs = 1000;
    static constexpr uint32_t kOpenRetries = 10;

    enum eError : uint8_t { E_None = 0, E_Busy = 2 };

public:
    explicit edNetResourceORB( edNetActionClient &actions );

    inline void setFacility( const uint8_t value ) { facility = value; }
    inline void setUserID( const uint32_t value ) { userID = value; }
    void setFileName( const char *pName );
    inline void setPeer( const edNetAddress &address ) { peer = address; }
    inline void setDestination( uint8_t *pBuffer, const uint32_t size ) {
        pDest = pBuffer;
        destSize = size;
    }

    void startDownload( Callback_t callback, const uint64_t nowMs );
    void tick( const uint64_t nowMs );

    inline uint32_t getReceivedSize() const { return received; }
    inline bool isBusy() const { return state != S_Idle; }

private:
    enum eState {
        S_Idle,
        S_Opening,
        S_WaitReopen,
        S_Downloading,
        S_WaitNextChunk,
    };

private:
    edNetActionClient &actions;
    uint8_t facility;
    uint32_t userID;
    char fileName[0x200];
    edNetAddress peer;
    uint8_t *pDest;
    uint32_t destSize;

    eState state;
    uint32_t fileID;
    uint32_t totalSize;
    uint32_t received;
    uint32_t chunkRetries;
    uint32_t chunkIntervalMs;
    uint64_t nextRequestMs;
    uint32_t generation;
    uint64_t lastTickMs;
    Callback_t callback;

private:
    void sendOpen( const uint64_t nowMs );
    void sendGetFile( const uint64_t nowMs );
    void onOpenReply( const edNetActionClient::Reply &reply,
                      const uint64_t nowMs );
    void onGetFileReply( const edNetActionClient::Reply &reply,
                         const uint64_t nowMs );
    void finish( const bool bSuccess );
};
