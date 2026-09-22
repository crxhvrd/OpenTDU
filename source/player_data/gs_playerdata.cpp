#include "shared.h"
#include "gs_playerdata.h"

#include "config/gs_config.h"
#include "database/gs_database.h"
#include "flash/gs_flash.h"
#include "flash/modal_message_box.h"
#include "game/game_cheats.h"
#include "game/mng_hud.h"
#include "input/gs_device.h"

#include "core/crypto/tea.h"
#include "core/ed_store.h"
#include "core/format.h"
#include "core/locale.h"
#include "core/mutex.h"

#include "filesystem/file_direct_access.h"
#include "filesystem/gs_file.h"
#include "game/avatar/gs_avatar.h"
#include "game/scenaric/avatar_manager.h"
#include "game/scenaric/entity/entity_avatar.h"
#include "game/scenaric/gs_scenaric.h"
#include "game/scenaric/local_player.h"
#include "game/vehicle/car_state.h"
#include "physics/vehicle/physics_vehicle.h"
#include "player_save.h"
#include "online/gs_online.h"
#include "render/camera/gs_camera.h"
#include "render/gs_render.h"
#include "save_game_manager.h"
#include "user_profile.h"

#include "tdu_instance.h"

// DAT_010e753c
float gSeatPosition[2] = { 0.0f, 0.0f };

GSPlayerData *gpPlayerData = nullptr;

// 'playersave' version
static constexpr int32_t kExpectedSaveVersion = 2;

char gActiveSaveContextName[128] = {}; // DAT_00f7a038

bool gbSaveAllowed = false; // DAT_0113f408

// 'Saving... Please wait...'
static constexpr uint64_t kSavingMessageHashcode = 0x37ba8e2ull;

static constexpr uint32_t kSaveScratchBufferSize = 0x80000;
static constexpr uint32_t kMaxEnumeratedContexts = 4;

static bool FindProfileSaveContext( uint64_t profileHashcode,
                                    char *pOutContextName ) {
    // FUN_0089b040
    pOutContextName[0] = '\0';

    if ( profileHashcode == 0ull ) {
        profileHashcode = gUserProfileRegister.getActiveProfileHashcode();
    }

    const bool bIsActiveProfile =
        ( profileHashcode == gUserProfileRegister.getActiveProfileHashcode() );

    FileDirectAccess fileAccess;
    fileAccess.bNeedName = false;

    EnumerateContextItem pContexts[kMaxEnumeratedContexts * 2] = {};
    uint32_t numContexts = 0u;
    uint32_t numContexts2 = 0u;

    fileAccess.enumerateContext( profileHashcode, eContextSource::CS_Savegame,
                                 nullptr, kPlayerSaveContextName,
                                 kMaxEnumeratedContexts, pContexts,
                                 &numContexts );
    fileAccess.enumerateContext( profileHashcode, eContextSource::CS_Savegame,
                                 nullptr, kPlayerSave2ContextName,
                                 kMaxEnumeratedContexts,
                                 pContexts + numContexts, &numContexts2 );

    const uint32_t numItems = numContexts + numContexts2;
    OTDU_LOG_DEBUG( "EnumerateContext() => %d items\n", numItems );

    bool bFound = false;
    for ( uint32_t i = 0; i < numItems; i++ ) {
        const EnumerateContextItem &context = pContexts[i];
        if ( !context.bIsValid ) {
            continue;
        }

        OTDU_LOG_DEBUG( "  -Found SaveGame: Name:%s\n",
                        context.Filename.c_str() );

        // Only the first 8 bytes are needed: the encrypted { magic, revision }
        // header of the stream.
        uint32_t pHeader[2] = {};
        uint32_t numBytesRead = 0u;

        bool bRead = gpFile->loadFileFromContext(
            context.Filename.c_str(), "playersave", pHeader, sizeof( pHeader ),
            &numBytesRead );

        if ( !bRead || numBytesRead == 0u ) {
            // A context holding only the shared options blob is still valid.
            bRead = gpFile->loadFileFromContext(
                context.Filename.c_str(), "commondt.sav", pHeader,
                sizeof( pHeader ), &numBytesRead );
            if ( !bRead || numBytesRead == 0u ) {
                OTDU_LOG_DEBUG( "  -NOT OK: cannot read\n" );
                continue;
            }

            OTDU_LOG_DEBUG( "  -SAVE GAME OK\n" );
            strncpy( pOutContextName, context.Filename.c_str(), 127 );
            pOutContextName[127] = '\0';
            bFound = true;
            continue;
        }

        TestDriveTEADecryptBuffer( pHeader, sizeof( pHeader ) );

        edStore headerStream;
        headerStream.bindExternalBuffer( pHeader, sizeof( pHeader ) );
        headerStream.seek( 0u );

        const int32_t magic = headerStream.readInteger();
        const int32_t revision = headerStream.readInteger();

        if ( magic != kPlayerSaveMagic ) {
            OTDU_LOG_DEBUG(
                "  -NOT OK: not the right one (too old version)\n" );
            continue;
        }

        if ( bIsActiveProfile && revision < gSaveStreamRevision ) {
            OTDU_LOG_DEBUG( "  -NOT OK: not the right one (not the last one "
                            "according to the savefile we found)\n" );
            continue;
        }

        OTDU_LOG_DEBUG( "  -SAVE GAME OK\n" );
        strncpy( pOutContextName, context.Filename.c_str(), 127 );
        pOutContextName[127] = '\0';
        bFound = true;

        if ( bIsActiveProfile ) {
            gSaveStreamRevision = revision;
        }
    }

    return bFound;
}

namespace TestDrive {
    const AvatarConfiguration *GetSavedAvatarConfiguration() {
        const AvatarConfiguration *pConfig =
            &gpAvatar->getDefaultConfiguration( 0 );

        IPlayerData *pLoadedSave = gpPlayerData->getLoadedSave();
        if ( pLoadedSave != nullptr ) {
            SavedAvatar *pSavedAvatar = pLoadedSave->getAvatar();
            if ( pSavedAvatar != nullptr ) {
                pConfig = &pSavedAvatar->Config;
            }
        }

        return pConfig;
    }

    void RebakePlayerAvatar() {
        // FUN_008955f0
        AvatarManager &avatarManager = gpScenaric->getAvatarManager();

        EntityAvatar *pAvatar = avatarManager.allocateAvatar( ~0ull, 1 );
        if ( pAvatar == nullptr ) {
            return;
        }

        pAvatar->installConfiguration( GetSavedAvatarConfiguration(), 0, false,
                                       false );

        gpAvatar->bakeAvatar( pAvatar, GetSavedAvatarConfiguration(), 0, false,
                              true );

        avatarManager.releaseAvatar( pAvatar );
    }
} // namespace TestDrive

GSPlayerData::GSPlayerData()
    : GameSystem(),
      Notifiable(),
      bInitialized( false ),
      bForceUpdate( false ),
      bTutorialCompleted( false ),
      bIsOnline( false ),
      bSaveEnabled( true ),
      field526864_0x967e5( false ),
      LastArrestedTime( -1.0f ),
      OffenceRepetitionFactor( 1u ),
      pLoadedSave( nullptr ),
      pLoadedSave2( nullptr ),
      localPlayerData(),
      saveScratchBuffer(),
      saveLoadState( SLS_None ),
      pendingSaveRequest( 0 ),
      saveVersion( 0 ),
      timeout( 0.0f ),
      pMutex( new TestDriveMutex() ) {
    // FUN_00898b80
    pMutex->initialize();

    {
        ScopedMutexLock lock( pMutex );

        localPlayerData.clear();

        OTDU_UNIMPLEMENTED_ONCE; // TODO: INPUT (FUN_00896bd0)

        LastArrestedTime = -1.0f;
        OffenceRepetitionFactor = 1u;
    }

    gpPlayerData = this;
}

GSPlayerData::~GSPlayerData() {}

bool GSPlayerData::initialize( TestDriveGameInstance *pGameInstance ) {
    // FUN_00898db0
    if ( bInitialized ) {
        return true;
    }

    this->pGameInstance = pGameInstance;
    bPaused = false;

    pLoadedSave = new PlayerSave();
    pLoadedSave2 = pLoadedSave;
    bInitialized = true;

    pLoadedSave2->initialize();

    localPlayerData.PadSensitivity =
        ( static_cast<float>( gDefaultSensitivity ) - 1.0f ) * 0.06666667f;
    localPlayerData.BarSensitivity = 0.5f;
    localPlayerData.MouseSensitivity = 0.5f;
    localPlayerData.SeatPositionHeight = gSeatPosition[0];
    localPlayerData.SeatPositionPosition = gSeatPosition[1];

    const bool bHasServerAddress = false; // TODO: ONLINE
    if ( bHasServerAddress != bool( bIsOnline ) ) {
        bIsOnline = bHasServerAddress;
        notify( 0u );
    }

    if ( gpRender != nullptr ) {
        localPlayerData.RenderWidth = gpRender->getWidth();
        localPlayerData.RenderHeight = gpRender->getHeight();
        localPlayerData.LODQuality = gpRender->getLODQuality();
    }

    // TODO: REMOVE ME: Redirect .ini vars to playerdata (to enable MSAA support
    // before the options screen writes them back)
    if ( gpConfig != nullptr ) {
        localPlayerData.AntiAliasing = gpConfig->AntiAliasing;
        localPlayerData.bHDREnabled = gpConfig->bEnableHDRI;
    }

    prepareActiveProfile();

    return true;
}

void GSPlayerData::tick( float totalTime, float deltaTime ) {
    // FUN_00897640
    if ( pLoadedSave2 != nullptr ) {
        pLoadedSave2->tick( deltaTime );
    }
}

void GSPlayerData::terminate() {
    // FUN_00898550
    if ( !bInitialized ) {
        return;
    }

    if ( pLoadedSave2 != nullptr ) {
        pLoadedSave2->terminate();
    }

    pLoadedSave2 = nullptr;

    if ( pLoadedSave != nullptr ) {
        delete pLoadedSave;
        pLoadedSave = nullptr;
    }

    bInitialized = false;
}

void GSPlayerData::resetPlayerData( const bool bApplyDefaults,
                                    const bool bClear ) {
    // FUN_00898a20
    ScopedMutexLock lock( pMutex );

    if ( bClear ) {
        localPlayerData.clear();

        OTDU_UNIMPLEMENTED_ONCE; // TODO: INPUT (FUN_00896bd0)
    }

    LastArrestedTime = -1.0f;
    OffenceRepetitionFactor = 1u;

    if ( bApplyDefaults && bClear ) {
        localPlayerData.PadSensitivity =
            ( static_cast<float>( gDefaultSensitivity ) - 1.0f ) * 0.06666667f;
        localPlayerData.BarSensitivity = 0.5f;
        localPlayerData.MouseSensitivity = 0.5f;
        localPlayerData.SeatPositionHeight = gSeatPosition[0];
        localPlayerData.SeatPositionPosition = gSeatPosition[1];

        if ( gpConfig != nullptr ) {
            strncpy( localPlayerData.Language, gpConfig->pLanguage,
                     sizeof( localPlayerData.Language ) - 1 );
        }

        updateUnitSystem();
    }
}

int32_t GSPlayerData::getSaveLoadState() const {
    // FUN_0089b010
    if ( pLoadedSave2 == nullptr || !pLoadedSave2->isReady() ) {
        return SLS_None;
    }

    return saveLoadState;
}

bool GSPlayerData::loadActiveProfileSave() {
    // FUN_0089b4b0
    if ( !bSaveEnabled ) {
        return false;
    }

    char pContextName[128] = {};
    if ( !FindProfileSaveContext( 0ull, pContextName ) ) {
        saveLoadState = SLS_NoSave;
        return false;
    }

    void *pCommonDT = nullptr;
    uint32_t commonDTSize = 0u;
    if ( gpFile->loadFileFromContext( pContextName, "commondt.sav", &pCommonDT,
                                      &commonDTSize ) ) {
        TestDriveTEADecryptBuffer( pCommonDT, commonDTSize );
        loadCommonDT( pCommonDT, int32_t( commonDTSize ), nullptr );
    }
    if ( pCommonDT != nullptr ) {
        TestDrive::Free( pCommonDT );
    }

    saveScratchBuffer.resize( kSaveScratchBufferSize );

    uint32_t saveSize = 0u;
    if ( !gpFile->loadFileFromContext( pContextName, "playersave",
                                       saveScratchBuffer.data(),
                                       kSaveScratchBufferSize, &saveSize ) ||
         saveSize == 0u ) {
        saveLoadState = SLS_NoSave;
        return false;
    }

    strncpy( gActiveSaveContextName, pContextName,
             sizeof( gActiveSaveContextName ) - 1 );

    TestDriveTEADecryptBuffer( saveScratchBuffer.data(), saveSize );

    const bool bDeserialized =
        pLoadedSave->deserialize( saveScratchBuffer.data(), saveSize );
    if ( !bDeserialized ) {
        saveLoadState = SLS_NoSave;
    } else {
        saveLoadState = SLS_Loaded;

        OTDU_UNIMPLEMENTED_ONCE; // TODO: PLAYERSAVE (GSFile::InitMapFiles and
                                 // the garage/current car fixup)
    }

    OTDU_UNIMPLEMENTED_ONCE; // TODO: PLAYERSAVE (save version check/upgrade)

    return bDeserialized;
}

bool GSPlayerData::loadCommonDT( const void *pSaveBuffer,
                                 const int32_t bufferLength,
                                 LocalPlayerData *pOutPlayerData ) {
    // FUN_00898ef0
    static constexpr int32_t kCommonDTVersion = 0x34;

    if ( pSaveBuffer == nullptr || bufferLength <= 0 ) {
        return false;
    }

    edStore saveStream;
    saveStream.bindExternalBuffer( const_cast<void *>( pSaveBuffer ),
                                   uint32_t( bufferLength ) );
    saveStream.seek( 0u );

    if ( saveStream.readInteger() != kCommonDTVersion ) {
        return false;
    }

    if ( pOutPlayerData != nullptr ) {
        pOutPlayerData->loadFromStream( saveStream );
        return true;
    }

    localPlayerData.loadFromStream( saveStream );

    OTDU_UNIMPLEMENTED_ONCE; // TODO: PLAYERSAVE (locale switch, camera mode,
                             // input bindings, audio and radio settings)

    if ( gpRender != nullptr ) {
        gpRender->setGamma( localPlayerData.Gamma );
        gpRender->setLODQuality( localPlayerData.LODQuality );
    }

    return true;
}

bool GSPlayerData::readProfileOptions( const uint64_t profileHashcode,
                                       LocalPlayerData &outPlayerData ) {
    // FUN_0089b6f0
    const uint64_t previousHashcode =
        gUserProfileRegister.getActiveProfileHashcode();
    const int32_t previousRevision = gSaveStreamRevision;

    gUserProfileRegister.initialize( profileHashcode );
    gSaveStreamRevision = 0;

    bool bSuccess = false;

    char pContextName[128] = {};
    if ( FindProfileSaveContext( profileHashcode, pContextName ) ) {
        void *pCommonDT = nullptr;
        uint32_t commonDTSize = 0u;

        if ( gpFile->loadFileFromContext( pContextName, "commondt.sav",
                                          &pCommonDT, &commonDTSize ) ) {
            TestDriveTEADecryptBuffer( pCommonDT, commonDTSize );
            bSuccess = loadCommonDT( pCommonDT, int32_t( commonDTSize ),
                                     &outPlayerData );
        }

        if ( pCommonDT != nullptr ) {
            TestDrive::Free( pCommonDT );
        }
    }

    gSaveStreamRevision = previousRevision;
    gUserProfileRegister.initialize( previousHashcode );

    return bSuccess;
}

void GSPlayerData::applyDrivingOptions() {
    // FUN_00897790
    CarState *pCarState = ( gpScenaric != nullptr )
                              ? gpScenaric->getLocalPlayer().getCarState()
                              : nullptr;
    if ( pCarState == nullptr ) {
        return;
    }

    PhysicsVehicle *pPhysicsVehicle = pCarState->getPhysicsVehicle();
    if ( pPhysicsVehicle == nullptr ) {
        return;
    }

    PhysicsVehicle::DrivingOptions options = {};
    options.bAutomatic = localPlayerData.bAuto;
    options.bSteerControl = !localPlayerData.bUseWheel;
    options.bHShifter = localPlayerData.bHShifterEnabled;
    options.bManualClutch = !localPlayerData.bAutoClutch;
    options.Sensitivity = pCarState->getBody()->bIsBike
                              ? localPlayerData.BarSensitivity
                              : localPlayerData.PadSensitivity;
    options.Linearity =
        localPlayerData.bUseWheel ? localPlayerData.Linearity : 0.0f;
    options.RumbleMax = localPlayerData.QuiverSensitivity;

    pPhysicsVehicle->applyDrivingOptions( options );

    OTDU_UNIMPLEMENTED_ONCE; // TODO: CAMERA (mouse sensitivity, invert mouse
                             // and TrackIR are read from the device config)
}

void GSPlayerData::requestSave( const int32_t mode ) {
    // FUN_00898770
    if ( !bSaveEnabled || !gbSaveAllowed ) {
        return;
    }

    if ( mode == 2 ) {
        pendingSaveRequest = 2;
        return;
    }

    if ( mode == 3 ) {
        if ( pendingSaveRequest != 2 ) {
            return;
        }

        const eGameMode activeGameMode =
            gpTestDriveInstance->getActiveGameMode();
        if ( activeGameMode == GM_Login || activeGameMode == GM_VideoBumper ) {
            pendingSaveRequest = 0;
            return;
        }

        if ( activeGameMode == GM_Living && !field526864_0x967e5 &&
             !gpScenaric->getIntroTutorial().isActive() ) {
            pendingSaveRequest = 0;
            field526864_0x967e5 = true;
            return;
        }
    } else if ( mode != 1 ) {
        return;
    }

    if ( gpFlashMessageBox != nullptr ) {
        gpFlashMessageBox->display( kSavingMessageHashcode, 3.0f, 0, nullptr,
                                    nullptr, false );
    }

    if ( pLoadedSave != nullptr ) {
        pLoadedSave->resetPendingOperations();
    }

    gSavegameManager.requestSave();

    pendingSaveRequest = 0;
}

void GSPlayerData::prepareActiveProfile() {
    // FUN_008982a0
    Driver *pDriver = pLoadedSave2->getDriver();
    strncpy( pDriver->Nickname,
             ( gpOnline != nullptr ) ? gpOnline->getUsername() : "",
             Driver::kNicknameLength );
    pDriver->Nickname[Driver::kNicknameLength - 1] = '\0';
    pDriver->notify( 0u );

    if ( gpScenaric != nullptr ) {
        gpScenaric->getLocalPlayer().setDriver( pDriver );
    }

    if ( gpDatabase == nullptr || gpConfig == nullptr ) {
        return;
    }

    const int32_t defaultCarID = gpDatabase->getVehicleId(
        gpConfig->pDefaultCarConfig[0], gpConfig->pDefaultCarConfig[1],
        gpConfig->pDefaultCarConfig[2] );
    if ( defaultCarID <= 0 ) {
        return;
    }

    Garage *pGarage = pLoadedSave->getGarage();
    if ( pGarage->getNumCars() > 0 ) {
        setCurrentCar( pGarage->getCarAtIndex( 0, 0ull, false ) );
    } else {
        pDriver->CurrentCar = 0u;
        pDriver->notify( 0u );
    }

    pGarage->pCars[pDriver->CurrentCar].setArchetype(
        uint32_t( defaultCarID ) );
}

void GSPlayerData::newGame( const bool bInstallCheats ) {
    // FUN_00898d20
    {
        ScopedMutexLock lock( pMutex );

        LastArrestedTime = -1.0f;
        OffenceRepetitionFactor = 1u;
    }

    pLoadedSave2->newGame( bInstallCheats );

    gpRender->setGamma( localPlayerData.Gamma );
}

CarDesc *GSPlayerData::getCurrentCar() {
    // FUN_008978d0
    if ( pLoadedSave == nullptr ) {
        return nullptr;
    }

    Driver *pDriver = pLoadedSave->getDriver();
    Garage *pGarage = pLoadedSave->getGarage();
    if ( pDriver == nullptr || pGarage == nullptr ||
         pDriver->CurrentCar >= uint32_t( Garage::kNumCarSlots ) ) {
        return nullptr;
    }

    return &pGarage->pCars[pDriver->CurrentCar];
}

void GSPlayerData::setCurrentCar( const CarDesc *pCar ) {
    // FUN_00897900
    Garage *pGarage = pLoadedSave->getGarage();

    int32_t slotIndex = 0;
    for ( ; slotIndex <= Garage::kNumSavedCarSlots; slotIndex++ ) {
        if ( pGarage->pCars[slotIndex].getCarID() == pCar->getCarID() ) {
            break;
        }
    }

    if ( slotIndex > Garage::kNumSavedCarSlots ) {
        slotIndex = 0;
    }

    pLoadedSave->setCurrentCar( uint16_t( slotIndex ) );
}

void GSPlayerData::setDefaultLanguage( const char *pDefaultLanguage,
                                       bool bForceInit ) {
    // FUN_008989b0
    strncpy( localPlayerData.Language, pDefaultLanguage,
             sizeof( localPlayerData.Language ) - 1 );
    localPlayerData.Language[sizeof( localPlayerData.Language ) - 1] = '\0';
    bForceUpdate = bForceInit;

    if ( gpTestDriveInstance &&
         gpTestDriveInstance->getActiveGameMode() != GM_Login ) {
        updateUnitSystem();
    }
}

void GSPlayerData::setDefaultSeatPosition( const float height,
                                           const float depth ) {}

void GSPlayerData::setLODQuality( const int32_t newValue ) {
    OTDU_ASSERT( newValue >= 0 && newValue <= 2 );
    localPlayerData.LODQuality = uint32_t( newValue );
}

void GSPlayerData::setResolution( const int32_t width, const int32_t height ) {
    localPlayerData.RenderWidth = uint32_t( width );
    localPlayerData.RenderHeight = uint32_t( height );
}

void GSPlayerData::setRefreshRate( const int32_t refreshRate ) {
    localPlayerData.RefreshRate = uint32_t( refreshRate );
}

void GSPlayerData::setAntiAliasing( const eAntiAliasingMethod method ) {
    localPlayerData.AntiAliasing = method;
}

void GSPlayerData::setHDREnabled( const bool bEnabled ) {
    localPlayerData.bHDREnabled = bEnabled;
}

const char *GSPlayerData::getLanguage() {
    // FUN_00898b40
    if ( localPlayerData.Language[0] == '\0' ) {
        strncpy( localPlayerData.Language, gpConfig->pLanguage,
                 sizeof( localPlayerData.Language ) - 1 );
        updateUnitSystem();
    }

    return localPlayerData.Language;
}

eAntiAliasingMethod GSPlayerData::getAAMethod() const {
    return localPlayerData.AntiAliasing;
}

bool GSPlayerData::isHDREnabled() const { return localPlayerData.bHDREnabled; }

void GSPlayerData::updateUnitSystem() {
    // FUN_00898960
    const uint16_t languageID =
        uint16_t( uint8_t( localPlayerData.Language[0] ) ) << 8 |
        uint16_t( uint8_t( localPlayerData.Language[1] ) );

    setUnitSystem( languageID == 0x7573 );
}

void GSPlayerData::setUnitSystem( const bool bUseImperial ) {
    localPlayerData.UnitSystem = bUseImperial ? US_Imperial : US_Metric;
    if ( gpMngHud != nullptr ) {
        gpMngHud->updateUnitSystem();
    }

    MoviePlayer *pMoviePlayer =
        gpFlash != nullptr ? gpFlash->getMoviePlayer( 1, 0 ) : nullptr;
    if ( pMoviePlayer != nullptr &&
         pMoviePlayer->MovieBank.getState() ==
             BankFlash::eBankFlashInit::BFI_Loaded ) {
        FlashPlayer *pFlashPlayer = pMoviePlayer->MovieBank.getFlashPlayer();
        if ( pFlashPlayer != nullptr ) {
            uint32_t flashUnitsFlash = bUseImperial ? 0u : 1u;
            pFlashPlayer->setVariableValue( "/:state_Metric", flashUnitsFlash );
        }
    }
}

bool GSPlayerData::isOnline() const { return bIsOnline; }

int32_t GSPlayerData::writeCommonDT( void *pExternalBuffer,
                                     const int32_t bufferLength,
                                     LocalPlayerData *pPlayerData ) {
    // FUN_008985d0
    static constexpr int32_t kCommonDTVersion = 0x34;

    ScopedMutexLock lock( pMutex );

    edStore saveStream;
    saveStream.bindExternalBuffer( pExternalBuffer, bufferLength );
    saveStream.clear();
    saveStream.writeInteger( kCommonDTVersion );

    if ( pPlayerData == nullptr ) {
        pPlayerData = &localPlayerData;
    }

    // The camera transition the player left the game on is stamped in right
    // before the write.
    pPlayerData->field36_0x64 =
        ( gpCamera != nullptr ) ? gpCamera->getActiveTransitionIndex() : -1;

    pPlayerData->serialize( saveStream );

    OTDU_UNIMPLEMENTED_ONCE;           // TODO: AUDIO/RADIO (FUN_009b2260)
    saveStream.writeInteger( 0 );      // master volume
    saveStream.writeInteger( 0 );      // number of mixer groups
    saveStream.writeULongLong( 0ull ); // current radio station hashcode
    saveStream.writeFloat( 0.0f );

    return int32_t( saveStream.getWrittenSize() );
}

int32_t GSPlayerData::writePlayerSave( void *pExternalBuffer,
                                       const int32_t bufferLength ) {
    // FUN_00899420
    ScopedMutexLock lock( pMutex );

    memset( pExternalBuffer, 0, size_t( bufferLength ) );

    if ( pLoadedSave == nullptr ) {
        return 0;
    }

    edStore saveStream;
    saveStream.bindExternalBuffer( pExternalBuffer, bufferLength );
    saveStream.clear();

    pLoadedSave->serialize( saveStream );

    if ( saveStream.getStatus() != 0 ) {
        return 0;
    }

    return int32_t( saveStream.getWrittenSize() );
}

bool GSPlayerData::FUN_00897990() {
    // FUN_00897990
    eGameMode eVar1 = gpTestDriveInstance->getActiveGameMode();

    if ( !bSaveEnabled || eVar1 == GM_Login || eVar1 == GM_VideoBumper ) {
        return false;
    }

    if ( ( eVar1 == GM_Living && !field526864_0x967e5 ) &&
         !gpScenaric->getIntroTutorial().isActive() ) {
        return false;
    }

    return pendingSaveRequest == 2;
}

bool GSPlayerData::isRentingCar() {
    // FUN_00416230
    OTDU_UNIMPLEMENTED;
    return false;
}

#ifdef OTDU_USE_IMGUI
void GSPlayerData::drawDebug( float deltaTime ) {
    if ( pLoadedSave == nullptr ) {
        ImGui::Text( "No active profile" );
        return;
    }

    Driver *pDriver = pLoadedSave->getDriver();
    ImGui::InputText( "Nickname", pDriver->Nickname, Driver::kNicknameLength );

    // Editing either of these notifies the driver's observers, which is what
    // repaints the '/:money' and '/:coupon' widgets on the HUD movie.
    int32_t money = int32_t( pDriver->getMoney() );
    if ( ImGui::InputInt( "Credits", &money, 1000, 100000 ) ) {
        pDriver->setMoney( uint32_t( Max( money, 0 ) ) );
        pDriver->notify();
    }

    int32_t numCoupons = int32_t( pDriver->getNumCoupons() );
    if ( ImGui::InputInt( "Coupons", &numCoupons ) ) {
        pDriver->setNumCoupons( uint32_t( Max( numCoupons, 0 ) ) );
        pDriver->notify();
    }

    // What the HUD should be showing for the credits above.
    char pMoneyString[32];
    FormatMoneyString( pMoneyString, int32_t( pDriver->getMoney() ) );
    ImGui::Text( "HUD reads '%s' / %u coupon(s)", pMoneyString,
                 pDriver->getNumCoupons() );

    if ( ImGui::CollapsingHeader( "Achievements" ) ) {
        pLoadedSave->getAchievements()->drawDebug();
    }

    if ( ImGui::Button( "Save" ) ) {
        gSavegameManager.refreshService();
        gSavegameManager.doSaveBlocking();
    }

    Garage *pGarage = pLoadedSave->getGarage();
    ImGui::Text( "Garage: %d car(s), %d house(s) owned", pGarage->getNumCars(),
                 pGarage->getNumOwnedHouses() );
    if ( ImGui::TreeNode( "Garage slots" ) ) {
        if ( ImGui::BeginTable( "garage_slots", 5,
                                ImGuiTableFlags_Borders |
                                    ImGuiTableFlags_RowBg |
                                    ImGuiTableFlags_ScrollY,
                                ImVec2( 0.0f, 260.0f ) ) ) {
            ImGui::TableSetupColumn( "Slot" );
            ImGui::TableSetupColumn( "House" );
            ImGui::TableSetupColumn( "Car cfg" );
            ImGui::TableSetupColumn( "Unique id" );
            ImGui::TableSetupColumn( "Paint/Rim/Int" );
            ImGui::TableSetupScrollFreeze( 0, 1 );
            ImGui::TableHeadersRow();

            for ( int32_t i = 0; i < Garage::kNumSavedCarSlots; i++ ) {
                GarageSlot &slot = pGarage->pCars[i];

                const uint64_t houseHashcode = slot.getHouseHashcode();
                if ( houseHashcode == 0ull && !slot.isValid() ) {
                    continue;
                }

                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::Text( "%d", i );
                ImGui::TableNextColumn();
                ImGui::Text( "%016llx",
                             static_cast<unsigned long long>( houseHashcode ) );
                ImGui::TableNextColumn();
                if ( slot.isValid() ) {
                    ImGui::Text( "%u", slot.getCarConfigID() );
                } else {
                    ImGui::TextUnformatted( "-" );
                }
                ImGui::TableNextColumn();
                ImGui::Text( "%u", slot.getCarID() );
                ImGui::TableNextColumn();
                ImGui::Text( "%d / %d / %d", slot.paintIndex, slot.rimID,
                             slot.interiorID );
            }

            ImGui::EndTable();
        }

        ImGui::TreePop();
    }
}
#endif