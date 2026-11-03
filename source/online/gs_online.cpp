#include "shared.h"
#include "gs_online.h"

#include "config/gs_config.h"
#include "core/ed_store.h"
#include "core/time.h"
#include "ed_net_gamespy_manager.h"
#include "online/net/ed_net_rpc.h"
#include "gs_cd_key.h"
#include "lobby_network_service.h"
#include "p2p_game_service.h"
#include "player_data/driver.h"
#include "player_data/gs_playerdata.h"
#include "player_data/player_data_interface.h"
#include "player_data/player_save.h"
#include "player_data/saved_garage.h"
#include "tdu_instance.h"
#include "core/arg_parser.h"

GSOnline gOnline = {};
GSOnline *gpOnline = nullptr;

char gORBAddress[128];

static bool gbORBAddressOverridden = false;

static CmdLineArg CmdLineArgORB( "orb", []( const char *pArg ) {
    // FUN_00406520
    strncpy( gORBAddress, pArg, 127 );
    gORBAddress[127] = '\0';
    gbORBAddressOverridden = true;

    OTDU_LOG_INFO( "ORB set to '%s'\n", gORBAddress );
} );

void edNetProxyHostInterfaceTDU::onProxyState( const int32_t state ) {
    // FUN_0087b6a0
    OTDU_LOG_DEBUG( "edNetProxy state %d\n", state );
}

void edNetProxyHostInterfaceTDU::onProxyConnected(
    const edNetAddress &coreAddress ) {
    // FUN_0087c450
    OTDU_LOG_INFO( "edNetProxy connected (core server %s)\n",
                   coreAddress.toString().c_str() );
    bResult = true;

    OnlineServices &services = gpOnline->getServices();
    services.Core = coreAddress;

    edNetProxy &proxy = gpCP2PGameService->getProxy();
    proxy.registerRoute( coreAddress );

    gpOnline->setNatType( 1 );
    if ( proxy.getPublicAddress().Port != GSOnline::kProxyPort ) {
        gpOnline->setNatType( 3 );
    }

    services.sendDiscovery();
}

void edNetProxyHostInterfaceTDU::onProxyError( const int32_t code,
                                               const char *pMessage ) {
    // FUN_0087b650
    OTDU_LOG_WARN( "edNetProxy error %d (%s)\n", code,
                   pMessage != nullptr ? pMessage : "" );
    bResult = true;
    errorCode = code;
    if ( code == edNetProxyProtocol::IE_Rejected && pMessage != nullptr ) {
        strncpy( pUpdateURL, pMessage, sizeof( pUpdateURL ) );
        pUpdateURL[sizeof( pUpdateURL ) - 1] = '\0';
    }
}

void edNetProxyHostInterfaceTDU::buildHello(
    edNetProxyProtocol::Hello &outHello ) {
    // FUN_0087bf30
    const char *pName = "UNKNOWN";
    IPlayerData *pLoadedSave = gpPlayerData->getLoadedSave();
    Driver *pDriver =
        ( pLoadedSave != nullptr ) ? pLoadedSave->getDriver() : nullptr;
    if ( pDriver != nullptr ) {
        pName = pDriver->Nickname;
    }

    memset( outHello.PlayerName, 0, sizeof( outHello.PlayerName ) );
    strncpy( outHello.PlayerName, pName, 0x15 );
    memset( outHello.CDKey, 0, sizeof( outHello.CDKey ) );
    memset( outHello.Version, 0, sizeof( outHello.Version ) );
    strncpy( outHello.Version, TestDrive::GetVersion(),
             strlen( TestDrive::GetVersion() ) );
    strncpy( outHello.CDKey, gpCDKey->getGameKey(), 0x13 );
}

GSOnline::GSOnline()
    : proxyHostInterface(),
      services(),
      pServerAddress{},
      serverPort( 4000 ),
      pORBAddress{},
      orbPort( OnlineServices::kORBPort ),
      lobbyPort( 0 ),
      pLogin( "none" ),
      pPassword( "edenpwd" ),
      loginState( LS_Idle ),
      bMatchmakingEnabled( false ),
      bOnlineEnabled( false ),
      pLobbyAddress{},
      pDbgCOM( "127.0.0.1" ),
      pGPSTracker{},
      pUsername{},
      bServicesActive( false ),
      bLoggedIn( false ),
      bLoginAnswered( false ),
      field_0xa713( false ),
      bInitialized( false ),
      pFreerideVersion( "PC Beta - 2.41A" ),
      natType( 1 ),
      userID( 0u ),
      teamID( 0u ),
      teamSpotHash( 0ull ),
      serverTime( 0u ),
      identityBuffer(),
      bORBFromIni( false ),
      bXBLModeSecure( false ),
      bNewsReaderOpen( false ),
      challengeLeaderboards(),
      numChallengeLeaderboards() {
    // FUN_0087c1c0
    gpOnline = this;
}

GSOnline::~GSOnline() {}

void GSOnline::initConnectionInfos() {
    // FUN_0087bba0
    bOnlineEnabled = gpConfig->bEnableOnline;
    bMatchmakingEnabled = gpConfig->bEnableMatchmaking;

    if ( pLobbyAddress[0] != '\0' && strncmp( pLobbyAddress, "none", 4 ) ) {
        edNetAddress::Resolve( pLobbyAddress, lobbyPort, services.GameMaster );
    }
    if ( pServerAddress[0] != '\0' && strncmp( pServerAddress, "none", 4 ) ) {
        edNetAddress::Resolve( pServerAddress, serverPort,
                               services.UserDatabase );
    }
    if ( pGPSTracker[0] != '\0' && strncmp( pGPSTracker, "none", 4 ) ) {
        edNetAddress::Resolve( pGPSTracker, kGPSTrackerPort,
                               services.GPSTracker );
    }
}

bool GSOnline::initialize( TestDriveGameInstance * ) {
    // FUN_0087c780
    natType = 1;

    if ( !bORBFromIni ) {
        setORBAddress( gORBAddress );
    }
    if ( isORBConfigured() ) {
        OTDU_LOG_INFO( "Online server: %s (proxy port %u)\n", pORBAddress,
                       kProxyPort );
    } else {
        OTDU_LOG_INFO( "No online server configured ('%s'): online profiles "
                       "will play offline\n",
                       pORBAddress );
    }

    initConnectionInfos();
    services.resetConnection();
    bServicesActive = true;
    natType = 1;

    field_0xa713 = false;
    bLoggedIn = false;

    gLobbyNetworkService.initialize();

    gpEdNetGamespyManager = new edNetGamespyManager();
    gpEdNetGamespyManager->initialize();

    memset( services.Username, 0, sizeof( services.Username ) );
    strncpy( services.Username, pUsername, sizeof( services.Username ) - 1 );

    gpCP2PGameService->setProxyHostInterface( &proxyHostInterface );
    bInitialized = true;
    return true;
}

void GSOnline::tick( float totalTime, float deltaTime ) {
    // FUN_0087dab0
    if ( gpEdNetGamespyManager != nullptr ) {
        gpEdNetGamespyManager->tick();
    }

    if ( !bServicesActive ) {
        return;
    }

    gLobbyNetworkService.refresh();
    services.refresh();

    if ( bLoggedIn ) {
        OTDU_UNIMPLEMENTED_ONCE; // TODO: ONLINE (news refresh FUN_0087c5c0,
                                 // FUN_0087be70, FUN_0087c030)
    }
}

void GSOnline::terminate() {
    // FUN_0087c620
    bInitialized = false;
    if ( gpEdNetGamespyManager != nullptr ) {
        gpEdNetGamespyManager->disconnect();
        delete gpEdNetGamespyManager;
        gpEdNetGamespyManager = nullptr;
    }
    gLobbyNetworkService.terminate();
}

void GSOnline::setFreeRideVersion( const char *pVersion ) {
    strncpy( pFreerideVersion, pVersion, sizeof( pFreerideVersion ) - 1 );
}

void GSOnline::setXBLModeSecure( bool param_1 ) { bXBLModeSecure = param_1; }

void GSOnline::setUsername( const char *param_1 ) {
    strncpy( pUsername, param_1, 0x10 );
    pUsername[0xf] = '\0';
}

bool GSOnline::setServerLogin( const char *pNewLogin,
                               const char *pNewPassword ) {
    // FUN_0087b490
    strncpy( pLogin, pNewLogin, sizeof( pLogin ) - 1 );
    strncpy( pPassword, pNewPassword, sizeof( pPassword ) - 1 );
    return true;
}

bool GSOnline::setServerAddress( const char *pAddress, const int32_t port ) {
    // FUN_0087b940
    if ( pAddress == nullptr ) {
        return false;
    }

    strncpy( pServerAddress, pAddress, sizeof( pServerAddress ) - 1 );
    serverPort = uint16_t( port );
    services.bServerFromIni = true;
    return true;
}

void GSOnline::setLobbying( const bool bEnabled ) {
    // FUN_00987820
    if ( !bEnabled ) {
        pLobbyAddress[0] = '\0';
    }
}

void GSOnline::setLobbyAddress( const char *pAddress, const int32_t port ) {
    // FUN_0087b900
    if ( pAddress == nullptr ) {
        pLobbyAddress[0] = '\0';
        return;
    }

    strncpy( pLobbyAddress, pAddress, sizeof( pLobbyAddress ) - 1 );
    lobbyPort = uint16_t( port );
    services.bLobbyFromIni = true;
}

void GSOnline::setGPSTrackerAddress( const char *pAddress ) {
    // FUN_009877d0 (inlined)
    strncpy( pGPSTracker, pAddress, sizeof( pGPSTracker ) - 1 );
    services.bGPSTrackerFromIni = true;
}

void GSOnline::setDbgCOMAddress( const char *param_1 ) {
    strncpy( pDbgCOM, param_1, sizeof( pDbgCOM ) - 1 );
}

void GSOnline::setORBAddress( const char *pAddress ) {
    strncpy( pORBAddress, pAddress, 0x7f );
    pORBAddress[0x7f] = '\0';
}

void GSOnline::setORBAddressFromIni( const char *pAddress,
                                     const int32_t port ) {
    if ( gbORBAddressOverridden ) {
        return;
    }

    setORBAddress( pAddress );
    if ( port > 0 ) {
        orbPort = uint16_t( port );
    }
    bORBFromIni = true;
}

bool GSOnline::isORBConfigured() const {
    static constexpr const char *kBlockedAddresses[] = { "83.145.75.75" };

    if ( pORBAddress[0] == '\0' || strcmp( pORBAddress, "none" ) == 0 ||
         strcmp( pORBAddress, gORBAddress ) == 0 ) {
        return false;
    }

    for ( const char *pBlocked : kBlockedAddresses ) {
        if ( strcmp( pORBAddress, pBlocked ) == 0 ) {
            return false;
        }
    }
    return true;
}

void GSOnline::startProxy() {
    // FUN_0074d9d0
    const edNetProxy::eResult result =
        gpCP2PGameService->getProxy().getResult();
    if ( result == edNetProxy::R_Connecting ||
         result == edNetProxy::R_Connected ) {
        return;
    }

    edNetAddress orbAddress;
    edNetAddress::Resolve( pORBAddress, kProxyPort, orbAddress );
    gpCP2PGameService->startProxy( orbAddress );
}

bool GSOnline::connectGameServer( const int32_t param ) {
    // FUN_0087bdb0
    if ( strncmp( pServerAddress, "offline", 8 ) == 0 ) {
        OTDU_LOG_INFO( "Offline mode - no connection to databases.\n" );
        return true;
    }

    if ( loginState != LS_Idle ) {
        return false;
    }

    OTDU_LOG_INFO( "Connecting to game server %s.\n",
                   services.UserDatabase.toString().c_str() );
    strncpy( pLogin, pUsername, sizeof( pLogin ) );

    if ( !sendLogin( uint8_t( param ) ) ) {
        return false;
    }

    loginState = LS_LoginSent;
    return true;
}

static constexpr uint32_t kIdentityNetBufferMode = 5u;

static uint32_t CountryCodeToCountryIndex( const uint32_t countryCode ) {
    // FUN_008797c0
    struct CountryIndex {
        uint32_t CountryCode;
        uint32_t Index;
    };
    static constexpr CountryIndex kCountries[] = {
        {1,   103},
        {2,   16 },
        {7,   88 },
        {20,  30 },
        {27,  109},
        {30,  37 },
        {31,  74 },
        {32,  8  },
        {33,  34 },
        {34,  31 },
        {36,  42 },
        {39,  50 },
        {40,  87 },
        {41,  18 },
        {43,  5  },
        {44,  35 },
        {45,  25 },
        {46,  90 },
        {47,  75 },
        {48,  82 },
        {49,  24 },
        {51,  79 },
        {52,  71 },
        {54,  4  },
        {55,  13 },
        {56,  19 },
        {57,  21 },
        {58,  106},
        {60,  72 },
        {61,  6  },
        {62,  43 },
        {63,  80 },
        {64,  76 },
        {65,  91 },
        {66,  97 },
        {81,  53 },
        {82,  56 },
        {84,  107},
        {86,  20 },
        {90,  99 },
        {91,  46 },
        {92,  81 },
        {212, 65 },
        {213, 27 },
        {216, 98 },
        {218, 64 },
        {254, 54 },
        {263, 110},
        {298, 33 },
        {351, 84 },
        {352, 62 },
        {353, 44 },
        {354, 49 },
        {355, 2  },
        {358, 32 },
        {359, 9  },
        {370, 61 },
        {371, 63 },
        {372, 29 },
        {374, 3  },
        {375, 14 },
        {380, 102},
        {385, 41 },
        {386, 92 },
        {389, 67 },
        {420, 23 },
        {421, 93 },
        {501, 15 },
        {502, 38 },
        {503, 95 },
        {504, 40 },
        {505, 73 },
        {506, 22 },
        {507, 78 },
        {591, 12 },
        {593, 28 },
        {595, 85 },
        {598, 104},
        {673, 11 },
        {852, 39 },
        {853, 69 },
        {886, 101},
        {960, 70 },
        {961, 59 },
        {962, 52 },
        {963, 96 },
        {964, 47 },
        {965, 57 },
        {966, 89 },
        {967, 108},
        {968, 77 },
        {971, 1  },
        {972, 45 },
        {973, 10 },
        {974, 86 },
        {976, 68 },
        {981, 48 },
        {994, 7  },
        {995, 36 },
        {996, 55 },
    };

    for ( const CountryIndex &country : kCountries ) {
        if ( country.CountryCode == countryCode ) {
            return country.Index;
        }
    }
    return 0u;
}

uint32_t GetCountryIndex() {
    // FUN_00879ec0
    static uint32_t gCountryIndex = 0xffffffff;
    if ( gCountryIndex == 0xffffffff ) {
        gCountryIndex = CountryCodeToCountryIndex( GetCountryCodeFromSystem() );
    }
    return gCountryIndex;
}

bool GSOnline::sendLogin( const uint8_t param ) {
    // FUN_00884c80
    edStore request;
    request.updateCRC( edNetRPC::RLoginPC() );
    request.writeString( pLogin );
    request.writeString( pPassword );
    request.writeInteger( int32_t( GetCountryIndex() ) );
    request.writeULongLong( uint64_t(
        int64_t( int32_t( gpEdNetGamespyManager->getProfileID() ) ) ) );
    request.writeByte( param );
    request.writeString( gpCDKey->getGameKey() );
    if ( gpCDKey->getAddonKey()[0] != '\0' ) {
        request.writeString( gpCDKey->getAddonKey() );
    }

    gpCP2PGameService->getActionClient().callAction(
        services.UserDatabase, request,
        [this]( const edNetActionClient::Reply &reply ) {
            onLoginComplete( reply );
        },
        uint64_t( MonotonicToMs( GetTimeMonotonic() ) ), 1500, 4 );
    return true;
}

void GSOnline::onLoginComplete( const edNetActionClient::Reply &reply ) {
    // FUN_008854a0
    if ( reply.Result != edNetActionClient::CR_Success ) {
        if ( gpPlayerData->isOnline() ) {
            gpPlayerData->setOnline( false );
            gpPlayerData->notify( 0u );
        }
        bLoginAnswered = true;
        return;
    }

    edStore *pReply = reply.pReply;
    if ( pReply == nullptr || pReply->getStreamCRC() != edNetRPC::ALogin() ) {
        return;
    }

    if ( pReply->readByte() != 1 ) {
        if ( gpPlayerData->isOnline() ) {
            gpPlayerData->setOnline( false );
            gpPlayerData->notify( 0u );
        }
        bLoginAnswered = true;
        return;
    }

    serverTime = pReply->readUInteger();
    gpPlayerData->getPlayerSave()->setServerTime( serverTime );
    gpCP2PGameService->setNetClock( serverTime );

    if ( pReply->readByte() == 0 ) {
        gpPlayerData->setOnline( false );
        gpPlayerData->notify( 0u );
        bLoginAnswered = true;
        return;
    }

    identityBuffer.assign( reply.BufferSize, 0u );
    gpCP2PGameService->getNetBufferClient().getNetBuffer(
        services.UserDatabase, reply.BufferID, identityBuffer.data(),
        reply.BufferSize,
        [this]( const bool bSuccess ) { onIdentityDownloaded( bSuccess ); },
        uint64_t( MonotonicToMs( GetTimeMonotonic() ) ) );
}

void GSOnline::onIdentityDownloaded( const bool bSuccess ) {
    // FUN_008853e0
    if ( bSuccess ) {
        edStore stream;
        stream.bindExternalBuffer( identityBuffer.data(),
                                   uint32_t( identityBuffer.size() ) );
        // The record has no CRC header: read from offset 0.
        stream.seek( 0 );
        parseLoginIdentity( stream );

        bLoggedIn = true;
        services.sendDiscovery();
    } else {
        OTDU_LOG_WARN( "in edNetServiceNetBufferGarage::OnTransferComplete "
                       "transfer failed\n" );
        gpPlayerData->getLoadedSave()->getGarage()->notify( 0u );
        bLoggedIn = false;
    }

    bLoginAnswered = true;
    gpPlayerData->requestSave( 2 );
}

void GSOnline::parseLoginIdentity( edStore &stream ) {
    // FUN_008850c0
    const uint32_t newUserID = stream.readUInteger();
    char nickname[Driver::kNicknameLength + 1] = {};
    stream.readStringNoAlloc( nickname, Driver::kNicknameLength );
    const uint32_t newTeamID = stream.readUInteger();
    const uint64_t newTeamSpotHash = stream.readULongLong();

    Driver *pDriver = gpPlayerData->getLoadedSave()->getDriver();
    if ( pDriver->isValid() ) {
        // FUN_00891d20
        pDriver->ID = newUserID;
        strncpy( pDriver->Nickname, nickname, Driver::kNicknameLength );
        pDriver->Nickname[Driver::kNicknameLength - 1] = '\0';
        pDriver->TeamID = newTeamID;
        pDriver->TeamSpotID = uint32_t( newTeamSpotHash );
        pDriver->field4_0x24 = uint32_t( newTeamSpotHash >> 32 );
        pDriver->setLocale( GetCountryIndex() );
        pDriver->setReputation( 0.0f );
        pDriver->notify( 0u );
    }

    userID = newUserID;
    teamID = newTeamID;
    teamSpotHash = newTeamSpotHash;

    Garage *pGarage = gpPlayerData->getLoadedSave()->getGarage();
    pGarage->refreshNumCars();
    pGarage->notify( kIdentityNetBufferMode );

    OTDU_LOG_INFO( "Logged in to the game server as '%s' (user %u)\n", nickname,
                   newUserID );
}

void GSOnline::resetForNewGame() {
    // FUN_0074ea80
    setOnlineEnabled( false );
    services.GameMaster.IP = 0u;
    services.UserDatabase.IP = 0u;
    services.UserDatabase.Port = 0u;
    services.GPSTracker.IP = 0u;
    services.clear();
    services.resetConnection();
    bServicesActive = true;
    natType = 1;
}

void GSOnline::resetForProfileChange() {
    // FUN_0087c520
    resetForNewGame();
    if ( gpPlayerData->isOnline() ) {
        gpPlayerData->setOnline( false );
        gpPlayerData->notify( 0u );
    }
    gpPlayerData->prepareActiveProfile();
}

int32_t GSOnline::notifyCheatDetected( Driver *pDriver ) {
    // FUN_00884c00
    if ( pDriver == nullptr || !pDriver->isValid() ) {
        return -1;
    }

    gpPlayerData->getLoadedSave()->getDriver()->bCheatDetected = true;

    OTDU_UNIMPLEMENTED_ONCE; // TODO: ONLINE (gpCP2PGameService)

    return -1;
}

void GSOnline::addChallengeLeaderboard( const eChallengeLeaderboard kind,
                                        const uint64_t spotHashcode,
                                        const int32_t leaderboardID ) {
    // FUN_00983200 / FUN_00983190 / FUN_00983120 (inlined)
    int32_t &count = numChallengeLeaderboards[kind];
    if ( count >= kMaxChallengeLeaderboards[kind] ) {
        return;
    }

    challengeLeaderboards[kind][count].SpotHashcode = spotHashcode;
    challengeLeaderboards[kind][count].LeaderboardID = leaderboardID;
    count++;
}
