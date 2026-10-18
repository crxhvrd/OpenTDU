#include "shared.h"
#include "mng_map_screenshot.h"

#include "ai/ai_pool.h"
#include "ai/gs_ai.h"
#include "config/gs_config.h"
#include "core/maths_helper.h"
#include "filesystem/gs_file.h"
#include "flash/gs_flash.h"
#include "flash/movie_player.h"
#include "game/scenaric/entity/entity_cinematic.h"
#include "game/scenaric/entity/entity_driver.h"
#include "game/scenaric/gs_scenaric.h"
#include "game/scenaric/local_player.h"
#include "game/scenaric/spots.h"
#include "game/vehicle/car_state.h"
#include "game/vehicle/vehicle_base.h"
#include "physics/gs_physics.h"
#include "physics/vehicle/physics_vehicle.h"
#include "render/camera/camera_game.h"
#include "render/camera/gs_camera.h"
#include "tdu_instance.h"
#include "world/details/gs_details.h"
#include "world/gs_world.h"
#include "world/mng_sector_name.h"
#include "world/mng_world.h"
#include "world/streaming_manager.h"

#ifdef OTDU_USE_IMGUI
#include "imgui.h"
#endif

bool gbMapScreenshotsMngEnabled = false; // DAT_010e8034

struct MSSDBReader {
    const uint8_t *pBuffer = nullptr;
    bool bReverseEndianness = false;

    // FUN_004324a0
    uint32_t getValue() {
        uint32_t value;
        std::memcpy( &value, pBuffer, sizeof( value ) );
        pBuffer += sizeof( value );
        if ( bReverseEndianness ) {
            value = ReverseEndianness32( value );
        }
        return value;
    }
};

static constexpr uint32_t kMSSDBMagic = 0x4d535344;
static constexpr uint32_t kMSSDBPadMarker = 0x50414421;

// DAT_00f9c380
static constexpr const char
    *kCategoryFolders[MngMapScreenshot::kNumCategories] = {
        "FrontEnd/AllRes", "IGE", "Scenarics/Missions" };

static constexpr int32_t kScreenshotCaptureCamera = 10;
static constexpr float kCameraTransitionTime = 1.0f; // FLOAT_00f4165c
static constexpr float kStreamingSettleTime = 7.0f;  // DAT_00f41844
static constexpr float kUnsetClipPlane = -1.0f;      // FLOAT_00f41664
static constexpr uint32_t kCaptureWidth = 0x500;
static constexpr uint32_t kCaptureHeight = 0x2d0;
static constexpr uint32_t kSavedSize = 0x100;
static constexpr int32_t kHudMoviesHiddenFlag = 0x1000;
static constexpr int32_t kSectorStateInstalled = 1;

static void SetHudMoviesHidden( const bool bHidden ) {
    const int32_t numMovies = gpFlash->getNumMovies( 1 );
    for ( int32_t i = 0; i < numMovies; i++ ) {
        MoviePlayer *pMovie = gpFlash->getMoviePlayer( 1, i );
        if ( pMovie == nullptr ) {
            continue;
        }
        if ( bHidden ) {
            pMovie->IsMoviePaused |= kHudMoviesHiddenFlag;
        } else {
            pMovie->IsMoviePaused &= ~kHudMoviesHiddenFlag;
        }
    }
}

static void SetPapersDetailEnabled( const bool bEnabled ) {
    // FUN_008a7630
    TDUDetail *pDetail = gpDetails->getDetailByHash( kDetailHashPapers );
    if ( pDetail != nullptr ) {
        pDetail->setEnabled( bEnabled );
    }
}

static void SetSectorNamesEnabled( const bool bEnabled ) {
    if ( gpMngSectorName != nullptr ) {
        gpMngSectorName->bEnabled = bEnabled;
    }
}

static Eigen::Vector4f GetMatrixPosition( const float ( &matrix )[4][4] ) {
    return Eigen::Vector4f( matrix[3][0], matrix[3][1], matrix[3][2],
                            matrix[3][3] );
}

static void BuildSpotScreenshotName( const SpotMap *pSpot, char *pOutName ) {
    char sourceName[9];
    std::memcpy( sourceName, &pSpot->SourceHashcode, 8 );
    sourceName[8] = '\0';

    GridPos2D sectorCell;
    GridPos2D areaCell;
    gpWorld->getStreamingManager()->sectorManager.computeCell(
        Eigen::Vector4f( pSpot->X, pSpot->Y, pSpot->Z, 1.0f ), sectorCell,
        areaCell );
    sprintf( pOutName, "%s_%d%d%d%d", sourceName, areaCell.posX, areaCell.posY,
             sectorCell.posX, sectorCell.posY );
}

static void GetSpotHashcodeName( const SpotMap *pSpot, char *pOutName ) {
    std::memcpy( pOutName, &pSpot->Hashcode, 8 );
    pOutName[8] = '\0';
}

MngMapScreenshot *gpMngMapScreenshot = nullptr; // DAT_010e8030

MngMapScreenshot::MngMapScreenshot()
    : Manager(),
      pScreenshotsData( nullptr ),
      MaxNumScreenshots( 0 ),
      NumScreenshots( 0 ),
      CurrentScreenshotIndex( -1 ),
      MaxScreenshotIndex( 0 ),
      CurrentCategory( 0 ),
      bIsDirty( false ),
      State( MSS_Idle ),
      pOldCamera( nullptr ),
      bSavedFreeRideFlag( false ),
      mngScreenshots(),
      CurrentWaitTime( 0.0f ) {
    // FUN_007938fc (inlined into GMFreeRide::GMFreeRide)
    if ( gpMngMapScreenshot == nullptr ) {
        gpMngMapScreenshot = this;
    }
}

MngMapScreenshot::~MngMapScreenshot() {
    // FUN_006ba5e0
    gpMngMapScreenshot = nullptr;
}

bool MngMapScreenshot::initialize( TestDriveGameInstance * ) {
    // FUN_006bb120
    allocScreenshotsData();

    char path[128];
    snprintf( path, sizeof( path ), "%s%s", gpConfig->getResRootPath(),
              "DataBase/MSSDB.bin" );

    void *pContent = nullptr;
    uint32_t contentSize = 0;
    if ( gpFile->loadFile( path, &pContent, &contentSize ) ) {
        loadFromConfigFileBuffer( pContent );
        bIsDirty = false;
        if ( pContent != nullptr ) {
            TestDrive::Free( pContent );
        }
    }

    return true;
}

void MngMapScreenshot::tick( float deltaTime, float ) {
    // FUN_006bb300
#ifdef OTDU_USE_IMGUI
    processDebugRequest();
#endif

    CarState *pLocalCar = gpScenaric->getLocalPlayer().getCarState();

    switch ( State ) {
    case MSS_TakeScreenshots: {
        if ( CurrentScreenshotIndex > MaxScreenshotIndex ||
             CurrentScreenshotIndex >= NumScreenshots ) {
            finishScreenshots();
            return;
        }

        Entry &entry = pScreenshotsData[CurrentScreenshotIndex];
        if ( CurrentCategory != -1 &&
             ( CurrentCategory < 0 ||
               entry.Type != (uint32_t)CurrentCategory ) ) {
            CurrentScreenshotIndex++;
            return;
        }

        Eigen::Vector4f streamingPosition =
            GetMatrixPosition( entry.ViewMatrix );
        gpWorld->setStreamingPosition( &streamingPosition );
        applyEntryCamera( entry );

        CurrentWaitTime = kStreamingSettleTime;
        State = MSS_WaitStreaming;
        return;
    }

    case MSS_WaitStreaming: {
        const Entry &entry = pScreenshotsData[CurrentScreenshotIndex];
        if ( pLocalCar != nullptr ) {
            ShowPlayerCar( false );

            PhysicsVehicle *pPhysicsVehicle = pLocalCar->getPhysicsVehicle();
            if ( pPhysicsVehicle != nullptr ) {
                const Matrix4x4f_t viewMatrix =
                    Eigen::Map<const Matrix4x4f_t>( &entry.ViewMatrix[0][0] );
                PhysicsLock physLock;
                pPhysicsVehicle->resetToTransform( viewMatrix, true );
            }
        }

        const Eigen::Vector4f bubblePosition =
            GetMatrixPosition( entry.ViewMatrix );
        gpAI->resumeTraffic( &bubblePosition );

        if ( !isReadyForScreenshot() ) {
            CurrentWaitTime = kStreamingSettleTime;
            return;
        }

        CurrentWaitTime -= deltaTime;
        if ( CurrentWaitTime > 0.0f ) {
            return;
        }

        CurrentWaitTime = 0.0f;
        EntityCinematic::ShowSpots( false );
        OTDU_UNIMPLEMENTED_ONCE; // TODO: ONLINE (CP2PFreeRideMgr flag)
        ShowPlayerCar( false );
        State = MSS_Screenshot;
        return;
    }

    case MSS_Screenshot: {
        const Entry &entry = pScreenshotsData[CurrentScreenshotIndex];

        // FUN_00608f40: the folder is created by GSFile::SaveFile.
        char folder[128];
        snprintf( folder, sizeof( folder ), "%s%s", gpConfig->getResRootPath(),
                  kCategoryFolders[entry.Type % kNumCategories] );
        char path[268];
        snprintf( path, sizeof( path ), "%s/%s.jpg", folder, entry.pName );

        mngScreenshots.destroy();
        mngScreenshots.create( kCaptureWidth, kCaptureHeight, VF_X8R8G8B8 );
        mngScreenshots.grabAndGetMaterial();
        mngScreenshots.save( path, "", L"", entry.pName, kSavedSize,
                             kSavedSize );
        State = MSS_WaitSaveEnd;
        return;
    }

    case MSS_WaitSaveEnd: {
        const eScreenshotWriteStatus saveState = mngScreenshots.getSaveState();
        if ( saveState == SWS_Success || saveState == SWS_Failure ) {
            CurrentScreenshotIndex++;
            State = MSS_TakeScreenshots;
            gpAI->setTrafficCategories( false, 2u );
        }
        return;
    }

    default:
        return;
    }
}

void MngMapScreenshot::finishScreenshots() {
    // FUN_006bb300 (inlined)
    ShowPlayerCar( true );
    gpCamera->setNextCamera( pOldCamera, 0, kCameraTransitionTime );
    SetHudMoviesHidden( false );
    SetSectorNamesEnabled( true );
    OTDU_UNIMPLEMENTED_ONCE; // TODO: ONLINE (restore CP2PFreeRideMgr flag)
    mngScreenshots.destroy();
    gpCamera->setUseBlur( false );
    EntityCinematic::ShowSpots( true );
    gpAI->resumeTraffic( nullptr );
    gpAI->setPauseTraffic( false );
    SetPapersDetailEnabled( true );

    State = MSS_Idle;
    CurrentScreenshotIndex = -1;
}

void MngMapScreenshot::applyEntryCamera( const Entry &entry ) {
    // FUN_006bb300 (inlined)
    CameraGame *pCamera = gpCamera->getActiveCamera();
    if ( pCamera == nullptr ) {
        return;
    }

    const Matrix4x4f_t camToWorld =
        Eigen::Map<const Matrix4x4f_t>( &entry.ViewMatrix[0][0] );
    Matrix4x4f_t worldToCam = OrthoInvert( camToWorld );
    worldToCam.row( 3 ) = camToWorld.row( 3 );

    pCamera->setCamToWorld( camToWorld );
    pCamera->setWorldToCam( worldToCam );

    const Eigen::Vector4f position = camToWorld.row( 3 ).transpose();
    Eigen::Vector4f target = position - camToWorld.row( 2 ).transpose();
    target.w() = 1.0f;
    pCamera->setPosition( position );
    pCamera->setTarget( target );

    OTDU_UNIMPLEMENTED_ONCE; // TODO: CAMERA (copy 0xb0 from the
                             // previous camera)
    pCamera->setFOV( entry.FOV );
    if ( entry.FarPlane >= 0.0f ) {
        pCamera->setNearPlane( entry.NearPlane );
        pCamera->setFarPlane( entry.FarPlane );
    }
}

bool MngMapScreenshot::isReadyForScreenshot() const {
    // FUN_006ba670
    const Entry &entry = pScreenshotsData[CurrentScreenshotIndex];
    StreamingManager *pStreaming = gpWorld->getStreamingManager();
    const Sector *pSector = pStreaming->sectorManager.getSectorAtPosition(
        GetMatrixPosition( entry.ViewMatrix ) );
    if ( pSector == nullptr || pSector->state != kSectorStateInstalled ) {
        return false;
    }

    if ( gpMngWorld != nullptr && pStreaming->spotStream.isLoading() ) {
        return false;
    }

    OTDU_UNIMPLEMENTED_ONCE; // TODO: RENDER (pending resource list at
                             // 0x100af5c)
    gpFile->flushPendingAsync();
    return true;
}

bool MngMapScreenshot::buildScreenshots( const int32_t category,
                                         const int32_t firstIndex,
                                         const int32_t lastIndex ) {
    // FUN_006bb1c0
    if ( CurrentScreenshotIndex != -1 ) {
        return true;
    }

    CurrentScreenshotIndex = firstIndex;
    MaxScreenshotIndex = ( lastIndex == -1 ) ? NumScreenshots - 1 : lastIndex;
    CurrentCategory = category;

    pOldCamera = gpCamera->getActiveCamera();
    gpCamera->setCameraMode( kScreenshotCaptureCamera, 0,
                             kCameraTransitionTime );
    ShowPlayerCar( false );
    SetHudMoviesHidden( true );
    SetSectorNamesEnabled( false );
    OTDU_UNIMPLEMENTED_ONCE; // TODO: ONLINE (save/clear CP2PFreeRideMgr flag)
    gpAI->setTrafficCategories( false, 2u );
    SetPapersDetailEnabled( false );

    mngScreenshots.create( kSavedSize, kSavedSize, VF_X8R8G8B8 );
    gpCamera->setUseBlur( true );
    State = MSS_TakeScreenshots;
    return true;
}

void MngMapScreenshot::saveSpotParams( const int32_t category,
                                       const char *pName,
                                       CameraGame *pCamera ) {
    // FUN_006b9bb0
    Entry *pEntry = nullptr;
    for ( int32_t i = 0; i < NumScreenshots; i++ ) {
        if ( pScreenshotsData[i].Type == (uint32_t)category &&
             StringCmpI( pScreenshotsData[i].pName, pName ) == 0 ) {
            pEntry = &pScreenshotsData[i];
            break;
        }
    }

    if ( pEntry == nullptr ) {
        if ( NumScreenshots >= MaxNumScreenshots ) {
            return;
        }
        pEntry = &pScreenshotsData[NumScreenshots++];
    }

    pEntry->Type = (uint32_t)category;
    strncpy( pEntry->pName, pName, sizeof( pEntry->pName ) - 1 );
    pEntry->pName[sizeof( pEntry->pName ) - 1] = '\0';

    Eigen::Map<Matrix4x4f_t>( &pEntry->ViewMatrix[0][0] ) =
        pCamera->getCamToWorld();
    pEntry->FOV = pCamera->getFOV();
    if ( gpCamera->isDepthAware() ) {
        pEntry->NearPlane = gpCamera->getNearPlane();
        pEntry->FarPlane = gpCamera->getFarPlane();
    } else {
        pEntry->NearPlane = kUnsetClipPlane;
        pEntry->FarPlane = kUnsetClipPlane;
    }

    for ( int32_t i = 0; i < 3; i++ ) {
        std::memcpy( &pEntry->__PADDING__[i * 4], &kMSSDBPadMarker,
                     sizeof( uint32_t ) );
    }
    pEntry->Unknown = kMSSDBPadMarker;

    bIsDirty = true;
}

bool MngMapScreenshot::IsEntryValid( const uint32_t category,
                                     const char *pName ) {
    // FUN_006ba7a0
    Spots &spots = gpScenaric->getSpotList();
    const int32_t numSpots = (int32_t)spots.getNumWorldSpots();
    char spotName[64];

    switch ( category ) {
    case MSC_Spots:
        for ( int32_t i = 0; i < numSpots; i++ ) {
            const SpotMap *pSpot = spots.getWorldSpot( i );
            if ( pSpot->Type == ST_Challenge ) {
                continue;
            }
            BuildSpotScreenshotName( pSpot, spotName );
            if ( strcmp( spotName, pName ) == 0 ) {
                return true;
            }
        }
        return false;

    case MSC_Challenges:
        for ( int32_t i = 0; i < numSpots; i++ ) {
            const SpotMap *pSpot = spots.getWorldSpot( i );
            if ( pSpot->Type != ST_Challenge ) {
                continue;
            }
            GetSpotHashcodeName( pSpot, spotName );
            if ( strcmp( spotName, pName ) == 0 ) {
                return true;
            }
        }
        return false;

    case MSC_ExtraChallenges:
        for ( int32_t i = 0; i < gpScenaric->getNumExtraChallenges(); i++ ) {
            const Entity *pChallenge = gpScenaric->findExtraChallenge( i );
            if ( pChallenge != nullptr &&
                 pChallenge->getInstanceName() == pName ) {
                return true;
            }
        }
        return false;

    default:
        return false;
    }
}

void MngMapScreenshot::cleanDB() {
    // FUN_006bad50
    const size_t bufferSize = (size_t)MaxNumScreenshots * sizeof( Entry );
    Entry *pCleanData = (Entry *)TestDrive::AllocAligned( bufferSize );
    std::memset( pCleanData, 0, bufferSize );

    int32_t numValid = 0;
    for ( int32_t i = 0; i < NumScreenshots; i++ ) {
        if ( IsEntryValid( pScreenshotsData[i].Type,
                           pScreenshotsData[i].pName ) ) {
            pCleanData[numValid++] = pScreenshotsData[i];
        }
    }

    if ( pScreenshotsData != nullptr ) {
        TestDrive::FreeAligned( pScreenshotsData );
    }
    pScreenshotsData = pCleanData;
    NumScreenshots = numValid;
    bIsDirty = true;
}

bool MngMapScreenshot::SearchCurrentSpot( const int32_t category,
                                          char *pOutName ) {
    // FUN_006ba9e0
    CameraGame *pCamera = gpCamera->getActiveCamera();
    if ( pCamera == nullptr ) {
        return false;
    }

    const Eigen::Vector4f cameraPosition = *pCamera->getWorldPositionRW();
    float nearestDistanceSq = std::numeric_limits<float>::max();

    Spots &spots = gpScenaric->getSpotList();
    const int32_t numSpots = (int32_t)spots.getNumWorldSpots();

    if ( category == MSC_Spots || category == MSC_Challenges ) {
        const bool bChallenges = ( category == MSC_Challenges );
        const SpotMap *pNearest = nullptr;
        for ( int32_t i = 0; i < numSpots; i++ ) {
            const SpotMap *pSpot = spots.getWorldSpot( i );
            if ( ( pSpot->Type == ST_Challenge ) != bChallenges ) {
                continue;
            }

            const Eigen::Vector4f spotPosition( pSpot->X, pSpot->Y, pSpot->Z,
                                                1.0f );
            const float distanceSq =
                ( spotPosition - cameraPosition ).squaredNorm();
            if ( distanceSq < nearestDistanceSq ) {
                nearestDistanceSq = distanceSq;
                pNearest = pSpot;
            }
        }

        if ( pNearest == nullptr ) {
            return false;
        }

        if ( bChallenges ) {
            GetSpotHashcodeName( pNearest, pOutName );
        } else {
            BuildSpotScreenshotName( pNearest, pOutName );
        }
        return true;
    }

    if ( category == MSC_ExtraChallenges ) {
        const Entity *pNearest = nullptr;
        for ( int32_t i = 0; i < gpScenaric->getNumExtraChallenges(); i++ ) {
            const Entity *pChallenge = gpScenaric->findExtraChallenge( i );
            if ( pChallenge == nullptr ) {
                continue;
            }

            const Eigen::Vector4f challengePosition =
                pChallenge->getModelMatrix().row( 3 ).transpose();
            const float distanceSq =
                ( challengePosition - cameraPosition ).squaredNorm();
            if ( distanceSq < nearestDistanceSq ) {
                nearestDistanceSq = distanceSq;
                pNearest = pChallenge;
            }
        }

        if ( pNearest == nullptr ) {
            return false;
        }

        strcpy( pOutName, pNearest->getInstanceName().c_str() );
        return true;
    }

    return false;
}

void MngMapScreenshot::ShowPlayerCar( const bool bShow ) {
    // FUN_006bb010
    CarState *pCarState = gpScenaric->getLocalPlayer().getCarState();
    if ( pCarState == nullptr || pCarState->getVehicleInstance() == nullptr ) {
        return;
    }

    pCarState->getVehicleInstance()->setShouldDraw( bShow );

    PhysicsVehicle *pPhysicsVehicle = pCarState->getPhysicsVehicle();
    const bool bPaused =
        ( pPhysicsVehicle != nullptr ) && pPhysicsVehicle->isPaused();
    if ( bShow ) {
        if ( bPaused ) {
            {
                PhysicsLock physLock;
                pPhysicsVehicle->setPostponedPause( 2 );
            }
            gpScenaric->placeDriver( pCarState, pCarState->getDriver() );
        }
    } else if ( !bPaused && pPhysicsVehicle != nullptr ) {
        {
            PhysicsLock physLock;
            pPhysicsVehicle->setPostponedPause( 1 );
        }

        EntityDriver *pDriver = pCarState->getDriver();
        if ( pDriver != nullptr ) {
            Eigen::Vector4f origin( 0.0f, 0.0f, 0.0f, 1.0f );
            pDriver->propagatePosition( origin, true );
        }
        pCarState->unloadDriver();
    }

    if ( gpMngWorld != nullptr ) {
        gpMngWorld->setDisableStreamingOnCar( !bShow );
    }
}

void MngMapScreenshot::reset() {
    // FUN_006b9cb0
    State = MSS_Idle;
}

void MngMapScreenshot::terminate() {
    // FUN_006ba5c0
    saveConfigFile( true );
    if ( pScreenshotsData != nullptr ) {
        TestDrive::FreeAligned( pScreenshotsData );
    }
}

void MngMapScreenshot::allocScreenshotsData() {
    // FUN_006ba560
    NumScreenshots = 0;
    MaxNumScreenshots = (int32_t)gpScenaric->getSpotList().getNumWorldSpots();

    const size_t bufferSize = (size_t)MaxNumScreenshots * sizeof( Entry );
    pScreenshotsData = (Entry *)TestDrive::AllocAligned( bufferSize );
    std::memset( pScreenshotsData, 0, bufferSize );
}

bool MngMapScreenshot::loadFromConfigFileBuffer( void *pBuffer ) {
    // FUN_006ba150
    if ( pBuffer == nullptr ) {
        return false;
    }

    const uint8_t *pBytes = (const uint8_t *)pBuffer;

    uint32_t header[4];
    std::memcpy( header, pBytes, sizeof( header ) );

    bool bReverseEndianness = false;
    if ( header[3] != kMSSDBMagic ) {
        for ( uint32_t &dword : header ) {
            dword = ReverseEndianness32( dword );
        }
        if ( header[3] != kMSSDBMagic ) {
            return false;
        }
        bReverseEndianness = true;
    }

    MSSDBReader parser{ pBytes + 0x10, bReverseEndianness };
    NumScreenshots = (int32_t)parser.getValue();
    parser.getValue(); // 3 padding markers
    parser.getValue();
    parser.getValue();

    for ( int32_t i = 0; i < NumScreenshots; ++i ) {
        Entry &entry = pScreenshotsData[i];

        entry.Type = parser.getValue();
        parser.getValue(); // "Alignment" block (3 dwords)
        parser.getValue();
        parser.getValue();

        // Names are raw bytes, never byte-swapped.
        std::strncpy( entry.pName, (const char *)parser.pBuffer,
                      sizeof( entry.pName ) );
        parser.pBuffer += sizeof( entry.pName );

        for ( int32_t row = 0; row < 4; ++row ) {
            for ( int32_t col = 0; col < 4; ++col ) {
                uint32_t value = parser.getValue();
                std::memcpy( &entry.ViewMatrix[row][col], &value,
                             sizeof( float ) );
            }
        }

        uint32_t fov = parser.getValue();
        uint32_t nearPlane = parser.getValue();
        uint32_t farPlane = parser.getValue();
        parser.getValue();

        std::memcpy( &entry.FOV, &fov, sizeof( float ) );
        std::memcpy( &entry.NearPlane, &nearPlane, sizeof( float ) );
        std::memcpy( &entry.FarPlane, &farPlane, sizeof( float ) );
    }

    return true;
}

bool MngMapScreenshot::saveConfigFile( bool /*bMapScreenshotsEnabled*/ ) {
    // FUN_006ba0b0
    if ( bIsDirty ) {
        const uint32_t contentSize = (uint32_t)( NumScreenshots * 0xA0 + 0x20 );
        uint8_t *pContent = (uint8_t *)TestDrive::AllocAligned( contentSize );

        saveConfigToMemory( pContent );

        char path[128];
        snprintf( path, sizeof( path ), "%s%s", gpConfig->getResRootPath(),
                  "DataBase/MSSDB.bin" );

        GSFile::SaveFile( path, pContent, contentSize );

        if ( pContent != nullptr ) {
            TestDrive::FreeAligned( pContent );
        }
        bIsDirty = false;
    }

    return true;
}

void MngMapScreenshot::saveConfigToMemory( uint8_t *pOut ) {
    // FUN_006b9cc0
    auto writeSwapped = []( uint8_t *pDst, uint32_t value ) {
        value = ReverseEndianness32( value );
        std::memcpy( pDst, &value, sizeof( value ) );
    };

    writeSwapped( pOut + 0x0, 0x00000001 );
    writeSwapped( pOut + 0x4, 0 );
    writeSwapped( pOut + 0x8, (uint32_t)( NumScreenshots * 0xA0 + 0x14 ) );
    writeSwapped( pOut + 0xC, kMSSDBMagic );

    writeSwapped( pOut + 0x10, (uint32_t)NumScreenshots );
    writeSwapped( pOut + 0x14, kMSSDBPadMarker );
    writeSwapped( pOut + 0x18, kMSSDBPadMarker );
    writeSwapped( pOut + 0x1C, kMSSDBPadMarker );

    uint8_t *pEntryOut = pOut + 0x20;
    for ( int32_t i = 0; i < NumScreenshots; ++i ) {
        const Entry &entry = pScreenshotsData[i];

        // Type + 12-byte "Alignment" block.
        writeSwapped( pEntryOut + 0x0, entry.Type );
        uint32_t pad[3];
        std::memcpy( pad, entry.__PADDING__, sizeof( pad ) );
        writeSwapped( pEntryOut + 0x4, pad[0] );
        writeSwapped( pEntryOut + 0x8, pad[1] );
        writeSwapped( pEntryOut + 0xC, pad[2] );

        // Names are copied verbatim.
        std::memcpy( pEntryOut + 0x10, entry.pName, sizeof( entry.pName ) );

        uint8_t *pMatrixOut = pEntryOut + 0x50;
        for ( int32_t row = 0; row < 4; ++row ) {
            for ( int32_t col = 0; col < 4; ++col ) {
                uint32_t value;
                std::memcpy( &value, &entry.ViewMatrix[row][col],
                             sizeof( value ) );
                writeSwapped( pMatrixOut, value );
                pMatrixOut += sizeof( value );
            }
        }

        uint32_t value;
        std::memcpy( &value, &entry.FOV, sizeof( value ) );
        writeSwapped( pEntryOut + 0x90, value );
        std::memcpy( &value, &entry.NearPlane, sizeof( value ) );
        writeSwapped( pEntryOut + 0x94, value );
        std::memcpy( &value, &entry.FarPlane, sizeof( value ) );
        writeSwapped( pEntryOut + 0x98, value );
        std::memcpy( &value, &entry.Unknown, sizeof( value ) );
        writeSwapped( pEntryOut + 0x9C, value );

        pEntryOut += 0xA0;
    }
}

#ifdef OTDU_USE_IMGUI
static int32_t gCurrentSpotCategory = MngMapScreenshot::MSC_Spots;
static char gCurrentSpotName[64] = {};
static int32_t gMapScreenshotFirstIndex = 0; // DAT_010e8028
static int32_t gMapScreenshotIndex = 0;

static constexpr const char *kCategoryLabels[MngMapScreenshot::kNumCategories] =
    { "Spots", "Challenges", "Extra challenges" };
static constexpr const char *kStateLabels[] = { "Idle", "Take screenshots",
                                                "Wait streaming", "Screenshot",
                                                "Wait save end" };

void MngMapScreenshot::processDebugRequest() {
    if ( debugRequest.bSaveSpotParams ) {
        debugRequest.bSaveSpotParams = false;

        // FUN_006bb950
        CameraGame *pCamera = gpCamera->getActiveCamera();
        if ( gCurrentSpotName[0] != '\0' && pCamera != nullptr ) {
            saveSpotParams( gCurrentSpotCategory, gCurrentSpotName, pCamera );
        }
    }

    if ( debugRequest.bBuild ) {
        debugRequest.bBuild = false;
        buildScreenshots( debugRequest.Category, debugRequest.FirstIndex,
                          debugRequest.LastIndex );
    }
}

void MngMapScreenshot::drawDebug() {
    // FUN_006bb9a0
    ImGui::Text( "State: %s", kStateLabels[State] );
    ImGui::Text( "Screenshot count: %d", NumScreenshots );
    if ( isCapturing() ) {
        ImGui::Text( "Capturing %d / %d", CurrentScreenshotIndex,
                     MaxScreenshotIndex );
        return;
    }

    ImGui::Combo( "Spot Category", &gCurrentSpotCategory, kCategoryLabels,
                  kNumCategories );
    ImGui::InputText( "Current spot", gCurrentSpotName,
                      sizeof( gCurrentSpotName ) );
    if ( ImGui::Button( "Search current spot" ) ) {
        // FUN_006bbbe0
        SearchCurrentSpot( gCurrentSpotCategory, gCurrentSpotName );
    }
    ImGui::SameLine();
    ImGui::BeginDisabled( gCurrentSpotName[0] == '\0' );
    if ( ImGui::Button( "Save spot params" ) ) {
        debugRequest.bSaveSpotParams = true;
    }
    ImGui::EndDisabled();

    ImGui::SeparatorText( "Range" );
    const int32_t lastEntry = Max( NumScreenshots - 1, 0 );
    ImGui::SliderInt( "First", &gMapScreenshotFirstIndex, 0, lastEntry );
    ImGui::SliderInt( "Last", &gMapScreenshotIndex, 0, lastEntry );
    if ( ImGui::Button( "Reset range" ) ) {
        // FUN_006bbb70
        gMapScreenshotFirstIndex = 0;
        gMapScreenshotIndex = NumScreenshots - 1;
    }
    for ( const int32_t index :
          { gMapScreenshotFirstIndex, gMapScreenshotIndex } ) {
        if ( index >= 0 && index < NumScreenshots ) {
            ImGui::Text( "%d - %s", index, pScreenshotsData[index].pName );
        }
    }

    ImGui::SeparatorText( "Build" );
    ImGui::BeginDisabled( NumScreenshots == 0 );
    if ( ImGui::Button( "All" ) ) {
        // FUN_006bbc70
        debugRequest = { true, -1, 0, -1, false };
    }
    ImGui::SameLine();
    if ( ImGui::Button( "Category" ) ) {
        // FUN_006bbc40
        debugRequest = { true, gCurrentSpotCategory, 0, -1, false };
    }
    ImGui::SameLine();
    if ( ImGui::Button( "Range" ) ) {
        // FUN_006bbc10
        debugRequest = { true, -1, gMapScreenshotFirstIndex,
                         gMapScreenshotIndex, false };
    }
    ImGui::EndDisabled();

    ImGui::SeparatorText( "Database" );
    if ( ImGui::Button( "Clean DB" ) ) {
        // FUN_006bbbc0
        cleanDB();
    }
    ImGui::SameLine();
    if ( ImGui::Button( "Save DB" ) ) {
        // FUN_006bbba0
        saveConfigFile( true );
    }
}
#endif
