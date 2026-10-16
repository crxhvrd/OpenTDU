#include "shared.h"
#include "gs_online.h"

#include "player_data/driver.h"
#include "player_data/gs_playerdata.h"
#include "player_data/player_data_interface.h"

GSOnline gOnline = {};
GSOnline *gpOnline = nullptr;

GSOnline::GSOnline()
    : pFreerideVersion( "PC Beta - 2.41A" ),
      pServerLogin( "none" ),
      pServerPassword( "edenpwd" ),
      pGPSTracker( "none" ),
      pDbgCOM( "127.0.0.1" ),
      bXBLModeSecure( false ),
      bLobbying( false ),
      bGPSTrackerEnabled( false ),
      bNewsReaderOpen( false ),
      challengeLeaderboards(),
      numChallengeLeaderboards() {
    gpOnline = this;
}

GSOnline::~GSOnline() {}

bool GSOnline::initialize( TestDriveGameInstance * ) { return true; }

void GSOnline::tick( float totalTime, float deltaTime ) {}

void GSOnline::terminate() {}

void GSOnline::setFreeRideVersion( const char *pVersion ) {
    strcpy( pFreerideVersion, pVersion );
}

void GSOnline::setXBLModeSecure( bool param_1 ) { bXBLModeSecure = param_1; }

void GSOnline::setUsername( const char *param_1 ) {
    strncpy( pUsername, param_1, 0x10 );
    pUsername[0xf] = '\0';
}

void GSOnline::setServerLogin( const char *param_1, const char *param_2 ) {
    // FUN_0087b490
    OTDU_UNIMPLEMENTED;
}

bool GSOnline::setServerAddress( const char *param_1, const int32_t param_2 ) {
    // FUN_0087b940
    OTDU_UNIMPLEMENTED;
    return false;
}

void GSOnline::setLobbying( const bool param_1 ) { bLobbying = param_1; }

void GSOnline::setLobbyAddress( const char *param_1, const int32_t param_2 ) {
    // FUN_0087b900
    OTDU_UNIMPLEMENTED;
}

void GSOnline::setGPSTrackerEnabled( bool param_1 ) {
    bGPSTrackerEnabled = param_1;
}

void GSOnline::setGPSTrackerAddress( const char *param_1 ) {
    strncpy( pGPSTracker, param_1, 30 );
    bGPSTrackerEnabled = true;
}

void GSOnline::setDbgCOMAddress( const char *param_1 ) {
    strncpy( pDbgCOM, param_1, 30 );
}

int32_t GSOnline::notifyCheatDetected( Driver *pDriver ) {
    // FUN_00884c00
    if ( pDriver == nullptr || !pDriver->isValid() ) {
        return -1;
    }

    gpPlayerData->getLoadedSave()->getDriver()->bCheatDetected = true;

    OTDU_UNIMPLEMENTED_ONCE; // TODO: ONLINE (gpCP2PGameService)

    return -1;
}

void GSOnline::addChallengeLeaderboard( const eChallengeLeaderboard kind,
                                        const uint64_t spotHashcode,
                                        const int32_t leaderboardID ) {
    // FUN_00983200 / FUN_00983190 / FUN_00983120 (inlined)
    int32_t &count = numChallengeLeaderboards[kind];
    if ( count >= kMaxChallengeLeaderboards[kind] ) {
        return;
    }

    challengeLeaderboards[kind][count].SpotHashcode = spotHashcode;
    challengeLeaderboards[kind][count].LeaderboardID = leaderboardID;
    count++;
}
