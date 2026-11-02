#pragma once

// IPv4 endpoint as stored by edNetClient (host byte order).
struct edNetAddress {
    uint32_t IP = 0u;
    uint16_t Port = 0u;

    inline bool operator==( const edNetAddress &other ) const {
        return IP == other.IP && Port == other.Port;
    }
    inline bool operator!=( const edNetAddress &other ) const {
        return !( *this == other );
    }
    inline bool isValid() const { return IP != 0u && Port != 0u; }

    std::string toString() const;

    static bool
    Resolve( const char *pHost, const uint16_t port, edNetAddress &outAddress );
};

// Non-blocking UDP socket trying to emulate WSA2 sockets used by og pcode
class edNetSocket {
public:
    static constexpr uint32_t kMaxDatagramSize = 0x578;

public:
    edNetSocket();
    ~edNetSocket();

    // port == 0 binds an ephemeral port.
    bool open( const uint16_t port, const char *pBindAddress = "0.0.0.0" );
    void close();

    inline bool isOpen() const { return pImpl != nullptr; }

    void setSimulatedConditions( const float lossRatio,
                                 const float duplicateRatio );
    uint16_t getLocalPort() const;

    bool sendTo( const edNetAddress &destination,
                 const void *pData,
                 const uint32_t size );

    int32_t receiveFrom( edNetAddress &outSender,
                         void *pBuffer,
                         const uint32_t bufferSize );

private:
    struct Impl;
    std::unique_ptr<Impl> pImpl;
    float simulatedLoss;
    float simulatedDuplication;
    uint32_t simulationSeed;

private:
    float nextSimulationRandom();
};
