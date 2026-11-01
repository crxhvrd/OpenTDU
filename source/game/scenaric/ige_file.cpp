#include "shared.h"
#include "ige_file.h"

#include <chrono>
#include <ctime>
#include <filesystem>

#include "ai/ai_road_query.h"
#include "config/gs_config.h"
#include "core/binary_helpers.h"
#include "core/hash/crc32.h"
#include "entity/entity_car_player.h"
#include "entity/entity_check_point.h"
#include "event_parameters.h"
#include "filesystem/gs_file.h"
#include "flash/modal_message_box.h"
#include "game/game_cheats.h"
#include "game/gm_world.h"
#include "game/map/mng_trac_road.h"
#include "gs_scenaric.h"
#include "mng_actor.h"
#include "scenaric_bank.h"

IGEFileRequest gIGEFileRequest = {};

static constexpr uint32_t kMagicMDSC = 0x4d445343; // "MDSC"
static constexpr uint32_t kMagicMDWV = 0x4d445756; // "MDWV"
static constexpr uint32_t kDefaultMissionNameLength = 0x11;
static constexpr uint32_t kMagicENTI = 0x454e5449;   // "ENTI"
static constexpr uint32_t kIGEWriteVersion = 0x217;
static constexpr uint32_t kUserIGEHeaderSize = 8; // CRC32s
static constexpr uint32_t kUnknownEntityType = 0x5f454e54;

static inline uint32_t ReadBigEndian32( const uint8_t *pData ) {
    return ReverseEndianness32( ReadUint32( pData ) );
}

static inline uint16_t ReadBigEndian16( const uint8_t *pData ) {
    return ReverseEndianness16( ReadU16( pData ) );
}

static inline void WriteBigEndian32( uint8_t *&pCursor, const uint32_t value ) {
    Write<uint32_t>( pCursor, 0, ReverseEndianness32( value ) );
    pCursor += 4;
}

static inline void WriteBigEndian16( uint8_t *&pCursor, const uint16_t value ) {
    Write<uint16_t>( pCursor, 0, ReverseEndianness16( value ) );
    pCursor += 2;
}

static inline void WriteChunkTag( uint8_t *pChunk, const uint32_t tag ) {
    // Chunk tags are stored as their character sequence; the chunk size that
    // follows is native (little endian).
    WriteBigEndian32( pChunk, tag );
}

static inline void CloseChunk( uint8_t *pChunk, const uint8_t *pCursor ) {
    Write<uint32_t>( pChunk, 4, uint32_t( pCursor - pChunk ) );
}

static void OnIGEFileLoaded( IGEFileRequest *pRequest ) {
    // FUN_008023c0
    ParseIGEFile( pRequest->pEventParameters, gpScenaric->getIGEBuffer(),
                  pRequest, pRequest->pMngActor, pRequest->Size,
                  pRequest->bParametersOnly );
    SetIGEFileName( pRequest->pEventParameters, pRequest->pName );
    if ( pRequest->pCallback != nullptr ) {
        pRequest->pCallback( 0, pRequest->pUserData );
        pRequest->pCallback = nullptr;
    }
}

bool LoadIGEFile( IGEFileRequest *pRequest,
                  const char *pName,
                  MngActor *pMngActor,
                  const bool bParametersOnly,
                  EventParameters *pEventParameters,
                  const bool bAsync,
                  IGEFileCallback_t pCallback,
                  void *pUserData ) {
    // FUN_008025e0
    void **ppBuffer = gpScenaric->getIGEBufferPointer();
    bool bReleaseBuffer = true;
    if ( bAsync && pRequest->pCallback != nullptr ) {
        bReleaseBuffer = gpFile->cancelAsyncRequest( pRequest );
    }
    if ( bReleaseBuffer ) {
        gpScenaric->releaseIGEBuffer();
    } else {
        *ppBuffer = nullptr;
    }

    char pPath[0x800];
    snprintf( pPath, sizeof( pPath ), "%s%s/%s.%s", gpConfig->getResRootPath(),
              "IGE", pName, "IGE" );

    uint32_t size = 0u;
    if ( bAsync ) {
        pRequest->pUserData = pUserData;
        pRequest->pCallback = pCallback;
        pRequest->bParametersOnly = bParametersOnly;
        pRequest->pEventParameters = pEventParameters;
        pRequest->pMngActor = pMngActor;
        pRequest->pName[sizeof( pRequest->pName ) - 1] = '\0';
        pRequest->pName[0] = '\0';
        strncpy( pRequest->pName, pName, sizeof( pRequest->pName ) );

        const bool bQueued =
            gpFile->loadBank( pPath, ppBuffer, &size, pRequest,
                              [pRequest]( int32_t type, int32_t bSuccess ) {
                                  // LAB_00802410
                                  if ( type == 4 && bSuccess != 0 ) {
                                      OnIGEFileLoaded( pRequest );
                                  }
                              } );
        pRequest->Size = size;
        return bQueued;
    }

    if ( gpFile->loadFile( pPath, ppBuffer, &size ) ) {
        ParseIGEFile( pEventParameters, gpScenaric->getIGEBuffer(), pRequest,
                      pMngActor, size, bParametersOnly );
        SetIGEFileName( pEventParameters, pName );
    }
    return true;
}

void SetIGEFileName( EventParameters *pEventParameters, const char *pName ) {
    // FUN_008017c0
    if ( pEventParameters == nullptr ) {
        pEventParameters = gpActiveGM->getMissionDescription();
    }

    char *pMissionName = pEventParameters->getNameBuffer();
    if ( pMissionName[0] == '\0' || memcmp( pMissionName, "Mission Honolulu",
                                            kDefaultMissionNameLength ) == 0 ) {
        strncpy( pMissionName, pName, GameParameterString::kMaxLength );
    }

    if ( pName != nullptr ) {
        char *pFileName = pEventParameters->getFileNameBuffer();
        pFileName[8] = '\0';
        pFileName[0] = '\0';
        strncpy( pFileName, pName, 8 );
    }
}

bool WriteIGEFile( IGEFileRequest * /*pRequest*/,
                   MngActor *pMngActor,
                   uint8_t *pBuffer,
                   uint32_t *pOutSize ) {
    // FUN_00801870
    uint8_t *pCursor = pBuffer;
    WriteChunkTag( pBuffer, kMagicMDWV );
    pCursor += 8;
    WriteBigEndian32( pCursor, kIGEWriteVersion );
    gpActiveGM->getMissionDescription()->getParameterList().serialize(
        &pCursor );
    CloseChunk( pBuffer, pCursor );

    uint8_t *pEntityChunk = pCursor;
    WriteChunkTag( pEntityChunk, kMagicENTI );
    pCursor += 8;
    uint8_t *pNumEntities = pCursor;
    pCursor += 4;

    uint32_t numEntities = 0;
    for ( int32_t i = 0; i < pMngActor->getActorCount(); i++ ) {
        Entity *pEntity = pMngActor->getActor( i );
        if ( !pEntity->isPrimaryInstance() ) {
            continue;
        }
        numEntities++;

        const uint32_t type = ( pEntity->getDesc() != nullptr )
                                  ? uint32_t( pEntity->getType() )
                                  : kUnknownEntityType;
        WriteBigEndian32( pCursor, type );
        if ( type == uint32_t( eEntityType::ET_CHPT ) ) {
            WriteBigEndian32(
                pCursor,
                uint32_t( static_cast<EntityCheckPoint *>( pEntity )
                              ->getLEDData()
                              ->Type ) );
        }
        WriteEntityControls( &pCursor, pEntity );
    }
    uint8_t *pCountCursor = pNumEntities;
    WriteBigEndian32( pCountCursor, numEntities );
    CloseChunk( pEntityChunk, pCursor );

    *pOutSize = uint32_t( pCursor - pBuffer );
    return true;
}

static int32_t gIGESortMode = 0;     // DAT_010e7c3c
static bool gbIGESortInverted = false; // DAT_010e7c40

static constexpr uint64_t kIGEStorageErrorHashcode = 0x55aff4;
static constexpr wchar_t kIGEContextDisplayName[] = L"Game Mode Editor Files";

static int CompareIGEFiles( const void *pLeft, const void *pRight ) {
    // FUN_00801640
    if ( pLeft == nullptr || pRight == nullptr ) {
        return 0;
    }
    const IGEFileEntry *pA = static_cast<const IGEFileEntry *>( pLeft );
    const IGEFileEntry *pB = static_cast<const IGEFileEntry *>( pRight );

    int result = 0;
    switch ( gIGESortMode ) {
    case 0:
        result = strcmp( pA->pName, pB->pName );
        break;
    case 1:
        result = int( pA->Year ) - int( pB->Year );
        if ( result == 0 ) {
            result = int( pA->Month ) - int( pB->Month );
        }
        if ( result == 0 ) {
            result = int( pA->Day ) - int( pB->Day );
        }
        break;
    case 2:
        result = pA->GameType - pB->GameType;
        break;
    default:
        break;
    }

    if ( gbIGESortInverted ) {
        if ( result > 0 ) {
            return -1;
        }
        if ( result < 0 ) {
            return 1;
        }
    }
    return result;
}

static void StripIGEExtension( char *pFileName ) {
    const size_t length = strlen( pFileName );
    if ( length >= 4 ) {
        pFileName[length - 4] = '\0';
    }
}

static void FillIGEFileEntry( IGEFileEntry &entry,
                              EventParameters &eventParameters ) {
    entry.GameType = eventParameters.getGameType();
    entry.SpotType = eventParameters.getSpotType();
    entry.bTested = eventParameters.getTest().getValue() != 0;
    strncpy( entry.pName, eventParameters.getDisplayName(), 0x40 );
    strncpy( entry.pDescription, eventParameters.getDisplayDescription(),
             0x40 );
}

static void ListDesignerIGEFiles( IGEFileRequest *pRequest ) {
    // FUN_00802770 (designer branch)
    std::error_code error;
    const std::filesystem::path folder =
        std::filesystem::path( gpConfig->getResRootPath() ) / "IGE";
    for ( const std::filesystem::directory_entry &file :
          std::filesystem::directory_iterator( folder, error ) ) {
        if ( pRequest->NumFiles >= IGEFileRequest::kMaxNumFiles ) {
            break;
        }
        if ( !file.is_regular_file() ||
             file.path().extension().string() != ".IGE" ) {
            continue;
        }

        IGEFileEntry &entry = pRequest->Files[pRequest->NumFiles];
        entry = {};
        strncpy( entry.pFileName, file.path().filename().string().c_str(),
                 0x1f );
        StripIGEExtension( entry.pFileName );

        // NOTE: retail reads the date from the edFileFind file info.
        const auto fileTime = file.last_write_time( error );
        const std::time_t writeTime = std::chrono::system_clock::to_time_t(
            std::chrono::time_point_cast<std::chrono::system_clock::duration>(
                fileTime - std::filesystem::file_time_type::clock::now() +
                std::chrono::system_clock::now() ) );
        const std::tm *pTime = std::localtime( &writeTime );
        if ( pTime != nullptr ) {
            entry.Year = uint16_t( pTime->tm_year + 1900 );
            entry.Month = uint8_t( pTime->tm_mon + 1 );
            entry.Day = uint8_t( pTime->tm_mday );
        }
        entry.GameType = -1;
        entry.SpotType = 0xc0c3;
        entry.bUserFile = false;

        EventParameters eventParameters;
        LoadIGEFile( pRequest, entry.pFileName, nullptr, true,
                     &eventParameters, false, nullptr, nullptr );
        FillIGEFileEntry( entry, eventParameters );
        gpScenaric->releaseIGEBuffer();
        pRequest->NumFiles++;
    }
}

static void ListUserIGEFiles( IGEFileRequest *pRequest,
                              const bool bNamesOnly ) {
    // FUN_00802770 (profile branch)
    const int32_t numFiles = gpFile->countFilesInContext( "CURRENT", "*.IGE" );
    for ( int32_t i = 0; i < numFiles; i++ ) {
        if ( pRequest->NumFiles >= IGEFileRequest::kMaxNumFiles ) {
            break;
        }
        char fileName[0x20] = {};
        ContextFileDate date;
        if ( !gpFile->getFileInfoInContext( "CURRENT", "*.IGE", i, fileName,
                                            sizeof( fileName ), &date ) ) {
            continue;
        }

        IGEFileEntry &entry = pRequest->Files[pRequest->NumFiles];
        entry = {};
        strncpy( entry.pFileName, fileName, 0x1f );
        entry.Year = date.Year;
        entry.Month = date.Month;
        entry.Day = date.Day;
        StripIGEExtension( entry.pFileName );
        entry.bUserFile = true;
        if ( bNamesOnly ) {
            pRequest->NumFiles++;
            continue;
        }

        void *pContent = nullptr;
        uint32_t size = 0u;
        if ( !gpFile->loadFileFromContext( "CURRENT", fileName, &pContent,
                                           &size ) ) {
            if ( pContent != nullptr ) {
                TestDrive::Free( pContent );
            }
            continue;
        }

        const uint8_t *pBytes = static_cast<const uint8_t *>( pContent );
        if ( size >= kUserIGEHeaderSize &&
             ReadUint32( pBytes + 4 ) ==
                 TestDriveCRC32Unfolded( pBytes + 8, size - 8 ) &&
             ReadUint32( pBytes ) ==
                 TestDriveCRC32Unfolded( pBytes + 4, size - 4 ) ) {
            EventParameters eventParameters;
            ParseIGEFile( &eventParameters, pBytes + kUserIGEHeaderSize,
                          pRequest, nullptr, size - kUserIGEHeaderSize, true );
            SetIGEFileName( &eventParameters, fileName );
            FillIGEFileEntry( entry, eventParameters );
            pRequest->NumFiles++;
        }
        TestDrive::Free( pContent );
    }
}

void RefreshIGEFileList( IGEFileRequest *pRequest,
                         const bool bOpenContext,
                         const bool bNamesOnly ) {
    // FUN_00802770
    pRequest->NumFiles = 0;
    pRequest->NumSortedFiles = 0;
    if ( gbGMEDesigner ) {
        ListDesignerIGEFiles( pRequest );
    }

    if ( !bOpenContext ) {
        ListUserIGEFiles( pRequest, bNamesOnly );
    } else {
        switch ( gpFile->ensureContext( "IGE", kIGEContextDisplayName ) ) {
        case CST_Error:
            if ( !bNamesOnly ) {
                TestDrive::DisplayModalPrompt(
                    true, kIGEStorageErrorHashcode, 0xb, nullptr, nullptr,
                    pRequest, 0, nullptr, true, false, MPT_Warning,
                    gpFlashMessageBox );
            }
            break;
        case CST_Ready:
            if ( gpFile->openContextAsCurrent( "IGE", kIGEContextDisplayName,
                                               true ) ) {
                ListUserIGEFiles( pRequest, bNamesOnly );
                gpFile->closeCurrentContext();
            }
            break;
        default:
            break;
        }
    }

    gIGESortMode = 0;
    gbIGESortInverted = false;
    qsort( pRequest->Files, size_t( pRequest->NumFiles ), sizeof( IGEFileEntry ),
           CompareIGEFiles );
    for ( int32_t i = 0; i < pRequest->NumFiles; i++ ) {
        pRequest->SortedFiles[pRequest->NumSortedFiles++] = i;
    }
}

void SortIGEFileList( IGEFileRequest *pRequest,
                      const int32_t sortMode,
                      const bool bInverted ) {
    // FUN_00789680 (inlined)
    gIGESortMode = sortMode;
    gbIGESortInverted = bInverted;
    qsort( pRequest->Files, size_t( pRequest->NumFiles ),
           sizeof( IGEFileEntry ), CompareIGEFiles );
}

bool LoadUserIGEFile( IGEFileRequest *pRequest,
                      MngActor *pMngActor,
                      const char *pName,
                      EventParameters *pEventParameters,
                      const bool bParametersOnly ) {
    // FUN_00802450
    gpScenaric->releaseIGEBuffer();

    char fileName[0x800];
    snprintf( fileName, sizeof( fileName ), "%s.%s", pName, "IGE" );
    if ( gpFile->ensureContext( "IGE", nullptr ) == CST_Error ) {
        TestDrive::DisplayModalPrompt( true, kIGEStorageErrorHashcode, 0xb,
                                       nullptr, nullptr, pRequest, 0, nullptr,
                                       true, false, MPT_Warning,
                                       gpFlashMessageBox );
    }

    void *pContent = nullptr;
    uint32_t size = 0u;
    if ( !gpFile->loadFileFromContext( "IGE", fileName, &pContent, &size ) ) {
        if ( pContent != nullptr ) {
            TestDrive::Free( pContent );
        }
        return true;
    }

    const uint8_t *pBytes = static_cast<const uint8_t *>( pContent );
    if ( size >= kUserIGEHeaderSize &&
         ReadUint32( pBytes + 4 ) ==
             TestDriveCRC32Unfolded( pBytes + 8, size - 8 ) &&
         ReadUint32( pBytes ) ==
             TestDriveCRC32Unfolded( pBytes + 4, size - 4 ) ) {
        const uint32_t levelSize = size - kUserIGEHeaderSize;
        void **ppLevelBuffer = gpScenaric->getIGEBufferPointer();
        *ppLevelBuffer = TestDrive::Alloc( levelSize );
        memcpy( *ppLevelBuffer, pBytes + kUserIGEHeaderSize, levelSize );
        TestDrive::Free( pContent );
        ParseIGEFile( pEventParameters, gpScenaric->getIGEBuffer(), pRequest,
                      pMngActor, levelSize, bParametersOnly );
        SetIGEFileName( pEventParameters, pName );
        return true;
    }
    TestDrive::Free( pContent );
    return true;
}

static uint32_t GetIGEBufferSize( const MngActor *pMngActor ) {
    return uint32_t( ( pMngActor->getActorCount() + 4 ) * 500 );
}

static bool SaveUserIGEFile( IGEFileRequest *pRequest,
                             MngActor *pMngActor,
                             const char *pName ) {
    // FUN_00801b20
    uint8_t *pContent = static_cast<uint8_t *>( TestDrive::AllocAligned(
        GetIGEBufferSize( pMngActor ) + kUserIGEHeaderSize, 16 ) );
    uint32_t size = 0u;
    WriteIGEFile( pRequest, pMngActor, pContent + kUserIGEHeaderSize, &size );
    Write<uint32_t>( pContent, 4,
                     TestDriveCRC32Unfolded( pContent + 8, size ) );
    Write<uint32_t>( pContent, 0,
                     TestDriveCRC32Unfolded( pContent + 4, size + 4 ) );

    char fileName[0x20];
    snprintf( fileName, sizeof( fileName ), "%s.%s", pName, "IGE" );
    return gpFile->pushSaveRequest( "IGE", fileName, L"Game Mode Editor Files",
                                    pContent, size + kUserIGEHeaderSize,
                                    true );
}

bool SaveIGEFile( IGEFileRequest *pRequest,
                  MngActor *pMngActor,
                  const char *pName ) {
    // FUN_00801be0
    if ( !gbGMEDesigner ) {
        return SaveUserIGEFile( pRequest, pMngActor, pName );
    }

    uint8_t *pContent = static_cast<uint8_t *>(
        TestDrive::AllocAligned( GetIGEBufferSize( pMngActor ), 16 ) );
    uint32_t size = 0u;
    WriteIGEFile( pRequest, pMngActor, pContent, &size );

    char pPath[0x800];
    snprintf( pPath, sizeof( pPath ), "%s%s/%s.%s", gpConfig->getResRootPath(),
              "IGE", pName, "IGE" );
    const bool bSaved = GSFile::SaveFile( pPath, pContent, size );
    TestDrive::FreeAligned( pContent );
    return bSaved;
}

void WriteEntityControls( uint8_t **ppCursor, Entity *pEntity ) {
    // FUN_00423df0
    uint8_t *&pCursor = *ppCursor;
    const int32_t numControls =
        pEntity->gmeEditionFactory( 0, GCR_NumControls, nullptr );
    WriteBigEndian32( pCursor, uint32_t( numControls ) );

    for ( int32_t i = 0; i < numControls; i++ ) {
        WriteBigEndian32(
            pCursor,
            uint32_t( pEntity->gmeEditionFactory( i, GCR_GetID, nullptr ) ) );
        const int32_t size =
            pEntity->gmeEditionFactory( i, GCR_GetSize, nullptr );
        if ( pEntity->gmeEditionFactory( i, GCR_GetVersion, nullptr ) != 0 ) {
            WriteBigEndian16( pCursor, uint16_t( pEntity->gmeEditionFactory(
                                           i, GCR_GetVersion, nullptr ) ) );
            WriteBigEndian16( pCursor, uint16_t( size ) );
        }
        uint8_t *pData = pCursor;
        pEntity->gmeEditionFactory( i, GCR_Serialize, &pData );
        pCursor += size;
    }
}

void ReadEntityControls( const uint8_t **ppCursor,
                         Entity *pEntity,
                         uint32_t *pVersion ) {
    // FUN_00423b60
    const uint8_t *&pCursor = *ppCursor;
    const int32_t numControls = int32_t( ReadBigEndian32( pCursor ) );
    pCursor += 4;

    for ( int32_t i = 0; i < numControls; i++ ) {
        const uint32_t controlID = ReadBigEndian32( pCursor );
        pCursor += 4;

        const int32_t controlIndex = pEntity->gmeGetControlIndex( controlID );
        if ( controlIndex < 0 ) {
            if ( *pVersion >= 100000 ) {
                pCursor += 4;
            } else {
                pCursor += 2;
                const uint16_t size = ReadBigEndian16( pCursor );
                pCursor += 2 + size;
            }
            continue;
        }

        const int32_t controlVersion =
            pEntity->gmeEditionFactory( controlIndex, GCR_GetVersion, nullptr );
        uint16_t size = 0;
        if ( controlVersion != 0 && *pVersion != 0 && *pVersion < 100000 ) {
            pCursor += 2;
            size = ReadBigEndian16( pCursor );
            pCursor += 2;
        } else {
            size = uint16_t( pEntity->gmeEditionFactory(
                controlIndex, GCR_GetSize, nullptr ) );
        }

        const uint8_t *pData = pCursor;
        pEntity->gmeEditionFactory( controlIndex, GCR_Deserialize, &pData );
        pCursor += size;

        if ( controlVersion == 0 ) {
            GameParameter *pVersionParameter = nullptr;
            pEntity->gmeEditionFactory( controlIndex, GCR_GetAttribute,
                                        &pVersionParameter );
            *pVersion = uint32_t( pVersionParameter->getValue() );
        }
    }
}

static Entity *FindCheckpointTemplate( const uint8_t *pControls,
                                       ScenaricBank *pCheckpointBank,
                                       const uint32_t checkpointType,
                                       float &outRoadWidth ) {
    // FUN_00801cb0 (inlined)
    bool bBigCheckpoint = false;
    const int32_t numControls = int32_t( ReadBigEndian32( pControls ) );
    pControls += 4;
    if ( numControls >= 1 && ReadBigEndian32( pControls ) == 0 ) {
        pControls += 8;

        Matrix4x4f_t matrix = Matrix4x4f_t::Identity();
        SerializedMatrix serializedMatrix( &matrix );
        serializedMatrix.deserialize( const_cast<uint8_t *>( pControls ) );

        AiRoadProfile *pProfile = nullptr;
        AiRoadLocation location;
        ProjectOnRoad( matrix.row( 3 ).transpose(), &pProfile, nullptr, false,
                       &location );
        if ( pProfile != nullptr ) {
            if ( pProfile->NumRightLanes + pProfile->NumLeftLanes > 2 ) {
                bBigCheckpoint = true;
            }
            outRoadWidth = pProfile->OuterWidth;
        }
    }
    return pCheckpointBank->findCheckpointTemplate( checkpointType,
                                                    bBigCheckpoint );
}

bool ParseIGEFile( EventParameters *pEventParameters,
                   const uint8_t *pBuffer,
                   IGEFileRequest * /*pRequest*/,
                   MngActor *pMngActor,
                   const uint32_t /*size*/,
                   const bool bParametersOnly ) {
    // FUN_00801cb0
    if ( pBuffer == nullptr ) {
        return false;
    }
    if ( pEventParameters == nullptr && gpActiveGM != nullptr ) {
        pEventParameters = gpActiveGM->getMissionDescription();
    }
    pEventParameters->reset();

    const uint8_t *pCursor = pBuffer + 8;

    uint32_t version = 0u;
    const uint32_t magic = ReadBigEndian32( pBuffer );
    if ( magic == kMagicMDSC || magic == kMagicMDWV ) {
        if ( magic == kMagicMDWV ) {
            version = ReadBigEndian32( pCursor );
            pCursor += 4;
        }
        pEventParameters->getParameterList().deserialize( &pCursor, &version );
    }

    if ( pEventParameters->getGameType() == -1 ) {
        pEventParameters->getGameTypeParameter().setValue(
            EventParameters::EGT_TimeAttack, false );
        pEventParameters->getSpotTypeParameter().setValue(
            EventParameters::EST_Solo, false );
    } else if ( gbAllChallengesSolo ) {
        pEventParameters->getSpotTypeParameter().setValue(
            EventParameters::EST_Solo, false );
    }

    if ( bParametersOnly ) {
        return true;
    }

    // "ENTI" chunk header
    pCursor += 8;
    const int32_t numEntities = int32_t( ReadBigEndian32( pCursor ) );
    pCursor += 4;

    ScenaricBank *pCheckpointBank = gpScenaric->getCheckpointBank();
    const bool bInstantiate =
        pCheckpointBank != nullptr && !gpScenaric->isLobbyMapOpened();

    int32_t numCheckpoints = 0;
    for ( int32_t i = 0; i < numEntities; i++ ) {
        EntityCheckPoint checkpoint;
        EntityCarPlayer entity;

        const eEntityType type = eEntityType( ReadBigEndian32( pCursor ) );
        pCursor += 4;

        Entity *pTemplate = nullptr;
        float roadWidth = 0.0f;
        if ( type == eEntityType::ET_CHPT ) {
            const uint32_t checkpointType = ReadBigEndian32( pCursor );
            pCursor += 4;
            if ( bInstantiate ) {
                pTemplate = FindCheckpointTemplate( pCursor, pCheckpointBank,
                                                    checkpointType, roadWidth );
            } else {
                pTemplate = &checkpoint;
            }
        } else if ( bInstantiate ) {
            pTemplate = pCheckpointBank->getFirst( type );
        } else {
            pTemplate = &entity;
        }

        if ( pTemplate == nullptr ) {
            continue;
        }

        if ( bInstantiate ) {
            Entity *pInstance = pMngActor->instanciateActor( pTemplate );
            pInstance->setPrimaryInstance( true );
            ReadEntityControls( &pCursor, pInstance, &version );

            Matrix4x4f_t transform = pInstance->getModelMatrix();
            Matrix4x4f_t identity = Matrix4x4f_t::Identity();
            pInstance->setTransform( identity, true );
            if ( pInstance->getType() == eEntityType::ET_CHPT ) {
                EntityCheckPoint *pCheckpoint =
                    static_cast<EntityCheckPoint *>( pInstance );
                pCheckpoint->setRoadWidth( roadWidth );

                const uint32_t ledType = pCheckpoint->getLEDData()->Type;
                if ( ledType == 1 || ledType == 2 ) {
                    Matrix4x4f_t roadFrame = transform;
                    ProjectFrameOnRoad( transform, roadFrame, nullptr, true );
                    transform = roadFrame;
                }
            }
            pInstance->propagateTransform( transform, true );
            continue;
        }

        ReadEntityControls( &pCursor, pTemplate, &version );
        if ( type != eEntityType::ET_CHPT ) {
            continue;
        }

        numCheckpoints++;
        const int32_t checkpointIndex = checkpoint.getIndex();
        const Matrix4x4f_t checkpointMatrix = checkpoint.getModelMatrix();
        if ( gpMngTracRoad != nullptr ) {
            const Eigen::Vector4f position =
                checkpointMatrix.row( 3 ).transpose();
            ProjectOnRoad( position, nullptr, nullptr, false,
                           gpMngTracRoad->getWaypoint( checkpointIndex - 1 ) );
            gpMngTracRoad->setNumWaypoints( numCheckpoints );
        }
    }

    return true;
}
