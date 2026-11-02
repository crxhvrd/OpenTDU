#include "shared.h"
#include "resource_orb_requests.h"

#include "service/action_service.h"
#include "service/proxy_service.h"
#include "tdu_server.h"

#include "core/ed_store.h"
#include "online/net/ed_net_config.h"
#include "online/net/ed_net_rpc.h"

#include <fstream>
#include <map>
#include <sstream>

namespace {
    static constexpr uint8_t kNetConfigFacility = 2;
    static constexpr uint32_t kNetConfigUserID = 0x494707d4;
    static constexpr const char *kNetConfigFileName = "netconfig.xml";

    static constexpr uint32_t kChunkSize = 1200;
    static constexpr uint32_t kMaxRetry = 5;
    static constexpr uint32_t kTimeoutMs = 1000;

    enum eORBError : uint8_t { OE_None = 0, OE_NotFound = 1 };

    struct OpenFile {
        uint32_t SessionID = 0u;
        std::string Content;
    };

    std::map<uint32_t, OpenFile> gOpenFiles;
    uint32_t gNextFileID = 1u;

    std::string LoadNetConfig( const Server &server ) {
        const std::string &path = server.getConfig().NetConfigPath;
        if ( !path.empty() ) {
            std::ifstream file( path, std::ios::binary );
            if ( file.is_open() ) {
                std::stringstream content;
                content << file.rdbuf();
                return content.str();
            }
            OTDU_LOG_WARN( "Failed to read '%s', serving the default "
                           "netconfig.xml\n",
                           path.c_str() );
        }
        return edNetConfig().toXml();
    }

    void WriteOpenReply( RPCCall &call,
                         const eORBError error,
                         const uint32_t fileID,
                         const uint32_t totalSize ) {
        call.Reply.updateCRC( edNetRPC::AOpenFile() );
        call.Reply.writeByte( error );
        call.Reply.writeInteger( int32_t( fileID ) );
        call.Reply.writeInteger( int32_t( totalSize ) );
        call.Reply.writeInteger( int32_t( error == OE_None ? kMaxRetry : 0 ) );
        call.Reply.writeInteger( int32_t( error == OE_None ? kTimeoutMs : 0 ) );
    }

    bool OnOpenFile( Server &server, RPCCall &call ) {
        const uint8_t facility = call.Request.readByte();
        const uint32_t userID = call.Request.readUInteger();
        char fileName[0x200] = {};
        call.Request.readStringNoAlloc( fileName, sizeof( fileName ) - 1 );
        const uint8_t bUpload = call.Request.readByte();
        call.Request.readUInteger(); // upload total size
        if ( call.Request.getStatus() != 0 ) {
            return false;
        }

        if ( bUpload != 0 || facility != kNetConfigFacility ||
             userID != kNetConfigUserID ||
             strcmp( fileName, kNetConfigFileName ) != 0 ) {
            OTDU_LOG_WARN( "r_open_file: '%s' (facility %u, user %08X, "
                           "upload %u) not available\n",
                           fileName, facility, userID, bUpload );
            WriteOpenReply( call, OE_NotFound, 0u, 0u );
            return true;
        }

        const uint32_t fileID = gNextFileID++;
        OpenFile &file = gOpenFiles[fileID];
        file.SessionID = call.Session.SessionID;
        file.Content = LoadNetConfig( server );
        // The client parses it as a C string.
        file.Content.push_back( '\0' );

        OTDU_LOG_INFO( "r_open_file: serving '%s' (%u bytes) to session "
                       "%08X\n",
                       fileName, uint32_t( file.Content.size() ),
                       call.Session.SessionID );
        WriteOpenReply( call, OE_None, fileID,
                        uint32_t( file.Content.size() ) );

        return true;
    }

    bool OnGetFile( RPCCall &call ) {
        const uint32_t fileID = call.Request.readUInteger();
        const uint32_t offset = call.Request.readUInteger();
        if ( call.Request.getStatus() != 0 ) {
            return false;
        }

        call.Reply.updateCRC( edNetRPC::AGetFile() );

        auto it = gOpenFiles.find( fileID );
        if ( it == gOpenFiles.end() ||
             it->second.SessionID != call.Session.SessionID ||
             offset > it->second.Content.size() ) {
            call.Reply.writeByte( OE_NotFound );
            call.Reply.writeInteger( int32_t( fileID ) );
            call.Reply.writeInteger( int32_t( offset ) );
            call.Reply.writeUShort( 0 );
            call.Reply.writeBuffer( nullptr, 0 );
            return true;
        }

        const std::string &content = it->second.Content;
        const uint32_t chunkSize =
            Min( uint32_t( content.size() ) - offset, kChunkSize );
        call.Reply.writeByte( OE_None );
        call.Reply.writeInteger( int32_t( fileID ) );
        call.Reply.writeInteger( int32_t( offset ) );
        call.Reply.writeUShort( 0 ); // next wait (ms)
        call.Reply.writeBuffer( content.data() + offset, chunkSize );

        if ( offset + chunkSize >= content.size() ) {
            gOpenFiles.erase( it );
        }
        return true;
    }
} // namespace

void RegisterResourceORBRequests( Server &server, ActionService &actions ) {
    actions.registerRPC(
        edNetRPC::ROpenFile(), "ednetresourceorb::r_open_file",
        [&server]( RPCCall &call ) { return OnOpenFile( server, call ); } );
    actions.registerRPC( edNetRPC::RGetFile(), "ednetresourceorb::r_getfile",
                         OnGetFile );
}
