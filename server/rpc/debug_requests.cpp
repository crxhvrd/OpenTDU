#include "shared.h"
#include "debug_requests.h"

#include "service/action_service.h"
#include "service/proxy_service.h"
#include "tdu_server.h"

#include "core/ed_store.h"
#include "online/net/ed_net_rpc.h"

#include <unordered_map>

void RegisterDebugRequests( Server &server, ActionService &actions ) {
    static std::unordered_map<uint32_t, uint32_t> sCounters;

    actions.registerRPC( DebugRequests::kCounterRequest, []( RPCCall &call ) {
        const uint32_t value = ++sCounters[call.Session.SessionID];
        call.Reply.updateCRC(
            edNetRPC::MessageID( DebugRequests::kCounterReply ) );
        call.Reply.writeInteger( int32_t( value ) );
        return true;
    } );

    actions.registerRPC( DebugRequests::kNetBufferRequest, []( RPCCall &call ) {
        const uint32_t size = call.Request.readUInteger();
        const uint32_t seed = call.Request.readUInteger();
        if ( call.Request.getStatus() != 0 || size > 1024u * 1024u ) {
            return false;
        }

        call.NetBuffer.resize( size );
        for ( uint32_t i = 0; i < size; i++ ) {
            call.NetBuffer[i] = DebugRequests::GetNetBufferByte( seed, i );
        }
        call.bUseNetBuffer = true;
        return true;
    } );

    OTDU_LOG_WARN( "Debug RPCs enabled\n" );
}
