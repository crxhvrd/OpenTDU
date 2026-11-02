#include "shared.h"
#include "core_requests.h"

#include "service/action_service.h"
#include "service/proxy_service.h"
#include "tdu_server.h"

#include "core/ed_store.h"
#include "online/net/ed_net_rpc.h"

static bool OnGetRequestHandlersEx( Server &server, RPCCall &call ) {
    ClientSession &session = call.Session;
    edStore &request = call.Request;
    edStore &reply = call.Reply;

    const uint32_t userID = request.readUInteger();
    const uint64_t sessionID = request.readULongLong();

    uint16_t requestedIDs[edNetRPC::kMaxRequestHandlers] = {};
    uint32_t numRequested = 0;
    for ( uint32_t i = 0; i < edNetRPC::kMaxRequestHandlers; i++ ) {
        requestedIDs[i] = request.readUShort();
        if ( requestedIDs[i] != 0 ) {
            numRequested = i + 1;
        }
    }
    if ( request.getStatus() != 0 ) {
        OTDU_LOG_WARN( "Malformed r_get_request_handlers_ex from session "
                       "%08X\n",
                       session.SessionID );
        return false;
    }

    OTDU_LOG_INFO( "Service discovery for session %08X (user %u, session "
                   "%llu, %u handlers)\n",
                   session.SessionID, userID, sessionID, numRequested );

    // Every service lives in this process.
    const edNetAddress &address = server.getPublicAddress();
    reply.updateCRC( edNetRPC::AGetRequestHandlersEx() );
    for ( uint32_t i = 0; i < numRequested; i++ ) {
        const bool bAvailable = requestedIDs[i] != 0;
        reply.writeUShort( requestedIDs[i] );
        reply.writeInteger( int32_t( bAvailable ? address.IP : 0u ) );
        reply.writeUShort( bAvailable ? address.Port : 0u );
    }
    return true;
}

void RegisterCoreRequests( Server &server, ActionService &actions ) {
    actions.registerRPC( edNetRPC::RGetRequestHandlersEx(),
                         "coreresquests::r_get_request_handlers_ex",
                         [&server]( RPCCall &call ) {
                             return OnGetRequestHandlersEx( server, call );
                         } );
}
