#pragma once

namespace TestDrive {
    namespace CRC {
        static uint32_t Protocol = 1;
    }
} // namespace TestDrive

#include "game/gs_base.h"

#include "online/net/ed_net_proxy.h"
#include "online_services.h"

class Driver;
class edStore;

// Database/Leaderboards.ini row
struct LeaderboardChallenge {
    uint64_t SpotHashcode = 0ull;
    int32_t LeaderboardID = 0;
};

// edNetProxyHostInterfaceTDU (named after debug log left in the original
// pcode)
class edNetProxyHostInterfaceTDU : public edNetProxyListener {
public:
    inline int32_t getLastError() const { return errorCode; }
    inline const char *getUpdateURL() const { return pUpdateURL; }

public:
    void onProxyState( const int32_t state ) override;
    void onProxyConnected( const edNetAddress &coreAddress ) override;
    void onProxyError( const int32_t errorCode, const char *pMessage ) override;
    void buildHello( edNetProxyProtocol::Hello &outHello ) override;

private:
    bool bResult = false;
    int32_t errorCode = 0;
    char pUpdateURL[256] = {};
};

class GSOnline : public GameSystem {
public:
    const char *getName() const override { return "Service : Online"; }

public:
    enum eChallengeLeaderboard : int32_t {
        CL_Time = 0,
        CL_Speed = 1,
        CL_Race = 2,
        CL_Count
    };

    static constexpr int32_t kMaxChallengeLeaderboards[CL_Count] = { 50, 30,
                                                                     50 };
    static constexpr uint16_t kGPSTrackerPort = 9002;
    static constexpr uint16_t kProxyPort = 8889;
    enum eLoginState : int32_t { LS_Idle = 0, LS_LoginSent = 4 };

public:
    inline int32_t
    getNumChallengeLeaderboards( const eChallengeLeaderboard kind ) const {
        return numChallengeLeaderboards[kind];
    }
    inline const LeaderboardChallenge &
    getChallengeLeaderboard( const eChallengeLeaderboard kind,
                             const int32_t index ) const {
        return challengeLeaderboards[kind][index];
    }
    inline const char *getORBAddress() const { return pORBAddress; }
    inline uint16_t getORBPort() const { return orbPort; }
    inline void setNewsReaderOpen( const bool bOpen ) {
        bNewsReaderOpen = bOpen;
    }
    inline bool isNewsReaderOpen() const { return bNewsReaderOpen; }
    inline uint32_t getUserID() const { return userID; }
    inline uint32_t getTeamID() const { return teamID; }
    inline uint64_t getTeamSpotHash() const { return teamSpotHash; }
    inline OnlineServices &getServices() { return services; }
    inline edNetProxyHostInterfaceTDU &getProxyHostInterface() {
        return proxyHostInterface;
    }

    inline void setOnlineEnabled( const bool bEnabled ) {
        bOnlineEnabled = bEnabled;
        bMatchmakingEnabled = bEnabled;
    }
    inline void setServicesActive( const bool bActive ) {
        bServicesActive = bActive;
    }
    inline void setNatType( const int32_t type ) { natType = type; }

    inline bool isLoggedIn() const { return bLoggedIn; }
    inline bool isLoginAnswered() const { return bLoginAnswered; }
    inline void resetLoginState() {
        loginState = LS_Idle;
        bLoggedIn = false;
        bLoginAnswered = false;
    }
    inline void clearFieldA713() { field_0xa713 = false; }

public:
    GSOnline();
    ~GSOnline();

    bool initialize( TestDriveGameInstance * ) override;
    void tick( float totalTime, float deltaTime ) override;
    void terminate() override;

    void setFreeRideVersion( const char *pVersion );
    void setXBLModeSecure( bool param_1 );
    void setUsername( const char *param_1 );
    inline const char *getUsername() const { return pUsername; }

    bool setServerLogin( const char *pLogin, const char *pPassword );
    bool setServerAddress( const char *pAddress, const int32_t port );

    void setLobbying( bool bEnabled );
    void setLobbyAddress( const char *pAddress, const int32_t port );

    void setGPSTrackerAddress( const char *pAddress );

    void setDbgCOMAddress( const char *param_1 );

    void setORBAddress( const char *pAddress );
    void setORBAddressFromIni( const char *pAddress, const int32_t port );
    bool isORBConfigured() const;

    int32_t notifyCheatDetected( Driver *pDriver );
    void addChallengeLeaderboard( const eChallengeLeaderboard kind,
                                  const uint64_t spotHashcode,
                                  const int32_t leaderboardID );
    void startProxy();
    bool connectGameServer( const int32_t param );
    void resetForProfileChange();
    void resetForNewGame();

private:
    edNetProxyHostInterfaceTDU proxyHostInterface;
    OnlineServices services;

    char pServerAddress[40];   // +0x660
    uint16_t serverPort;       // +0x688
    char pORBAddress[128];     // +0x68a
    uint16_t orbPort;          // +0x70a
    uint16_t lobbyPort;        // +0x70c
    char pLogin[20];           // +0x70e
    char pPassword[20];        // +0x722
    eLoginState loginState;    // +0x73c
    bool bMatchmakingEnabled;  // +0x740
    bool bOnlineEnabled;       // +0x741
    char pLobbyAddress[30];    // +0x742
    char pDbgCOM[30];          // +0x760
    char pGPSTracker[30];      // +0x77e
    char pUsername[16];        // +0x79c
    bool bServicesActive;      // +0x7cc
    bool bLoggedIn;            // +0xa711
    bool bLoginAnswered;       // +0xa712
    bool field_0xa713;         // +0xa713
    bool bInitialized;         // +0xaec1
    char pFreerideVersion[32]; // +0xaec2
    int32_t natType;           // +0xaee4
    uint32_t userID;           // +0x7ac
    uint32_t teamID;           // +0x7b0
    uint64_t teamSpotHash;     // +0x7b8
    uint32_t serverTime;
    std::vector<uint8_t> identityBuffer;
    bool bORBFromIni;

    bool bXBLModeSecure;
    bool bNewsReaderOpen;

    LeaderboardChallenge challengeLeaderboards[CL_Count][50];
    int32_t numChallengeLeaderboards[CL_Count];

private:
    void initConnectionInfos();
    bool sendLogin( const uint8_t param );
    void onLoginComplete( const edNetActionClient::Reply &reply );
    void onIdentityDownloaded( const bool bSuccess );
    void parseLoginIdentity( edStore &stream );
};

uint32_t GetCountryIndex();

extern GSOnline gOnline;
extern GSOnline *gpOnline;
extern char gORBAddress[128];
