#include "shared.h"
#include "identity_requests.h"

#include "service/action_service.h"
#include "database/database.h"
#include "service/proxy_service.h"
#include "tdu_server.h"

#include "core/ed_store.h"
#include "online/net/ed_net_rpc.h"
#include "online/net/opentdu_rpc.h"

namespace {
    enum eIdentityError : uint8_t {
        IE_Success = 0,
        IE_InvalidNick = 6,
        IE_InvalidPassword = 8,
        IE_LoginFailed = 10,
        IE_NickInUse = 0xc,
    };

    enum eLoginMode : uint8_t {
        LM_Login = 0,
        LM_CreateAndLogin = 1,
    };

    static constexpr uint32_t kMaxNickLength = 31;
    static constexpr uint32_t kMaxPasswordLength = 31;

    void WriteReply( RPCCall &call,
                     const eIdentityError error,
                     const uint32_t profileID ) {
        call.Reply.updateCRC(
            edNetRPC::MessageID( OpenTDURPC::kIdentityLoginReply ) );
        call.Reply.writeByte( error );
        call.Reply.writeInteger( int32_t( profileID ) );
    }

    bool OnIdentityLogin( Server &server, RPCCall &call ) {
        char nick[64] = {};
        char email[64] = {};
        char password[64] = {};
        char cdKey[80] = {};
        call.Request.readStringNoAlloc( nick, sizeof( nick ) - 1 );
        call.Request.readStringNoAlloc( email, sizeof( email ) - 1 );
        call.Request.readStringNoAlloc( password, sizeof( password ) - 1 );
        call.Request.readStringNoAlloc( cdKey, sizeof( cdKey ) - 1 );
        const uint8_t mode = call.Request.readByte();
        if ( call.Request.getStatus() != 0 ) {
            return false;
        }

        const size_t nickLength = strlen( nick );
        if ( nickLength == 0 || nickLength > kMaxNickLength ) {
            WriteReply( call, IE_InvalidNick, 0u );
            return true;
        }
        const size_t passwordLength = strlen( password );
        if ( passwordLength == 0 || passwordLength > kMaxPasswordLength ) {
            WriteReply( call, IE_InvalidPassword, 0u );
            return true;
        }

        Database &database = server.getDatabase();
        Account account;
        if ( database.findAccountByNick( nick, account ) ) {
            if ( !Database::CheckPassword( account, password ) ) {
                OTDU_LOG_INFO( "Identity: wrong password for '%s' (session "
                               "%08X)\n",
                               nick, call.Session.SessionID );
                WriteReply( call, IE_InvalidPassword, 0u );
                return true;
            }
        } else if ( mode == LM_CreateAndLogin ||
                    server.getConfig().bAutoRegister ) {
            if ( !database.createAccount( nick, email, password, cdKey,
                                          account ) ) {
                WriteReply( call, IE_NickInUse, 0u );
                return true;
            }
            OTDU_LOG_INFO( "Identity: registered '%s' as account %u\n", nick,
                           account.ID );
        } else {
            WriteReply( call, IE_InvalidNick, 0u );
            return true;
        }

        database.touchAccountLogin( account.ID );
        call.Session.AccountID = account.ID;
        OTDU_LOG_INFO( "Identity: '%s' logged in (account %u, session %08X)\n",
                       account.Nick.c_str(), account.ID,
                       call.Session.SessionID );
        WriteReply( call, IE_Success, account.ID );
        return true;
    }
} // namespace

void RegisterIdentityRequests( Server &server, ActionService &actions ) {
    actions.registerRPC( OpenTDURPC::kIdentityLogin,
                         [&server]( RPCCall &call ) {
                             return OnIdentityLogin( server, call );
                         } );
}
