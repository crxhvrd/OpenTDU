#pragma once

#include "online/net/ed_net_action.h"

class edNetGamespyManager {
public:
    enum eState : int32_t {
        S_Idle = 0,
        S_AvailabilityCheck = 1,
        S_AvailabilityFailed = 2,
        S_ConnectRequested = 3,
        S_PeerConnecting = 4,
        S_PeerRetryWait = 5,
        S_PeerConnected = 6,
        S_PeerThinking = 7,
        S_Connected = 8,
        S_Failed = 9
    };

    enum eGPState : int32_t {
        GPS_Idle = 0,
        GPS_ConnectPending = 1,
        GPS_Connecting = 2,
        GPS_LoggedIn = 3,
        GPS_LoginFailed = 4,
        GPS_Creating = 5,
        GPS_Created = 6,
        GPS_CreateFailed = 7,
        GPS_ConnectingNewUser = 8,
        GPS_NewUserLoggedIn = 9,
        GPS_NewUserFailed = 10
    };

    enum eError : int32_t {
        E_None = 0,
        E_ServiceTemporarilyUnavailable = 2,
        E_ServiceUnavailable = 3,
        E_FriendsServiceUnavailable = 4,
        E_LoginTimeout = 5,
        E_InvalidNick = 6,
        E_InvalidEmail = 7,
        E_InvalidPassword = 8,
        E_InvalidProfile = 9,
        E_LoginFailed = 10,
        E_NickInUse = 0xc,
        E_PeerConnectionFailed = 0x12,
        E_AccountInUse = 0x18,
        E_InvalidCDKey = 0x1f
    };

    enum eLoginMode : int32_t {
        LM_Login = 0,
        LM_CreateAndLogin = 1,
        LM_Create = 2
    };

    static constexpr int32_t kPollConnectOK = 0;
    static constexpr int32_t kPollConnectFailed = 1;
    static constexpr int32_t kPollConnectPending = 2;

    static constexpr int32_t kPollCreateOK = 0;
    static constexpr int32_t kPollCreatePending = 1;
    static constexpr int32_t kPollCreateFailed = 2;

    static constexpr uint32_t kConnectTimeoutMs = 30000;

public:
    inline bool isConnectedToGamespy() const { return state == S_Connected; }
    inline eState getState() const { return state; }
    inline eError getLastError() const { return lastError; }
    inline eGPState getGPState() const { return gpState; }
    inline uint32_t getProfileID() const { return profileID; }

public:
    edNetGamespyManager();
    ~edNetGamespyManager();

    void initialize();
    void tick();
    void disconnect();

    int32_t resetConnect();
    int32_t resetConnectNewAccount();
    int32_t pollConnect();
    int32_t pollConnectCreateAccount();

    void setNick( const char *pNick );
    void setEmail( const char *pEmail );
    void setPassword( const char *pPassword );
    void setCDKey( const char *pCDKey );

    void denyFriendRequest();
    void acceptFriendRequest();

private:
    eState state;
    eError lastError;
    eLoginMode loginMode;
    eGPState gpState;
    uint32_t profileID;
    bool bPollArmed;
    bool bPollCreateArmed;
    uint32_t pollStartTicks;

    bool bIdentityRequestSent;
    uint32_t connectGeneration;

    char nick[32];     // config +0xb0
    char password[32]; // config +0xd0
    char email[52];    // config +0xf0
    char cdKey[66];    // config +0x13a

private:
    bool startConnect();
    void onIdentityReply( const edNetActionClient::Reply &reply );
};

extern edNetGamespyManager *gpEdNetGamespyManager;
