#include "shared.h"
#include "ed_net_netbuffer.h"

#include "core/ed_store.h"
#include "core/hash/crc32.h"
#include "ed_net_proxy.h"
#include "ed_net_rpc.h"

edNetNetBufferClient::edNetNetBufferClient( edNetProxy &transport )
    : transport( transport ), downloads() {}

edNetNetBufferClient::~edNetNetBufferClient() {}

void edNetNetBufferClient::getNetBuffer( const edNetAddress &source,
                                         const uint32_t bufferID,
                                         uint8_t *pDest,
                                         const uint32_t size,
                                         Callback_t callback,
                                         const uint64_t nowMs ) {
    // FUN_00625130
    Download download;
    download.Source = source;
    download.BufferID = bufferID;
    download.pDest = pDest;
    download.Size = size;
    download.Callback = std::move( callback );
    downloads.push_back( std::move( download ) );

    tick( nowMs );
}

void edNetNetBufferClient::sendRequest( Download &download,
                                        const uint64_t nowMs ) {
    // FUN_006250c0
    edStore request;
    request.updateCRC( edNetRPC::RGetNetBuffer() );
    request.writeInteger( int32_t( download.BufferID ) );
    request.writeInteger( int32_t( download.Received ) );
    transport.send( download.Source, request );

    download.NumTries++;
    download.LastRequestMs = nowMs;
    download.bRequestNow = false;
}

void edNetNetBufferClient::finish( const size_t index, const bool bSuccess ) {
    Download download = std::move( downloads[index] );
    downloads.erase( downloads.begin() + index );

    edStore ack;
    ack.updateCRC( edNetRPC::EGetNetBufferAck() );
    ack.writeInteger( int32_t( download.BufferID ) );
    transport.send( download.Source, ack );

    if ( download.Callback ) {
        download.Callback( bSuccess );
    }
}

bool edNetNetBufferClient::onMessage( const edNetAddress &sender,
                                      edStore &message ) {
    // FUN_00624e80
    if ( message.getStreamCRC() != edNetRPC::AGetNetBuffer() ) {
        return false;
    }

    const uint32_t bufferID = message.readUInteger();
    const uint8_t errorCode = message.readByte();

    auto it = std::find_if(
        downloads.begin(), downloads.end(), [&]( const Download &download ) {
            return download.BufferID == bufferID && download.Source == sender;
        } );
    if ( it == downloads.end() ) {
        return true;
    }
    const size_t index = size_t( it - downloads.begin() );

    if ( errorCode != 1 ) {
        finish( index, false );
        return true;
    }

    const uint32_t offset = message.readUInteger();
    message.readUInteger(); // totalSize (the size from a_callaction wins)
    const uint16_t chunkCRC = message.readUShort();

    uint8_t chunk[edNetSocket::kMaxDatagramSize];
    const uint32_t chunkLength =
        message.readBufferNoAlloc( chunk, sizeof( chunk ) );
    if ( message.getStatus() != 0 ) {
        return true;
    }

    Download &download = *it;
    if ( offset != download.Received ||
         uint16_t( TestDriveCRC32( chunk, chunkLength ) ) != chunkCRC ||
         chunkLength > download.Size - download.Received ) {
        return true;
    }

    memcpy( download.pDest + offset, chunk, chunkLength );
    download.Received += chunkLength;
    download.NumTries = 0u;
    download.bRequestNow = true;

    if ( download.Received >= download.Size ) {
        finish( index, true );
    }
    return true;
}

void edNetNetBufferClient::tick( const uint64_t nowMs ) {
    // FUN_00625560
    for ( size_t i = 0; i < downloads.size(); ) {
        Download &download = downloads[i];
        if ( download.Size == 0u ) {
            finish( i, true );
            continue;
        }

        if ( !download.bRequestNow &&
             nowMs < download.LastRequestMs + kRetryIntervalMs ) {
            i++;
            continue;
        }

        if ( download.NumTries >= kMaxTries ) {
            finish( i, false );
            continue;
        }

        sendRequest( download, nowMs );
        i++;
    }
}

void edNetNetBufferClient::cancelAll() { downloads.clear(); }
