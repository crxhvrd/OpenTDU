#pragma once

#include "online/net/ed_net_socket.h"

struct ServerConfig {
    uint16_t ProxyPort = 8889;
    std::string BindAddress = "0.0.0.0";
    std::string PublicAddress = "127.0.0.1";
    bool bVerbose = false;
    std::string DatabasePath = "opentdu_server.db";
    std::string NetConfigPath;
    // Unknown nicks are registered on their first login (otherwise the client
    // goes through the retail "create account" flow).
    bool bAutoRegister = true;
    // Registers the OpenTDU::r_debug_* RPCs used by opentdu_netprobe.
    bool bDebugRPCs = false;
    // Simulated network conditions on outgoing datagrams (testing only).
    float SimulatedLoss = 0.0f;
    float SimulatedDuplication = 0.0f;
};

class ProxyService;
class Database;

class Server {
public:
    explicit Server( const ServerConfig &config );
    ~Server();

    bool initialize();
    bool tick();
    void terminate();

    inline const ServerConfig &getConfig() const { return config; }
    inline edNetSocket &getSocket() { return socket; }
    inline Database &getDatabase() { return *pDatabase; }
    inline const edNetAddress &getPublicAddress() const {
        return publicAddress;
    }

    // Milliseconds since the server started.
    uint64_t getTimeMs() const;

private:
    ServerConfig config;
    edNetSocket socket;
    edNetAddress publicAddress;
    std::unique_ptr<Database> pDatabase;
    std::unique_ptr<ProxyService> pProxyService;
    std::chrono::steady_clock::time_point startTime;
};
