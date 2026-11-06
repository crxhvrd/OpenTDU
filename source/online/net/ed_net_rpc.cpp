#include "shared.h"
#include "ed_net_rpc.h"

#include "core/hash/crc32.h"

#define OTDU_NET_MESSAGE_ID( name, signature )                                 \
    uint16_t name() {                                                          \
        static const uint16_t kID = MessageID( signature );                    \
        return kID;                                                            \
    }

namespace edNetRPC {
    uint16_t MessageID( const char *pSignature ) {
        return uint16_t( TestDriveCRC32( pSignature ) );
    }

    OTDU_NET_MESSAGE_ID(
        RCallAction,
        "edNetServiceHandlerAction::r_callaction(uint,uint,object)" )
    OTDU_NET_MESSAGE_ID(
        RCallActionUnreliable,
        "edNetServiceHandlerAction::r_callaction_unreliable(object)" )
    OTDU_NET_MESSAGE_ID(
        ACallAction,
        "edNetServiceHandlerAction::a_callaction(uint,object=NULL)" )
    OTDU_NET_MESSAGE_ID( ACallActionWithNetBufferID,
                         "edNetServiceHandlerAction::a_callaction_with_"
                         "netbufferid(uint,uint,object=NULL)" )
    OTDU_NET_MESSAGE_ID( ECallActionAck,
                         "edNetServiceHandlerAction::e_callaction_ack(uint)" )

    OTDU_NET_MESSAGE_ID(
        RGetNetBuffer,
        "edNetServiceNetBuffer::r_get_ednetbuffer(uint bufferid,uint offset)" )
    OTDU_NET_MESSAGE_ID(
        AGetNetBuffer,
        "edNetServiceNetBuffer::a_get_ednetbuffer(uint bufferid,byte "
        "errcode,{uint offset,uint totalsize,ushort buffer_crc,buffer}=NULL)" )
    OTDU_NET_MESSAGE_ID(
        EGetNetBufferAck,
        "edNetServiceNetBuffer::e_get_ednetbuffer_ack(uint bufferid)" )

    OTDU_NET_MESSAGE_ID(
        RLoginPC,
        "Database::r_login_pc(string,string,uint,ulonglong,byte,string)" )
    OTDU_NET_MESSAGE_ID( ALogin, "Database::a_login(byte,uint,uint)" )
    OTDU_NET_MESSAGE_ID( RNewGame, "Database::r_newgame(uint)" )

    OTDU_NET_MESSAGE_ID(
        ROpenFile,
        "ednetresourceorb::r_open_file(byte facility,uint user_id,string "
        "filename,byte bupdload_wanted,ed32 uploadtotalsize)" )
    OTDU_NET_MESSAGE_ID(
        AOpenFile,
        "ednetresourceorb::a_open_file(byte errcode,uint fileid,uint "
        "totalsize,uint maxretry,uint timeout)" )
    OTDU_NET_MESSAGE_ID(
        RGetFile, "ednetresourceorb::r_getfile(uint fileid,uint offset)" )
    OTDU_NET_MESSAGE_ID(
        AGetFile,
        "ednetresourceorb::a_getfile(byte errcode,uint fileid,uint "
        "offset,ushort nexttimewait,buffer datas=NULL)" )

    OTDU_NET_MESSAGE_ID(
        EUserStatisticsUpdateV2,
        "Stats::e_user_statistics_update_v2(object userrights)" )
    OTDU_NET_MESSAGE_ID( UserStatisticsV1, "Stats::user_statistics_v1()" )
    OTDU_NET_MESSAGE_ID( SavegameRCheck, "Savegame::r_check()" )
    OTDU_NET_MESSAGE_ID( SavegameACheck, "Savegame::a_check()" )
    OTDU_NET_MESSAGE_ID( SavegameRSave, "Savegame::r_save()" )
    OTDU_NET_MESSAGE_ID( SavegameASave, "Savegame::a_save()" )
    OTDU_NET_MESSAGE_ID( UserStatisticsExtraDatasV1,
                         "Stats::user_statistics_extra_datas_v1()" )

    OTDU_NET_MESSAGE_ID( REcho, "edNetServiceHandlerEcho::r_echo()" )
    OTDU_NET_MESSAGE_ID( AEcho, "edNetServiceHandlerEcho::a_echo()" )

    OTDU_NET_MESSAGE_ID(
        RGetRequestHandlersEx,
        "coreresquests::r_get_request_handlers_ex(uint user_id_in,ulonglong "
        "session_id_in,ushort "
        "request_crc_in[COREREQUESTS_MAX_GETRPC_HANDLERS_EX],object "
        "context=NULL)" )
    OTDU_NET_MESSAGE_ID(
        AGetRequestHandlersEx,
        "coreresquests::a_get_request_handlers_ex((ushort request_crc_in,uint "
        "ip_address,ushort port)[COREREQUESTS_MAX_GETRPC_HANDLERS_EX])" )
} // namespace edNetRPC
