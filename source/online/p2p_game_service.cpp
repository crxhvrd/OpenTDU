#include "shared.h"
#include "p2p_game_service.h"

#include "core/ed_store.h"
#include "core/time.h"

static CP2PGameService gP2PGameService;
CP2PGameService *gpCP2PGameService = &gP2PGameService; // DAT_010e75f8

static uint64_t GetTickCountMs() {
    return uint64_t( MonotonicToMs( GetTimeMonotonic() ) );
}

CP2PFreeRideMgr::CP2PFreeRideMgr()
    : bUnknownBool1( true ),
      bUnknownBool2( true ),
      bUnknownBool3( true ),
      BotNameLabels{} {}

CP2PGameService::CP2PGameService()
    : FreeRideMgr(),
      socket(),
      proxy(),
      pProxyHostInterface( nullptr ),
      actionClient( proxy ),
      netBufferClient( proxy ),
      netClockLocalMs( 0u ),
      netClockServerMs( 0u ) {
    // FUN_008563c0
}

bool CP2PGameService::init() {
    // FUN_008576e0
    if ( socket.isOpen() ) {
        return true;
    }

    if ( socket.open( kDefaultPort ) ) {
        return true;
    }

    OTDU_LOG_WARN( "Failed to bind UDP %u, using an ephemeral port\n",
                   kDefaultPort );
    return socket.open( 0 );
}

void CP2PGameService::term() {
    // FUN_00857660
    actionClient.cancelAll();
    netBufferClient.cancelAll();
    proxy.stop();
    socket.close();
}

void CP2PGameService::setNetClock( const uint32_t serverTimeSeconds ) {
    netClockLocalMs = uint32_t( GetTickCountMs() );
    netClockServerMs = serverTimeSeconds * 1000u;
}

void CP2PGameService::startProxy( const edNetAddress &orbAddress ) {
    // FUN_0062cfc0
    proxy.start( socket, orbAddress, pProxyHostInterface, GetTickCountMs() );
}

void CP2PGameService::refresh() {
    // FUN_0085cb70
    if ( !socket.isOpen() ) {
        return;
    }

    uint8_t datagram[edNetSocket::kMaxDatagramSize];
    uint8_t payload[edNetSocket::kMaxDatagramSize];
    edNetAddress sender;
    int32_t size = 0;
    while ( ( size = socket.receiveFrom( sender, datagram,
                                         sizeof( datagram ) ) ) > 0 ) {
        edNetAddress source;
        uint32_t payloadLength = 0u;
        if ( proxy.onDatagram( sender, datagram, uint32_t( size ), source,
                               payload, payloadLength ) ) {
            if ( payloadLength == 0u ) {
                continue;
            }

            edStore message;
            message.bindExternalBuffer( payload, payloadLength );
            if ( actionClient.onMessage( source, message ) ||
                 netBufferClient.onMessage( source, message ) ) {
                continue;
            }

            OTDU_LOG_DEBUG( "Unhandled service message %04X from %s\n",
                            message.getStreamCRC(), source.toString().c_str() );
            continue;
        }

        OTDU_UNIMPLEMENTED_ONCE; // TODO: ONLINE
    }

    const uint64_t now = GetTickCountMs();
    proxy.tick( now );
    actionClient.tick( now );
    netBufferClient.tick( now );
}
