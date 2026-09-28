#include "shared.h"
#include "mng_flash_localize.h"

#include "flash/implementation/flash_player.h"
#include "gs_flash.h"
#include "tdu_instance.h"

#include "core/locale.h"
#include "database/gs_database.h"
#include "player_data/achievements_data.h"
#include "player_data/driver.h"
#include "player_data/gs_playerdata.h"
#include "player_data/player_data_interface.h"

MngFlashLocalize *gpMngFlashLocalize = nullptr;
char MngFlashLocalize::VersionString[64] = "VERSION UKNOW";

static constexpr char kSentenceMarker[3] = "\xC2\xA7"; // DAT_00f04dc8

static constexpr uint64_t kDrivingHelpHashcodes[4] = { // DAT_00f99720
    0x03326ae2, 0x0341ad22, 0x0350ef62, 0x03b7ee92 };

// Rookie / Amateur / Pro / Expert / Champion / Ace
static constexpr uint64_t kPlayerLevelNameHashcodes[6] = {
    0x037add73, 0x038a1fb3, 0x03a8a433, 0x039961f3, 0x03b7e673, 0x039a7b82 };
static constexpr int32_t kNumPlayerLevelNames =
    int32_t( sizeof( kPlayerLevelNameHashcodes ) /
             sizeof( kPlayerLevelNameHashcodes[0] ) );

// Completion percentage is the achieved points over the 1000pts Ace requires
static constexpr float kPointsToCompletionRatio = 0.001f; // FLOAT_00f4167c

static inline bool IsMarkerAt( const char *pStr ) {
    // FUN_00451aa0 (inlined)
    return pStr[0] == kSentenceMarker[0] && pStr[1] == kSentenceMarker[1];
}

static inline size_t
BoundedCopy( char *pDest, const char *pSrc, const size_t maxLen ) {
    // FUN_00451a70 (inlined)
    size_t numCopied = 0;
    while ( numCopied < maxLen && pSrc[numCopied] != '\0' ) {
        pDest[numCopied] = pSrc[numCopied];
        numCopied++;
    }
    return numCopied;
}

static inline const char *SkipToNextMarker( const char *pStr ) {
    while ( *pStr != '\0' && !IsMarkerAt( pStr ) ) {
        pStr += GetUTF8CodepointLength( pStr );
    }
    return pStr;
}

MngFlashLocalize::MngFlashLocalize() : Manager(), dbContext( 0x0 ) {
    // FUN_007173a0
    clear();

    if ( gpMngFlashLocalize == nullptr ) {
        gpMngFlashLocalize = this;
    }
}

MngFlashLocalize::~MngFlashLocalize() {
    if ( gpMngFlashLocalize == this ) {
        gpMngFlashLocalize = nullptr;
    }
}

bool MngFlashLocalize::initialize( TestDriveGameInstance * ) {
    // FUN_00717370
    strcpy( MngFlashLocalize::VersionString, TestDrive::GetVersion() );
    return true;
}

void MngFlashLocalize::clear() {
    // FUN_00716f00
    for ( int32_t i = 0; i < kNumLocalizedStrings; i++ ) {
        localizedStrings[i].pFlashVarName[0] = '\0';
        localizedStrings[i].pFormattedString[0] = '\0';
    }

    clearAllSysVar();
}

void MngFlashLocalize::OnFlashInstall() {
    // FUN_00717770
    if ( gpFlash == nullptr ) {
        return;
    }

    for ( int32_t level = 0; level < 2; level++ ) {
        const int32_t numMovies = gpFlash->getNumMovies( level );
        for ( int32_t movieIndex = 0; movieIndex < numMovies; movieIndex++ ) {
            MoviePlayer *pMoviePlayer =
                gpFlash->getMoviePlayer( level, movieIndex );
            if ( pMoviePlayer == nullptr ||
                 !pMoviePlayer->MovieBank.isLoaded() ) {
                continue;
            }

            FlashPlayer *pPlayer = pMoviePlayer->MovieBank.getFlashPlayer();
            if ( pPlayer == nullptr ) {
                continue;
            }

            pPlayer->setVariableValue( "/:Version",
                                       MngFlashLocalize::VersionString );
        }
    }
}

int32_t MngFlashLocalize::computeNumArgs( const char *pValue ) {
    // FUN_00716f60
    int32_t consecutiveCount = 0;
    int32_t maxFound = 0;

    const char *pIt = pValue;
    while ( *pIt != '\0' ) {
        if ( !IsMarkerAt( pIt ) ) {
            pIt += GetUTF8CodepointLength( pIt );
            continue;
        }

        pIt += GetUTF8CodepointLength( pIt );

        int32_t argIndex = 0;
        if ( sscanf( pIt, "%d", &argIndex ) == 1 && argIndex < 10 ) {
            if ( argIndex == consecutiveCount + 1 ) {
                consecutiveCount = argIndex;
            }
            if ( maxFound < argIndex ) {
                maxFound = argIndex;
            }
        }

        pIt = SkipToNextMarker( pIt );
        if ( *pIt != '\0' ) {
            pIt += GetUTF8CodepointLength( pIt );
        }
    }

    if ( consecutiveCount < maxFound ) {
        return -1;
    }
    return Min( consecutiveCount, kMaxLocalizeArgs );
}

void MngFlashLocalize::clearAllSysVar() {
    // FUN_00716cf0
    for ( int32_t i = 0; i < kNumLocalizeSysVars; i++ ) {
        sysVars[i].pVarName[0] = '\0';
        sysVars[i].pValue[0] = '\0';
    }
}

void MngFlashLocalize::setSysVar( const char *pName, const char *pValue ) {
    // FUN_00716d30
    if ( pName == nullptr || pValue == nullptr || pName[0] == '\0' ) {
        return;
    }

    for ( int32_t i = 0; i < kNumLocalizeSysVars; i++ ) {
        FlashLocalizeSysVar &sysVar = sysVars[i];
        if ( sysVar.pVarName[0] != '\0' ) {
            continue;
        }

        snprintf( sysVar.pVarName, sizeof( sysVar.pVarName ), "%s%s", pName,
                  kSentenceMarker );

        strncpy( sysVar.pValue, pValue, sizeof( sysVar.pValue ) );
        sysVar.pValue[sizeof( sysVar.pValue ) - 1] = '\0';
        return;
    }
}

void MngFlashLocalize::setSysVar( const char *pName, const int32_t value ) {
    // FUN_00716ed0
    char pFormatted[32];
    sprintf( pFormatted, "%d", value );
    setSysVar( pName, pFormatted );
}

void MngFlashLocalize::setSysVar( const char *pName, const float value ) {
    // FUN_00716e90
    char pFormatted[32];
    sprintf( pFormatted, "%.2f", (double)value );
    setSysVar( pName, pFormatted );
}

const char *MngFlashLocalize::findSysVar( const char *pToken ) const {
    // FUN_00716c40
    for ( int32_t i = 0; i < kNumLocalizeSysVars; i++ ) {
        const FlashLocalizeSysVar &sysVar = sysVars[i];
        if ( sysVar.pVarName[0] == '\0' ) {
            continue;
        }

        if ( strncmp( sysVar.pVarName, pToken, strlen( sysVar.pVarName ) ) ==
             0 ) {
            return sysVar.pValue;
        }
    }

    return nullptr;
}

void MngFlashLocalize::expandMacroToken( const char *pToken, char *pDest ) {
    // FUN_007178c0
    static constexpr size_t kDestLength = 512;

    char pLocalToken[512];
    strncpy( pLocalToken, pToken, kDestLength );
    pLocalToken[kDestLength - 1] = '\0';

    IPlayerData *pSave =
        ( gpPlayerData != nullptr ) ? gpPlayerData->getLoadedSave() : nullptr;

    if ( strncmp( pLocalToken, "PLNA", 4 ) == 0 ) {
        if ( pSave != nullptr ) {
            strncpy( pDest, pSave->getDriver()->Nickname, kDestLength );
            pDest[kDestLength - 1] = '\0';
        } else {
            pDest[0] = '\0';
        }
        return;
    }

    if ( strncmp( pLocalToken, "NEWS", 4 ) == 0 ) {
        OTDU_UNIMPLEMENTED_ONCE; // TODO: PLAYER_DATA (unread news count)
        pDest[0] = '\0';
        return;
    }

    if ( strncmp( pLocalToken, "ACHV_PTS", 8 ) == 0 ) {
        const uint32_t achievedPoints =
            ( pSave != nullptr ) ? pSave->getAchievements()->getAchievedPoints()
                                 : 0u;
        snprintf( pDest, kDestLength, "%d", int32_t( achievedPoints ) );
        return;
    }

    if ( strncmp( pLocalToken, "LEVEL", 5 ) == 0 ) {
        const int32_t playerLevel =
            ( pSave != nullptr ) ? pSave->getAchievements()->getPlayerLevel()
                                 : -1;

        const char *pLevelName = nullptr;
        if ( playerLevel >= 0 && playerLevel < kNumPlayerLevelNames &&
             gpDatabase != nullptr ) {
            pLevelName = gpDatabase->getStringByHashcode(
                0, kPlayerLevelNameHashcodes[playerLevel] );
        }

        if ( pLevelName == nullptr ) {
            pLevelName = "(ERR:LVL)";
        }

        snprintf( pDest, kDestLength, "%s", pLevelName );
        return;
    }

    if ( strncmp( pLocalToken, "COMPL_PCT", 9 ) == 0 ) {
        const uint32_t achievedPoints =
            ( pSave != nullptr ) ? pSave->getAchievements()->getAchievedPoints()
                                 : 0u;
        const float completion = floorf( float( int32_t( achievedPoints ) ) *
                                         kPointsToCompletionRatio * 100.0f );
        snprintf( pDest, kDestLength, "%d", int32_t( completion ) );
        return;
    }

    if ( strncmp( pLocalToken, "DRV_MODE", 8 ) == 0 ) {
        const int32_t drivingHelpLevel =
            ( gpPlayerData != nullptr ) ? gpPlayerData->getDrivingHelpLevel()
                                        : 0;

        const char *pDrivingMode = nullptr;
        if ( drivingHelpLevel > 0 && drivingHelpLevel < 4 &&
             gpDatabase != nullptr ) {
            pDrivingMode = gpDatabase->getStringByHashcode(
                0, kDrivingHelpHashcodes[drivingHelpLevel] );
        }

        if ( pDrivingMode == nullptr ) {
            pDrivingMode = "(ERR:NODRVMODE)";
        }

        snprintf( pDest, kDestLength, "%s", pDrivingMode );
        return;
    }

    if ( strncmp( pLocalToken, "RACE_LUX", 8 ) == 0 ) {
        const char *pRaceLux =
            ( gpDatabase != nullptr )
                ? gpDatabase->getStringByHashcode( 7, 0x3ab6967 )
                : nullptr;
        if ( pRaceLux == nullptr ) {
            pRaceLux = "(ERR:RACE_LX)";
        }

        snprintf( pDest, kDestLength, "%s", pRaceLux );
        return;
    }

    if ( strncmp( pLocalToken, "MY_CLUB", 7 ) == 0 ) {
        const char *pClubName =
            ( pSave != nullptr ) ? pSave->getDriver()->ClubName : nullptr;
        if ( pClubName == nullptr || pClubName[0] == '\0' ) {
            pClubName = "ERR:NO_CLUB";
        }

        snprintf( pDest, kDestLength, "%s", pClubName );
        return;
    }

    if ( strncmp( pLocalToken, "REW_MAX", 7 ) == 0 ) {
        OTDU_UNIMPLEMENTED_ONCE; // TODO: ONLINE (lobby max reward)
        pDest[0] = '\0';
        return;
    }

    // Not a built-in macro
    const char *pSysVar = findSysVar( pLocalToken );
    if ( pSysVar == nullptr ) {
        snprintf( pDest, kDestLength, "%s", "(ERR:3)" );
        return;
    }

    snprintf( pDest, kDestLength, "%s", pSysVar );
}

bool MngFlashLocalize::makeLocalizedSentence( char *pDest,
                                              size_t maxLen,
                                              const char *pSrc,
                                              const char *const *pArgs,
                                              int32_t numArgs ) {
    // FUN_00717bf0
    bool bFoundMarker = false;
    size_t writePos = 0;

    while ( *pSrc != '\0' && writePos < maxLen ) {
        const int32_t codepointLength = GetUTF8CodepointLength( pSrc );

        if ( !IsMarkerAt( pSrc ) ) {
            const size_t copyLength =
                Min( (size_t)codepointLength, maxLen - writePos );
            memcpy( pDest + writePos, pSrc, copyLength );

            writePos += codepointLength;
            pSrc += codepointLength;
            continue;
        }

        bFoundMarker = true;
        pSrc += codepointLength;

        char pExpanded[512];
        const char *pSubstitution = nullptr;

        if ( *pSrc < '0' || *pSrc > '9' ) {
            expandMacroToken( pSrc, pExpanded );
            pSubstitution = pExpanded;
        } else if ( !IsMarkerAt( pSrc + GetUTF8CodepointLength( pSrc ) ) ) {
            expandMacroToken( pSrc, pExpanded );
            pSubstitution = pExpanded;
        } else {
            int32_t argIndex = atoi( pSrc ) - 1;
            if ( argIndex < 0 || argIndex >= kMaxLocalizeArgs ) {
                argIndex = 0;
            }

            pSubstitution = ( argIndex < numArgs && pArgs[argIndex] != nullptr )
                                ? pArgs[argIndex]
                                : "(ERR:2)";
        }

        writePos +=
            BoundedCopy( pDest + writePos, pSubstitution, maxLen - writePos );

        pSrc = SkipToNextMarker( pSrc );
        if ( *pSrc != '\0' ) {
            pSrc += GetUTF8CodepointLength( pSrc );
        }
    }

    pDest[Min( writePos, maxLen - 1 )] = '\0';
    return bFoundMarker;
}

void MngFlashLocalize::makeSentenceInternal( char *pDest,
                                             size_t maxLen,
                                             const char *pSrc,
                                             va_list args ) {
    // FUN_007171b0
    if ( pSrc == nullptr ) {
        return;
    }

    const int32_t numArgs = computeNumArgs( pSrc );
    if ( numArgs < 0 ) {
        strncpy( pDest, "(ERR:1)", maxLen );
        pDest[maxLen - 1] = '\0';
        return;
    }

    const char *pArgs[kMaxLocalizeArgs] = {};
    for ( int32_t i = 0; i < numArgs && i < kMaxLocalizeArgs; i++ ) {
        pArgs[i] = va_arg( args, const char * );
        if ( pArgs[i] == nullptr ) {
            break;
        }
    }

    makeLocalizedSentence( pDest, maxLen, pSrc, pArgs, numArgs );
}

void MngFlashLocalize::makeSentence( char *pDest,
                                     size_t maxLen,
                                     const char *pSrc,
                                     ... ) {
    // FUN_007171b0
    va_list args;
    va_start( args, pSrc );
    makeSentenceInternal( pDest, maxLen, pSrc, args );
    va_end( args );
}

void MngFlashLocalize::makeSentenceFromHashcode( char *pDest,
                                                 size_t maxLen,
                                                 uint64_t hashcode,
                                                 ... ) {
    // FUN_00717280
    const char *pSrc = gpDatabase->getStringByHashcode( dbContext, hashcode );

    va_list args;
    va_start( args, hashcode );
    makeSentenceInternal( pDest, maxLen, pSrc, args );
    va_end( args );
}

void MngFlashLocalize::addSentenceAndSet( FlashPlayer *pPlayer,
                                          const char *pVarName,
                                          const char *pValue ) {
    // FUN_00717420
    for ( int32_t i = 0; i < kNumLocalizedStrings; i++ ) {
        FlashLocalizedString &entry = localizedStrings[i];
        if ( strcmp( entry.pFlashVarName, pVarName ) != 0 ) {
            continue;
        }

        strncpy( entry.pFormattedString, pValue,
                 sizeof( entry.pFormattedString ) );
        entry.pFormattedString[sizeof( entry.pFormattedString ) - 1] = '\0';
        pPlayer->setVariableValue( entry.pFlashVarName,
                                   entry.pFormattedString );
        return;
    }

    for ( int32_t i = 0; i < kNumLocalizedStrings; i++ ) {
        FlashLocalizedString &entry = localizedStrings[i];
        if ( entry.pFlashVarName[0] != '\0' ) {
            continue;
        }

        strncpy( entry.pFlashVarName, pVarName, sizeof( entry.pFlashVarName ) );
        entry.pFlashVarName[sizeof( entry.pFlashVarName ) - 1] = '\0';

        strncpy( entry.pFormattedString, pValue,
                 sizeof( entry.pFormattedString ) );
        entry.pFormattedString[sizeof( entry.pFormattedString ) - 1] = '\0';

        pPlayer->setVariableValue( entry.pFlashVarName,
                                   entry.pFormattedString );
        return;
    }

    pPlayer->setVariableValueNonLinked( pVarName, "(ERR:4)" );
}

void MngFlashLocalize::setVarLocalized( FlashPlayer *pPlayer,
                                        const char *pValue,
                                        const char *pVarName,
                                        const char *pArg1 ) {
    // FUN_00717ff0
    if ( pValue == nullptr ) {
        return;
    }

    if ( strlen( pVarName ) >= 0x41 ) {
        return;
    }

    const int32_t numArgs = computeNumArgs( pValue );
    if ( numArgs < 0 ) {
        pPlayer->setVariableValueNonLinked( pVarName, "(ERR:1)" );
        return;
    }

    const char *pArgs[kMaxLocalizeArgs] = {};
    pArgs[0] = pVarName;
    pArgs[1] = pArg1;

    char pFormatted[512];
    const bool bFormatted = makeLocalizedSentence(
        pFormatted, sizeof( pFormatted ), pValue, pArgs, numArgs );
    if ( bFormatted ) {
        addSentenceAndSet( pPlayer, pVarName, pFormatted );
        return;
    }

    pPlayer->setVariableValue( pVarName, pValue );
}

void MngFlashLocalize::setVarLocalized( FlashPlayer *pPlayer,
                                        const char *pVarName,
                                        uint64_t hashcode ) {
    // FUN_00718120
    char *pcVar1 = gpDatabase->getStringByHashcode( dbContext, hashcode );
    setVarLocalized( pPlayer, pcVar1, pVarName );
}
