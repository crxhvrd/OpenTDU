#include <asio.hpp>

#include "shared.h"
#include "ed_net_socket.h"

struct edNetSocket::Impl {
    asio::io_context context;
    asio::ip::udp::socket socket;

    Impl() : context(), socket( context ) {}
};

std::string edNetAddress::toString() const {
    char buffer[32];
    snprintf( buffer, sizeof( buffer ), "%u.%u.%u.%u:%u", ( IP >> 24 ) & 0xff,
              ( IP >> 16 ) & 0xff, ( IP >> 8 ) & 0xff, IP & 0xff, Port );
    return buffer;
}

bool edNetAddress::Resolve( const char *pHost,
                            const uint16_t port,
                            edNetAddress &outAddress ) {
    asio::error_code error;
    const asio::ip::address_v4 address =
        asio::ip::make_address_v4( pHost, error );
    if ( !error ) {
        outAddress.IP = address.to_uint();
        outAddress.Port = port;
        return true;
    }

    asio::io_context context;
    asio::ip::udp::resolver resolver( context );
    const auto results =
        resolver.resolve( asio::ip::udp::v4(), pHost, "", error );
    if ( error || results.empty() ) {
        OTDU_LOG_WARN( "Failed to resolve '%s' (%s)\n", pHost,
                       error.message().c_str() );
        return false;
    }

    outAddress.IP = results.begin()->endpoint().address().to_v4().to_uint();
    outAddress.Port = port;
    return true;
}

edNetSocket::edNetSocket()
    : pImpl( nullptr ),
      simulatedLoss( 0.0f ),
      simulatedDuplication( 0.0f ),
      simulationSeed( 0x2a7bu ) {}

void edNetSocket::setSimulatedConditions( const float lossRatio,
                                          const float duplicateRatio ) {
    simulatedLoss = lossRatio;
    simulatedDuplication = duplicateRatio;
    simulationSeed =
        uint32_t(
            std::chrono::steady_clock::now().time_since_epoch().count() ) |
        1u;
}

float edNetSocket::nextSimulationRandom() {
    // xorshift32
    simulationSeed ^= simulationSeed << 13;
    simulationSeed ^= simulationSeed >> 17;
    simulationSeed ^= simulationSeed << 5;
    return float( simulationSeed >> 8 ) / float( 1u << 24 );
}

edNetSocket::~edNetSocket() { close(); }

bool edNetSocket::open( const uint16_t port, const char *pBindAddress ) {
    close();

    asio::error_code error;
    const asio::ip::address bindAddress =
        asio::ip::make_address( pBindAddress, error );
    if ( error ) {
        OTDU_LOG_ERROR( "edNetSocket: invalid bind address '%s' (%s)\n",
                        pBindAddress, error.message().c_str() );
        return false;
    }

    std::unique_ptr<Impl> pNewImpl = std::make_unique<Impl>();
    const asio::ip::udp::endpoint endpoint( bindAddress, port );
    pNewImpl->socket.open( endpoint.protocol(), error );
    if ( !error ) {
        pNewImpl->socket.bind( endpoint, error );
    }
    if ( !error ) {
        pNewImpl->socket.non_blocking( true, error );
    }
    if ( error ) {
        OTDU_LOG_ERROR( "edNetSocket: failed to open UDP %s:%u (%s)\n",
                        pBindAddress, port, error.message().c_str() );
        return false;
    }

    pImpl = std::move( pNewImpl );
    return true;
}

void edNetSocket::close() {
    if ( pImpl == nullptr ) {
        return;
    }

    asio::error_code error;
    pImpl->socket.close( error );
    pImpl.reset();
}

uint16_t edNetSocket::getLocalPort() const {
    if ( pImpl == nullptr ) {
        return 0;
    }

    asio::error_code error;
    const asio::ip::udp::endpoint endpoint =
        pImpl->socket.local_endpoint( error );
    return error ? 0 : endpoint.port();
}

bool edNetSocket::sendTo( const edNetAddress &destination,
                          const void *pData,
                          const uint32_t size ) {
    if ( pImpl == nullptr ) {
        return false;
    }

    if ( simulatedLoss > 0.0f && nextSimulationRandom() < simulatedLoss ) {
        return true;
    }
    const int32_t numCopies = ( simulatedDuplication > 0.0f &&
                                nextSimulationRandom() < simulatedDuplication )
                                  ? 2
                                  : 1;

    const asio::ip::udp::endpoint endpoint(
        asio::ip::address_v4( destination.IP ), destination.Port );
    asio::error_code error;
    for ( int32_t i = 0; i < numCopies && !error; i++ ) {
        pImpl->socket.send_to( asio::buffer( pData, size ), endpoint, 0,
                               error );
    }
    if ( error ) {
        OTDU_LOG_WARN( "edNetSocket: sendto %s failed (%s)\n",
                       destination.toString().c_str(),
                       error.message().c_str() );
        return false;
    }
    return true;
}

int32_t edNetSocket::receiveFrom( edNetAddress &outSender,
                                  void *pBuffer,
                                  const uint32_t bufferSize ) {
    if ( pImpl == nullptr ) {
        return -1;
    }

    while ( true ) {
        asio::ip::udp::endpoint sender;
        asio::error_code error;
        const size_t size = pImpl->socket.receive_from(
            asio::buffer( pBuffer, bufferSize ), sender, 0, error );
        if ( error == asio::error::would_block ||
             error == asio::error::try_again ) {
            return 0;
        }
        if ( error ) {
            if ( error == asio::error::connection_refused ||
                 error == asio::error::connection_reset ) {
                continue;
            }
            OTDU_LOG_WARN( "edNetSocket: recvfrom failed (%s)\n",
                           error.message().c_str() );
            return -1;
        }
        if ( !sender.address().is_v4() ) {
            continue;
        }

        outSender.IP = sender.address().to_v4().to_uint();
        outSender.Port = sender.port();
        return int32_t( size );
    }
}
