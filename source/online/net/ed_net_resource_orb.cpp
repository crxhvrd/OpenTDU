#include "shared.h"
#include "ed_net_resource_orb.h"

#include "core/ed_store.h"
#include "ed_net_rpc.h"

edNetResourceORB::edNetResourceORB( edNetActionClient &actions )
    : actions( actions ),
      facility( 0 ),
      userID( 0u ),
      fileName{},
      peer(),
      pDest( nullptr ),
      destSize( 0u ),
      state( S_Idle ),
      fileID( 0u ),
      totalSize( 0u ),
      received( 0u ),
      chunkRetries( 0u ),
      chunkIntervalMs( 0u ),
      nextRequestMs( 0ull ),
      generation( 0u ),
      lastTickMs( 0ull ),
      callback() {}

void edNetResourceORB::setFileName( const char *pName ) {
    strncpy( fileName, pName, sizeof( fileName ) - 1 );
    fileName[sizeof( fileName ) - 1] = '\0';
}

void edNetResourceORB::startDownload( Callback_t newCallback,
                                      const uint64_t nowMs ) {
    // FUN_0062ddc0
    generation++;
    callback = std::move( newCallback );
    lastTickMs = nowMs;
    fileID = 0u;
    totalSize = 0u;
    received = 0u;
    sendOpen( nowMs );
}

void edNetResourceORB::sendOpen( const uint64_t nowMs ) {
    // FUN_0062da40
    edStore request;
    request.updateCRC( edNetRPC::ROpenFile() );
    request.writeByte( facility );
    request.writeInteger( int32_t( userID ) );
    request.writeString( fileName );
    request.writeByte( 0 );    // bupdload_wanted
    request.writeInteger( 0 ); // uploadtotalsize

    state = S_Opening;
    const uint32_t requestGeneration = generation;
    actions.callAction(
        peer, request,
        [this, requestGeneration]( const edNetActionClient::Reply &reply ) {
            if ( requestGeneration == generation ) {
                onOpenReply( reply, lastTickMs );
            }
        },
        nowMs, kOpenIntervalMs, kOpenRetries );
}

void edNetResourceORB::onOpenReply( const edNetActionClient::Reply &reply,
                                    const uint64_t nowMs ) {
    if ( reply.Result != edNetActionClient::CR_Success ||
         reply.pReply == nullptr ||
         reply.pReply->getStreamCRC() != edNetRPC::AOpenFile() ) {
        finish( false );
        return;
    }

    edStore &answer = *reply.pReply;
    const uint8_t error = answer.readByte();
    fileID = answer.readUInteger();
    totalSize = answer.readUInteger();
    chunkRetries = answer.readUInteger();
    chunkIntervalMs = answer.readUInteger();
    if ( error != E_None || answer.getStatus() != 0 ) {
        finish( false );
        return;
    }

    if ( received >= totalSize ) {
        finish( true );
        return;
    }
    sendGetFile( nowMs );
}

void edNetResourceORB::sendGetFile( const uint64_t nowMs ) {
    edStore request;
    request.updateCRC( edNetRPC::RGetFile() );
    request.writeInteger( int32_t( fileID ) );
    request.writeInteger( int32_t( received ) );

    state = S_Downloading;
    const uint32_t requestGeneration = generation;
    actions.callAction(
        peer, request,
        [this, requestGeneration]( const edNetActionClient::Reply &reply ) {
            if ( requestGeneration == generation ) {
                onGetFileReply( reply, lastTickMs );
            }
        },
        nowMs, Max( chunkIntervalMs, 100u ), Max( chunkRetries, 1u ) );
}

void edNetResourceORB::onGetFileReply( const edNetActionClient::Reply &reply,
                                       const uint64_t nowMs ) {
    // FUN_0062d600
    if ( reply.Result != edNetActionClient::CR_Success ||
         reply.pReply == nullptr ||
         reply.pReply->getStreamCRC() != edNetRPC::AGetFile() ) {
        finish( false );
        return;
    }

    edStore &answer = *reply.pReply;
    const uint8_t error = answer.readByte();
    answer.readUInteger(); // fileid
    const uint32_t offset = answer.readUInteger();
    const uint16_t nextWaitMs = answer.readUShort();

    if ( error == E_Busy ) {
        state = S_WaitReopen;
        nextRequestMs = nowMs + nextWaitMs;
        return;
    }
    if ( error != E_None || offset != received ) {
        finish( false );
        return;
    }

    uint8_t chunk[edNetSocket::kMaxDatagramSize];
    const uint32_t chunkLength =
        answer.readBufferNoAlloc( chunk, sizeof( chunk ) );
    if ( answer.getStatus() != 0 ) {
        finish( false );
        return;
    }

    if ( pDest != nullptr && received < destSize ) {
        memcpy( pDest + received, chunk,
                Min( chunkLength, destSize - received ) );
    }
    received += chunkLength;

    if ( chunkLength == 0u || received >= totalSize ) {
        finish( received >= totalSize );
        return;
    }

    state = S_WaitNextChunk;
    nextRequestMs = nowMs + nextWaitMs;
}

void edNetResourceORB::tick( const uint64_t nowMs ) {
    lastTickMs = nowMs;
    if ( nowMs < nextRequestMs ) {
        return;
    }

    if ( state == S_WaitReopen ) {
        sendOpen( nowMs );
    } else if ( state == S_WaitNextChunk ) {
        sendGetFile( nowMs );
    }
}

void edNetResourceORB::finish( const bool bSuccess ) {
    state = S_Idle;
    generation++;
    Callback_t finished = std::move( callback );
    callback = nullptr;
    if ( finished ) {
        finished( bSuccess );
    }
}
