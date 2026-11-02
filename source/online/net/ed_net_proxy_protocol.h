#pragma once

#include "ed_net_socket.h"

namespace edNetProxyProtocol {
    static constexpr uint32_t kMagic = 0x2A7B;
    static constexpr uint16_t kDefaultPort = 8889;

    static constexpr uint32_t kVersionLength = 65;
    static constexpr uint32_t kCDKeyLength = 20;
    static constexpr uint32_t kPlayerNameLength = 22;
    static constexpr uint32_t kMessageLength = 512;
    static constexpr uint32_t kNumChallenges = 3;

    static constexpr uint32_t kHelloSize = 116;
    static constexpr uint32_t kChallengeSize = 616;
    static constexpr uint32_t kChallengeResponseSize = 144;
    static constexpr uint32_t kResultSize = 528;
    static constexpr uint32_t kRelayHeaderSize = 20;

    static constexpr uint32_t kHandshakeResendIntervalMs = 5000;
    static constexpr uint32_t kHandshakeMaxSends = 5;

    enum eFrameType : uint8_t {
        FT_Hello = 0,
        FT_Challenge = 1,
        FT_ChallengeResponse = 2,
        FT_Result = 3,
        FT_ToRelay = 4,
        FT_FromRelay = 5
    };

    enum eResultCode : uint8_t {
        RC_OK = 0,
        RC_RejectedWithMessage = 3,
        RC_RejectedNoMessage = 4
    };

    enum eInitError : int32_t {
        IE_NoAnswer = 1,
        IE_BadMagic = 2,
        IE_Rejected = 3,
        IE_RejectedNoMessage = 4
    };

    uint16_t GetToProxyHeader();
    uint16_t GetFromProxyHeader();

    struct Hello {
        char Version[kVersionLength];
        char CDKey[kCDKeyLength];
        char PlayerName[kPlayerNameLength];
    };

    struct Challenge {
        edNetAddress ClientPublicAddress;
        edNetAddress RedirectAddress;
        uint32_t Magic = kMagic;
        char Version[kVersionLength];
        uint32_t Challenges[kNumChallenges];
        char Message[kMessageLength];
    };

    struct ChallengeResponse {
        char Version[kVersionLength];
        uint32_t Challenges[kNumChallenges];
        uint32_t Responses[kNumChallenges];
        char CDKey[kCDKeyLength];
        char PlayerName[kPlayerNameLength];
    };

    struct Result {
        uint32_t SessionID = 0u;
        edNetAddress CoreAddress;
        bool bEncrypt = true;
        eResultCode ResultCode = RC_OK;
        char Message[kMessageLength];
    };

    struct RelayHeader {
        bool bToProxy = true;
        // Destination handler (type 4) or source handler (type 5).
        edNetAddress Endpoint;
        uint32_t SessionID = 0u;
        uint16_t PayloadLength = 0u;
    };

    uint32_t WriteHello( const Hello &frame, uint8_t *pOut );
    bool ReadHello( const uint8_t *pIn, const uint32_t size, Hello &outFrame );

    uint32_t WriteChallenge( const Challenge &frame, uint8_t *pOut );
    bool ReadChallenge( const uint8_t *pIn,
                        const uint32_t size,
                        Challenge &outFrame );

    uint32_t WriteChallengeResponse( const ChallengeResponse &frame,
                                     uint8_t *pOut );
    bool ReadChallengeResponse( const uint8_t *pIn,
                                const uint32_t size,
                                ChallengeResponse &outFrame );

    uint32_t WriteResult( const Result &frame, uint8_t *pOut );
    bool
    ReadResult( const uint8_t *pIn, const uint32_t size, Result &outFrame );

    uint32_t WriteRelayHeader( const RelayHeader &header, uint8_t *pOut );
    bool ReadRelayHeader( const uint8_t *pIn,
                          const uint32_t size,
                          RelayHeader &outHeader );

    int32_t PeekFrameType( const uint8_t *pIn,
                           const uint32_t size,
                           const bool bToProxy );

    inline uint32_t GetRelayPayloadPaddedSize( const uint32_t length ) {
        return ( length == 0 ) ? 0 : ( length & ~7u ) + 8;
    }

    void DeriveSessionKey( const uint32_t challenge0,
                           const uint32_t responses[kNumChallenges],
                           char outKey[8] );

    uint32_t ComputeChallengeResponse( const uint32_t challenge );
} // namespace edNetProxyProtocol
