#include "shared.h"
#include "radio_station.h"

#include "audio/radio/radio_stream_player.h"
#include "filesystem/file_direct_access.h"

#include <filesystem>

#if defined( OTDU_WIN32 )
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
    OTDU_UNIMPLEMENTED_ONCE; // TODO: RADIO (.lnk resolution via IShellLinkA)
}

static void
GetFileTitle( const char *pFilePath, char *pOutTitle, const size_t maxLength ) {
    // GetFileTitleA
    const std::string title =
        std::filesystem::path( pFilePath ).stem().string();
    strncpy( pOutTitle, title.c_str(), maxLength );
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
        OTDU_UNIMPLEMENTED_ONCE; // TODO: RADIO (ID3v2 FUN_007c6ee0, ID3v1
                                 // FUN_007c6e60)
        bHasTitle = track.Title[0] != '\0';
        file.close();
    }
    return bHasTitle;
}
