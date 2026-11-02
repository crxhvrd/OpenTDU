#include "shared.h"
#include "ed_net_proxy_protocol.h"

#include "core/hash/crc32.h"

namespace edNetProxyProtocol {
    static void WriteU16( uint8_t *pOut, const uint16_t value ) {
        pOut[0] = uint8_t( value >> 8 );
        pOut[1] = uint8_t( value );
    }

    static void WriteU32( uint8_t *pOut, const uint32_t value ) {
        pOut[0] = uint8_t( value >> 24 );
        pOut[1] = uint8_t( value >> 16 );
        pOut[2] = uint8_t( value >> 8 );
        pOut[3] = uint8_t( value );
    }

    static uint16_t ReadU16( const uint8_t *pIn ) {
        return uint16_t( ( uint16_t( pIn[0] ) << 8 ) | pIn[1] );
    }

    static uint32_t ReadU32( const uint8_t *pIn ) {
        return ( uint32_t( pIn[0] ) << 24 ) | ( uint32_t( pIn[1] ) << 16 ) |
               ( uint32_t( pIn[2] ) << 8 ) | uint32_t( pIn[3] );
    }

    static void WriteFixedString( uint8_t *pOut,
                                  const char *pValue,
                                  const uint32_t fieldLength ) {
        memset( pOut, 0, fieldLength );
        strncpy( (char *)pOut, pValue, fieldLength - 1 );
    }

    static void ReadFixedString( const uint8_t *pIn,
                                 char *pOut,
                                 const uint32_t fieldLength ) {
        memcpy( pOut, pIn, fieldLength );
        pOut[fieldLength - 1] = '\0';
    }

    static uint32_t WriteFrameHeader( uint8_t *pOut,
                                      const bool bToProxy,
                                      const eFrameType type ) {
        WriteU16( pOut, bToProxy ? GetToProxyHeader() : GetFromProxyHeader() );
        pOut[2] = type;
        pOut[3] = 0;
        return 4;
    }

    static bool CheckFrame( const uint8_t *pIn,
                            const uint32_t size,
                            const uint32_t expectedSize,
                            const bool bToProxy,
                            const eFrameType type ) {
        return size >= expectedSize &&
               PeekFrameType( pIn, size, bToProxy ) == int32_t( type );
    }

    uint16_t GetToProxyHeader() {
        static const uint16_t kHeader =
            uint16_t( TestDriveCRC32( "TO_PROXY_HEADER" ) );
        return kHeader;
    }

    uint16_t GetFromProxyHeader() {
        static const uint16_t kHeader =
            uint16_t( TestDriveCRC32( "FROM_PROXY_HEADER" ) );
        return kHeader;
    }

    int32_t PeekFrameType( const uint8_t *pIn,
                           const uint32_t size,
                           const bool bToProxy ) {
        // FUN_0062c640
        if ( size < 4 ) {
            return -1;
        }

        const uint16_t expected =
            bToProxy ? GetToProxyHeader() : GetFromProxyHeader();
        if ( ReadU16( pIn ) != expected ) {
            return -1;
        }
        return pIn[2];
    }

    uint32_t WriteHello( const Hello &frame, uint8_t *pOut ) {
        // FUN_0062c240
        memset( pOut, 0, kHelloSize );
        WriteFrameHeader( pOut, true, FT_Hello );
        WriteU32( pOut + 0x4, kMagic );
        WriteFixedString( pOut + 0x8, frame.Version, kVersionLength );
        WriteFixedString( pOut + 0x49, frame.CDKey, kCDKeyLength );
        WriteFixedString( pOut + 0x5d, frame.PlayerName, kPlayerNameLength );
        return kHelloSize;
    }

    bool ReadHello( const uint8_t *pIn, const uint32_t size, Hello &outFrame ) {
        if ( !CheckFrame( pIn, size, kHelloSize, true, FT_Hello ) ||
             ReadU32( pIn + 0x4 ) != kMagic ) {
            return false;
        }

        ReadFixedString( pIn + 0x8, outFrame.Version, kVersionLength );
        ReadFixedString( pIn + 0x49, outFrame.CDKey, kCDKeyLength );
        ReadFixedString( pIn + 0x5d, outFrame.PlayerName, kPlayerNameLength );
        return true;
    }

    uint32_t WriteChallenge( const Challenge &frame, uint8_t *pOut ) {
        memset( pOut, 0, kChallengeSize );
        WriteFrameHeader( pOut, false, FT_Challenge );
        WriteU32( pOut + 0x4, frame.ClientPublicAddress.IP );
        WriteU16( pOut + 0x8, frame.ClientPublicAddress.Port );
        WriteU32( pOut + 0xc, frame.RedirectAddress.IP );
        WriteU16( pOut + 0x10, frame.RedirectAddress.Port );
        WriteU32( pOut + 0x14, frame.Magic );
        WriteFixedString( pOut + 0x18, frame.Version, kVersionLength );
        for ( uint32_t i = 0; i < kNumChallenges; i++ ) {
            WriteU32( pOut + 0x5c + i * 4, frame.Challenges[i] );
        }
        WriteFixedString( pOut + 0x68, frame.Message, kMessageLength );
        return kChallengeSize;
    }

    bool ReadChallenge( const uint8_t *pIn,
                        const uint32_t size,
                        Challenge &outFrame ) {
        // FUN_0062bc90
        if ( !CheckFrame( pIn, size, kChallengeSize, false, FT_Challenge ) ) {
            return false;
        }

        outFrame.ClientPublicAddress.IP = ReadU32( pIn + 0x4 );
        outFrame.ClientPublicAddress.Port = ReadU16( pIn + 0x8 );
        outFrame.RedirectAddress.IP = ReadU32( pIn + 0xc );
        outFrame.RedirectAddress.Port = ReadU16( pIn + 0x10 );
        outFrame.Magic = ReadU32( pIn + 0x14 );
        ReadFixedString( pIn + 0x18, outFrame.Version, kVersionLength );
        for ( uint32_t i = 0; i < kNumChallenges; i++ ) {
            outFrame.Challenges[i] = ReadU32( pIn + 0x5c + i * 4 );
        }
        ReadFixedString( pIn + 0x68, outFrame.Message, kMessageLength );
        return true;
    }

    uint32_t WriteChallengeResponse( const ChallengeResponse &frame,
                                     uint8_t *pOut ) {
        memset( pOut, 0, kChallengeResponseSize );
        WriteFrameHeader( pOut, true, FT_ChallengeResponse );
        WriteU32( pOut + 0x4, kMagic );
        WriteFixedString( pOut + 0x8, frame.Version, kVersionLength );
        for ( uint32_t i = 0; i < kNumChallenges; i++ ) {
            WriteU32( pOut + 0x4c + i * 4, frame.Challenges[i] );
            WriteU32( pOut + 0x58 + i * 4, frame.Responses[i] );
        }
        WriteFixedString( pOut + 0x64, frame.CDKey, kCDKeyLength );
        WriteFixedString( pOut + 0x78, frame.PlayerName, kPlayerNameLength );
        return kChallengeResponseSize;
    }

    bool ReadChallengeResponse( const uint8_t *pIn,
                                const uint32_t size,
                                ChallengeResponse &outFrame ) {
        if ( !CheckFrame( pIn, size, kChallengeResponseSize, true,
                          FT_ChallengeResponse ) ||
             ReadU32( pIn + 0x4 ) != kMagic ) {
            return false;
        }

        ReadFixedString( pIn + 0x8, outFrame.Version, kVersionLength );
        for ( uint32_t i = 0; i < kNumChallenges; i++ ) {
            outFrame.Challenges[i] = ReadU32( pIn + 0x4c + i * 4 );
            outFrame.Responses[i] = ReadU32( pIn + 0x58 + i * 4 );
        }
        ReadFixedString( pIn + 0x64, outFrame.CDKey, kCDKeyLength );
        ReadFixedString( pIn + 0x78, outFrame.PlayerName, kPlayerNameLength );
        return true;
    }

    uint32_t WriteResult( const Result &frame, uint8_t *pOut ) {
        memset( pOut, 0, kResultSize );
        WriteFrameHeader( pOut, false, FT_Result );
        WriteU32( pOut + 0x4, frame.SessionID );
        WriteU32( pOut + 0x8, frame.CoreAddress.IP );
        WriteU16( pOut + 0xc, frame.CoreAddress.Port );
        pOut[0xe] = frame.bEncrypt ? 1 : 0;
        pOut[0xf] = frame.ResultCode;
        WriteFixedString( pOut + 0x10, frame.Message, kMessageLength );
        return kResultSize;
    }

    bool
    ReadResult( const uint8_t *pIn, const uint32_t size, Result &outFrame ) {
        // FUN_0062bee0
        if ( !CheckFrame( pIn, size, kResultSize, false, FT_Result ) ) {
            return false;
        }

        outFrame.SessionID = ReadU32( pIn + 0x4 );
        outFrame.CoreAddress.IP = ReadU32( pIn + 0x8 );
        outFrame.CoreAddress.Port = ReadU16( pIn + 0xc );
        outFrame.bEncrypt = pIn[0xe] != 0;
        outFrame.ResultCode = eResultCode( pIn[0xf] );
        ReadFixedString( pIn + 0x10, outFrame.Message, kMessageLength );
        return true;
    }

    uint32_t WriteRelayHeader( const RelayHeader &header, uint8_t *pOut ) {
        // FUN_0062c830
        memset( pOut, 0, kRelayHeaderSize );
        WriteFrameHeader( pOut, header.bToProxy,
                          header.bToProxy ? FT_ToRelay : FT_FromRelay );
        WriteU32( pOut + 0x4, header.Endpoint.IP );
        WriteU16( pOut + 0x8, header.Endpoint.Port );
        WriteU32( pOut + 0xc, header.SessionID );
        WriteU16( pOut + 0x10, header.PayloadLength );
        return kRelayHeaderSize;
    }

    bool ReadRelayHeader( const uint8_t *pIn,
                          const uint32_t size,
                          RelayHeader &outHeader ) {
        if ( size < kRelayHeaderSize ) {
            return false;
        }

        const int32_t type = PeekFrameType( pIn, size, outHeader.bToProxy );
        if ( type != ( outHeader.bToProxy ? FT_ToRelay : FT_FromRelay ) ) {
            return false;
        }

        outHeader.Endpoint.IP = ReadU32( pIn + 0x4 );
        outHeader.Endpoint.Port = ReadU16( pIn + 0x8 );
        outHeader.SessionID = ReadU32( pIn + 0xc );
        outHeader.PayloadLength = ReadU16( pIn + 0x10 );
        return true;
    }

    void DeriveSessionKey( const uint32_t challenge0,
                           const uint32_t responses[kNumChallenges],
                           char outKey[8] ) {
        const uint32_t seed =
            challenge0 ^ responses[0] ^ responses[1] ^ responses[2];
        char buffer[9];
        snprintf( buffer, sizeof( buffer ), "%08X", seed );
        memcpy( outKey, buffer, 8 );
    }

    uint32_t ComputeChallengeResponse( const uint32_t challenge ) {
        uint8_t bytes[4];
        WriteU32( bytes, challenge ^ kMagic );
        return TestDriveCRC32Unfolded( bytes, sizeof( bytes ) );
    }
} // namespace edNetProxyProtocol
