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

    bool OnUserStatisticsUpdate( RPCCall &call ) {
        // Client: FUN_0087cc40. The retail client ignores the answer.
        OTDU_UNIMPLEMENTED_ONCE; // TODO: ONLINE (store user_statistics_v1)
        return true;
    }

    bool OnSavegameCheck( RPCCall &call ) {
        // Client: CSysMessageMgr::SavegameCheck FUN_00871ac0
        call.Reply.updateCRC( edNetRPC::SavegameACheck() );
        call.Reply.writeByte( 1 );
        return true;
    }
} // namespace

void RegisterDatabaseRequests( Server &server, ActionService &actions ) {
    actions.registerRPC(
        edNetRPC::RLoginPC(), "Database::r_login_pc",
        [&server]( RPCCall &call ) { return OnLoginPC( server, call ); } );
    actions.registerRPC( edNetRPC::EUserStatisticsUpdateV2(),
                         "Stats::e_user_statistics_update_v2",
                         OnUserStatisticsUpdate );
    actions.registerRPC( edNetRPC::SavegameRCheck(), "Savegame::r_check",
                         OnSavegameCheck );
}
