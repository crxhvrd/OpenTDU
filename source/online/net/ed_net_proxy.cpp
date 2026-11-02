#include "shared.h"
#include "ed_net_proxy.h"

#include "core/ed_store.h"

using namespace edNetProxyProtocol;

edNetProxy::edNetProxy()
    : pSocket( nullptr ),
      pListener( nullptr ),
      proxyAddress(),
      publicAddress(),
      coreAddress(),
      result( R_Failed ),
      phase( P_Done ),
      lastSendMs( 0ull ),
      lastTickMs( 0ull ),
      bSendNow( false ),
      sendCount( 0u ),
      sessionID( 0u ),
      bEncrypt( true ),
      hello(),
      challenges(),
      responses(),
      cipher(),
      routes(),
      numRoutes( 0u ) {}

void edNetProxy::start( edNetSocket &socket,
                        const edNetAddress &address,
                        edNetProxyListener *pNewListener,
                        const uint64_t nowMs ) {
    // FUN_0062bbf0
    pSocket = &socket;
    pListener = pNewListener;
    proxyAddress = address;
    publicAddress = {};
    coreAddress = {};
    result = R_Connecting;
    phase = P_Hello;
    sendCount = 0u;
    sessionID = 0u;
    numRoutes = 0u;

    // The first HELLO goes out on the first tick.
    lastSendMs = nowMs;
    bSendNow = true;
    lastTickMs = nowMs;

    if ( pListener != nullptr ) {
        pListener->buildHello( hello );
    }
}

void edNetProxy::stop() {
    result = R_Failed;
    phase = P_Done;
    sessionID = 0u;
    numRoutes = 0u;
}

void edNetProxy::fail( const int32_t errorCode, const char *pMessage ) {
    phase = P_Done;
    result = R_Failed;
    if ( pListener != nullptr ) {
        pListener->onProxyState( R_Failed );
        pListener->onProxyError( errorCode, pMessage );
    }
}

void edNetProxy::tick( const uint64_t nowMs ) {
    // FUN_0062c240
    lastTickMs = nowMs;
    if ( result != R_Connecting || phase == P_Done ) {
        return;
    }

    if ( !bSendNow && nowMs <= lastSendMs + kHandshakeResendIntervalMs ) {
        return;
    }
    bSendNow = false;

    if ( sendCount >= kHandshakeMaxSends ) {
        fail( IE_NoAnswer, nullptr );
        return;
    }

    sendCount++;
    lastSendMs = nowMs;
    sendHandshakeFrame();
}

void edNetProxy::sendHandshakeFrame() {
    uint8_t frame[kChallengeResponseSize > kHelloSize ? kChallengeResponseSize
                                                      : kHelloSize];
    uint32_t frameSize = 0u;
    if ( phase == P_Hello ) {
        frameSize = WriteHello( hello, frame );
    } else {
        ChallengeResponse response = {};
        memcpy( response.Version, hello.Version, kVersionLength );
        memcpy( response.CDKey, hello.CDKey, kCDKeyLength );
        memcpy( response.PlayerName, hello.PlayerName, kPlayerNameLength );
        memcpy( response.Challenges, challenges, sizeof( challenges ) );
        memcpy( response.Responses, responses, sizeof( responses ) );
        frameSize = WriteChallengeResponse( response, frame );
    }
    pSocket->sendTo( proxyAddress, frame, frameSize );
}

bool edNetProxy::onDatagram( const edNetAddress &sender,
                             const uint8_t *pData,
                             const uint32_t size,
                             edNetAddress &outSource,
                             uint8_t *pOutPayload,
                             uint32_t &outPayloadLength ) {
    // FUN_0062c640
    outPayloadLength = 0u;
    if ( sender != proxyAddress ) {
        return false;
    }

    const int32_t frameType = PeekFrameType( pData, size, false );
    if ( frameType == FT_Challenge && result == R_Connecting ) {
        onChallenge( pData, size, lastTickMs );
        return true;
    }
    if ( frameType == FT_Result && result == R_Connecting ) {
        onResult( pData, size );
        return true;
    }
    if ( frameType != FT_FromRelay || result != R_Connected ) {
        return false;
    }

    // FUN_0062bbc0
    RelayHeader header;
    header.bToProxy = false;
    if ( !ReadRelayHeader( pData, size, header ) ||
         header.SessionID != sessionID || !isRouted( header.Endpoint ) ) {
        return true;
    }

    const uint32_t cipherSize =
        ( size - kRelayHeaderSize ) & ~( BlowFishCrypto::kBlockSize - 1 );
    if ( header.PayloadLength > cipherSize ) {
        return true;
    }

    if ( bEncrypt ) {
        cipher.decrypt( pData + kRelayHeaderSize, pOutPayload, cipherSize );
    } else {
        memcpy( pOutPayload, pData + kRelayHeaderSize, header.PayloadLength );
    }
    outSource = header.Endpoint;
    outPayloadLength = header.PayloadLength;
    return true;
}

void edNetProxy::onChallenge( const uint8_t *pData,
                              const uint32_t size,
                              const uint64_t nowMs ) {
    // FUN_0062bc90
    Challenge challenge;
    if ( !ReadChallenge( pData, size, challenge ) ) {
        return;
    }

    if ( challenge.RedirectAddress.isValid() &&
         challenge.RedirectAddress != proxyAddress ) {
        proxyAddress = challenge.RedirectAddress;
        bSendNow = true;
        return;
    }

    if ( challenge.Magic != kMagic ) {
        fail( IE_BadMagic, nullptr );
        return;
    }

    if ( strncmp( challenge.Version, hello.Version, kVersionLength ) != 0 ) {
        fail( IE_Rejected, challenge.Message );
        return;
    }

    publicAddress = challenge.ClientPublicAddress;
    for ( uint32_t i = 0; i < kNumChallenges; i++ ) {
        challenges[i] = challenge.Challenges[i];
        responses[i] = ComputeChallengeResponse( challenges[i] );
    }

    phase = P_Challenge;
    sendCount = 1u;
    lastSendMs = nowMs;
    sendHandshakeFrame();
}

void edNetProxy::onResult( const uint8_t *pData, const uint32_t size ) {
    // FUN_0062bee0
    Result frame;
    if ( !ReadResult( pData, size, frame ) ) {
        return;
    }

    if ( frame.ResultCode == RC_RejectedNoMessage ) {
        fail( IE_RejectedNoMessage, nullptr );
        return;
    }
    if ( frame.ResultCode != RC_OK || frame.SessionID == 0u ) {
        fail( IE_Rejected, frame.Message );
        return;
    }

    char key[8];
    DeriveSessionKey( challenges[0], responses, key );
    cipher.setKey( (const uint8_t *)key, sizeof( key ) );

    sessionID = frame.SessionID;
    bEncrypt = frame.bEncrypt;
    coreAddress = frame.CoreAddress;
    phase = P_Done;
    result = R_Connected;

    if ( coreAddress.isValid() ) {
        registerRoute( coreAddress );
    }

    if ( pListener != nullptr ) {
        pListener->onProxyState( R_Connected );
        pListener->onProxyConnected( coreAddress );
    }
}

bool edNetProxy::send( const edNetAddress &destination,
                       const edStore &message ) {
    // FUN_0062cf20
    if ( pSocket == nullptr ) {
        return false;
    }

    const uint32_t length = message.getWrittenSize();
    if ( result != R_Connected || !isRouted( destination ) ) {
        return pSocket->sendTo( destination, message.getBuffer(), length );
    }

    // FUN_0062c830
    const uint32_t cipherSize = GetRelayPayloadPaddedSize( length );
    if ( kRelayHeaderSize + cipherSize > edNetSocket::kMaxDatagramSize ) {
        OTDU_LOG_ERROR( "edNetProxy: message too large (%u bytes)\n", length );
        return false;
    }

    uint8_t frame[edNetSocket::kMaxDatagramSize];
    RelayHeader header;
    header.bToProxy = true;
    header.Endpoint = destination;
    header.SessionID = sessionID;
    header.PayloadLength = uint16_t( length );
    WriteRelayHeader( header, frame );

    uint8_t *pCipher = frame + kRelayHeaderSize;
    memcpy( pCipher, message.getBuffer(), length );
    memset( pCipher + length, 0, cipherSize - length );
    if ( bEncrypt ) {
        cipher.encrypt( pCipher, pCipher, cipherSize );
    }

    return pSocket->sendTo( proxyAddress, frame,
                            kRelayHeaderSize + cipherSize );
}

void edNetProxy::registerRoute( const edNetAddress &destination ) {
    // FUN_0062c5a0
    if ( isRouted( destination ) || numRoutes >= kMaxRoutes ) {
        return;
    }
    routes[numRoutes++] = destination;
}

bool edNetProxy::isRouted( const edNetAddress &destination ) const {
    // FUN_0062c1c0
    for ( uint32_t i = 0; i < numRoutes; i++ ) {
        if ( routes[i].IP == destination.IP ) {
            return true;
        }
    }
    return false;
}
