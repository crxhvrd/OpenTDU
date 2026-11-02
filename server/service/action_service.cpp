#include "shared.h"
#include "action_service.h"

#include "proxy_service.h"
#include "tdu_server.h"

#include "core/ed_store.h"
#include "core/hash/crc32.h"
#include "online/net/ed_net_rpc.h"

ActionService::ActionService( Server &server, ProxyService &proxy )
    : server( server ),
      proxy( proxy ),
      rpcs(),
      replyCache(),
      netBuffers(),
      nextNetBufferID( 1u ) {}

ActionService::~ActionService() {}

uint64_t ActionService::GetSessionKey( const ClientSession &session,
                                       const uint32_t id ) {
    return ( uint64_t( session.SessionID ) << 32 ) | id;
}

void ActionService::registerRPC( const uint16_t messageID,
                                 const char *pName,
                                 RPCHandler_t handler ) {
    auto it = rpcs.find( messageID );
    if ( it != rpcs.end() ) {
        OTDU_LOG_ERROR( "RPC id %04X collision: '%s' vs '%s'\n", messageID,
                        it->second.Name.c_str(), pName );
        return;
    }
    rpcs[messageID] = RPCEntry{ pName, std::move( handler ) };
}

void ActionService::registerRPC( const char *pSignature,
                                 RPCHandler_t handler ) {
    registerRPC( edNetRPC::MessageID( pSignature ), pSignature,
                 std::move( handler ) );
}

void ActionService::onMessage( ClientSession &session,
                               const edNetAddress &endpoint,
                               edStore &message ) {
    const uint16_t messageID = message.getStreamCRC();

    if ( messageID == edNetRPC::RCallAction() ) {
        onCallAction( session, endpoint, message, true );
    } else if ( messageID == edNetRPC::RCallActionUnreliable() ) {
        onCallAction( session, endpoint, message, false );
    } else if ( messageID == edNetRPC::ECallActionAck() ) {
        // The cached reply is kept until it expires: a duplicate request
        // still in flight may arrive after the ack.
    } else if ( messageID == edNetRPC::RGetNetBuffer() ) {
        onGetNetBuffer( session, endpoint, message );
    } else if ( messageID == edNetRPC::EGetNetBufferAck() ) {
        const uint32_t bufferID = message.readUInteger();
        netBuffers.erase( GetSessionKey( session, bufferID ) );
    } else if ( messageID == edNetRPC::REcho() ) {
        edStore reply;
        reply.updateCRC( edNetRPC::AEcho() );
        proxy.sendToClient( session, endpoint, reply );
    } else {
        OTDU_LOG_WARN( "Unhandled service message %04X from session %08X "
                       "(to %s)\n",
                       messageID, session.SessionID,
                       endpoint.toString().c_str() );
    }
}

void ActionService::onCallAction( ClientSession &session,
                                  const edNetAddress &endpoint,
                                  edStore &message,
                                  const bool bReliable ) {
    // Client side counterpart: OnRequest 0x00626560
    uint32_t reqID = 0u;
    if ( bReliable ) {
        reqID = message.readUInteger();
        message.readUInteger(); // timeoutMs (interval x retries)
    }

    if ( reqID != 0u ) {
        auto cached = replyCache.find( GetSessionKey( session, reqID ) );
        if ( cached != replyCache.end() ) {
            proxy.sendToClient( session, cached->second.Endpoint,
                                cached->second.Payload.data(),
                                uint32_t( cached->second.Payload.size() ) );
            return;
        }
    }

    uint8_t requestBuffer[edNetSocket::kMaxDatagramSize];
    edStore request;
    request.bindExternalBuffer( requestBuffer, sizeof( requestBuffer ) );
    if ( !message.popObjectNoAlloc( request ) ) {
        OTDU_LOG_WARN( "r_callaction %u from session %08X has no payload\n",
                       reqID, session.SessionID );
        return;
    }

    edStore reply;
    RPCCall call{ session, request, reply };
    const bool bHasReply = dispatchRPC( call );
    if ( reqID == 0u || !bHasReply ) {
        return;
    }

    edStore answer;
    if ( call.bUseNetBuffer ) {
        const uint32_t bufferID = nextNetBufferID++;
        if ( nextNetBufferID == 0u ) {
            nextNetBufferID = 1u;
        }

        NetBuffer &buffer = netBuffers[GetSessionKey( session, bufferID )];
        buffer.Data = std::move( call.NetBuffer );
        buffer.TimeMs = server.getTimeMs();

        answer.updateCRC( edNetRPC::ACallActionWithNetBufferID() );
        answer.writeInteger( int32_t( reqID ) );
        answer.writeInteger( int32_t( bufferID ) );
        answer.writeInteger( int32_t( buffer.Data.size() ) );
    } else {
        answer.updateCRC( edNetRPC::ACallAction() );
        answer.writeInteger( int32_t( reqID ) );
    }
    if ( reply.getWrittenSize() > 0 ) {
        answer.writeObject( reply );
    }
    if ( answer.getStatus() != 0 ) {
        OTDU_LOG_ERROR( "Failed to serialize the reply to %u\n", reqID );
        return;
    }

    CachedReply &cached = replyCache[GetSessionKey( session, reqID )];
    cached.Endpoint = endpoint;
    cached.Payload.assign( answer.getBuffer(),
                           answer.getBuffer() + answer.getWrittenSize() );
    cached.TimeMs = server.getTimeMs();

    proxy.sendToClient( session, endpoint, answer );
}

void ActionService::onGetNetBuffer( ClientSession &session,
                                    const edNetAddress &endpoint,
                                    edStore &message ) {
    // Retail server side: 0x006252c0
    const uint32_t bufferID = message.readUInteger();
    const uint32_t offset = message.readUInteger();

    edStore answer;
    answer.updateCRC( edNetRPC::AGetNetBuffer() );
    answer.writeInteger( int32_t( bufferID ) );

    auto it = netBuffers.find( GetSessionKey( session, bufferID ) );
    if ( it == netBuffers.end() || offset > it->second.Data.size() ) {
        answer.writeByte( 0 );
        answer.writeInteger( int32_t( offset ) );
        proxy.sendToClient( session, endpoint, answer );
        return;
    }

    NetBuffer &buffer = it->second;
    buffer.TimeMs = server.getTimeMs();

    const uint32_t totalSize = uint32_t( buffer.Data.size() );
    const uint32_t chunkSize = Min( totalSize - offset, kNetBufferChunkSize );
    const uint8_t *pChunk = buffer.Data.data() + offset;

    answer.writeByte( 1 );
    answer.writeInteger( int32_t( offset ) );
    answer.writeInteger( int32_t( totalSize ) );
    answer.writeUShort( uint16_t( TestDriveCRC32( pChunk, chunkSize ) ) );
    answer.writeBuffer( pChunk, chunkSize );
    proxy.sendToClient( session, endpoint, answer );
}

bool ActionService::dispatchRPC( RPCCall &call ) {
    const uint16_t rpcID = call.Request.getStreamCRC();
    auto it = rpcs.find( rpcID );
    if ( it == rpcs.end() ) {
        OTDU_LOG_WARN( "Unimplemented RPC %04X (session %08X)\n", rpcID,
                       call.Session.SessionID );
        return false;
    }

    if ( server.getConfig().bVerbose ) {
        OTDU_LOG_DEBUG( "RPC %s (session %08X)\n", it->second.Name.c_str(),
                        call.Session.SessionID );
    }
    return it->second.Handler( call );
}

void ActionService::onSessionClosed( const ClientSession &session ) {
    const uint64_t first = GetSessionKey( session, 0u );
    const uint64_t last = GetSessionKey( session, 0xffffffffu );
    replyCache.erase( replyCache.lower_bound( first ),
                      replyCache.upper_bound( last ) );
    netBuffers.erase( netBuffers.lower_bound( first ),
                      netBuffers.upper_bound( last ) );
}

void ActionService::tick() {
    const uint64_t now = server.getTimeMs();
    for ( auto it = replyCache.begin(); it != replyCache.end(); ) {
        if ( now - it->second.TimeMs > kReplyCacheLifetimeMs ) {
            it = replyCache.erase( it );
        } else {
            ++it;
        }
    }
    for ( auto it = netBuffers.begin(); it != netBuffers.end(); ) {
        if ( now - it->second.TimeMs > kNetBufferLifetimeMs ) {
            it = netBuffers.erase( it );
        } else {
            ++it;
        }
    }
}
