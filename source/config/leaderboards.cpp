#include "shared.h"
#include "gs_config.h"

#include "online/gs_online.h"

static uint64_t SpotNameToHashcode( const char *pName ) {
    // FUN_007d68f0
    uint64_t hashcode = 0ull;
    memcpy( &hashcode, pName, std::min( strlen( pName ), sizeof( hashcode ) ) );
    return hashcode;
}

static void ParseChallengeLeaderboard( GSOnline::eChallengeLeaderboard kind,
                                       const char *pArg ) {
    std::string argStr( pArg );
    size_t it = 0;

    const std::string spotName = GSConfig::ParseStringNoQuotes( argStr, it );
    const int32_t leaderboardID = GSConfig::ParseInteger( argStr, it );

    gpOnline->addChallengeLeaderboard(
        kind, SpotNameToHashcode( spotName.c_str() ), leaderboardID );
}

void GSConfig::registerLeaderboardsCommands() {
    registerCommand( "CHALL_TIME", [&]( const char *pArg ) {
        // FUN_00983200
        ParseChallengeLeaderboard( GSOnline::CL_Time, pArg );
    } );

    registerCommand( "CHALL_SPEED", [&]( const char *pArg ) {
        // FUN_00983190
        ParseChallengeLeaderboard( GSOnline::CL_Speed, pArg );
    } );

    registerCommand( "CHALL_RACE", [&]( const char *pArg ) {
        // FUN_00983120
        ParseChallengeLeaderboard( GSOnline::CL_Race, pArg );
    } );

    registerCommand( "NB_TIMES", [&]( const char *pArg ) {
        // FUN_009830f0
        char local_40[64];
        sprintf( local_40, "NB_TIMES %d", atoi( pArg ) );
    } );

    registerCommand( "NB_SPEEDS", [&]( const char *pArg ) {
        // FUN_009830c0
        char local_40[64];
        sprintf( local_40, "NB_SPEEDS %d", atoi( pArg ) );
    } );

    registerCommand( "NB_RACES", [&]( const char *pArg ) {
        // FUN_00983090
        char local_40[64];
        sprintf( local_40, "NB_RACES %d", atoi( pArg ) );
    } );
}
