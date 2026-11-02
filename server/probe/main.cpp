// Server test client:
//   1. proxy handshake
//   2. service discovery
//   3. echo
//   4. N reliable calls of OpenTDU::r_debug_counter (de-duplication check)
//   5. NetBuffer download of OpenTDU::r_debug_netbuffer (content check)

#include "shared.h"

#include "rpc/debug_requests.h"

#include "core/arg_parser.h"
#include "core/ed_store.h"
#include "online/net/ed_net_action.h"
#include "online/net/ed_net_netbuffer.h"
#include "online/net/ed_net_proxy.h"
#include "online/net/ed_net_rpc.h"
#include "online/net/opentdu_rpc.h"
#include "online/net/ed_net_config.h"
#include "online/net/ed_net_resource_orb.h"

#include <chrono>

bool gBreakOnAssertFailure = false;
bool gDisableAssert = false;

static std::string gServerHost = "127.0.0.1";
static uint16_t gServerPort = edNetProxyProtocol::kDefaultPort;
static uint32_t gStepTimeoutMs = 60000;
static uint32_t gNumCalls = 50;
static uint32_t gNetBufferSize = 64 * 1024;
static bool gbDebugRPCs = false;
static std::string gIdentityNick;
static std::string gIdentityPassword = "secret";
static int32_t gExpectIdentityError = -1;
static bool gbFullLogin = false;
static int32_t gExpectNetConfigTimeout = 2500;
static float gSimulatedLoss = 0.0f;
static float gSimulatedDuplication = 0.0f;

static CmdLineArg CmdLineArgServer( "server", []( const char *pArg ) {
    gServerHost = pArg;
} );
static CmdLineArg CmdLineArgPort( "port", []( const char *pArg ) {
    gServerPort = uint16_t( atoi( pArg ) );
} );
static CmdLineArg CmdLineArgTimeout( "timeout", []( const char *pArg ) {
    gStepTimeoutMs = uint32_t( atoi( pArg ) );
} );
static CmdLineArg CmdLineArgCalls( "calls", []( const char *pArg ) {
    gNumCalls = uint32_t( atoi( pArg ) );
} );
static CmdLineArg CmdLineArgNetBufferSize( "netbuffer_size",
                                           []( const char *pArg ) {
                                               gNetBufferSize =
                                                   uint32_t( atoi( pArg ) );
                                           } );
static CmdLineArg CmdLineArgDebugRPCs( "debug_rpcs", []( const char *pArg ) {
    gbDebugRPCs = true;
} );
static CmdLineArg CmdLineArgIdentityNick( "identity_nick",
                                          []( const char *pArg ) {
                                              gIdentityNick = pArg;
                                          } );
static CmdLineArg CmdLineArgIdentityPassword( "identity_password",
                                              []( const char *pArg ) {
                                                  gIdentityPassword = pArg;
                                              } );
static CmdLineArg CmdLineArgExpectError( "expect_error",
                                         []( const char *pArg ) {
                                             gExpectIdentityError =
                                                 atoi( pArg );
                                         } );
static CmdLineArg CmdLineArgFullLogin( "full_login", []( const char *pArg ) {
    gbFullLogin = true;
} );
static CmdLineArg CmdLineArgExpectTimeout( "expect_ednet_timeout",
                                           []( const char *pArg ) {
                                               gExpectNetConfigTimeout =
                                                   atoi( pArg );
                                           } );
static CmdLineArg CmdLineArgLoss( "sim_loss", []( const char *pArg ) {
    gSimulatedLoss = float( atof( pArg ) );
} );
static CmdLineArg CmdLineArgDuplication( "sim_dup", []( const char *pArg ) {
    gSimulatedDuplication = float( atof( pArg ) );
} );

static uint64_t GetTimeMs() {
    static const auto kStart = std::chrono::steady_clock::now();
    return uint64_t( std::chrono::duration_cast<std::chrono::milliseconds>(
                         std::chrono::steady_clock::now() - kStart )
                         .count() );
}

class ProbeListener : public edNetProxyListener {
public:
    bool bConnected = false;
    bool bFailed = false;

    void buildHello( edNetProxyProtocol::Hello &outHello ) override {
        memset( &outHello, 0, sizeof( outHello ) );
        strncpy( outHello.Version, "OpenTDU netprobe",
                 sizeof( outHello.Version ) - 1 );
        strncpy( outHello.CDKey, "PROBE", sizeof( outHello.CDKey ) - 1 );
        strncpy( outHello.PlayerName, "netprobe",
                 sizeof( outHello.PlayerName ) - 1 );
    }

    void onProxyConnected( const edNetAddress &coreAddress ) override {
        bConnected = true;
    }

    void onProxyError( const int32_t errorCode,
                       const char *pMessage ) override {
        OTDU_LOG_ALWAYS( "Handshake error %d (%s)\n", errorCode,
                         pMessage != nullptr ? pMessage : "no message" );
        bFailed = true;
    }
};

class Probe {
public:
    edNetSocket Socket;
    ProbeListener Listener;
    edNetProxy Proxy;
    edNetActionClient Actions;
    edNetNetBufferClient NetBuffers;
    edNetResourceORB ResourceORB;
    uint32_t NumEchoReplies = 0u;
    uint32_t ProfileID = 0u;

    Probe() : Actions( Proxy ), NetBuffers( Proxy ), ResourceORB( Actions ) {}

    void pumpOnce() {
        uint8_t datagram[edNetSocket::kMaxDatagramSize];
        uint8_t payload[edNetSocket::kMaxDatagramSize];
        edNetAddress sender;
        int32_t size = 0;
        while ( ( size = Socket.receiveFrom( sender, datagram,
                                             sizeof( datagram ) ) ) > 0 ) {
            edNetAddress source;
            uint32_t payloadLength = 0u;
            if ( !Proxy.onDatagram( sender, datagram, uint32_t( size ), source,
                                    payload, payloadLength ) ||
                 payloadLength == 0u ) {
                continue;
            }

            edStore message;
            message.bindExternalBuffer( payload, payloadLength );
            if ( Actions.onMessage( source, message ) ||
                 NetBuffers.onMessage( source, message ) ) {
                continue;
            }

            if ( message.getStreamCRC() == edNetRPC::AEcho() ) {
                NumEchoReplies++;
            } else {
                OTDU_LOG_ALWAYS( "Unexpected message %04X from %s\n",
                                 message.getStreamCRC(),
                                 source.toString().c_str() );
            }
        }

        const uint64_t now = GetTimeMs();
        Proxy.tick( now );
        Actions.tick( now );
        NetBuffers.tick( now );
        ResourceORB.tick( now );
    }

    // Pumps until bDone becomes true, or fails after gStepTimeoutMs.
    template <typename Predicate_t>
    bool pumpUntil( const char *pStepName, Predicate_t bDone ) {
        const uint64_t start = GetTimeMs();
        while ( !bDone() ) {
            if ( Listener.bFailed || GetTimeMs() > start + gStepTimeoutMs ) {
                OTDU_LOG_ALWAYS( "[FAIL] %s\n", pStepName );
                return false;
            }
            pumpOnce();
            std::this_thread::sleep_for( std::chrono::milliseconds( 1 ) );
        }
        OTDU_LOG_ALWAYS( "[OK] %s (%llu ms)\n", pStepName,
                         GetTimeMs() - start );
        return true;
    }

    bool runDiscovery() {
        edStore request;
        request.updateCRC( edNetRPC::RGetRequestHandlersEx() );
        request.writeInteger( 0 );
        request.writeULongLong( Proxy.getSessionID() );
        for ( uint32_t i = 0; i < edNetRPC::kMaxRequestHandlers; i++ ) {
            const uint16_t requestedID =
                ( i < edNetRPC::kNumRequestHandlerSignatures )
                    ? edNetRPC::MessageID(
                          edNetRPC::kRequestHandlerSignatures[i] )
                    : 0u;
            request.writeUShort( requestedID );
        }

        bool bDone = false;
        bool bSuccess = false;
        uint32_t numHandlers = 0u;
        // Retail retry policy for discovery after the first attempt
        // (1500 ms x 7).
        Actions.callAction(
            Proxy.getCoreAddress(), request,
            [&]( const edNetActionClient::Reply &reply ) {
                bDone = true;
                if ( reply.Result != edNetActionClient::CR_Success ||
                     reply.pReply == nullptr ||
                     reply.pReply->getStreamCRC() !=
                         edNetRPC::AGetRequestHandlersEx() ) {
                    return;
                }

                while ( reply.pReply->getReadCursor() <
                        reply.pReply->getWrittenSize() ) {
                    reply.pReply->readUShort();
                    edNetAddress handler;
                    handler.IP = reply.pReply->readUInteger();
                    handler.Port = reply.pReply->readUShort();
                    if ( handler.isValid() ) {
                        Proxy.registerRoute( handler );
                    }
                    numHandlers++;
                }
                bSuccess =
                    numHandlers == edNetRPC::kNumRequestHandlerSignatures;
            },
            GetTimeMs(), 1500, 7 );

        return pumpUntil( "Service discovery", [&]() { return bDone; } ) &&
               bSuccess;
    }

    bool runEcho() {
        // Echo is unreliable: resend every second until one answer arrives.
        uint64_t lastSendMs = 0ull;
        uint32_t numSent = 0u;
        return pumpUntil( "Echo", [&]() {
            if ( NumEchoReplies > 0u ) {
                return true;
            }
            const uint64_t now = GetTimeMs();
            if ( numSent == 0u || now > lastSendMs + 1000 ) {
                edStore echo;
                echo.updateCRC( edNetRPC::REcho() );
                Proxy.send( Proxy.getCoreAddress(), echo );
                lastSendMs = now;
                numSent++;
            }
            return false;
        } );
    }

    // OpenTDU::Identity::r_login (mode 0). Prints the error / profile id;
    // fails if -expect_error is set and does not match.
    bool runIdentity() {
        edStore request;
        request.updateCRC( edNetRPC::MessageID( OpenTDURPC::kIdentityLogin ) );
        request.writeString( gIdentityNick.c_str() );
        request.writeString( "probe@opentdu" );
        request.writeString( gIdentityPassword.c_str() );
        request.writeString( "PROBE" );
        request.writeByte( 0 );

        bool bDone = false;
        int32_t error = -1;
        uint32_t profileID = 0u;
        Actions.callAction(
            Proxy.getCoreAddress(), request,
            [&]( const edNetActionClient::Reply &reply ) {
                bDone = true;
                if ( reply.Result == edNetActionClient::CR_Success &&
                     reply.pReply != nullptr &&
                     reply.pReply->getStreamCRC() ==
                         edNetRPC::MessageID(
                             OpenTDURPC::kIdentityLoginReply ) ) {
                    error = reply.pReply->readByte();
                    profileID = reply.pReply->readUInteger();
                    ProfileID = profileID;
                }
            },
            GetTimeMs(), 1500, 7 );

        if ( !pumpUntil( "Identity login", [&]() { return bDone; } ) ) {
            return false;
        }
        OTDU_LOG_ALWAYS( "Identity '%s': error %d, profile %u\n",
                         gIdentityNick.c_str(), error, profileID );
        const int32_t expected =
            ( gExpectIdentityError >= 0 ) ? gExpectIdentityError : 0;
        return error == expected;
    }

    // GSOnline::sendLogin -> a_login + identity NetBuffer, then the
    // netconfig.xml download (OnlineServices::requestNetConfig).
    bool runFullLogin() {
        edStore request;
        request.updateCRC( edNetRPC::RLoginPC() );
        request.writeString( gIdentityNick.c_str() );
        request.writeString( "edenpwd" );
        request.writeInteger( 0 );
        request.writeULongLong( uint64_t( ProfileID ) );
        request.writeByte( 0 );
        request.writeString( "PROBE" );

        std::vector<uint8_t> identity;
        bool bDone = false;
        bool bLoggedIn = false;
        uint32_t serverTime = 0u;
        Actions.callAction(
            Proxy.getCoreAddress(), request,
            [&]( const edNetActionClient::Reply &reply ) {
                if ( reply.Result != edNetActionClient::CR_Success ||
                     reply.pReply == nullptr ||
                     reply.pReply->getStreamCRC() != edNetRPC::ALogin() ||
                     reply.pReply->readByte() != 1 ) {
                    bDone = true;
                    return;
                }
                serverTime = reply.pReply->readUInteger();
                if ( reply.pReply->readByte() == 0 ) {
                    bDone = true;
                    return;
                }
                identity.resize( reply.BufferSize );
                NetBuffers.getNetBuffer(
                    Proxy.getCoreAddress(), reply.BufferID, identity.data(),
                    reply.BufferSize,
                    [&]( const bool bOk ) {
                        bLoggedIn = bOk;
                        bDone = true;
                    },
                    GetTimeMs() );
            },
            GetTimeMs(), 1500, 4 );
        if ( !pumpUntil( "r_login_pc + identity record",
                         [&]() { return bDone; } ) ||
             !bLoggedIn ) {
            return false;
        }

        edStore record;
        record.bindExternalBuffer( identity.data(),
                                   uint32_t( identity.size() ) );
        record.seek( 0 );
        const uint32_t userID = record.readUInteger();
        char nick[33] = {};
        record.readStringNoAlloc( nick, 32 );
        const uint32_t teamID = record.readUInteger();
        const uint64_t teamSpot = record.readULongLong();
        OTDU_LOG_ALWAYS( "Identity record: user %u, nick '%s', team %u, "
                         "spot %llu, server time %u\n",
                         userID, nick, teamID, (unsigned long long)teamSpot,
                         serverTime );
        if ( userID != ProfileID || gIdentityNick.size() != strlen( nick ) ) {
            return false;
        }

        uint8_t xml[0x1000] = {};
        bool bFileDone = false;
        bool bFileOk = false;
        ResourceORB.setFacility( 2 );
        ResourceORB.setUserID( 0x494707d4 );
        ResourceORB.setFileName( "netconfig.xml" );
        ResourceORB.setPeer( Proxy.getCoreAddress() );
        ResourceORB.setDestination( xml, sizeof( xml ) - 1 );
        ResourceORB.startDownload(
            [&]( const bool bOk ) {
                bFileOk = bOk;
                bFileDone = true;
            },
            GetTimeMs() );
        if ( !pumpUntil( "netconfig.xml download",
                         [&]() { return bFileDone; } ) ||
             !bFileOk ) {
            return false;
        }

        edNetConfig config;
        const bool bParsed = config.parse( (const char *)xml );
        const int32_t timeout = config.getInt( "EDNET_DEFAULT_TIMEOUT" );
        OTDU_LOG_ALWAYS( "netconfig.xml: %u bytes, parsed %d, "
                         "EDNET_DEFAULT_TIMEOUT %d, GR_DELAY_CARDEALER %d, "
                         "CORRECTION_DT %.3f\n",
                         ResourceORB.getReceivedSize(), bParsed, timeout,
                         config.getInt( "GR_DELAY_CARDEALER" ),
                         config.getFloat( "CORRECTION_DT" ) );
        return bParsed && timeout == gExpectNetConfigTimeout;
    }

    bool runCounterCalls() {
        const uint16_t replyID =
            edNetRPC::MessageID( DebugRequests::kCounterReply );

        uint32_t expected = 1u;
        bool bFailed = false;
        for ( uint32_t i = 0; i < gNumCalls && !bFailed; i++ ) {
            edStore request;
            request.updateCRC(
                edNetRPC::MessageID( DebugRequests::kCounterRequest ) );

            bool bDone = false;
            Actions.callAction(
                Proxy.getCoreAddress(), request,
                [&]( const edNetActionClient::Reply &reply ) {
                    bDone = true;
                    if ( reply.Result != edNetActionClient::CR_Success ||
                         reply.pReply == nullptr ||
                         reply.pReply->getStreamCRC() != replyID ) {
                        OTDU_LOG_ALWAYS( "Call %u failed (result %d)\n", i,
                                         reply.Result );
                        bFailed = true;
                        return;
                    }

                    const uint32_t value = reply.pReply->readUInteger();
                    if ( value != expected ) {
                        OTDU_LOG_ALWAYS( "Call %u: counter %u, expected %u "
                                         "(RPC executed more than once)\n",
                                         i, value, expected );
                        bFailed = true;
                    }
                    expected++;
                },
                GetTimeMs(), 1000, 10 );

            const uint64_t start = GetTimeMs();
            while ( !bDone && GetTimeMs() < start + gStepTimeoutMs ) {
                pumpOnce();
                std::this_thread::sleep_for( std::chrono::milliseconds( 1 ) );
            }
            bFailed |= !bDone;
        }

        OTDU_LOG_ALWAYS( "[%s] %u reliable calls, each executed once\n",
                         bFailed ? "FAIL" : "OK", gNumCalls );
        return !bFailed;
    }

    bool runNetBuffer() {
        const uint32_t seed = uint32_t( GetTimeMs() );
        edStore request;
        request.updateCRC(
            edNetRPC::MessageID( DebugRequests::kNetBufferRequest ) );
        request.writeInteger( int32_t( gNetBufferSize ) );
        request.writeInteger( int32_t( seed ) );

        std::vector<uint8_t> data;
        bool bDone = false;
        bool bSuccess = false;
        Actions.callAction(
            Proxy.getCoreAddress(), request,
            [&]( const edNetActionClient::Reply &reply ) {
                if ( reply.Result != edNetActionClient::CR_Success ||
                     reply.BufferSize != gNetBufferSize ) {
                    OTDU_LOG_ALWAYS( "NetBuffer call failed (result %d, "
                                     "size %u)\n",
                                     reply.Result, reply.BufferSize );
                    bDone = true;
                    return;
                }

                data.resize( reply.BufferSize );
                NetBuffers.getNetBuffer(
                    Proxy.getCoreAddress(), reply.BufferID, data.data(),
                    reply.BufferSize,
                    [&]( const bool bDownloaded ) {
                        bDone = true;
                        bSuccess = bDownloaded;
                    },
                    GetTimeMs() );
            },
            GetTimeMs(), 1000, 10 );

        if ( !pumpUntil( "NetBuffer download", [&]() { return bDone; } ) ||
             !bSuccess ) {
            return false;
        }

        for ( uint32_t i = 0; i < gNetBufferSize; i++ ) {
            if ( data[i] != DebugRequests::GetNetBufferByte( seed, i ) ) {
                OTDU_LOG_ALWAYS( "[FAIL] NetBuffer content mismatch at "
                                 "byte %u\n",
                                 i );
                return false;
            }
        }
        OTDU_LOG_ALWAYS( "[OK] NetBuffer content (%u bytes)\n",
                         gNetBufferSize );
        return true;
    }
};

int main( int argc, char *argv[] ) {
    GetArgParserRegister().parseCmdLineArgs( (const char **)argv, argc );

    edNetAddress serverAddress;
    if ( !edNetAddress::Resolve( gServerHost.c_str(), gServerPort,
                                 serverAddress ) ) {
        return 1;
    }

    Probe probe;
    if ( !probe.Socket.open( 0 ) ) {
        return 1;
    }
    probe.Socket.setSimulatedConditions( gSimulatedLoss,
                                         gSimulatedDuplication );

    OTDU_LOG_ALWAYS( "Connecting to %s (loss %.0f%%, duplication %.0f%%)\n",
                     serverAddress.toString().c_str(), gSimulatedLoss * 100.0f,
                     gSimulatedDuplication * 100.0f );
    probe.Proxy.start( probe.Socket, serverAddress, &probe.Listener,
                       GetTimeMs() );

    bool bSuccess =
        probe.pumpUntil( "Handshake",
                         [&]() { return probe.Listener.bConnected; } ) &&
        probe.runDiscovery() && probe.runEcho();
    if ( bSuccess && !gIdentityNick.empty() ) {
        bSuccess = probe.runIdentity();
    }
    if ( bSuccess && gbFullLogin ) {
        bSuccess = probe.runFullLogin();
    }
    if ( bSuccess && gbDebugRPCs ) {
        bSuccess = probe.runCounterCalls() && probe.runNetBuffer();
    }

    OTDU_LOG_ALWAYS( "%s\n", bSuccess ? "PASS" : "FAIL" );
    return bSuccess ? 0 : 1;
}
