#include "shared.h"
#include "lobby_network_service.h"

#include "p2p_game_service.h"

CLobbyNetworkService gLobbyNetworkService;

CLobbyNetworkService::CLobbyNetworkService()
    : bInitialized( false ), bServicesActive( false ), bP2PInit( false ) {}

void CLobbyNetworkService::initialize() {
    // FUN_00880030
    if ( bInitialized ) {
        return;
    }

    OTDU_UNIMPLEMENTED_ONCE; // TODO: ONLINE

    bInitialized = true;
    bServicesActive = true;
    if ( gpCP2PGameService->init() ) {
        bP2PInit = true;
        OTDU_LOG_INFO( "GET_CUR_P2P()->Init() succeded (UDP %u)\n",
                       gpCP2PGameService->getSocket().getLocalPort() );
    }
}

void CLobbyNetworkService::refresh() {
    // FUN_00880cf0
    OTDU_UNIMPLEMENTED_ONCE; // TODO: ONLINE

    if ( bP2PInit ) {
        gpCP2PGameService->refresh();
    }
}

void CLobbyNetworkService::terminate() {
    // FUN_0087c620
    bServicesActive = false;
    gpCP2PGameService->term();
    bP2PInit = false;
}
