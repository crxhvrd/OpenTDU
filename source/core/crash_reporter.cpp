#include "shared.h"
#include "crash_reporter.h"

#ifdef OTDU_CRASH_REPORTER
#include "crash_reporter_internal.h"

#include "core/arg_parser.h"

#include "zlib.h"

namespace CrashReporter {
    Context gContext;

    static constexpr const char *kCrashFolderName = "crashes";

    static bool gbTriggerTestCrash = false;
    static CmdLineArg CmdLineArgCrashTest( "crashtest", []( const char *pArg ) {
        gbTriggerTestCrash = true;
    } );

    namespace {
        struct ZipEntry {
            std::string Name;
            uint32_t Crc;
            uint32_t CompressedSize;
            uint32_t Size;
            uint32_t LocalHeaderOffset;
        };

        class ByteWriter {
        public:
            void u16( const uint16_t value ) {
                bytes.push_back( static_cast<uint8_t>( value & 0xff ) );
                bytes.push_back( static_cast<uint8_t>( value >> 8 ) );
            }

            void u32( const uint32_t value ) {
                u16( static_cast<uint16_t>( value & 0xffff ) );
                u16( static_cast<uint16_t>( value >> 16 ) );
            }

            void str( const std::string &value ) {
                bytes.insert( bytes.end(), value.begin(), value.end() );
            }

            std::vector<uint8_t> bytes;
        };

        struct DosDateTime {
            uint16_t Time;
            uint16_t Date;
        };

        DosDateTime GetDosDateTime() {
            DosDateTime dateTime = { 0, ( 1 << 5 ) | 1 };

            time_t rawTime = time( nullptr );
            struct tm *pLocalTime = localtime( &rawTime );
            if ( pLocalTime == nullptr || pLocalTime->tm_year < 80 ) {
                return dateTime;
            }

            dateTime.Time = static_cast<uint16_t>(
                ( pLocalTime->tm_hour << 11 ) | ( pLocalTime->tm_min << 5 ) |
                ( pLocalTime->tm_sec / 2 ) );
            dateTime.Date = static_cast<uint16_t>(
                ( ( pLocalTime->tm_year - 80 ) << 9 ) |
                ( ( pLocalTime->tm_mon + 1 ) << 5 ) | pLocalTime->tm_mday );
            return dateTime;
        }

        bool ReadWholeFile( const std::filesystem::path &filePath,
                            std::vector<uint8_t> &outContent ) {
            std::ifstream stream( filePath, std::ios::binary | std::ios::ate );
            if ( !stream ) {
                return false;
            }

            const std::streamoff fileSize = stream.tellg();
            if ( fileSize < 0 || fileSize >= std::streamoff( 0x7fffffff ) ) {
                return false;
            }

            outContent.resize( static_cast<size_t>( fileSize ) );
            stream.seekg( 0 );
            return outContent.empty() ||
                   static_cast<bool>( stream.read(
                       reinterpret_cast<char *>( outContent.data() ),
                       fileSize ) );
        }

        bool DeflateRaw( const std::vector<uint8_t> &input,
                         std::vector<uint8_t> &output ) {
            z_stream stream = {};
            if ( deflateInit2( &stream, Z_DEFAULT_COMPRESSION, Z_DEFLATED,
                               -MAX_WBITS, 8, Z_DEFAULT_STRATEGY ) != Z_OK ) {
                return false;
            }

            output.resize(
                deflateBound( &stream, static_cast<uLong>( input.size() ) ) );
            stream.next_in = const_cast<Bytef *>( input.data() );
            stream.avail_in = static_cast<uInt>( input.size() );
            stream.next_out = output.data();
            stream.avail_out = static_cast<uInt>( output.size() );

            const int result = deflate( &stream, Z_FINISH );
            output.resize( stream.total_out );
            deflateEnd( &stream );

            return result == Z_STREAM_END;
        }

        // Minimal (non-ZIP64) archive writer: every file of 'folder' (non
        // recursive) is deflated at the archive root.
        bool WriteZipArchive( const std::filesystem::path &folder,
                              const std::filesystem::path &archivePath ) {
            std::error_code errorCode;
            std::vector<std::filesystem::path> files;
            for ( const auto &entry :
                  std::filesystem::directory_iterator( folder, errorCode ) ) {
                if ( entry.is_regular_file( errorCode ) ) {
                    files.push_back( entry.path() );
                }
            }

            std::filesystem::path tmpArchivePath = archivePath;
            tmpArchivePath += ".tmp";

            std::ofstream archive( tmpArchivePath,
                                   std::ios::binary | std::ios::trunc );
            if ( !archive ) {
                return false;
            }

            const DosDateTime dateTime = GetDosDateTime();
            std::vector<ZipEntry> entries;
            uint32_t offset = 0;

            for ( const std::filesystem::path &filePath : files ) {
                std::vector<uint8_t> content;
                std::vector<uint8_t> compressed;
                if ( !ReadWholeFile( filePath, content ) ||
                     !DeflateRaw( content, compressed ) ) {
                    continue;
                }

                ZipEntry entry;
                entry.Name = filePath.filename().string();
                entry.Crc = static_cast<uint32_t>( crc32(
                    0, content.data(), static_cast<uInt>( content.size() ) ) );
                entry.CompressedSize =
                    static_cast<uint32_t>( compressed.size() );
                entry.Size = static_cast<uint32_t>( content.size() );
                entry.LocalHeaderOffset = offset;

                ByteWriter header;
                header.u32( 0x04034b50 );
                header.u16( 20 ); // Version needed to extract (2.0: deflate)
                header.u16( 0 );  // Flags
                header.u16( 8 );  // Method: deflate
                header.u16( dateTime.Time );
                header.u16( dateTime.Date );
                header.u32( entry.Crc );
                header.u32( entry.CompressedSize );
                header.u32( entry.Size );
                header.u16( static_cast<uint16_t>( entry.Name.size() ) );
                header.u16( 0 ); // Extra field length
                header.str( entry.Name );

                archive.write(
                    reinterpret_cast<const char *>( header.bytes.data() ),
                    header.bytes.size() );
                archive.write(
                    reinterpret_cast<const char *>( compressed.data() ),
                    compressed.size() );

                const uint64_t nextOffset = uint64_t( offset ) +
                                            header.bytes.size() +
                                            compressed.size();
                if ( nextOffset > 0xffffffffull ) {
                    return false;
                }
                offset = static_cast<uint32_t>( nextOffset );
                entries.push_back( entry );
            }

            ByteWriter centralDirectory;
            for ( const ZipEntry &entry : entries ) {
                centralDirectory.u32( 0x02014b50 );
                centralDirectory.u16( 20 ); // Version made by
                centralDirectory.u16( 20 ); // Version needed to extract
                centralDirectory.u16( 0 );
                centralDirectory.u16( 8 );
                centralDirectory.u16( dateTime.Time );
                centralDirectory.u16( dateTime.Date );
                centralDirectory.u32( entry.Crc );
                centralDirectory.u32( entry.CompressedSize );
                centralDirectory.u32( entry.Size );
                centralDirectory.u16(
                    static_cast<uint16_t>( entry.Name.size() ) );
                centralDirectory.u16( 0 ); // Extra field length
                centralDirectory.u16( 0 ); // Comment length
                centralDirectory.u16( 0 ); // Disk number start
                centralDirectory.u16( 0 ); // Internal attributes
                centralDirectory.u32( 0 ); // External attributes
                centralDirectory.u32( entry.LocalHeaderOffset );
                centralDirectory.str( entry.Name );
            }

            ByteWriter endOfCentralDirectory;
            endOfCentralDirectory.u32( 0x06054b50 );
            endOfCentralDirectory.u16( 0 );
            endOfCentralDirectory.u16( 0 );
            endOfCentralDirectory.u16(
                static_cast<uint16_t>( entries.size() ) );
            endOfCentralDirectory.u16(
                static_cast<uint16_t>( entries.size() ) );
            endOfCentralDirectory.u32(
                static_cast<uint32_t>( centralDirectory.bytes.size() ) );
            endOfCentralDirectory.u32( offset );
            endOfCentralDirectory.u16( 0 ); // Comment length

            archive.write(
                reinterpret_cast<const char *>( centralDirectory.bytes.data() ),
                centralDirectory.bytes.size() );
            archive.write( reinterpret_cast<const char *>(
                               endOfCentralDirectory.bytes.data() ),
                           endOfCentralDirectory.bytes.size() );
            archive.close();

            if ( !archive || entries.empty() ) {
                std::filesystem::remove( tmpArchivePath, errorCode );
                return false;
            }

            std::filesystem::rename( tmpArchivePath, archivePath, errorCode );
            return !errorCode;
        }

        std::string BuildFolderURL( const std::filesystem::path &folder ) {
#if defined( OTDU_WIN32 )
            return folder.u8string();
#else
            static constexpr const char *kHexDigits = "0123456789ABCDEF";

            std::string url = "file://";
            for ( const char character : folder.u8string() ) {
                const unsigned char byte =
                    static_cast<unsigned char>( character );
                if ( isalnum( byte ) ||
                     strchr( "/-_.~", character ) != nullptr ) {
                    url += character;
                } else {
                    url += '%';
                    url += kHexDigits[byte >> 4];
                    url += kHexDigits[byte & 0xf];
                }
            }
            return url;
#endif
        }

        void ShowCrashNotice( const std::filesystem::path &archivePath ) {
            const std::string message =
                "OpenTDU crashed during the previous session.\n\n"
                "A crash report was saved to:\n" +
                archivePath.u8string() +
                "\n\nPlease attach this file when reporting the issue.";

            enum eButton : int { B_Ok = 0, B_OpenFolder = 1 };

            const SDL_MessageBoxButtonData buttons[] = {
                {SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT |
                      SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, B_Ok,         "OK"         },
                {0,                                                             B_OpenFolder, "Open folder"},
            };

            SDL_MessageBoxData messageBox = {};
            messageBox.flags = SDL_MESSAGEBOX_WARNING;
            messageBox.title = "OpenTDU";
            messageBox.message = message.c_str();
            messageBox.numbuttons = SDL_arraysize( buttons );
            messageBox.buttons = buttons;

            int pressedButton = B_Ok;
            if ( !SDL_ShowMessageBox( &messageBox, &pressedButton ) ) {
                OTDU_LOG_ALWAYS(
                    "Previous session crashed; report saved to '%s'\n",
                    archivePath.u8string().c_str() );
                return;
            }

            if ( pressedButton == B_OpenFolder ) {
                SDL_OpenURL(
                    BuildFolderURL( archivePath.parent_path() ).c_str() );
            }
        }
    } // namespace

    void ProcessPendingReports() {
        std::error_code errorCode;
        const std::filesystem::path crashFolder =
            std::filesystem::absolute( kCrashFolderName, errorCode );
        if ( errorCode ||
             !std::filesystem::is_directory( crashFolder, errorCode ) ) {
            return;
        }

        std::vector<std::filesystem::path> pendingReports;
        for ( const auto &entry :
              std::filesystem::directory_iterator( crashFolder, errorCode ) ) {
            if ( entry.is_directory( errorCode ) ) {
                pendingReports.push_back( entry.path() );
            }
        }
        std::sort( pendingReports.begin(), pendingReports.end() );

        std::filesystem::path latestArchive;
        for ( const std::filesystem::path &reportFolder : pendingReports ) {
            std::filesystem::path archivePath = reportFolder;
            archivePath += ".zip";

            if ( !WriteZipArchive( reportFolder, archivePath ) ) {
                continue;
            }

            std::filesystem::remove_all( reportFolder, errorCode );
            latestArchive = archivePath;
        }

        if ( !latestArchive.empty() ) {
            ShowCrashNotice( latestArchive );
        }
    }

    void Install( const char *pLogFilePath,
                  const char **pCmdLineArgs,
                  const int32_t argCount ) {
        std::error_code errorCode;
        gContext.CrashFolder =
            std::filesystem::absolute( kCrashFolderName, errorCode );
        if ( errorCode ) {
            return;
        }

        if ( pLogFilePath != nullptr && pLogFilePath[0] != '\0' ) {
            gContext.LogFilePath =
                std::filesystem::absolute( pLogFilePath, errorCode );
        }

        for ( int32_t argIdx = 1; argIdx < argCount; argIdx++ ) {
            gContext.CommandLine += pCmdLineArgs[argIdx];
            gContext.CommandLine += ' ';
        }

        InstallPlatformHandlers();

        if ( gbTriggerTestCrash ) {
            volatile int32_t *pInvalidAddress = nullptr;
            *pInvalidAddress = 0;
        }
    }
} // namespace CrashReporter
#endif
