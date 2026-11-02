#include "shared.h"
#include "ed_net_action.h"

#include "core/ed_store.h"
#include "ed_net_proxy.h"
#include "ed_net_rpc.h"

uint32_t edNetActionClient::DefaultIntervalMs = 1000; // DAT_00f56aa8
uint32_t edNetActionClient::DefaultRetries = 5;       // DAT_00f56aac

edNetActionClient::edNetActionClient( edNetProxy &transport )
    : transport( transport ), pendingCalls(), nextReqID( 1u ) {}

edNetActionClient::~edNetActionClient() {}

void edNetActionClient::send( const edNetAddress &destination,
                              const uint8_t *pData,
                              const uint32_t size ) {
    edStore message;
    message.bindExternalBuffer( const_cast<uint8_t *>( pData ), size );
    transport.send( destination, message );
}

uint32_t edNetActionClient::callAction( const edNetAddress &destination,
                                        const edStore &request,
                                        Callback_t callback,
                                        const uint64_t nowMs,
                                        const uint32_t intervalMs,
                                        const uint32_t retries ) {
    // FUN_00625b30
    const uint32_t reqID = nextReqID++;
    if ( nextReqID == 0u ) {
        nextReqID = 1u;
    }

    edStore message;
    message.updateCRC( edNetRPC::RCallAction() );
    message.writeInteger( int32_t( reqID ) );
    message.writeInteger( int32_t( intervalMs * retries ) );
    message.writeObject( const_cast<edStore &>( request ) );

    PendingCall call;
    call.ReqID = reqID;
    call.Destination = destination;
    call.Datagram.assign( message.getBuffer(),
                          message.getBuffer() + message.getWrittenSize() );
    call.Callback = std::move( callback );
    call.StartMs = nowMs;
    call.IntervalMs = intervalMs;
    call.Retries = retries;

    send( destination, call.Datagram.data(), uint32_t( call.Datagram.size() ) );
    pendingCalls.push_back( std::move( call ) );
    return reqID;
}

void edNetActionClient::callActionUnreliable( const edNetAddress &destination,
                                              const edStore &request ) {
    edStore message;
    message.updateCRC( edNetRPC::RCallActionUnreliable() );
    message.writeObject( const_cast<edStore &>( request ) );
    transport.send( destination, message );
}

bool edNetActionClient::onMessage( const edNetAddress &sender,
                                   edStore &message ) {
    // FUN_00626560
    const uint16_t messageID = message.getStreamCRC();
    const bool bWithNetBuffer =
        messageID == edNetRPC::ACallActionWithNetBufferID();
    if ( messageID != edNetRPC::ACallAction() && !bWithNetBuffer ) {
        return false;
    }

    const uint32_t reqID = message.readUInteger();
    auto it = std::find_if( pendingCalls.begin(), pendingCalls.end(),
                            [&]( const PendingCall &call ) {
                                return call.ReqID == reqID &&
                                       call.Destination == sender;
                            } );
    if ( it == pendingCalls.end() ) {
        return true;
    }

    Reply reply;
    reply.Result = CR_Success;
    if ( bWithNetBuffer ) {
        reply.BufferID = message.readUInteger();
        reply.BufferSize = message.readUInteger();
    }

    if ( reqID != 0u ) {
        edStore ack;
        ack.updateCRC( edNetRPC::ECallActionAck() );
        ack.writeInteger( int32_t( reqID ) );
        transport.send( sender, ack );
    }

    uint8_t replyBuffer[edNetSocket::kMaxDatagramSize];
    edStore replyStore;
    replyStore.bindExternalBuffer( replyBuffer, sizeof( replyBuffer ) );
    if ( message.getReadCursor() < message.getWrittenSize() &&
         message.popObjectNoAlloc( replyStore ) ) {
        reply.pReply = &replyStore;
    }

    PendingCall call = std::move( *it );
    pendingCalls.erase( it );
    if ( call.Callback ) {
        call.Callback( reply );
    }
    return true;
}

void edNetActionClient::tick( const uint64_t nowMs ) {
    // FUN_00626490
    for ( size_t i = 0; i < pendingCalls.size(); ) {
        PendingCall &call = pendingCalls[i];
        const uint64_t nextResendMs =
            call.StartMs +
            uint64_t( call.IntervalMs ) * uint64_t( call.NumResends + 1 );
        if ( nowMs < nextResendMs ) {
            i++;
            continue;
        }

        if ( call.NumResends >= call.Retries ) {
            PendingCall expired = std::move( call );
            pendingCalls.erase( pendingCalls.begin() + i );
            if ( expired.Callback ) {
                expired.Callback( Reply() );
            }
            continue;
        }

        call.NumResends++;
        send( call.Destination, call.Datagram.data(),
              uint32_t( call.Datagram.size() ) );
        i++;
    }
}

void edNetActionClient::cancelAll() { pendingCalls.clear(); }
