#pragma once

#include <cstdint>
#include <memory>
#include <string>

class HeadTrackingReceiver {
public:
    HeadTrackingReceiver();
    ~HeadTrackingReceiver();

    void setEndpoint( const char *pAddress, const uint16_t port );

    bool open();
    void close();
    bool poll( float &outYaw, float &outPitch );

private:
    struct Socket;

    std::unique_ptr<Socket> pSocket;
    std::string address;
    uint16_t port;
};
