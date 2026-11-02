#include "shared.h"
#include "proxy_service.h"

#include "action_service.h"
#include "tdu_server.h"

#include "core/ed_store.h"
#include "online/net/ed_net_proxy_protocol.h"

using namespace edNetProxyProtocol;

ProxyService::ProxyService( Server &server )
    : server( server ),
      pActionService( std::make_unique<ActionService>( server, *this ) ),
      sessions(),
      random( std::random_device()() ),
      nextSessionID( 0u ) {
    nextSessionID = ( random() & 0x7fffffffu ) | 1u;
}

ProxyService::~ProxyService() {}

uint64_t ProxyService::GetSessionKey( const edNetAddress &address ) {
    return ( uint64_t( address.IP ) << 16 ) | address.Port;
}

ClientSession *ProxyService::findSession( const edNetAddress &address ) {
    auto it = sessions.find( GetSessionKey( address ) );
    return ( it != sessions.end() ) ? it->second.get() : nullptr;
}

void ProxyService::onDatagram( const edNetAddress &sender,
                               const uint8_t *pData,
                               const uint32_t size ) {
    const int32_t frameType = PeekFrameType( pData, size, true );
    switch ( frameType ) {
    case FT_Hello:
        onHello( sender, pData, size );
        break;
    case FT_ChallengeResponse:
        onChallengeResponse( sender, pData, size );
        break;
    case FT_ToRelay:
        onRelay( sender, pData, size );
        break;
    default:
        if ( server.getConfig().bVerbose ) {
            OTDU_LOG_DEBUG( "Dropped %u byte datagram from %s (type %d)\n",
                            size, sender.toString().c_str(), frameType );
        }
        break;
    }
}

void ProxyService::tick() {
    const uint64_t now = server.getTimeMs();
    for ( auto it = sessions.begin(); it != sessions.end(); ) {
        if ( now - it->second->LastSeenMs > kSessionTimeoutMs ) {
            OTDU_LOG_INFO( "Session %08X (%s) timed out\n",
                           it->second->SessionID,
                           it->second->Address.toString().c_str() );
            pActionService->onSessionClosed( *it->second );
            it = sessions.erase( it );
        } else {
            ++it;
        }
    }

    pActionService->tick();
}

void ProxyService::onHello( const edNetAddress &sender,
                            const uint8_t *pData,
                            const uint32_t size ) {
    Hello hello;
    if ( !ReadHello( pData, size, hello ) ) {
        OTDU_LOG_WARN( "Malformed HELLO from %s\n", sender.toString().c_str() );
        return;
    }

    ClientSession *pSession = findSession( sender );
    if ( pSession != nullptr && pSession->bConnected ) {
        pActionService->onSessionClosed( *pSession );
        sessions.erase( GetSessionKey( sender ) );
        pSession = nullptr;
    }

    if ( pSession == nullptr ) {
        std::unique_ptr<ClientSession> pNewSession =
            std::make_unique<ClientSession>();
        pNewSession->Address = sender;
        for ( uint32_t &challenge : pNewSession->Challenges ) {
            challenge = random();
        }
        pSession = pNewSession.get();
        sessions[GetSessionKey( sender )] = std::move( pNewSession );
    }

    pSession->LastSeenMs = server.getTimeMs();
    pSession->Version = hello.Version;
    pSession->PlayerName = hello.PlayerName;

    OTDU_LOG_INFO( "HELLO from %s (player '%s', version '%s')\n",
                   sender.toString().c_str(), hello.PlayerName, hello.Version );

    Challenge challenge = {};
    challenge.ClientPublicAddress = sender;
    challenge.Magic = kMagic;
    memcpy( challenge.Version, hello.Version, kVersionLength );
    memcpy( challenge.Challenges, pSession->Challenges,
            sizeof( challenge.Challenges ) );

    uint8_t frame[kChallengeSize];
    const uint32_t frameSize = WriteChallenge( challenge, frame );
    server.getSocket().sendTo( sender, frame, frameSize );
}

void ProxyService::onChallengeResponse( const edNetAddress &sender,
                                        const uint8_t *pData,
                                        const uint32_t size ) {
    ChallengeResponse response;
    if ( !ReadChallengeResponse( pData, size, response ) ) {
        OTDU_LOG_WARN( "Malformed CHALLENGE RESPONSE from %s\n",
                       sender.toString().c_str() );
        return;
    }

    ClientSession *pSession = findSession( sender );
    if ( pSession == nullptr ) {
        OTDU_LOG_WARN( "CHALLENGE RESPONSE from unknown client %s\n",
                       sender.toString().c_str() );
        return;
    }
    pSession->LastSeenMs = server.getTimeMs();

    Result result = {};
    result.bEncrypt = true;

    bool bValid = true;
    for ( uint32_t i = 0; i < kNumChallenges; i++ ) {
        bValid &= response.Challenges[i] == pSession->Challenges[i] &&
                  response.Responses[i] ==
                      ComputeChallengeResponse( pSession->Challenges[i] );
    }

    if ( !bValid ) {
        OTDU_LOG_WARN( "Rejected %s: invalid challenge response\n",
                       sender.toString().c_str() );
        result.ResultCode = RC_RejectedWithMessage;
        strncpy( result.Message, "This server only accepts OpenTDU clients.",
                 kMessageLength - 1 );
    } else {
        // The response is resent until the RESULT arrives; reuse the session
        // id so every copy of the RESULT agrees.
        if ( pSession->SessionID == 0u ) {
            pSession->SessionID = nextSessionID++;
            if ( nextSessionID == 0u ) {
                nextSessionID = 1u;
            }
        }

        char key[8];
        DeriveSessionKey( response.Challenges[0], response.Responses, key );
        pSession->Cipher.setKey( (const uint8_t *)key, sizeof( key ) );
        pSession->bConnected = true;

        result.SessionID = pSession->SessionID;
        result.CoreAddress = server.getPublicAddress();
        result.ResultCode = RC_OK;

        OTDU_LOG_INFO( "Session %08X opened for %s ('%s')\n",
                       pSession->SessionID, sender.toString().c_str(),
                       pSession->PlayerName.c_str() );
    }

    uint8_t frame[kResultSize];
    const uint32_t frameSize = WriteResult( result, frame );
    server.getSocket().sendTo( sender, frame, frameSize );

    if ( !bValid ) {
        sessions.erase( GetSessionKey( sender ) );
    }
}

void ProxyService::onRelay( const edNetAddress &sender,
                            const uint8_t *pData,
                            const uint32_t size ) {
    RelayHeader header;
    header.bToProxy = true;
    if ( !ReadRelayHeader( pData, size, header ) ) {
        return;
    }

    ClientSession *pSession = findSession( sender );
    if ( pSession == nullptr || !pSession->bConnected ||
         pSession->SessionID != header.SessionID ) {
        if ( server.getConfig().bVerbose ) {
            OTDU_LOG_DEBUG( "Dropped relay frame from %s (unknown session)\n",
                            sender.toString().c_str() );
        }
        return;
    }

    const uint32_t cipherSize =
        GetRelayPayloadPaddedSize( header.PayloadLength );
    if ( header.PayloadLength < 2 || cipherSize > size - kRelayHeaderSize ) {
        OTDU_LOG_WARN( "Truncated relay frame from %s (%u/%u bytes)\n",
                       sender.toString().c_str(), size - kRelayHeaderSize,
                       cipherSize );
        return;
    }

    pSession->LastSeenMs = server.getTimeMs();

    uint8_t payload[edNetSocket::kMaxDatagramSize];
    pSession->Cipher.decrypt( pData + kRelayHeaderSize, payload, cipherSize );

    edStore message;
    message.bindExternalBuffer( payload, header.PayloadLength );
    pActionService->onMessage( *pSession, header.Endpoint, message );
}

bool ProxyService::sendToClient( ClientSession &session,
                                 const edNetAddress &sourceEndpoint,
                                 const edStore &message ) {
    return sendToClient( session, sourceEndpoint, message.getBuffer(),
                         message.getWrittenSize() );
}

bool ProxyService::sendToClient( ClientSession &session,
                                 const edNetAddress &sourceEndpoint,
                                 const uint8_t *pPayload,
                                 const uint32_t length ) {
    const uint32_t cipherSize = GetRelayPayloadPaddedSize( length );
    if ( length > 0xffff ||
         kRelayHeaderSize + cipherSize > edNetSocket::kMaxDatagramSize ) {
        OTDU_LOG_ERROR( "Relay payload too large (%u bytes)\n", length );
        return false;
    }

    uint8_t frame[edNetSocket::kMaxDatagramSize];
    RelayHeader header;
    header.bToProxy = false;
    header.Endpoint = sourceEndpoint;
    header.SessionID = session.SessionID;
    header.PayloadLength = uint16_t( length );
    WriteRelayHeader( header, frame );

    uint8_t *pCipher = frame + kRelayHeaderSize;
    memcpy( pCipher, pPayload, length );
    memset( pCipher + length, 0, cipherSize - length );
    session.Cipher.encrypt( pCipher, pCipher, cipherSize );

    return server.getSocket().sendTo( session.Address, frame,
                                      kRelayHeaderSize + cipherSize );
}
