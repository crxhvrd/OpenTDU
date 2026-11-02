#pragma once

class CLobbyNetworkService {
public:
    CLobbyNetworkService();

    void initialize();
    void refresh();
    void terminate();

    inline bool areServicesActive() const { return bServicesActive; }

private:
    bool bInitialized;    // +0x8258
    bool bServicesActive; // +0x85cc
    bool bP2PInit;        // +0x85cd
};

extern CLobbyNetworkService gLobbyNetworkService;
