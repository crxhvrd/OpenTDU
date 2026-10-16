#include "shared.h"
#include "save_game_manager.h"

#include "tdu_instance.h"
#include "user_profile.h"

#include "core/crypto/tea.h"
#include "filesystem/gs_file.h"
#include "gs_playerdata.h"
#include "player_data_interface.h"
#include "player_save.h"

bool BOOL_010e7364 = false;              // Always false at runtime
int32_t gPreviousSaveStreamRevision = 0; // DAT_010e7360

static constexpr uint32_t kBufferSize = 0x2000;
static uint8_t DAT_01041f50[kBufferSize];
static constexpr uint32_t kPlayerSaveBufferSize = 0x80000;
static uint8_t gActivePlayerSave[kPlayerSaveBufferSize]; // DAT_01043f50
static uint32_t gActivePlayerSaveSize = 0u;              // DAT_010c3f50

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

    if ( gActivePlayerSaveSize != 0u ) {
        if ( file.openFile( "playersave", false, true ) ) {
            const uint32_t numBytesWritten =
                file.writeToFile( gActivePlayerSave, gActivePlayerSaveSize );
            file.close();
            bPlayerSaveWritten = numBytesWritten == gActivePlayerSaveSize;
        }
    }

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
PlayerSaveDescriptor gPlayerSaveDescriptor = {}; // DAT_01041b48

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

void SavegameManager::refreshService() {
    // FUN_0097c6e0
    if ( gpPlayerData != nullptr && gpPlayerData->isOnline() ) {
        OTDU_UNIMPLEMENTED_ONCE; // TODO: ONLINE (flush the pending online
                                 // savegame notification)
    }

    if ( BOOL_010e7364 || bUnknownBool4 ) {
        return;
    }

    if ( int32_t( pendingSaveCount - unknownDword ) <= 0 ) {
        return;
    }

    const bool bOnlineOperationPending = false;

    IPlayerData *pSave =
        ( gpPlayerData != nullptr ) ? gpPlayerData->getLoadedSave() : nullptr;
    if ( pSave == nullptr || !pSave->getDriver()->isValid() ) {
        return;
    }

    if ( !gbSaveAllowed || bOnlineOperationPending ) {
        return;
    }

    gPreviousSaveStreamRevision = gSaveStreamRevision;
    gSaveStreamRevision++;
    if ( uint32_t( gSaveStreamRevision ) > 0xffffffeeu ) {
        gSaveStreamRevision = 0;
    }

    bUnknownBool3 = buildPlayerSaveBuffer();

    if ( BOOL_010e7364 ) {
        // The dead threaded path: the save thread would be woken up here
        // instead of arming the blocking write.
        OTDU_UNIMPLEMENTED;
        return;
    }

    bUnknownBool4 = true;
}

bool PlayerSaveDescriptor::isSameAs( const PlayerSaveDescriptor &other ) const {
    // FUN_00406660
    bool bSame = Money == other.Money && NumHouses == other.NumHouses &&
                 NumCars == other.NumCars &&
                 CurrentCarConfigID == other.CurrentCarConfigID &&
                 CurrentCarPackHashcode == other.CurrentCarPackHashcode;

    const int32_t numCars = Min( NumCars, kMaxCars );
    for ( int32_t i = 0; i < numCars; i++ ) {
        bSame = bSame && CarConfigIDs[i] == other.CarConfigIDs[i];
    }

    return bSame;
}

bool SavegameManager::buildPlayerSaveBuffer() {
    // FUN_0097c240
    const PlayerSaveDescriptor previousDescriptor = gPlayerSaveDescriptor;
    gPlayerSaveDescriptor.Revision = gSaveStreamRevision;
    if ( gpPlayerData != nullptr ) {
        gpPlayerData->fillSaveDescriptor( &gPlayerSaveDescriptor );
    }

    const bool bDescriptorAlreadyKnown =
        gPlayerSaveDescriptor.isSameAs( previousDescriptor );
    if ( !bDescriptorAlreadyKnown ) {
        gSaveStreamTrailer++;
        if ( uint32_t( gSaveStreamTrailer ) > 0xffffffeeu ) {
            gSaveStreamTrailer = 0;
        }
    }
    gPlayerSaveDescriptor.Trailer = gSaveStreamTrailer;

    const int32_t writtenSize =
        ( gpPlayerData != nullptr )
            ? gpPlayerData->writePlayerSave( gActivePlayerSave,
                                             int32_t( kPlayerSaveBufferSize ) )
            : 0;

    uint32_t contentSize = uint32_t( writtenSize ) & 0xfffffff8u;
    if ( contentSize < uint32_t( writtenSize ) ) {
        contentSize += 8u;
    }

    TestDriveTEAEncryptBuffer( gActivePlayerSave, contentSize );
    gActivePlayerSaveSize = contentSize;

    return !bDescriptorAlreadyKnown;
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
    } else {
        gSaveStreamRevision = gPreviousSaveStreamRevision;
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

bool SavegameManager::writeCommonDTBlocking( LocalPlayerData *pPlayerData,
                                             const uint64_t profileHashcode ) {
    // FUN_0097bf40
    ScopedMutexLock lock( &mutex );

    gUserProfileRegister.FUN_0060a160();

    const char *pContextName =
        ( strcmp( gActiveSaveContextName, kPlayerSave2ContextName ) == 0 )
            ? kPlayerSave2ContextName
            : kPlayerSaveContextName;

    FileDirectAccess file;
    file.bNeedName = false;
    gUserProfileRegister.FUN_00419de0();
    if ( !file.openContext( profileHashcode, pContextName,
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

    bool bCommonDTWritten = false;
    if ( contentSize != 0u && file.openFile( "commondt.sav", true, true ) ) {
        const uint32_t numBytesWritten =
            file.writeToFile( DAT_01041f50, contentSize );
        file.close();
        bCommonDTWritten = numBytesWritten == contentSize;
    }

    file.reset();

    return bCommonDTWritten;
}

void SavegameManager::requestSave() {
    // FUN_0097bdb0
    pendingSaveCount++;
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
