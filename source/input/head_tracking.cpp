#if defined( _WIN32 ) && !defined( _WIN32_WINNT )
#define _WIN32_WINNT 0x0601
#endif
#include <asio.hpp>

#include "shared.h"
#include "head_tracking.h"

#include "core/maths_helper.h"

static constexpr uint16_t kDefaultPort = 4242;
static constexpr size_t kPoseSize = sizeof( double ) * 6;

enum ePoseAxis : int32_t { PA_X = 0, PA_Y, PA_Z, PA_Yaw, PA_Pitch, PA_Roll };

struct HeadTrackingReceiver::Socket {
    asio::io_context context;
    asio::ip::udp::socket socket;

    Socket() : context(), socket( context ) {}
};

HeadTrackingReceiver::HeadTrackingReceiver()
    : pSocket(), address( "127.0.0.1" ), port( kDefaultPort ) {}

HeadTrackingReceiver::~HeadTrackingReceiver() { close(); }

void HeadTrackingReceiver::setEndpoint( const char *pAddress,
                                        const uint16_t newPort ) {
    address = pAddress;
    port = newPort;
}

bool HeadTrackingReceiver::open() {
    close();

    asio::error_code error;
    const asio::ip::address bindAddress =
        asio::ip::make_address( address, error );
    if ( error ) {
        OTDU_LOG_WARN( "Head tracking: invalid address '%s' (%s)\n",
                       address.c_str(), error.message().c_str() );
        return false;
    }

    std::unique_ptr<Socket> pNewSocket = std::make_unique<Socket>();
    const asio::ip::udp::endpoint endpoint( bindAddress, port );
    pNewSocket->socket.open( endpoint.protocol(), error );
    if ( !error ) {
        pNewSocket->socket.bind( endpoint, error );
    }
    if ( !error ) {
        pNewSocket->socket.non_blocking( true, error );
    }
    if ( error ) {
        OTDU_LOG_WARN( "Head tracking: failed to listen on %s:%u (%s)\n",
                       address.c_str(), port, error.message().c_str() );
        return false;
    }

    OTDU_LOG_INFO( "Head tracking: listening on %s:%u\n", address.c_str(),
                   port );
    pSocket = std::move( pNewSocket );
    return true;
}

void HeadTrackingReceiver::close() {
    if ( pSocket == nullptr ) {
        return;
    }

    asio::error_code error;
    pSocket->socket.close( error );
    pSocket.reset();
}

bool HeadTrackingReceiver::poll( float &outYaw, float &outPitch ) {
    if ( pSocket == nullptr ) {
        return false;
    }

    bool bReceived = false;
    double pose[6];
    asio::ip::udp::endpoint sender;
    asio::error_code error;
    while ( true ) {
        const size_t size = pSocket->socket.receive_from(
            asio::buffer( pose, sizeof( pose ) ), sender, 0, error );
        if ( error ) {
            break;
        }

        if ( size != kPoseSize ) {
            continue;
        }

        outYaw = static_cast<float>( pose[PA_Yaw] ) * kDegToRad;
        outPitch = static_cast<float>( pose[PA_Pitch] ) * kDegToRad;
        bReceived = true;
    }

    return bReceived;
}
