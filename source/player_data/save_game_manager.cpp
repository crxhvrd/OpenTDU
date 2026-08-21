#include "shared.h"
#include "save_game_manager.h"

#include "tdu_instance.h"
#include "user_profile.h"

#include "core/crypto/tea.h"
#include "filesystem/gs_file.h"
#include "gs_playerdata.h"

bool BOOL_010e7364 = false; // Always false at runtime

static constexpr uint32_t kBufferSize = 0x2000;
static uint8_t DAT_01041f50[kBufferSize];

static std::wstring FormatPlayerSaveTimestamp( const wchar_t *pSaveType ) {
    // FUN_0097bed0
    // NOTE: Replaced GetLocalTime() with time (for x compatibility)
    time_t rawtime;
    struct tm *info;

    time( &rawtime );
    info = localtime( &rawtime );

    wchar_t formatBuffer[260];
    memset( formatBuffer, 0, sizeof( wchar_t ) * 260 );
    swprintf( formatBuffer, 260, L"%s (%d/%d/%02d - %d:%02d'%02d'')", pSaveType,
              info->tm_mon, info->tm_wday, info->tm_year, info->tm_hour,
              info->tm_min, info->tm_sec );

    return formatBuffer;
}

static bool WriteSaveContext( const char *pContextName,
                              const std::wstring &timestamp,
                              LocalPlayerData *pPlayerData ) {
    // FUN_0097c0d0
    bool bPlayerSaveWritten = false;
    bool bCommonDTWritten = false;

    FileDirectAccess file;
    file.bNeedName = false;

    OTDU_UNIMPLEMENTED_ONCE; // TODO: PLAYERSAVE (profile list bookkeeping)
    gUserProfileRegister.FUN_00419de0();

    const uint64_t hashcode = gUserProfileRegister.getActiveProfileHashcode();
    if ( !file.openContext( hashcode, pContextName,
                            eContextSource::CS_Savegame ) ) {
        return false;
    }

    const int32_t writtenSize =
        ( gpPlayerData != nullptr )
            ? gpPlayerData->writeCommonDT( DAT_01041f50, kBufferSize,
                                           pPlayerData )
            : 0;

    uint32_t contentSize = uint32_t( writtenSize ) & 0xfffffff8u;
    if ( contentSize < uint32_t( writtenSize ) ) {
        contentSize += 8u;
    }
    TestDriveTEAEncryptBuffer( DAT_01041f50, contentSize );

    // TODO: PLAYERSAVE
    OTDU_UNIMPLEMENTED_ONCE;

    if ( contentSize != 0u ) {
        if ( file.openFile( "commondt.sav", false, true ) ) {
            const uint32_t numBytesWritten =
                file.writeToFile( DAT_01041f50, contentSize );
            file.close();
            bCommonDTWritten = numBytesWritten == contentSize;
        }
    }

    file.reset();

    return bPlayerSaveWritten && bCommonDTWritten;
}

SavegameManager gSavegameManager = {};

SavegameManager::SavegameManager()
    : event(),
      mutex(),
      pendingSaveCount( 0 ),
      unknownDword( 0 ),
      unknownDword2( 0 ),
      bProfileChange( false ),
      bUnknownBool1( false ),
      bUnknownBool2( true ),
      bUnknownBool3( false ),
      bUnknownBool4( false ),
      bSaveRequeued( false ) {
    memset( DAT_01041f50, 0, sizeof( uint8_t ) * kBufferSize );
}

SavegameManager::~SavegameManager() {}

void SavegameManager::create() {
    // FUN_0097c830
    if ( BOOL_010e7364 ) {
        // TODO: Looks like this is a dead branch (which somehow did not get
        // culled at compile time)
        OTDU_UNIMPLEMENTED;
    }

    pendingSaveCount = 0;
    unknownDword = 0;
    bProfileChange = false;
    bUnknownBool2 = true;
    unknownDword2 = 0;
    bUnknownBool3 = false;
    bUnknownBool4 = false;
    bSaveRequeued = false;
    bUnknownBool1 = false;
}

bool SavegameManager::doSaveBlocking( LocalPlayerData *pPlayerData ) {
    // FUN_0097c2f0
    if ( ( gpTestDriveInstance->hasRequestedExit() ) ||
         ( bUnknownBool4 == false ) ) {
        return false;
    }

    ScopedMutexLock lock( &mutex );

    unknownDword2 = 0;
    const uint32_t numQueuedSaves = pendingSaveCount - unknownDword;
    gUserProfileRegister.FUN_0060a160();

    const std::wstring timestamp = FormatPlayerSaveTimestamp( L"Player Save" );

    const bool bActiveIsFirstContext =
        strcmp( gActiveSaveContextName, kPlayerSaveContextName ) == 0;
    const char *pNextContextName = bActiveIsFirstContext
                                       ? kPlayerSave2ContextName
                                       : kPlayerSaveContextName;

    if ( WriteSaveContext( pNextContextName, timestamp, pPlayerData ) ) {
        strncpy( gActiveSaveContextName, pNextContextName,
                 sizeof( gActiveSaveContextName ) - 1 );
        unknownDword2 = 1;
    }

    bUnknownBool4 = false;
    if ( bSaveRequeued ) {
        bSaveRequeued = false;
        pendingSaveCount++;
        bUnknownBool4 = true;
    }

    unknownDword += numQueuedSaves;

    return true;
}

void SavegameManager::requestSave() {
    ScopedMutexLock lock( &mutex );

    if ( bUnknownBool4 ) {
        bSaveRequeued = true;
        return;
    }

    pendingSaveCount++;
    bUnknownBool4 = true;
}

bool SavegameManager::isProfileChangeInProgress() const {
    // FUN_00406630
    return bProfileChange;
}

void SavegameManager::toggleProfileChange() {
    // FUN_0097bd90
    if ( !bProfileChange ) {
        bProfileChange = true;
    }
}

void SavegameManager::setUnknownBool2( bool param_1 ) {
    // FUN_00406620
    bUnknownBool2 = param_1;
}

bool SavegameManager::getUnknownBool2() const {
    // FUN_00406610
    return bUnknownBool2;
}

bool SavegameManager::isDword2Zero() const {
    // FUN_00406600
    return unknownDword2 != 0;
}

bool SavegameManager::isDwordDiffFromPendingSaveCount() const {
    // FUN_00406640
    return pendingSaveCount != unknownDword;
}

void SavegameManager::increaseNumSaveCount() {
    // FUN_00406640
    pendingSaveCount++;
}
