#include "shared.h"
#include "ed_net_gamespy_manager.h"

#include "core/ed_store.h"
#include "core/time.h"
#include "gs_online.h"
#include "online/net/ed_net_rpc.h"
#include "online/net/opentdu_rpc.h"
#include "p2p_game_service.h"

edNetGamespyManager *gpEdNetGamespyManager = nullptr; // DAT_010e75f0

static uint32_t GetTickCountMs() {
    return uint32_t( MonotonicToMs( GetTimeMonotonic() ) );
}

edNetGamespyManager::edNetGamespyManager()
    : state( S_Idle ),
      lastError( E_None ),
      loginMode( LM_Login ),
      gpState( GPS_Idle ),
      profileID( 0u ),
      bPollArmed( false ),
      bPollCreateArmed( false ),
      pollStartTicks( 0u ),
      bIdentityRequestSent( false ),
      connectGeneration( 0u ),
      nick{},
      password{},
      email{},
      cdKey{} {
    // FUN_00867650
}

edNetGamespyManager::~edNetGamespyManager() {}

void edNetGamespyManager::initialize() {
    // FUN_00867330
    state = S_Idle;
    lastError = E_None;
    gpState = GPS_Idle;
}

void edNetGamespyManager::tick() {
    // FUN_00868450
    if ( state != S_ConnectRequested ) {
        return;
    }

    edNetProxy &proxy = gpCP2PGameService->getProxy();
    if ( proxy.getResult() == edNetProxy::R_Failed ) {
        profileID = 0u;
        gpState = GPS_LoggedIn;
        state = S_Connected;
        return;
    }

    if ( proxy.getResult() != edNetProxy::R_Connected ||
         bIdentityRequestSent ) {
        return;
    }
    bIdentityRequestSent = true;
    gpState = ( loginMode == LM_CreateAndLogin ) ? GPS_ConnectingNewUser
                                                 : GPS_Connecting;

    edStore request;
    request.updateCRC( edNetRPC::MessageID( OpenTDURPC::kIdentityLogin ) );
    request.writeString( nick );
    request.writeString( email );
    request.writeString( password );
    request.writeString( cdKey );
    request.writeByte( uint8_t( loginMode ) );

    const uint32_t generation = connectGeneration;
    gpCP2PGameService->getActionClient().callAction(
        proxy.getCoreAddress(), request,
        [this, generation]( const edNetActionClient::Reply &reply ) {
            if ( generation == connectGeneration ) {
                onIdentityReply( reply );
            }
        },
        GetTickCountMs(), 1500, 7 );
}

void edNetGamespyManager::onIdentityReply(
    const edNetActionClient::Reply &reply ) {
    const bool bCreate = ( loginMode == LM_CreateAndLogin );
    if ( reply.Result != edNetActionClient::CR_Success ||
         reply.pReply == nullptr ||
         reply.pReply->getStreamCRC() !=
             edNetRPC::MessageID( OpenTDURPC::kIdentityLoginReply ) ) {
        lastError = E_PeerConnectionFailed;
        state = S_Failed;
        return;
    }

    const uint8_t error = reply.pReply->readByte();
    const uint32_t newProfileID = reply.pReply->readUInteger();
    if ( error != E_None ) {
        lastError = eError( error );
        gpState = bCreate ? GPS_NewUserFailed : GPS_LoginFailed;
        state = S_Connected;
        return;
    }

    profileID = newProfileID;
    gpState = bCreate ? GPS_NewUserLoggedIn : GPS_LoggedIn;
    state = S_Connected;
    OTDU_LOG_INFO( "Logged in as '%s' (profile %u)\n", nick, profileID );
}

void edNetGamespyManager::disconnect() {
    // FUN_008663d0
    connectGeneration++;
    bIdentityRequestSent = false;
    state = S_Idle;
    gpState = GPS_Idle;
}

int32_t edNetGamespyManager::resetConnect() {
    // FUN_008664e0
    disconnect();
    bPollArmed = false;
    pollStartTicks = 0u;
    state = S_Idle;
    return 1;
}

int32_t edNetGamespyManager::resetConnectNewAccount() {
    // FUN_00866470
    disconnect();
    bPollCreateArmed = false;
    pollStartTicks = 0u;
    state = S_Idle;
    return 1;
}

bool edNetGamespyManager::startConnect() {
    // FUN_00635e80
    if ( !gpOnline->isORBConfigured() ) {
        lastError = E_ServiceUnavailable;
        state = S_AvailabilityFailed;
        return false;
    }

    lastError = E_None;
    gpState = GPS_ConnectPending;
    state = S_ConnectRequested;
    connectGeneration++;
    bIdentityRequestSent = false;

    gpOnline->startProxy();
    return true;
}

int32_t edNetGamespyManager::pollConnect() {
    // FUN_00867170
    if ( !bPollArmed ) {
        bPollArmed = true;

        if ( !gpOnline->isORBConfigured() ) {
            lastError = E_ServiceUnavailable;
            state = S_AvailabilityFailed;
            return kPollConnectFailed;
        }

        edNetAddress orbAddress;
        if ( !edNetAddress::Resolve( gpOnline->getORBAddress(), 0,
                                     orbAddress ) ) {
            return kPollConnectFailed;
        }

        pollStartTicks = GetTickCountMs();
        loginMode = LM_Login;
        return startConnect() ? kPollConnectPending : kPollConnectFailed;
    }

    if ( GetTickCountMs() > pollStartTicks + kConnectTimeoutMs ) {
        gpState = GPS_LoginFailed;
        return kPollConnectFailed;
    }

    switch ( state ) {
    case S_AvailabilityFailed:
    case S_Failed:
        return kPollConnectFailed;
    case S_Connected:
        if ( gpState == GPS_LoggedIn ) {
            OTDU_UNIMPLEMENTED_ONCE; // TODO: ONLINE
            return kPollConnectOK;
        }
        return ( gpState == GPS_LoginFailed ) ? kPollConnectFailed
                                              : kPollConnectPending;
    default:
        return kPollConnectPending;
    }
}

int32_t edNetGamespyManager::pollConnectCreateAccount() {
    // FUN_00867020
    if ( !bPollCreateArmed ) {
        bPollCreateArmed = true;
        pollStartTicks = GetTickCountMs();
        loginMode = LM_Login;
        return startConnect() ? kPollCreatePending : kPollCreateFailed;
    }

    if ( GetTickCountMs() > pollStartTicks + kConnectTimeoutMs ) {
        gpState = GPS_LoginFailed;
        return kPollCreateFailed;
    }

    switch ( state ) {
    case S_AvailabilityFailed:
    case S_Failed:
        return kPollCreateFailed;
    case S_Connected:
        switch ( gpState ) {
        case GPS_LoggedIn:
        case GPS_Created:
        case GPS_NewUserLoggedIn:
            gpState = GPS_LoggedIn;
            OTDU_UNIMPLEMENTED_ONCE; // TODO: ONLINE (presence_set( 1 ))
            return kPollCreateOK;
        case GPS_LoginFailed:
        case GPS_CreateFailed:
            // Login refused: create account and login
            resetConnectNewAccount();
            bPollCreateArmed = true;
            pollStartTicks = GetTickCountMs();
            loginMode = LM_CreateAndLogin;
            if ( !startConnect() ) {
                gpState = GPS_LoginFailed;
                return kPollCreateFailed;
            }
            return kPollCreatePending;
        case GPS_NewUserFailed:
            gpState = GPS_LoginFailed;
            return kPollCreateFailed;
        default:
            break;
        }
        break;
    default:
        break;
    }
    return kPollCreatePending;
}

void edNetGamespyManager::setNick( const char *pNick ) {
    // FUN_0042dd40
    strncpy( nick, pNick, sizeof( nick ) - 1 );
}

void edNetGamespyManager::setEmail( const char *pEmail ) {
    // FUN_0042dd20
    strncpy( email, pEmail, sizeof( email ) - 1 );
}

void edNetGamespyManager::setPassword( const char *pPassword ) {
    // FUN_0042dd00
    strncpy( password, pPassword, sizeof( password ) - 1 );
}

void edNetGamespyManager::setCDKey( const char *pCDKey ) {
    // FUN_0042dce0
    strncpy( cdKey, pCDKey, sizeof( cdKey ) - 1 );
}

void edNetGamespyManager::denyFriendRequest() {
    // FUN_00866040
    OTDU_UNIMPLEMENTED;
}

void edNetGamespyManager::acceptFriendRequest() {
    // FUN_00866080
    OTDU_UNIMPLEMENTED;
}
