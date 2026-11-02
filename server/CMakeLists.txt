# OpenTDU online server
set( OTDU_SERVER_FOLDER "${OTDU_BASE_FOLDER}/server" )

set( OTDU_NET_SHARED_SRC "${OTDU_SRC_FOLDER}/core/arg_parser.cpp"
                         "${OTDU_SRC_FOLDER}/core/logger.cpp"
                         "${OTDU_SRC_FOLDER}/core/ed_store.cpp"
                         "${OTDU_SRC_FOLDER}/core/crypto/blowfish.cpp"
                         "${OTDU_SRC_FOLDER}/online/net/ed_net_socket.cpp"
                         "${OTDU_SRC_FOLDER}/online/net/ed_net_proxy_protocol.cpp"
                         "${OTDU_SRC_FOLDER}/online/net/ed_net_rpc.cpp"
                         "${OTDU_SRC_FOLDER}/online/net/ed_net_proxy.cpp"
                         "${OTDU_SRC_FOLDER}/online/net/ed_net_action.cpp"
                         "${OTDU_SRC_FOLDER}/online/net/ed_net_netbuffer.cpp"
                         "${OTDU_SRC_FOLDER}/online/net/ed_net_resource_orb.cpp"
                         "${OTDU_SRC_FOLDER}/online/net/ed_net_config.cpp"
                         "${OTDU_SRC_FOLDER}/core/markup.cpp"
                         "${OTDU_SRC_FOLDER}/core/hash/sha256.cpp" )

set( OTDU_SERVER_SRC "${OTDU_SERVER_FOLDER}/entry_point.cpp"
                     "${OTDU_SERVER_FOLDER}/tdu_server.cpp"
                     "${OTDU_SERVER_FOLDER}/tdu_server.h"
                     "${OTDU_SERVER_FOLDER}/service/proxy_service.cpp"
                     "${OTDU_SERVER_FOLDER}/service/proxy_service.h"
                     "${OTDU_SERVER_FOLDER}/service/action_service.cpp"
                     "${OTDU_SERVER_FOLDER}/service/action_service.h"
                     "${OTDU_SERVER_FOLDER}/rpc/core_requests.cpp"
                     "${OTDU_SERVER_FOLDER}/rpc/core_requests.h"
                     "${OTDU_SERVER_FOLDER}/rpc/debug_requests.cpp"
                     "${OTDU_SERVER_FOLDER}/rpc/debug_requests.h"
                     "${OTDU_SERVER_FOLDER}/rpc/identity_requests.cpp"
                     "${OTDU_SERVER_FOLDER}/rpc/identity_requests.h"
                     "${OTDU_SERVER_FOLDER}/database/database.cpp"
                     "${OTDU_SERVER_FOLDER}/database/database.h"
                     "${OTDU_SERVER_FOLDER}/rpc/database_requests.cpp"
                     "${OTDU_SERVER_FOLDER}/rpc/database_requests.h"
                     "${OTDU_SERVER_FOLDER}/rpc/resource_orb_requests.cpp"
                     "${OTDU_SERVER_FOLDER}/rpc/resource_orb_requests.h" )

add_library( otdu_sqlite STATIC "${OTDU_THIRDPARTY_FOLDER}/sqlite/sqlite3.c" )
set_property( TARGET otdu_sqlite PROPERTY FOLDER "Server" )
target_include_directories( otdu_sqlite PUBLIC "${OTDU_THIRDPARTY_FOLDER}/sqlite" )
target_compile_definitions( otdu_sqlite PRIVATE SQLITE_OMIT_LOAD_EXTENSION SQLITE_THREADSAFE=1 SQLITE_DQS=0 )
if ( NOT MSVC )
    target_compile_options( otdu_sqlite PRIVATE -w )
endif()

add_executable( opentdu_server ${OTDU_SERVER_SRC} ${OTDU_NET_SHARED_SRC} )
target_link_libraries( opentdu_server otdu_sqlite )
set_property( TARGET opentdu_server PROPERTY FOLDER "Server" )
target_include_directories( opentdu_server PRIVATE "${OTDU_SERVER_FOLDER}" )
target_compile_definitions( opentdu_server PRIVATE OTDU_LOG_STDOUT OTDU_SERVER )

if ( WIN32 )
    target_link_libraries( opentdu_server ws2_32 mswsock winmm Pathcch Shlwapi )
    set_target_properties( opentdu_server PROPERTIES OUTPUT_NAME "opentdu_server_${CMAKE_GENERATOR_PLATFORM}" )
elseif ( APPLE )
    set_target_properties( opentdu_server PROPERTIES OUTPUT_NAME "opentdu_server_darwin" )
else()
    find_package( Threads REQUIRED )
    target_link_libraries( opentdu_server Threads::Threads )
    set_target_properties( opentdu_server PROPERTIES OUTPUT_NAME "opentdu_server_unix" )
endif()

# Test client
add_executable( opentdu_netprobe "${OTDU_SERVER_FOLDER}/probe/main.cpp" ${OTDU_NET_SHARED_SRC} )
target_include_directories( opentdu_netprobe PRIVATE "${OTDU_SERVER_FOLDER}" )
set_property( TARGET opentdu_netprobe PROPERTY FOLDER "Server" )
target_compile_definitions( opentdu_netprobe PRIVATE OTDU_LOG_STDOUT )

if ( WIN32 )
    target_link_libraries( opentdu_netprobe ws2_32 mswsock winmm Pathcch Shlwapi )
elseif ( NOT APPLE )
    target_link_libraries( opentdu_netprobe Threads::Threads )
endif()
