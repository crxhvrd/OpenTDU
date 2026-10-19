#include "shared.h"
#include "radio_station.h"

#include "audio/radio/radio_stream_player.h"
#include "filesystem/file_direct_access.h"

#include <filesystem>

#if defined( OTDU_WIN32 )
#include "system/system_shared.h"

static constexpr char kPathSeparator = '\\';
#else
static constexpr char kPathSeparator = '/';
#endif

static constexpr size_t kMaxTokenLength = 129;       // local_108[130]
static constexpr float kRandToUnit = 3.0518509e-05f; // DAT_00f41714
static constexpr int32_t kMaxPlaylistLineLength = 0x104;

static const char16_t *FindWideChar( const char16_t *pString,
                                     const char16_t character ) {
    // wcschr
    for ( ; *pString != u'\0'; pString++ ) {
        if ( *pString == character ) {
            return pString;
        }
    }
    return nullptr;
}

static const char16_t *SkipToNextLine( const char16_t *pCursor ) {
    const char16_t *pNewLine = FindWideChar( pCursor, u'\n' );
    return ( pNewLine != nullptr ) ? pNewLine + 1 : nullptr;
}

static size_t
WideToMultiByte( char *pDest, const char16_t *pSource, const size_t maxCount ) {
    // wcstombs (MSVC "C" locale: code units above 0xff do not convert)
    size_t count = 0;
    while ( count < maxCount ) {
        const char16_t character = pSource[count];
        if ( character > 0xff ) {
            return static_cast<size_t>( -1 );
        }

        pDest[count] = static_cast<char>( character );
        if ( character == u'\0' ) {
            return count;
        }
        count++;
    }
    return count;
}

static int32_t ScanWideInteger( const char16_t *pString ) {
    char buffer[32] = {};
    for ( size_t i = 0; i < sizeof( buffer ) - 1 && pString[i] != u'\0'; i++ ) {
        buffer[i] =
            ( pString[i] <= 0x7f ) ? static_cast<char>( pString[i] ) : '?';
    }

    int32_t value = 0;
    sscanf( buffer, "%i", &value );
    return value;
}

static const char16_t *ParseQuotedToken( const char16_t *pCursor,
                                         char16_t *pOutToken ) {
    // FUN_007c7480
    const char16_t *pBegin = FindWideChar( pCursor, u'"' );
    if ( pBegin == nullptr ) {
        return nullptr;
    }
    pBegin++;

    const char16_t *pEnd = FindWideChar( pBegin, u'"' );
    if ( pEnd == nullptr ) {
        return nullptr;
    }

    const size_t length =
        Min( static_cast<size_t>( pEnd - pBegin ), kMaxTokenLength );
    memcpy( pOutToken, pBegin, length * sizeof( char16_t ) );
    pOutToken[length] = u'\0';

    return pEnd + 1;
}

static const char *
ReadLine( const char *pCursor, char *pOutLine, const int32_t maxLength ) {
    // FUN_007c6c60
    while ( *pCursor != '\0' &&
            isgraph( static_cast<uint8_t>( *pCursor ) ) == 0 ) {
        pCursor++;
    }

    const char *pLineEnd = pCursor;
    while ( *pLineEnd != '\0' && *pLineEnd != '\n' ) {
        pLineEnd++;
    }

    if ( pOutLine != nullptr ) {
        int32_t length = static_cast<int32_t>( pLineEnd - pCursor ) - 1;
        length = Clamp( length, 0, maxLength );
        memcpy( pOutLine, pCursor, static_cast<size_t>( length ) );
        pOutLine[length] = '\0';
    }

    return pLineEnd;
}

static bool IsPlaylistFile( const char *pFilename ) {
    // FUN_007c6d40
    char filename[261] = {};
    strncpy( filename, pFilename, 0x104 );
    for ( char *pCharacter = filename; *pCharacter != '\0'; pCharacter++ ) {
        *pCharacter =
            static_cast<char>( toupper( static_cast<uint8_t>( *pCharacter ) ) );
    }
    return strstr( filename, ".M3U" ) != nullptr;
}

static void ResolveShortcut( char *pFilePath ) {
    // FUN_007c6da0
#if defined( OTDU_WIN32 )
    const HRESULT comResult =
        CoInitializeEx( nullptr, COINIT_APARTMENTTHREADED );

    IShellLinkA *pShellLink = nullptr;
    if ( SUCCEEDED( CoCreateInstance( CLSID_ShellLink, nullptr,
                                      CLSCTX_INPROC_SERVER, IID_IShellLinkA,
                                      (void **)&pShellLink ) ) ) {
        IPersistFile *pPersistFile = nullptr;
        if ( SUCCEEDED( pShellLink->QueryInterface(
                 IID_IPersistFile, (void **)&pPersistFile ) ) ) {
            WCHAR widePath[MAX_PATH];
            if ( MultiByteToWideChar( CP_ACP, 0, pFilePath, -1, widePath,
                                      MAX_PATH ) > 0 ) {
                WIN32_FIND_DATAA findData;
                if ( SUCCEEDED( pPersistFile->Load( widePath, STGM_READ ) ) &&
                     SUCCEEDED( pShellLink->GetPath(
                         pFilePath, MAX_PATH, &findData, SLGP_SHORTPATH ) ) ) {
                    GetLongPathNameA( pFilePath, pFilePath, MAX_PATH );
                }
            }
            pPersistFile->Release();
        }
        pShellLink->Release();
    }

    if ( SUCCEEDED( comResult ) ) {
        CoUninitialize();
    }
#else
    // Windows shortcuts are not supported outside Windows.
    (void)pFilePath;
#endif
}

static void
GetFileTitle( const char *pFilePath, char *pOutTitle, const size_t maxLength ) {
    // GetFileTitleA
    const std::string title =
        std::filesystem::path( pFilePath ).stem().string();
    strncpy( pOutTitle, title.c_str(), maxLength );
}

struct ID3v1Tag {
    char Artist[31];
    char Album[31];
    char Title[31];
    bool bValid;
};

static void ReadID3v1Tag( FileDirectAccess &file, ID3v1Tag &tag ) {
    // FUN_007c7e30
    static constexpr uint32_t kTagSize = 0x80;
    static constexpr size_t kFieldLength = 30;

    tag.bValid = false;

    char rawTag[kTagSize];
    if ( !file.seek( kTagSize, true ) ||
         file.readFromFile( rawTag, kTagSize ) != kTagSize ||
         strncmp( rawTag, "TAG", 3 ) != 0 ) {
        return;
    }

    memcpy( tag.Title, rawTag + 3, kFieldLength );
    memcpy( tag.Artist, rawTag + 33, kFieldLength );
    memcpy( tag.Album, rawTag + 63, kFieldLength );
    tag.Title[kFieldLength] = '\0';
    tag.Artist[kFieldLength] = '\0';
    tag.Album[kFieldLength] = '\0';
    tag.bValid = true;
}

static bool ReadID3v1( FileDirectAccess &file, RadioTrack &track ) {
    // FUN_007c6e60
    ID3v1Tag tag;
    ReadID3v1Tag( file, tag );
    if ( !tag.bValid ) {
        return false;
    }

    strncpy( track.Title, tag.Title, RadioStation::kMaxNameLength );
    strncpy( track.Artist, tag.Artist, RadioStation::kMaxNameLength );
    return true;
}

static int32_t DecodeSyncSafeInteger( const uint8_t *pBytes ) {
    // FUN_0042b140
    int32_t value = 0;
    int32_t shift = 0;
    for ( int32_t i = 3; i > 0; i--, shift += 7 ) {
        if ( ( pBytes[i] & 0x80 ) != 0 ) {
            return -1;
        }
        value += int32_t( pBytes[i] ) << shift;
    }
    return value;
}

struct ID3v2Reader {
    FileDirectAccess *pFile = nullptr;
    uint32_t FramesOffset = 0;
    int8_t Version = 0;
    int32_t TagSize = 0;
    char FrameID[4] = {};
    int32_t FrameSize = 0;
    uint8_t FrameFlags[2] = {};
    bool bValid = false;

    void open( FileDirectAccess &file );
    bool readTitle( char *pOutTitle, const int32_t maxLength );
    bool readArtist( char *pOutArtist, const int32_t maxLength );

private:
    bool readHeader();
    bool readFrameHeaderV22();
    bool readFrameHeaderV23();
    bool findFrame( const char *pFrameID );
    bool readTextFrame( char *pOutText, const int32_t maxLength );

    inline bool useV22Frames() const { return !bValid || Version < 3; }
};

bool ID3v2Reader::readHeader() {
    // FUN_007c79f0
    char magic[3];
    if ( pFile->readFromFile( magic, 3 ) != 3 ||
         ( strncmp( magic, "ID3", 3 ) != 0 &&
           strncmp( magic, "3DI", 3 ) != 0 ) ) {
        return false;
    }

    if ( pFile->readFromFile( &Version, 1 ) != 1 || Version > 4 ) {
        return false;
    }

    uint8_t flags = 0;
    uint8_t sizeBytes[4];
    if ( !pFile->seek( int32_t( pFile->tell() ) + 1 ) ||
         pFile->readFromFile( &flags, 1 ) != 1 ||
         pFile->readFromFile( sizeBytes, 4 ) != 4 ) {
        return false;
    }

    TagSize = DecodeSyncSafeInteger( sizeBytes );
    if ( TagSize < 0 ) {
        return false;
    }

    if ( ( flags & 2 ) == 0 ) {
        return true;
    }

    uint8_t extendedSizeBytes[4];
    if ( pFile->readFromFile( extendedSizeBytes, 4 ) != 4 ) {
        return false;
    }

    const int32_t extendedSize = DecodeSyncSafeInteger( extendedSizeBytes );
    return extendedSize >= 0 && pFile->seek( extendedSize );
}

void ID3v2Reader::open( FileDirectAccess &file ) {
    // FUN_007c7ce0
    pFile = &file;
    memset( FrameID, 0, sizeof( FrameID ) );
    FrameSize = 0;
    memset( FrameFlags, 0, sizeof( FrameFlags ) );

    bValid = pFile->seek( 0 ) && readHeader();
    if ( !bValid ) {
        bValid = pFile->seek( 10, true ) && readHeader();
        if ( bValid ) {
            bValid = pFile->seek( TagSize + 0xc, true );
        }
    }

    if ( bValid ) {
        FramesOffset = pFile->tell();
    }
}

bool ID3v2Reader::readFrameHeaderV22() {
    // FUN_007c7920
    FrameID[3] = '\0';

    int8_t sizeBytes[3];
    if ( pFile->readFromFile( FrameID, 3 ) != 3 ||
         pFile->readFromFile( sizeBytes, 3 ) != 3 ) {
        return false;
    }

    FrameSize = ( ( ( int32_t( sizeBytes[0] ) << 8 ) + sizeBytes[1] ) << 8 ) +
                sizeBytes[2];
    return FrameSize >= 0;
}

bool ID3v2Reader::readFrameHeaderV23() {
    // FUN_007c7980
    uint8_t sizeBytes[4];
    if ( pFile->readFromFile( FrameID, 4 ) != 4 ||
         pFile->readFromFile( sizeBytes, 4 ) != 4 ) {
        return false;
    }

    // Retail decodes v2.3 frame sizes as sync-safe too.
    FrameSize = DecodeSyncSafeInteger( sizeBytes );
    if ( FrameSize < 0 ) {
        return false;
    }

    return pFile->readFromFile( FrameFlags, 2 ) == 2;
}

bool ID3v2Reader::findFrame( const char *pFrameID ) {
    // FUN_007c7b40
    if ( !bValid || !pFile->seek( int32_t( FramesOffset ) ) ) {
        return false;
    }

    const bool bV22Frames = useV22Frames();
    while ( bV22Frames ? readFrameHeaderV22() : readFrameHeaderV23() ) {
        if ( memcmp( FrameID, pFrameID, sizeof( FrameID ) ) == 0 ) {
            return true;
        }

        if ( !pFile->seek( int32_t( pFile->tell() ) + FrameSize ) ) {
            return false;
        }
    }
    return false;
}

bool ID3v2Reader::readTextFrame( char *pOutText, const int32_t maxLength ) {
    // FUN_007c7af0
    const int32_t length = FrameSize - 1;
    if ( length < 0 || length >= maxLength ) {
        return false;
    }

    // Only ISO-8859-1 (0) and UTF-8 (3) are read; the bytes are kept as is.
    uint8_t encoding = 0;
    if ( pFile->readFromFile( &encoding, 1 ) != 1 ||
         ( encoding != 0 && encoding != 3 ) ) {
        return false;
    }

    if ( pFile->readFromFile( pOutText, uint32_t( length ) ) !=
         uint32_t( length ) ) {
        return false;
    }

    pOutText[length] = '\0';
    return true;
}

bool ID3v2Reader::readTitle( char *pOutTitle, const int32_t maxLength ) {
    // FUN_007c7bf0
    const char pFrameID[4] = { 'T', useV22Frames() ? 'T' : 'I',
                               useV22Frames() ? '2' : 'T',
                               useV22Frames() ? '\0' : '2' };
    return findFrame( pFrameID ) && readTextFrame( pOutTitle, maxLength );
}

bool ID3v2Reader::readArtist( char *pOutArtist, const int32_t maxLength ) {
    // FUN_007c7c90
    const char pFrameID[4] = { 'T', 'P', useV22Frames() ? '1' : 'E',
                               useV22Frames() ? '\0' : '1' };
    return findFrame( pFrameID ) && readTextFrame( pOutArtist, maxLength );
}

static bool ReadID3v2( FileDirectAccess &file, RadioTrack &track ) {
    // FUN_007c6ee0
    ID3v2Reader reader;
    reader.open( file );
    if ( !reader.bValid ||
         !reader.readTitle( track.Title,
                            int32_t( RadioStation::kMaxNameLength ) ) ) {
        return false;
    }

    reader.readArtist( track.Artist, int32_t( RadioStation::kMaxNameLength ) );
    return true;
}

RadioStation::RadioStation()
    : Name{}, NumTracks( 0 ), NumTracksLeft( 0 ), CurrentTrack( 0 ), Tracks{} {
    // FUN_007c7860
}

RadioStation::~RadioStation() {
    // FUN_0042b180
}

void RadioStation::loadPlaylist( const void *pStationData,
                                 const uint32_t stationDataSize,
                                 const char *pWorkingDir ) {
    // FUN_007c7540
    std::u16string stationText( stationDataSize / sizeof( char16_t ), u'\0' );
    memcpy( &stationText[0], pStationData,
            stationText.size() * sizeof( char16_t ) );

    // Skip BOM
    const char16_t *pCursor = stationText.c_str() + 1;

    char16_t token[kMaxTokenLength + 1] = {};
    const char16_t *pNext = ParseQuotedToken( pCursor, token );
    WideToMultiByte( Name, token, kMaxNameLength );

    const char16_t *pLine =
        ( pNext != nullptr ) ? SkipToNextLine( pNext ) : nullptr;
    if ( pLine == nullptr ) {
        return;
    }

    const int32_t numTracks = ScanWideInteger( pLine );
    for ( int32_t i = 0; i < numTracks; i++ ) {
        RadioTrack track = {};
        char filename[261] = {};

        pLine = SkipToNextLine( pLine );
        if ( pLine == nullptr ) {
            break;
        }

        pNext = ParseQuotedToken( pLine, token );
        if ( pNext == nullptr ) {
            break;
        }
        WideToMultiByte( filename, token, kMaxNameLength );
        snprintf( track.Path, sizeof( track.Path ), "%s%s/%s", pWorkingDir,
                  Name, filename );

        pNext = ParseQuotedToken( pNext, token );
        if ( pNext == nullptr ) {
            break;
        }
        WideToMultiByte( track.Title, token, kMaxNameLength );

        pLine = ParseQuotedToken( pNext, token );
        if ( pLine == nullptr ) {
            break;
        }
        WideToMultiByte( track.Artist, token, kMaxNameLength );

        addTrack( track );
    }
}

void RadioStation::loadFolder( const char *pName, const char *pFolder ) {
    // FUN_007c72b0
    strncpy( Name, pName, kMaxNameLength );

    std::error_code errorCode;
    for ( const std::filesystem::directory_entry &entry :
          std::filesystem::directory_iterator( pFolder, errorCode ) ) {
        if ( entry.is_directory( errorCode ) ) {
            continue;
        }

        const std::string filename = entry.path().filename().string();

        char filePath[261] = {};
        snprintf( filePath, sizeof( filePath ), "%s%c%s", pFolder,
                  kPathSeparator, filename.c_str() );

        ResolveShortcut( filePath );

        if ( IsPlaylistFile( filename.c_str() ) ) {
            addPlaylistFile( filePath );
        } else {
            addTrackFile( filePath, filename.c_str() );
        }
    }
}

void RadioStation::resetPlayedTracks() {
    NumTracksLeft = NumTracks;
    for ( int32_t i = 0; i < NumTracks; i++ ) {
        Tracks[i].bPlayed = false;
    }
}

bool RadioStation::pickNextTrack() {
    // FUN_007c77b0
    if ( NumTracks <= 0 ) {
        return false;
    }

    if ( NumTracksLeft == 0 ) {
        resetPlayedTracks();
    }

    int32_t pick = static_cast<int32_t>(
        static_cast<float>( rand() & 0x7fff ) * kRandToUnit *
        static_cast<float>( NumTracksLeft - 1 ) );

    for ( int32_t i = 0; i < NumTracks; i++ ) {
        if ( Tracks[i].bPlayed ) {
            continue;
        }

        if ( pick == 0 ) {
            CurrentTrack = i;
            Tracks[i].bPlayed = true;
            NumTracksLeft--;
            return true;
        }
        pick--;
    }

    return false;
}

void RadioStation::addTrack( const RadioTrack &track ) {
    if ( NumTracks < kMaxTracks ) {
        Tracks[NumTracks] = track;
        NumTracks++;
    }
}

void RadioStation::addTrackFile( const char *pFilePath,
                                 const char *pTitleSource ) {
    // FUN_007c6fc0
    if ( !RadioStreamPlayer::IsDecodableFile( pFilePath ) ) {
        return;
    }

    RadioTrack track = {};
    strncpy( track.Path, pFilePath, sizeof( track.Path ) - 1 );

    if ( !readTrackTags( track ) ) {
        GetFileTitle( pTitleSource, track.Title, kMaxNameLength );
    }

    track.bUserTrack = true;
    addTrack( track );
}

void RadioStation::addPlaylistFile( const char *pPlaylistPath ) {
    // FUN_007c70a0
    FileDirectAccess file;
    file.bNeedName = false;

    char playlistFolder[261] = {};
    const char *pSeparator = strrchr( pPlaylistPath, kPathSeparator );
    if ( pSeparator != nullptr ) {
        const size_t folderLength =
            static_cast<size_t>( pSeparator - pPlaylistPath ) + 1;
        if ( folderLength < 0x104 ) {
            memcpy( playlistFolder, pPlaylistPath, folderLength );
            playlistFolder[folderLength] = '\0';
        }
    }

    if ( !file.openFile( pPlaylistPath, true, false ) ) {
        return;
    }

    const uint32_t fileSize = file.getFileSize();
    char *pPlaylist = static_cast<char *>( TestDrive::Alloc( fileSize + 1 ) );
    if ( pPlaylist != nullptr ) {
        pPlaylist[fileSize] = '\0';

        if ( file.readFromFile( pPlaylist, fileSize ) == fileSize ) {
            const char *pCursor = pPlaylist;
            while ( pCursor != nullptr && *pCursor != '\0' ) {
                char line[kMaxPlaylistLineLength + 4];
                const char *pLineEnd =
                    ReadLine( pCursor, line, kMaxPlaylistLineLength );

                if ( line[0] != '#' ) {
                    FileDirectAccess trackFile;
                    trackFile.bNeedName = false;

                    if ( trackFile.fileExists( line ) ) {
                        addTrackFile( line, line );
                    } else {
                        char trackPath[261] = {};
                        snprintf( trackPath, sizeof( trackPath ), "%s%s",
                                  playlistFolder, line );
                        if ( trackFile.fileExists( trackPath ) ) {
                            addTrackFile( trackPath, trackPath );
                        }
                    }
                }

                const char *pNewLine = strchr( pLineEnd, '\n' );
                pCursor = ( pNewLine != nullptr ) ? pNewLine + 1 : nullptr;
            }
        }

        TestDrive::Free( pPlaylist );
    }

    file.close();
}

bool RadioStation::readTrackTags( RadioTrack &track ) {
    // FUN_007c6f30
    FileDirectAccess file;
    file.bNeedName = false;

    bool bHasTitle = false;
    if ( file.openFile( track.Path, true, false ) ) {
        if ( !ReadID3v2( file, track ) ) {
            ReadID3v1( file, track );
        }
        bHasTitle = track.Title[0] != '\0';
        file.close();
    }
    return bHasTitle;
}
