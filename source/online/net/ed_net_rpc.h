#pragma once

namespace edNetRPC {
    uint16_t MessageID( const char *pSignature );

    // edNetServiceHandlerAction
    uint16_t RCallAction();
    uint16_t RCallActionUnreliable();
    uint16_t ACallAction();
    uint16_t ACallActionWithNetBufferID();
    uint16_t ECallActionAck();

    // edNetServiceNetBuffer
    uint16_t RGetNetBuffer();
    uint16_t AGetNetBuffer();
    uint16_t EGetNetBufferAck();

    // edNetServiceHandlerEcho
    uint16_t REcho();
    uint16_t AEcho();

    // coreresquests service discovery
    uint16_t RGetRequestHandlersEx();
    uint16_t AGetRequestHandlersEx();

    // Database (USERDATABASE handler): login
    uint16_t RLoginPC();
    uint16_t ALogin();
    uint16_t RNewGame();

    // edNetResourceORB (PTS/PUS storage handlers): file transfer
    uint16_t ROpenFile();
    uint16_t AOpenFile();
    uint16_t RGetFile();
    uint16_t AGetFile();

    // Stats / Savegame (sent right after login, never waited on)
    uint16_t EUserStatisticsUpdateV2();
    uint16_t UserStatisticsV1();
    uint16_t SavegameRCheck();
    uint16_t SavegameACheck();
    uint16_t SavegameRSave();
    uint16_t SavegameASave();
    uint16_t UserStatisticsExtraDatasV1();

    static constexpr uint32_t kMaxRequestHandlers = 128;

    // Signatures requested by the client during service discovery, in slot
    // order (slot 22 is shared by CHECK_SERVER and SAVEGAME_SERVER).
    static constexpr const char *kRequestHandlerSignatures[] = {
        "coreresquests::r_log(string server_name,string hostname_in,string "
        "txt)",
        "coreresquests::r_gettime()",
        "gpstracker::r_get_density_map_by_level(byte level)",
        "Database::r_login(string,string,uint,ulonglong)"
        "WithEbayStatusAndTeamSpotHashAndRegion",
        "coreresquests::e_update_user(uint user_id_in, uint team_id_in, string "
        "gamertag_in, int nat_type, ULONGLONG xuid, short client_type, float "
        "coord_x_in, float coord_z_in, int level_in, int car_type_in, uint "
        "game_id_in, edU64 car_cat)",
        "coreresquests::team_r_team_add(uint user_id_in, STeamDetails "
        "team_details_in)",
        "News::r_getnews(uint,uint)",
        "coreresquests::r_create_replay_stream(uint user_id_in,string "
        "user_name_in,string stream_name_in,object playerdata = NULL)",
        "Actors::r_cardealer_server()",
        "Stats::r_stats_server()",
        "PUS/TDU/0",
        "PUS/TDU/1",
        "PUS/TDU/2",
        "PUS/TDU/3",
        "PUS/TDU/4",
        "PUS/TDU/5",
        "PUS/TDU/6",
        "PUS/TDU/7",
        "PUS/TDU/8",
        "PUS/TDU/9",
        "PTS/TDU",
        "Ranking::r_ranking_server()",
        "Check::r_check_phys(int,int,int)",
    };
    static constexpr uint32_t kNumRequestHandlerSignatures =
        sizeof( kRequestHandlerSignatures ) / sizeof( const char * );
} // namespace edNetRPC
