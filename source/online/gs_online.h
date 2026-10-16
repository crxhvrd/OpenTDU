#pragma once

namespace TestDrive {
    namespace CRC {
        static uint32_t Protocol = 1;
    }
} // namespace TestDrive
#include "game/gs_base.h"

class Driver;

// Database/Leaderboards.ini row
struct LeaderboardChallenge {
    uint64_t SpotHashcode = 0ull;
    int32_t LeaderboardID = 0;
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
    inline void setNewsReaderOpen( const bool bOpen ) {
        bNewsReaderOpen = bOpen;
    }
    inline bool isNewsReaderOpen() const { return bNewsReaderOpen; }

    void setServerLogin( const char *param_1, const char *param_2 );
    bool setServerAddress( const char *param_1, const int32_t param_2 );

    void setLobbying( bool param_1 );
    void setLobbyAddress( const char *param_1, const int32_t param_2 );

    void setGPSTrackerEnabled( bool param_1 );
    void setGPSTrackerAddress( const char *param_1 );

    void setDbgCOMAddress( const char *param_1 );

    int32_t notifyCheatDetected( Driver *pDriver );
    void addChallengeLeaderboard( const eChallengeLeaderboard kind,
                                  const uint64_t spotHashcode,
                                  const int32_t leaderboardID );

private:
    char pFreerideVersion[32];
    char pUsername[16];

    char pServerLogin[20];
    char pServerPassword[20];

    char pGPSTracker[30];
    char pDbgCOM[30];

    uint8_t bXBLModeSecure : 1;
    uint8_t bLobbying : 1;
    uint8_t bGPSTrackerEnabled : 1;
    uint8_t bNewsReaderOpen : 1;

    LeaderboardChallenge challengeLeaderboards[CL_Count][50];
    int32_t numChallengeLeaderboards[CL_Count];
};

extern GSOnline gOnline;
extern GSOnline *gpOnline;
