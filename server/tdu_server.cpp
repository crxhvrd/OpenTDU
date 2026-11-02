#include "shared.h"
#include "tdu_server.h"

#include "service/action_service.h"
#include "rpc/core_requests.h"
#include "rpc/database_requests.h"
#include "rpc/resource_orb_requests.h"
#include "database/database.h"
#include "rpc/identity_requests.h"
#include "rpc/debug_requests.h"
#include "service/proxy_service.h"

Server::Server( const ServerConfig &config )
    : config( config ),
      socket(),
      publicAddress(),
      pDatabase( std::make_unique<Database>() ),
      pProxyService( nullptr ),
      startTime( std::chrono::steady_clock::now() ) {}

Server::~Server() { terminate(); }

bool Server::initialize() {
    if ( !edNetAddress::Resolve( config.PublicAddress.c_str(), config.ProxyPort,
                                 publicAddress ) ) {
        OTDU_LOG_ERROR( "Invalid public address '%s'\n",
                        config.PublicAddress.c_str() );
        return false;
    }

    if ( !pDatabase->open( config.DatabasePath.c_str() ) ) {
        return false;
    }

    if ( !socket.open( config.ProxyPort, config.BindAddress.c_str() ) ) {
        return false;
    }
    socket.setSimulatedConditions( config.SimulatedLoss,
                                   config.SimulatedDuplication );

    pProxyService = std::make_unique<ProxyService>( *this );
    RegisterCoreRequests( *this, pProxyService->getActionService() );
    RegisterIdentityRequests( *this, pProxyService->getActionService() );
    RegisterDatabaseRequests( *this, pProxyService->getActionService() );
    RegisterResourceORBRequests( *this, pProxyService->getActionService() );
    if ( config.bDebugRPCs ) {
        RegisterDebugRequests( *this, pProxyService->getActionService() );
    }
    return true;
}

bool Server::tick() {
    if ( pProxyService == nullptr ) {
        return false;
    }

    bool bReceivedAny = false;
    uint8_t buffer[edNetSocket::kMaxDatagramSize];
    for ( int32_t i = 0; i < 256; i++ ) {
        edNetAddress sender;
        const int32_t size =
            socket.receiveFrom( sender, buffer, sizeof( buffer ) );
        if ( size <= 0 ) {
            break;
        }

        pProxyService->onDatagram( sender, buffer, uint32_t( size ) );
        bReceivedAny = true;
    }

    pProxyService->tick();
    return bReceivedAny;
}

void Server::terminate() {
    pProxyService.reset();
    socket.close();
    pDatabase->close();
}

uint64_t Server::getTimeMs() const {
    return uint64_t( std::chrono::duration_cast<std::chrono::milliseconds>(
                         std::chrono::steady_clock::now() - startTime )
                         .count() );
}
