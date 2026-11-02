#include "shared.h"
#include "online_services.h"

#include "core/ed_store.h"
#include "core/time.h"
#include "online/net/ed_net_config.h"
#include "ed_net_gamespy_manager.h"
#include "gs_online.h"
#include "p2p_game_service.h"
#include "player_data/driver.h"
#include "player_data/gs_playerdata.h"
#include "player_data/player_data_interface.h"

static uint32_t GetTickCountMs() {
    return uint32_t( MonotonicToMs( GetTimeMonotonic() ) );
}

void GetRPCHandlersRequest::reset() {
    // FUN_00417890
    bDone = false;
    bSuccess = false;
    memset( CRCs, 0, sizeof( CRCs ) );
}

OnlineServices::OnlineServices()
    : Core(),
      Log(),
      GameMaster(),
      UserMap(),
      GPSTracker(),
      UserDatabase(),
      Team(),
      News(),
      Replay(),
      CarDealer(),
      PUSStorage(),
      PTSStorage(),
      Stats(),
      Ranking(),
      Check(),
      Savegame(),
      Username( "TDU-UNDEFINED" ),
      bGPSTrackerFromIni( false ),
      bLobbyFromIni( false ),
      bServerFromIni( false ),
      bReconnectPending( false ),
      bFastDiscovery( false ),
      lastDiscoveryTick( 0u ),
      discovery(),
      lastSuccessTick( 0u ),
      discoveryStartTick( 0u ),
      pResourceORB( nullptr ),
      netConfigBuffer{} {
    // FUN_00878850
}

void OnlineServices::sendDiscovery() {
    // FUN_00878f20
    lastDiscoveryTick = Max(
        GetTickCountMs(), uint32_t( gNetConfig.getInt( "ORB_REFRESH_DT" ) ) );
    if ( Core.IP == 0u ) {
        return;
    }

    uint32_t userID = 0u;
    uint32_t sessionID = 0u;
    IPlayerData *pLoadedSave = gpPlayerData->getLoadedSave();
    Driver *pDriver =
        ( pLoadedSave != nullptr ) ? pLoadedSave->getDriver() : nullptr;
    if ( pDriver != nullptr && pDriver->isValid() ) {
        userID = pDriver->ID;
        sessionID = pDriver->getLocale();
    }

    discovery.reset();
    for ( uint32_t i = 0; i < edNetRPC::kNumRequestHandlerSignatures; i++ ) {
        discovery.CRCs[i] =
            edNetRPC::MessageID( edNetRPC::kRequestHandlerSignatures[i] );
    }

    sendGetRPCHandlersEx( userID, sessionID );
}

bool OnlineServices::sendGetRPCHandlersEx( const uint32_t userID,
                                           const uint32_t sessionID ) {
    // FUN_00875fd0
    discoveryStartTick = GetTickCountMs();

    uint32_t intervalMs = 1500;
    uint32_t retries = 7;
    if ( bFastDiscovery ) {
        bFastDiscovery = false;
        intervalMs = 1000;
        retries = 3;
    }

    edStore request;
    request.updateCRC( edNetRPC::RGetRequestHandlersEx() );
    request.writeInteger( int32_t( userID ) );
    request.writeULongLong( uint64_t( sessionID ) );
    for ( uint32_t i = 0; i < edNetRPC::kMaxRequestHandlers; i++ ) {
        request.writeUShort( discovery.CRCs[i] );
    }

    gpCP2PGameService->getActionClient().callAction(
        Core, request,
        [this]( const edNetActionClient::Reply &reply ) {
            onDiscoveryComplete( reply );
        },
        GetTickCountMs(), intervalMs, retries );
    return true;
}

void OnlineServices::onDiscoveryComplete(
    const edNetActionClient::Reply &reply ) {
    // FUN_00879500
    discovery.bSuccess = reply.Result == edNetActionClient::CR_Success;
    discovery.bDone = true;

    OTDU_LOG_INFO( "**GetRPCHandlersEx() Took = %u Ms**  result = %s\n",
                   GetTickCountMs() - discoveryStartTick,
                   discovery.bSuccess ? "SUCCESS" : "FAILED" );

    if ( discovery.bSuccess ) {
        lastSuccessTick = GetTickCountMs();

        edStore *pReply = reply.pReply;
        if ( pReply != nullptr &&
             pReply->getStreamCRC() == edNetRPC::AGetRequestHandlersEx() ) {
            edNetProxy &proxy = gpCP2PGameService->getProxy();
            for ( uint32_t i = 0; i < edNetRPC::kMaxRequestHandlers; i++ ) {
                edNetAddress &result = discovery.Results[i];
                if ( pReply->getReadCursor() < pReply->getWrittenSize() ) {
                    pReply->readUShort();
                    result.IP = pReply->readUInteger();
                    result.Port = pReply->readUShort();
                    if ( result.IP != 0u ) {
                        proxy.registerRoute( result );
                    }
                } else {
                    result = {};
                }
            }
        } else {
            discovery.bSuccess = false;
        }

        if ( discovery.bSuccess ) {
            // FUN_008762a0
            const edNetAddress *pResults = discovery.Results;
            Log = pResults[0];
            if ( !bLobbyFromIni ) {
                GameMaster = pResults[1];
            }
            if ( !bGPSTrackerFromIni ) {
                GPSTracker = pResults[2];
            }
            if ( !bServerFromIni ) {
                UserDatabase = pResults[3];
            }
            UserMap = pResults[4];
            Team = pResults[5];
            News = pResults[6];
            Replay = pResults[7];
            CarDealer = pResults[8];
            Stats = pResults[9];
            for ( uint32_t k = 0; k < kNumPUSStorages; k++ ) {
                PUSStorage[k] = pResults[10 + k];
            }
            PTSStorage = pResults[20];
            Ranking = pResults[21];
            Check = pResults[22];
            Savegame = pResults[22];

            OTDU_LOG_INFO( "===> Starting application : GetRPCHandlersEx() "
                           "Complete\n" );
            if ( CarDealer.IP == 0u ) {
                OTDU_LOG_WARN( "===>  CARDEALER NOT AVAILABLE ... USING LOCAL "
                               "DEALER\n" );
            }
            return;
        }
    }

    if ( lastSuccessTick + uint32_t( gNetConfig.getInt( "ORB_LOST_RPC_DT" ) ) <
         GetTickCountMs() ) {
        lastSuccessTick = GetTickCountMs();
        onORBLost();
    }
}

void OnlineServices::requestNetConfig( const char *pFileName ) {
    // FUN_00876850
    static constexpr uint8_t kFacility = 2;
    static constexpr uint32_t kUserID = 0x494707d4;

    if ( pResourceORB == nullptr ) {
        pResourceORB = std::make_unique<edNetResourceORB>(
            gpCP2PGameService->getActionClient() );
    }

    memset( netConfigBuffer, 0, sizeof( netConfigBuffer ) );
    pResourceORB->setDestination( netConfigBuffer,
                                  sizeof( netConfigBuffer ) - 1 );
    pResourceORB->setFacility( kFacility );
    pResourceORB->setUserID( kUserID );
    pResourceORB->setPeer( PTSStorage );
    pResourceORB->setFileName( pFileName );
    pResourceORB->startDownload(
        [this]( const bool bSuccess ) { onNetConfigDownloaded( bSuccess ); },
        GetTickCountMs() );
}

void OnlineServices::onNetConfigDownloaded( const bool bSuccess ) {
    // FUN_00879320
    if ( !bSuccess || pResourceORB->getReceivedSize() == 0u ) {
        OTDU_LOG_WARN( "**NetConfig Download** FAILED\n" );
        return;
    }

    OTDU_LOG_INFO( "**NetConfig Downloaded**\n" );
    netConfigBuffer[sizeof( netConfigBuffer ) - 1] = '\0';
    gNetConfig.parse( reinterpret_cast<const char *>( netConfigBuffer ) );
    edNetActionClient::DefaultIntervalMs =
        uint32_t( gNetConfig.getInt( "EDNET_DEFAULT_TIMEOUT" ) );
    edNetActionClient::DefaultRetries =
        uint32_t( gNetConfig.getInt( "EDNET_DEFAULT_MAXRETRY" ) );
}

void OnlineServices::refresh() {
    // FUN_00879240
    if ( pResourceORB != nullptr ) {
        pResourceORB->tick( GetTickCountMs() );
    }

    if ( bReconnectPending && gpEdNetGamespyManager != nullptr &&
         gpEdNetGamespyManager->isConnectedToGamespy() &&
         gpEdNetGamespyManager->getGPState() ==
             edNetGamespyManager::GPS_LoggedIn ) {
        bReconnectPending = false;
        gpOnline->setOnlineEnabled( true );
        resetConnection();
        gpOnline->setServicesActive( true );
        gpOnline->setNatType( 1 );
        if ( !gpPlayerData->isOnline() ) {
            gpPlayerData->setOnline( true );
            gpPlayerData->notify( 0u );
        }
        OTDU_UNIMPLEMENTED_ONCE; // TODO: PLAYERSAVE
        lastDiscoveryTick = 0u;
        discovery.reset();
    }

    const uint32_t now = Max(
        GetTickCountMs(), uint32_t( gNetConfig.getInt( "ORB_REFRESH_DT" ) ) );
    if ( !gpPlayerData->isOnline() ) {
        lastSuccessTick = now;
        lastDiscoveryTick = now;
    } else if ( lastDiscoveryTick +
                    uint32_t( gNetConfig.getInt( "ORB_REFRESH_DT" ) ) <=
                now ) {
        sendDiscovery();
    }
}

void OnlineServices::onORBLost() {
    // FUN_00879380
    OTDU_LOG_WARN( "** OnORBLost() !! \n" );
    OTDU_UNIMPLEMENTED_ONCE; // TODO: FRONTEND (connection lost prompt
                             // FUN_5d5114 when the "/avert" clip is closed)

    gpOnline->setOnlineEnabled( false );
    GameMaster.IP = 0u;
    UserDatabase.IP = 0u;
    UserDatabase.Port = 0u;
    GPSTracker.IP = 0u;

    clear();
    resetConnection();
    gpOnline->setServicesActive( true );
    gpOnline->setNatType( 1 );

    if ( gpPlayerData->isOnline() ) {
        gpPlayerData->setOnline( false );
        gpPlayerData->notify( 0u );
    }
    OTDU_UNIMPLEMENTED_ONCE; // TODO: PLAYERSAVE (PrepareActiveProfile)

    bReconnectPending = true;
}

void OnlineServices::resetConnection() {
    // FUN_00876970
    if ( gpOnline->isORBConfigured() ) {
        edNetAddress::Resolve( gpOnline->getORBAddress(),
                               gpOnline->getORBPort(), Core );
    }

    lastDiscoveryTick = 0u;
    lastSuccessTick = GetTickCountMs();
    discovery.reset();
    OTDU_UNIMPLEMENTED_ONCE; // TODO: ONLINE
}

void OnlineServices::clear() {
    // FUN_008768d0
    Core.IP = 0u;
    Log.IP = 0u;
    GameMaster.IP = 0u;
    UserMap.IP = 0u;
    GPSTracker.IP = 0u;
    UserDatabase.IP = 0u;
    Team.IP = 0u;
    News.IP = 0u;
    Replay.IP = 0u;
    CarDealer.IP = 0u;
    for ( edNetAddress &storage : PUSStorage ) {
        storage.IP = 0u;
    }
    PTSStorage.IP = 0u;
    Stats.IP = 0u;
    Ranking.IP = 0u;
    Check.IP = 0u;
    Savegame.IP = 0u;
}
