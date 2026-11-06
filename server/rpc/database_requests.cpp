#include "shared.h"
#include "database_requests.h"

#include "service/action_service.h"
#include "database/database.h"
#include "service/proxy_service.h"
#include "tdu_server.h"

#include "core/ed_store.h"
#include "online/net/ed_net_rpc.h"

#include <ctime>

namespace {
    void WriteLoginReply( RPCCall &call,
                          const bool bSuccess,
                          const uint32_t serverTime ) {
        // Database::a_login: u8 err (1 = OK), u32 server time (seconds),
        // u8 identity flag (read as a byte by the client, FUN_008854a0).
        call.Reply.updateCRC( edNetRPC::ALogin() );
        call.Reply.writeByte( bSuccess ? 1 : 0 );
        call.Reply.writeInteger( int32_t( serverTime ) );
        call.Reply.writeByte( bSuccess ? 1 : 0 );
    }

    bool OnLoginPC( Server &server, RPCCall &call ) {
        // Client: SendLogin FUN_00884c80
        char login[64] = {};
        char password[64] = {};
        char cdKey[64] = {};
        call.Request.readStringNoAlloc( login, sizeof( login ) - 1 );
        call.Request.readStringNoAlloc( password, sizeof( password ) - 1 );
        const uint32_t country = call.Request.readUInteger();
        const uint64_t profileID = call.Request.readULongLong();
        call.Request.readByte();
        call.Request.readStringNoAlloc( cdKey, sizeof( cdKey ) - 1 );
        if ( call.Request.getStatus() != 0 ) {
            return false;
        }

        // The password is the [Online] DefaultLogin one, not the player's:
        // the account was authenticated by OpenTDU::Identity::r_login.
        Account account;
        const bool bAuthenticated =
            call.Session.AccountID != 0u &&
            uint64_t( call.Session.AccountID ) == profileID &&
            server.getDatabase().findAccountByID( call.Session.AccountID,
                                                  account );
        if ( !bAuthenticated ) {
            OTDU_LOG_WARN( "r_login_pc refused for '%s' (session %08X, "
                           "profile %llu)\n",
                           login, call.Session.SessionID,
                           (unsigned long long)profileID );
            WriteLoginReply( call, false, 0u );
            return true;
        }

        const uint32_t serverTime = uint32_t( time( nullptr ) );
        WriteLoginReply( call, true, serverTime );

        // Identity record (parsed by FUN_008850c0, no CRC header):
        // u32 user id, string nickname, u32 team id, u64 team spot hash.
        edStore record;
        record.writeInteger( int32_t( account.ID ) );
        record.writeString( account.Nick.substr( 0, 31 ).c_str() );
        record.writeInteger( 0 );
        record.writeULongLong( 0ull );
        call.NetBuffer.assign( record.getBuffer(),
                               record.getBuffer() + record.getWrittenSize() );
        call.bUseNetBuffer = true;

        OTDU_LOG_INFO( "r_login_pc: '%s' logged in to the game server "
                       "(account %u, country %u)\n",
                       account.Nick.c_str(), account.ID, country );
        return true;
    }

    bool OnNewGame( RPCCall &call ) {
        // Client: FUN_00884c00. The client only checks that the reply CRC is
        // the request one (FUN_008854a0), then clears Driver::bNewGamePending.
        const uint32_t newGameUserID = call.Request.readUInteger();
        if ( call.Request.getStatus() != 0 ) {
            return false;
        }

        if ( call.Session.AccountID == 0u ||
             call.Session.AccountID != newGameUserID ) {
            OTDU_LOG_WARN( "r_newgame refused for user %u (session %08X)\n",
                           newGameUserID, call.Session.SessionID );
            return false;
        }

        call.Reply.updateCRC( edNetRPC::RNewGame() );
        OTDU_LOG_INFO( "r_newgame: account %u started a new game\n",
                       newGameUserID );
        return true;
    }

    bool OnUserStatisticsUpdate( RPCCall &call ) {
        // Client: FUN_0087cc40. The retail client ignores the answer.
        uint8_t summaryBuffer[0x578];
        edStore summary;
        summary.bindExternalBuffer( summaryBuffer, sizeof( summaryBuffer ) );
        if ( !call.Request.popObjectNoAlloc( summary ) ||
             summary.getStreamCRC() != edNetRPC::UserStatisticsV1() ) {
            OTDU_LOG_WARN( "e_user_statistics_update_v2: unexpected payload "
                           "(session %08X)\n",
                           call.Session.SessionID );
            return true;
        }

        const uint32_t userID = summary.readUInteger();
        const uint32_t playerLevel = summary.readUInteger();
        const uint32_t currentCar = summary.readUInteger();
        uint32_t extraSize = 0u;
        uint8_t extraBuffer[0x578];
        edStore extraDatas;
        extraDatas.bindExternalBuffer( extraBuffer, sizeof( extraBuffer ) );
        if ( summary.popObjectNoAlloc( extraDatas ) &&
             extraDatas.getStreamCRC() ==
                 edNetRPC::UserStatisticsExtraDatasV1() ) {
            extraSize = extraDatas.getWrittenSize();
        }

        OTDU_LOG_INFO( "user_statistics_v1: user %u, level %u, car %u, "
                       "extra datas %u bytes\n",
                       userID, playerLevel, currentCar, extraSize );
        OTDU_UNIMPLEMENTED_ONCE; // TODO: ONLINE (store user_statistics_v1)
        return true;
    }

    bool LogSaveDescriptor( RPCCall &call, const char *pName ) {
        const uint32_t userID = call.Request.readUInteger();
        const uint32_t trailer = call.Request.readUInteger();
        const uint32_t revision = call.Request.readUInteger();
        const uint32_t money = call.Request.readUInteger();
        const int32_t numHouses = call.Request.readInteger();
        const int32_t numCars = call.Request.readInteger();
        const uint32_t currentCar = call.Request.readUInteger();
        call.Request.readULongLong();
        for ( int32_t i = 0; i < Min( numCars, 500 ); i++ ) {
            call.Request.readUShort();
        }
        if ( call.Request.getStatus() != 0 ) {
            OTDU_LOG_WARN( "%s: malformed save descriptor (session %08X)\n",
                           pName, call.Session.SessionID );
            return false;
        }

        OTDU_LOG_INFO( "%s: user %u, revision %u/%u, money %u, %d houses, "
                       "%d cars, current car %u\n",
                       pName, userID, trailer, revision, money, numHouses,
                       numCars, currentCar );
        return true;
    }

    bool OnSavegameCheck( RPCCall &call ) {
        // Client: CSysMessageMgr::SavegameCheck FUN_00871ac0
        if ( !LogSaveDescriptor( call, "Savegame::r_check" ) ) {
            return false;
        }

        call.Reply.updateCRC( edNetRPC::SavegameACheck() );
        call.Reply.writeByte( 1 );
        return true;
    }

    bool OnSavegameSave( RPCCall &call ) {
        // Client: CSysMessageMgr::SavegameSave FUN_00871c40
        if ( !LogSaveDescriptor( call, "Savegame::r_save" ) ) {
            return false;
        }

        call.Reply.updateCRC( edNetRPC::SavegameASave() );
        call.Reply.writeByte( 1 );
        return true;
    }
} // namespace

void RegisterDatabaseRequests( Server &server, ActionService &actions ) {
    actions.registerRPC(
        edNetRPC::RLoginPC(), "Database::r_login_pc",
        [&server]( RPCCall &call ) { return OnLoginPC( server, call ); } );
    actions.registerRPC( edNetRPC::RNewGame(), "Database::r_newgame",
                         OnNewGame );
    actions.registerRPC( edNetRPC::EUserStatisticsUpdateV2(),
                         "Stats::e_user_statistics_update_v2",
                         OnUserStatisticsUpdate );
    actions.registerRPC( edNetRPC::SavegameRCheck(), "Savegame::r_check",
                         OnSavegameCheck );
    actions.registerRPC( edNetRPC::SavegameRSave(), "Savegame::r_save",
                         OnSavegameSave );
}
