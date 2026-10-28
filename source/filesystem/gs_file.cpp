#include "shared.h"
#include "gs_file.h"

#include "config/gs_config.h"
#include "core/mutex.h"
#include "file_direct_access.h"
#include "file_map.h"
#include "file_path_register.h"
#include "flash/implementation/flash_player.h"
#include "flash/modal_message_box.h"
#include "gs_dirty_disk.h"
#include "mod_installer.h"
#include "mod_manager.h"
#include "player_data/user_profile.h"
#include "system/gs_pc_options.h"
#include "tdu_instance.h"

#include "gs_boot_datas.h"

GSFile *gpFile = nullptr;

static TestDriveMutex gUnknownMutex = {};

TestDriveMutex *gpFilemapMutex = nullptr; // TODO: Define the thread routine in
                                          // GSFile to avoid doing this crap

static constexpr const char *kCameraBinPath = "Euro/BNK/DataBase/Cameras.bin";
static constexpr uint32_t kMaxFilemapCount = 0x80;
static constexpr uint32_t kFirstPackFilemapIndex = 2;
static constexpr const char *kModsFolderPath = "opentdu_data/mods";
static constexpr const char *kPackMapFilename = "CarPack.map";
static constexpr const char *kPackBigFilename = "CarPack.big";

static constexpr const wchar_t *kSaveQueueNoName = L"NO NAME";
static constexpr uint32_t kSaveDeviceFreeSpace = 0xffffffffu; // FUN_00608570
static constexpr uint64_t kSaveQueueNoProfileHashcode = 0x33d6b4a;
static constexpr uint64_t kSaveQueueNotEnoughSpaceHashcode = 0x236d9b92;
static constexpr uint64_t kSaveQueueCorruptedFileHashcode = 0x55aff4;
static constexpr uint64_t kSaveQueueWriteFailedHashcode = 0x24063212;
static constexpr uint64_t kSaveQueueDeviceErrorHashcode = 0x36b320a;
static constexpr int32_t kSaveQueuePromptWriteFailed = 0;
static constexpr int32_t kSaveQueuePromptCorruptedFile = 1;
static constexpr int32_t kSaveQueuePromptNotEnoughSpace = 2;
static constexpr int32_t kSaveQueuePromptNoProfile = 4;
static constexpr int32_t kSaveQueuePromptDeviceError = 5;

static TestDriveThread::ThreadReturnType_t ThreadRoutine( void *in_param_1 ) {
    WorkerThreadContext *param_1 = (WorkerThreadContext *)in_param_1;

    FileDirectAccess local_c48 = {};

    do {
        param_1->pEvent->waitForEvent();
        gpFilemapMutex->lock();
        if ( param_1->pAsyncData->pFilemap == nullptr ) {
            local_c48.bNeedName = true;
        } else {
            local_c48.bNeedName = param_1->pAsyncData->pFilemap->bNeedName;

            if ( param_1->pAsyncData->pFilemap->Context.bIsValid ) {
                bool bVar3 = local_c48.openContext(
                    0ull,
                    param_1->pAsyncData->pFilemap->Context.Filename.c_str(),
                    param_1->pAsyncData->pFilemap->Context.Source );

                if ( !bVar3 ) {
                    param_1->bJobDone = true;
                    gpFilemapMutex->unlock();
                    continue;
                }
            }
        }

        bool bVar4 = false;
        {
            ScopedMutexLock unknownLock( &gUnknownMutex );

            if ( param_1->pAsyncData->bCompressed ) {
                bVar4 = local_c48.loadzCompressFileInMem(
                    param_1->pAsyncData->Filename.c_str(),
                    param_1->pAsyncData->Size, param_1->pAsyncData->Offset,
                    &param_1->pAsyncData->pBuffer, nullptr );
            } else {
                bVar4 = local_c48.loadContentIntoMemory(
                    param_1->pAsyncData->Filename.c_str(),
                    param_1->pAsyncData->Size, param_1->pAsyncData->Offset,
                    &param_1->pAsyncData->pBuffer, nullptr );
            }
        }

        if ( !bVar4 && param_1->pAsyncData->pFilemap != nullptr ) {
            gpDirtyDisk->onDataLoadFailure();
        }

        if ( param_1->pAsyncData->pFilemap != nullptr &&
             param_1->pAsyncData->pFilemap->Context.bIsValid ) {
            local_c48.reset();
        }

        gpFilemapMutex->unlock();
        param_1->bJobDone = true;
    } while ( true );

    return (TestDriveThread::ThreadReturnType_t)0;
}

GSFile::GSFile()
    : pFilemapMutex( new TestDriveMutex() ),
      pWorkerThread( new TestDriveThread() ),
      initStep( 0 ),
      bFilemapActive( false ),
      bTaskInProgress( false ),
      bIsCurrentContextReady( false ),
      bDeletingInProgress( false ),
      bSaveDeviceError( false ) {
    gpFile = this;

    filemaps.resize( kMaxFilemapCount );
    for ( FileMap &filemap : filemaps ) {
        filemap.initializeCRCLUT();
    }

    gpFilemapMutex = pFilemapMutex;

    workerThreadContext.pEvent = new TestDriveEvent();
    pWorkerThread->initialize( (TestDriveThread::StartRoutine_t)ThreadRoutine,
                               &workerThreadContext, 0, 0 );
}

GSFile::~GSFile() {
    for ( FileMap &filemap : filemaps ) {
        filemap.releaseMap();
    }

    delete pFilemapMutex;
}

bool GSFile::initialize( TestDriveGameInstance * ) {
    bool bInitialized = retrieveSaveFolder();

    hardDriveVFS.initialize();
    gVirtualFileSystemRegister.registerVFS( &hardDriveVFS );

    gUserProfileRegister.deserialize();

    // std::string defaultRootPath = "<pc>";
    // std::string local_200;
    // VirtualFileSystem* peVar1 =
    // gVirtualFileSystemRegister.get(defaultRootPath, local_200, true); if
    // (peVar1 != nullptr) {
    //     gFilePathRegister.registerPath(local_200);
    //     gFilePathRegister.setDefaultPath(local_200);
    //     peVar1->setDefaultPath(local_200.c_str());
    // }

    return bInitialized;
}

void GSFile::tick( float totalTime, float deltaTime ) {
    if ( gpTestDriveInstance == nullptr ||
         !gpTestDriveInstance->hasRequestedExit() ) {
        if ( initStep == 1 ) {
            // gpFlashMessageBox->display(0x3b863c2, 3.0f, 0, nullptr, nullptr,
            // false);

            // puVar1 = (undefined4 *)&gGSDatabase.field_0x43fc;
            // do {
            //     puVar1[-1] = 0;
            //     *puVar1 = 0;
            //     puVar1 = puVar1 + 2;
            // } while ((int)puVar1 < 0x11f30bc);
            initStep = 2;
        } else if ( initStep == 2 ) {
            initStep = 0;
        }
    }

    syncAsyncWorkerThread();
    if ( gpTestDriveInstance != nullptr ) {
        gpTestDriveInstance->updateFileInstanciation();
    }
    tickSaveQueue();
}

void GSFile::terminate() { pFilemapMutex->terminate(); }

bool GSFile::loadFile( const char *pFilename,
                       void **pOutContent,
                       uint32_t *pOutContentSize ) {
    // FUN_009a5d20
    *pOutContent = (void *)0x0;
    if ( pOutContentSize != nullptr ) {
        *pOutContentSize = 0u;
    }

    FileMapEntry *pEntry = nullptr;
    FileMap *pFilemap = nullptr;
    uint32_t fileSize = 0;
    if ( !bFilemapActive ) {
        bool bVar2 = findFileOutsideFilemap( pFilename, &fileSize );
        if ( !bVar2 ) {
            return false;
        }
    } else {
        if ( !resolveFileInFilemaps( pFilename, &pEntry, &pFilemap,
                                     &fileSize ) ) {
            return false;
        }
    }

    // TODO: What's the 0x801 required for?

    void *pFileAlloc = TestDrive::Alloc( fileSize + 0x801 );
    OTDU_LOG_DEBUG( "Allocated %llu for file '%s'\n", fileSize + 0x801,
                    pFilename );
    *pOutContent = pFileAlloc;
    if ( pFileAlloc != nullptr ) {
        *(uint32_t *)*pOutContent = fileSize;

        bool bLoadedContentFromDisk =
            loadFileRaw( pFilename, pFilemap, pEntry, fileSize, pOutContent );
        if ( bLoadedContentFromDisk ) {
            uint8_t *pFileContentByte = (uint8_t *)*pOutContent;
            pFileContentByte[fileSize] = 0;
            if ( pOutContentSize != nullptr ) {
                *pOutContentSize = fileSize;
            }
            return true;
        }

        TestDrive::Free( pFileAlloc );
        *pOutContent = (void *)0x0;

        return false;
    }

    return false;
}

bool GSFile::loadBank( const char *pFilename,
                       void **pOutContent,
                       uint32_t *pOutContentSize,
                       uint32_t param_5 ) {
    // FUN_009a5a70
    if ( pOutContentSize != nullptr ) {
        *pOutContentSize = 0u;
    }

    FileMapEntry *pEntry = nullptr;
    FileMap *pFilemap = nullptr;
    uint32_t fileSize = 0;
    if ( !bFilemapActive ) {
        bool bVar2 = findFileOutsideFilemap( pFilename, &fileSize );
        if ( !bVar2 ) {
            return false;
        }
    } else {
        if ( !resolveFileInFilemaps( pFilename, &pEntry, &pFilemap,
                                     &fileSize ) ) {
            return false;
        }
    }

    if ( param_5 == 0 ) {
        OTDU_ASSERT( pOutContent && *pOutContent == nullptr );
        *pOutContent = TestDrive::Alloc( fileSize + 0x1a1 );
        OTDU_LOG_DEBUG( "Allocated %llu for file '%s'\n", fileSize + 0x1a1,
                        pFilename );
    } else if ( fileSize + 0x1a1 > param_5 ) {
        OTDU_LOG_ERROR( "Failed to load bank: input buffer is too small!\n" );
        return false;
    }
    OTDU_ASSERT( pOutContent && *pOutContent != nullptr );

    if ( pOutContentSize != nullptr ) {
        *pOutContentSize = fileSize;
    }

    return loadFileRaw( pFilename, pFilemap, pEntry, fileSize, pOutContent );
}

bool GSFile::loadBank( const char *pFilename,
                       void **pOutContent,
                       uint32_t *pOutContentSize,
                       void *pUserData,
                       AsyncFileOpen::CompletionCallback_t onComplete ) {
    // FUN_009a4d40
    if ( pOutContentSize != nullptr ) {
        *pOutContentSize = 0;
    }

    FileMapEntry *pEntry = nullptr;
    FileMap *pFilemap = nullptr;
    uint32_t fileSize = 0;
    if ( !bFilemapActive ) {
        if ( !findFileOutsideFilemap( pFilename, &fileSize ) ) {
            return false;
        }
    } else {
        if ( !resolveFileInFilemaps( pFilename, &pEntry, &pFilemap,
                                     &fileSize ) ) {
            return false;
        }
    }

    // Buffer: the caller supplies an external pool, otherwise allocate.
    if ( *pOutContent == nullptr ) {
        *pOutContent = TestDrive::Alloc( fileSize + 0x1a1 );
        if ( *pOutContent == nullptr ) {
            return false;
        }
    }
    if ( pOutContentSize != nullptr ) {
        *pOutContentSize = fileSize;
    }

    // Resolve the real on-disk name / offset / compression (mirrors the prep in
    // loadFileRaw; the worker thread performs the actual read into pBuffer).
    std::string resolvedName;
    uint32_t offset = 0;
    bool bCompressed = false;
    if ( pFilemap == nullptr || pEntry == nullptr ) {
        FileIterator it;
        it.findFirstMatch( pFilename );
#ifdef OTDU_UNIX
        resolvedName = it.ItInfos.Filename;
#else
        resolvedName = it.SearchDirectory;
        resolvedName += "/";
        resolvedName += it.SearchPattern;
#endif
    } else {
        bCompressed = pEntry->bCompressed;
        if ( !pFilemap->bUseBigFile ) {
            resolvedName = pFilemap->resolveName( pFilename, bCompressed );
        } else {
            offset = pEntry->Offset;
            resolvedName = pFilemap->FilenameBigFile;
        }
    }

    // Queue the async read.
    AsyncFileOpen *pRequest = new AsyncFileOpen();
    pRequest->Filename = resolvedName;
    pRequest->Size = ( fileSize + 0x7ffU ) & 0xfffff800;
    pRequest->Offset = offset;
    pRequest->bCompressed = bCompressed;
    pRequest->pFilemap = pFilemap;
    pRequest->pBuffer = *pOutContent;
    pRequest->pUserData = pUserData;
    pRequest->pCompletionCallback = std::move( onComplete );
    pRequest->bInUse = true;

    asyncData.insert( pRequest );
    bTaskInProgress = true;
    return true;
}

bool GSFile::retrieveSaveFolder() {
    savegameFolder = L"\\";

    // TODO: Move platform specific under system/
#if defined( OTDU_WIN32 )
    HANDLE tokenHandle = NULL;
    HANDLE ProcessHandle = GetCurrentProcess();
    OpenProcessToken( ProcessHandle, 0x200ec, &tokenHandle );

    PWSTR pSavegameFolderPath = nullptr;
    HRESULT operationResult =
        SHGetKnownFolderPath( FOLDERID_Documents, KF_FLAG_DEFAULT, tokenHandle,
                              &pSavegameFolderPath );
    if ( operationResult != S_OK ) {
        return false;
    }

    savegameFolder = pSavegameFolderPath;
    savegameFolder += L"\\Test Drive Unlimited\\";
#elif defined( OTDU_UNIX )
    std::string homeDirectory = getenv( "HOME" );
    if ( homeDirectory.empty() ) {
        return false;
    }

    savegameFolder = std::wstring( homeDirectory.begin(), homeDirectory.end() );
#ifdef OTDU_MACOS
    savegameFolder += L"/Library/Application Support";
#endif

    savegameFolder += L"/Test Drive Unlimited/";
#else
    static_assert( false, "Unimplemented" );
    return false;
#endif

    OTDU_LOG_DEBUG( "Active savegame folder: '%S'\n", savegameFolder.c_str() );

    std::error_code errorCode;
    for ( const wchar_t *pSubFolder :
          { L"", L"savegame", L"publisher", L"marketplace" } ) {
        std::filesystem::create_directories( savegameFolder + pSubFolder,
                                             errorCode );
        if ( errorCode ) {
            OTDU_LOG_ERROR( "Failed to create save folder '%S%S': %s\n",
                            savegameFolder.c_str(), pSubFolder,
                            errorCode.message().c_str() );
        }
    }

    return true;
}

bool GSFile::findFileOutsideFilemap( const char *pFilename,
                                     uint32_t *pOutFilesize ) {
    OTDU_ASSERT( pOutFilesize );

    FileIterator it;
    bool bVar2 = it.findFirstMatch( pFilename );
    if ( !bVar2 ) {
        OTDU_LOG_ERROR( "Failed to open file '%s': file not found\n",
                        pFilename );
        return false;
    }

    uint32_t fileSize = it.getSize();
    *pOutFilesize = fileSize;
    it.closeHandle();

    if ( fileSize == 0 ) {
        OTDU_LOG_ERROR( "Failed to open file '%s': file size is 0\n",
                        pFilename );
        return false;
    }

    return true;
}

bool GSFile::resolveFileInFilemaps( const char *pFilename,
                                    FileMapEntry **ppOutEntry,
                                    FileMap **ppOutFilemap,
                                    uint32_t *pOutFileSize ) {
    const bool bFound =
        findMapEntriesForFile( pFilename, ppOutEntry, ppOutFilemap );

    if ( bFound && ( *ppOutEntry )->Size != 0 ) {
        *pOutFileSize = ( *ppOutEntry )->Size;
        return true;
    }

    *ppOutEntry = nullptr;
    *ppOutFilemap = nullptr;

    return findFileOutsideFilemap( pFilename, pOutFileSize );
}

bool GSFile::findMapEntriesForFile( const char *pEntryName,
                                    FileMapEntry **ppOutMapEntry,
                                    FileMap **ppOutMap ) {
    // FUN_009a4bd0
    ScopedMutexLock filemapLock( pFilemapMutex );
    *ppOutMapEntry = nullptr;
    *ppOutMap = nullptr;

    FileMap *pBestMap = nullptr;
    FileMapEntry *pBestEntry = nullptr;
    int32_t filemapIndex = -1;
    for ( FileMap &filemap : filemaps ) {
        filemapIndex++;

        if ( filemap.MapSize == 0 ) {
            continue;
        }

        FileMapEntry *pFoundEntry = nullptr;
        bool bFoundEntryInMap =
            filemap.findEntryInSet( pEntryName, &pFoundEntry );
        if ( bFoundEntryInMap ) {
            if ( pBestEntry != nullptr ) {
                if ( pBestEntry->Version < pFoundEntry->Version ) {
                    OTDU_LOG_DEBUG( "(found best revision=>%08X on map %d)",
                                    pFoundEntry->Version, filemapIndex );
                    pBestEntry = pFoundEntry;
                    pBestMap = &filemap;
                }
            } else {
                pBestEntry = pFoundEntry;
                pBestMap = &filemap;
                if ( pFoundEntry->Version != 0 ) {
                    OTDU_LOG_DEBUG( "(found revision=>%08X on map %d)",
                                    pFoundEntry->Version, filemapIndex );
                }
            }
        }
    }

    if ( pBestEntry == nullptr ) {
        *ppOutMap = nullptr;
        *ppOutMapEntry = nullptr;
        return false;
    }

    *ppOutMap = pBestMap;
    *ppOutMapEntry = pBestEntry;
    return true;
}

bool GSFile::loadFileRaw( const char *pFilename,
                          FileMap *pFilemap,
                          FileMapEntry *pEntry,
                          const uint32_t size,
                          void **ppOutContent ) {
    // FUN_009a5730
    FileDirectAccess local_c48;
    bTaskInProgress = true;

    std::string filename = "";
    uint32_t offset = 0;
    bool bCompressed = false;
    if ( pFilemap == nullptr || pEntry == nullptr ) {
        FileIterator it;
        it.findFirstMatch( pFilename );

#ifdef OTDU_UNIX
        filename = it.ItInfos.Filename;
#else
        filename = it.SearchDirectory;
        filename += "/";
        filename += it.SearchPattern;
#endif
        tmpFilename.clear();
        tmpFilename += pFilename;

        local_c48.bNeedName = true;
    } else {
        local_c48.bNeedName = pFilemap->bNeedName;
        bCompressed = pEntry->bCompressed;

        if ( !pFilemap->bUseBigFile ) {
            filename = pFilemap->resolveName( pFilename, bCompressed );
        } else {
            offset = pEntry->Offset;
            filename = pFilemap->FilenameBigFile;
        }
    }

    bool bVar1 = false;
    if ( pFilemap == nullptr || !pFilemap->Context.bIsValid ) {
        bVar1 = true;
    } else {
        bVar1 = local_c48.openContext( 0, pFilemap->Context.Filename.c_str(),
                                       pFilemap->Context.Source );
    }

    {
        ScopedMutexLock unknownMutexLock( &gUnknownMutex );
        if ( bVar1 ) {
            if ( bCompressed ) {
                bVar1 = local_c48.loadzCompressFileInMem(
                    filename.c_str(), size + 0x7ffU & 0xfffff800, offset,
                    ppOutContent, nullptr );
            } else {
                bVar1 = local_c48.loadContentIntoMemory(
                    filename.c_str(), size + 0x7ffU & 0xfffff800, offset,
                    ppOutContent, nullptr );
            }
        }
    }
    if ( !bVar1 ) {
        if ( pFilemap != nullptr ) {
            gpDirtyDisk->onDataLoadFailure();
        }
    }

    if ( pFilemap != nullptr && pFilemap->Context.bIsValid ) {
        local_c48.reset();
    }

    bTaskInProgress = false;
    return bVar1;
}

int32_t GSFile::appendMapFile( const char *pVFSName,
                               const char *pPackBigName,
                               const char *pPackMapName,
                               FileDirectAccess *pContext,
                               const int32_t maxFilemapCount,
                               const bool bReadFromVFS,
                               int32_t filemapIndex ) {
    // FUN_009a4530
    if ( filemapIndex < maxFilemapCount ) {
        while ( filemaps[filemapIndex].MapSize != 0 ) {
            filemapIndex++;
            if ( maxFilemapCount <= filemapIndex ) {
                return -1;
            }
        }
    }

    OTDU_ASSERT( filemapIndex >= 0 && filemapIndex < (int32_t)filemaps.size() );
    FileMap &filemap = filemaps[filemapIndex];

    FileDirectAccess file;
    file.bNeedName = bReadFromVFS;
    if ( pContext != nullptr ) {
        file.copyContextFrom( *pContext );
    }

    bool bMounted = false;
    if ( file.openFile( pPackMapName, true, false ) ) {
        const uint32_t mapFileSize = file.getFileSize();
        if ( mapFileSize != 0 ) {
            void *pMapMemory =
                TestDrive::AllocAligned( mapFileSize + 0x801, 0x10 );
            if ( pMapMemory != nullptr ) {
                if ( file.readFromFile( pMapMemory, mapFileSize ) ==
                     mapFileSize ) {
                    filemap.bNeedName = bReadFromVFS;

                    if ( filemap.initFromMem( pMapMemory, mapFileSize ) ) {
                        filemap.setHeaderToSkip( pVFSName );
                        bMounted = true;

                        if ( pPackBigName != nullptr ) {
                            filemap.setBigFileName( pPackBigName );
                        }
                    } else {
                        OTDU_LOG_ERROR( "Failed to mount '%s': not a valid "
                                        "MAP3/MAP4 file\n",
                                        pPackMapName );
                        TestDrive::FreeAligned( pMapMemory );
                    }
                } else {
                    OTDU_LOG_ERROR( "Failed to read '%s' (%u bytes)\n",
                                    pPackMapName, mapFileSize );
                    TestDrive::FreeAligned( pMapMemory );
                }
            }
        }
    }
    file.close();

    if ( !bFilemapActive ) {
        bFilemapActive = bMounted;
    }

    return bMounted ? filemapIndex : -1;
}

void GSFile::initCarPacks( bool bApplyContent ) {
    // FUN_009a6040
    ScopedMutexLock filemapLock( pFilemapMutex );

    for ( uint32_t i = kFirstPackFilemapIndex; i < kMaxFilemapCount; i++ ) {
        filemaps[i].releaseMap();
    }

    gpModManager->refresh();
    std::vector<ModPack> &packs = gpModManager->getPacks();

    gpModInstaller->reset();

    OTDU_LOG_INFO( "=>EnumerateContext() => %s, %zu items\n",
                   packs.empty() ? "FALSE" : "TRUE", packs.size() );

    uint32_t numMounted = 0u;
    for ( ModPack &pack : packs ) {
        OTDU_LOG_INFO( "  -Found Pack: Name:%S  Enabled:%d Conflicting:%d "
                       "Mandatory:%d\n",
                       pack.DisplayName.c_str(), pack.bEnabled,
                       pack.bConflicting, pack.bMandatory );

        if ( !pack.bEnabled || pack.bConflicting ) {
            OTDU_LOG_INFO( "  -Skipping '%S': %s\n", pack.DisplayName.c_str(),
                           pack.Status.empty() ? "disabled"
                                               : pack.Status.c_str() );
            continue;
        }

        if ( !pack.bDescriptorOnly &&
             numMounted >= kMaxFilemapCount - kFirstPackFilemapIndex ) {
            pack.Status = "there are no filemap slots left";
            OTDU_LOG_WARN( "  -Skipping '%S': %s\n", pack.DisplayName.c_str(),
                           pack.Status.c_str() );
            continue;
        }

        EnumerateContextItem item;
        item.bIsValid = true;
        item.StoredOnHDD = 0u;
        item.Source = eContextSource::CS_None;
        item.Filename = pack.Path;
        item.DisplayName = pack.DisplayName;

        if ( ModInstaller::HasContentDesc( item ) ) {
            const eContentDescResult descResult =
                gpModInstaller->loadContentDesc( item, bApplyContent );

            OTDU_LOG_INFO( "  -CXmlInstaller=> Content Desc is %s\n",
                           ( descResult != CDR_Invalid ) ? "VALID"
                                                         : "INVALID" );

            if ( descResult == CDR_Invalid ) {
                pack.Status = "its content descriptor is unusable";
                OTDU_LOG_ERROR( "  -Skipping pack '%S': %s\n",
                                pack.DisplayName.c_str(), pack.Status.c_str() );
                continue;
            }

            if ( pack.bMandatory ) {
                gpModInstaller->markInjectionsMandatory( pack.DisplayName );
            }
        }

        if ( pack.bDescriptorOnly ) {
            pack.Status.clear();
            OTDU_LOG_INFO( "  -Loaded '%S' (content descriptor only)\n",
                           pack.DisplayName.c_str() );
            continue;
        }

        const std::filesystem::path packPath( pack.Path );
        const std::string mapPath = ( packPath / kPackMapFilename ).string();
        const std::string bigPath = ( packPath / kPackBigFilename ).string();

        const int32_t filemapIndex = appendMapFile(
            gpConfig->getResRootPath(), bigPath.c_str(), mapPath.c_str(),
            nullptr, kMaxFilemapCount, false, kFirstPackFilemapIndex );
        if ( filemapIndex <= 0 ) {
            pack.Status = "its CarPack.map could not be mounted";
            OTDU_LOG_ERROR( "  -Failed to mount pack '%S'\n",
                            pack.DisplayName.c_str() );
            continue;
        }

        FileMap &filemap = filemaps[filemapIndex];
        filemap.bNeedName = false;
        filemap.Context = item;
        filemap.Context.bIsValid = false;

        pack.bMounted = true;
        pack.FilemapIndex = filemapIndex;
        pack.NumEntries = filemap.MapSize / 0x18;
        pack.Status.clear();
        numMounted++;

        OTDU_LOG_INFO( "  -Mounted '%S' on map %d: %u entries\n",
                       pack.DisplayName.c_str(), filemapIndex,
                       pack.NumEntries );
    }

    gpModManager->setPendingChanges( false );
}

bool GSFile::loadBootFile( const char *pFilename,
                           void **pOutContent,
                           uint32_t *pOutContentSize ) {
    bool bVar2 = false;
    if ( gpConfig->bSearchInBootFirst ) {
        // Boot Data bank lookup first?
        *pOutContent = gpBootDatas->findInCache( pFilename, pOutContentSize );
        bVar2 = ( *pOutContent != nullptr );

        if ( !bVar2 ) {
            bVar2 = loadBank( pFilename, pOutContent, pOutContentSize, 0 );
            if ( !bVar2 ) {
                return false;
            }
        }
    } else {
        bVar2 = loadBank( pFilename, pOutContent, pOutContentSize, 0 );
        if ( !bVar2 ) {
            *pOutContent =
                gpBootDatas->findInCache( pFilename, pOutContentSize );
            bVar2 = ( *pOutContent != nullptr );
            if ( !bVar2 ) {
                return false;
            }
        }
    }

    return true;
}

std::wstring GSFile::getSaveLocation( eContextSource source ) {
    std::wstring saveLocation = getSaveFolder();

    switch ( source ) {
    case eContextSource::CS_Publisher:
        saveLocation += L"publisher";
        break;
    case eContextSource::CS_Marketplace:
        saveLocation += L"marketplace";
        break;
    case eContextSource::CS_Savegame:
        saveLocation += L"savegame";
        break;
    default:
        break;
    }

    return saveLocation;
}

bool GSFile::deleteProfileSaveFolder( const uint64_t profileHashcode,
                                      const eContextSource source ) {
    // FUN_006085a0
    std::wstring saveLocation = getSaveLocation( source );
    if ( saveLocation.empty() ) {
        return false;
    }

    const std::string profileName =
        gUserProfileRegister.getProfileName( profileHashcode );
    if ( profileName.empty() ) {
        return false;
    }

    std::filesystem::path folderPath( saveLocation );
    folderPath /= std::filesystem::path( profileName );

    std::error_code errorCode;
    std::filesystem::remove_all( folderPath, errorCode );

    return !errorCode;
}

bool GSFile::hasPendingAsyncRequests() {
    if ( workerThreadContext.bRunning ) {
        return true;
    }
    for ( AsyncFileOpen *pRequest : asyncData ) {
        if ( pRequest->bInUse && !pRequest->bCanceled ) {
            return true;
        }
    }
    return false;
}

bool GSFile::cancelAsyncRequest( void *pUserData ) {
    // FUN_009a2a90
    for ( AsyncFileOpen *pRequest : asyncData ) {
        if ( !pRequest->bInUse || pRequest->pUserData != pUserData ) {
            continue;
        }

        pRequest->bCanceled = true;
        if ( workerThreadContext.bRunning &&
             workerThreadContext.pAsyncData == pRequest ) {
            return false;
        }

        asyncData.erase( pRequest );
        delete pRequest;
        return true;
    }
    return true;
}

void GSFile::flushPendingAsync() {
    while ( hasPendingAsyncRequests() ) {
        syncAsyncWorkerThread();
    }

    gpTestDriveInstance->flushPendingFileInstanciation( false );
}

bool GSFile::isUsingFilemap() const {
    ScopedMutexLock filemapLock( pFilemapMutex );

    for ( const FileMap &filemap : filemaps ) {
        if ( filemap.MapSize != 0 ) {
            return true;
        }
    }

    return false;
}

bool GSFile::fileExists( const char *pFilename ) {
    // FUN_009a4fe0
    if ( bFilemapActive ) {
        FileMapEntry *local_628 = nullptr;
        FileMap *local_624 = nullptr;

        bool bVar3 = findMapEntriesForFile( pFilename, &local_628, &local_624 );
        if ( bVar3 ) {
            return true;
        }

        const char *pcVar4 = gpConfig->getResRootPath();
        int32_t iVar5 = strcmp( pcVar4, pFilename );
        if ( iVar5 == 0 ) {
            return false;
        }
    }

    FileIterator local_620;
    bool bVar2 = local_620.findFirstMatch( pFilename );
    return bVar2;
}

bool GSFile::SaveFile( const char *pFilename,
                       const void *pContent,
                       uint32_t contentSize ) {
    // FUN_009a2af0
    if ( pFilename == nullptr || pContent == nullptr ) {
        return false;
    }

    std::error_code errorCode;
    const std::filesystem::path filePath( pFilename );
    if ( filePath.has_parent_path() ) {
        std::filesystem::create_directories( filePath.parent_path(),
                                             errorCode );
    }

    std::ofstream stream( filePath, std::ofstream::out | std::ofstream::binary |
                                        std::ofstream::trunc );
    if ( !stream.is_open() ) {
        OTDU_LOG_ERROR( "Failed to open '%s' for writing\n", pFilename );
        return false;
    }

    stream.write( (const char *)pContent, (std::streamsize)contentSize );
    const bool bSuccess = stream.good();
    stream.close();

    if ( !bSuccess ) {
        OTDU_LOG_ERROR( "Failed to write %u bytes to '%s'\n", contentSize,
                        pFilename );
    }

    return bSuccess;
}

bool GSFile::pushSaveRequest( const char *pContextName,
                              const char *pFilename,
                              const wchar_t *pDisplayName,
                              void *pContent,
                              uint32_t contentSize,
                              const bool bReleaseContent ) {
    // FUN_009a26d0
    auto releaseContent = [&]() {
        if ( bReleaseContent && pContent != nullptr ) {
            TestDrive::FreeAligned( pContent );
        }
    };
    auto rejectQuery = [&]() {
        saveQueue.pContextName[0] = '\0';
        wcscpy( saveQueue.pDisplayName, kSaveQueueNoName );
        releaseContent();
        return false;
    };

    ScopedMutexLock lock( saveQueue.pMutex );
    if ( strcmp( pContextName, saveQueue.pContextName ) != 0 &&
         saveQueue.pContextName[0] != '\0' ) {
        return rejectQuery();
    }

    strcpy( saveQueue.pContextName, pContextName );
    wcscpy( saveQueue.pDisplayName, pDisplayName );
    for ( const SaveQueue::FileToSave &file : saveQueue.pFilesToSave ) {
        if ( file.pContent == nullptr &&
             strcmp( file.pFilename, pFilename ) == 0 ) {
            return rejectQuery();
        }
    }

    saveQueue.LastError = SQE_None;
    saveQueue.pLastCorruptContext[0] = '\0';
    for ( SaveQueue::FileToSave &file : saveQueue.pFilesToSave ) {
        if ( file.pContent == nullptr ) {
            file.pContent = pContent;
            file.ContentSize = contentSize;
            strcpy( file.pFilename, pFilename );
            file.bReleaseContent = bReleaseContent;
            return true;
        }
    }

    releaseContent();
    return false;
}

bool GSFile::deleteFile( const char *pContextName,
                         const char *pFilename,
                         const wchar_t * /*pDisplayName*/ ) {
    // FUN_009a3490
    if ( strcmp( pContextName, "CURRENT" ) == 0 ) {
        if ( !bIsCurrentContextReady ) {
            return false;
        }
        return currentContextDirectAccess.deleteFileInContext( pFilename );
    }

    FileDirectAccess fileAccess;
    fileAccess.bNeedName = false;
    if ( !fileAccess.openContext(
             gUserProfileRegister.getActiveProfileHashcode(), pContextName,
             eContextSource::CS_Savegame ) ) {
        return false;
    }

    const bool bDeleted = fileAccess.deleteFileInContext( pFilename );
    fileAccess.reset();
    return bDeleted;
}

bool GSFile::writeFileInContext( const char *pContextName,
                                 const char *pFilename,
                                 const void *pContent,
                                 uint32_t contentSize ) {
    // FUN_009a3870
    if ( strcmp( pContextName, "CURRENT" ) == 0 ) {
        if ( !bIsCurrentContextReady ||
             !currentContextDirectAccess.openFile( pFilename, false, true ) ) {
            return false;
        }
        const bool bWritten = currentContextDirectAccess.writeToFile(
                                  pContent, contentSize ) == contentSize;
        currentContextDirectAccess.close();
        return bWritten;
    }

    FileDirectAccess fileAccess;
    fileAccess.bNeedName = false;
    if ( !fileAccess.openContext(
             gUserProfileRegister.getActiveProfileHashcode(), pContextName,
             eContextSource::CS_Savegame ) ) {
        return false;
    }

    bool bWritten = false;
    if ( fileAccess.openFile( pFilename, false, true ) ) {
        bWritten =
            fileAccess.writeToFile( pContent, contentSize ) == contentSize;
        fileAccess.close();
    }

    fileAccess.reset();
    return bWritten;
}

static void OnSaveQueuePromptAnswer( ePopupAnswer answer,
                                     void *pUserData,
                                     int32_t promptCode ) {
    // FUN_009a4510
    if ( pUserData == nullptr ) {
        return;
    }

    // FUN_009a4490
    SaveQueue *pQueue = static_cast<SaveQueue *>( pUserData );
    ScopedMutexLock lock( pQueue->pMutex );
    pQueue->bErrorDisplayed = false;
    if ( promptCode == kSaveQueuePromptCorruptedFile ) {
        if ( answer == ePopupAnswer::PA_Accept ) {
            pQueue->LastError = SQE_None;
        } else if ( answer == ePopupAnswer::PA_Cancel ) {
            pQueue->clear();
        }
        pQueue->pLastCorruptContext[0] = '\0';
    } else if ( promptCode == kSaveQueuePromptDeviceError ) {
        pQueue->LastError = SQE_None;
    }
}

void GSFile::displaySaveQueuePrompt( const bool bHasCancel,
                                     const uint64_t messageHashcode,
                                     const int32_t messageDatabaseIndex,
                                     const int32_t promptCode ) {
    TestDrive::DisplayModalPrompt(
        bHasCancel, messageHashcode, messageDatabaseIndex,
        &OnSaveQueuePromptAnswer, nullptr, &saveQueue, promptCode, nullptr,
        true, false, MPT_Warning, gpFlashMessageBox );
}

void GSFile::tickSaveQueue() {
    // FUN_009a5090
    ScopedMutexLock lock( saveQueue.pMutex );
    if ( bSaveDeviceError ) {
        FlashPlayer *pMessageBoxPlayer = gpFlashMessageBox->getFlashPlayer();
        if ( !saveQueue.bErrorDisplayed &&
             gpFlashMessageBox->getNumOpenMsgBox() == 0 &&
             !gpFlashMessageBox->getUnknownFlag() &&
             pMessageBoxPlayer != nullptr &&
             pMessageBoxPlayer->spriteExists( "/avert" ) &&
             gpFlashMessageBox->getDisplayState() ==
                 ModalMessageBox::MMBDS_Closed &&
             !gpPCOptions->isMainMenuOpened() ) {
            displaySaveQueuePrompt( false, kSaveQueueDeviceErrorHashcode, 0,
                                    kSaveQueuePromptDeviceError );
            saveQueue.bErrorDisplayed = true;
            bSaveDeviceError = false;
        }
        return;
    }

    if ( saveQueue.getNumPendingFiles() == 0 || saveQueue.bDelay ) {
        return;
    }

    if ( saveQueue.LastError == SQE_CorruptedFile ||
         saveQueue.LastError == SQE_Blocked ) {
        return;
    }

    uint32_t totalSize = 0;
    for ( const SaveQueue::FileToSave &file : saveQueue.pFilesToSave ) {
        if ( file.pContent != nullptr ) {
            totalSize += file.ContentSize;
        }
    }
    if ( totalSize == 0 ) {
        return;
    }

    if ( kSaveDeviceFreeSpace < totalSize ) {
        if ( saveQueue.bErrorDisplayed ) {
            return;
        }

        if ( gUserProfileRegister.getActiveProfileHashcode() == 0 ) {
            saveQueue.LastError = SQE_NoProfile;
            saveQueue.pLastCorruptContext[0] = '\0';
            displaySaveQueuePrompt( false, kSaveQueueNoProfileHashcode, 0,
                                    kSaveQueuePromptNoProfile );
        } else {
            saveQueue.LastError = SQE_NotEnoughSpace;
            saveQueue.pLastCorruptContext[0] = '\0';
            displaySaveQueuePrompt( false, kSaveQueueNotEnoughSpaceHashcode, 0,
                                    kSaveQueuePromptNotEnoughSpace );
        }
        saveQueue.bErrorDisplayed = true;
        saveQueue.clear();
    } else {
        const bool bContextOpened = openContextAsCurrent(
            saveQueue.pContextName, saveQueue.pDisplayName, true );
        for ( SaveQueue::FileToSave &file : saveQueue.pFilesToSave ) {
            if ( file.pContent == nullptr ) {
                continue;
            }

            const bool bWritten =
                bContextOpened &&
                writeFileInContext( "CURRENT", file.pFilename, file.pContent,
                                    file.ContentSize );
            if ( !bWritten && !saveQueue.bErrorDisplayed ) {
                saveQueue.LastError = SQE_WriteFailed;
                saveQueue.pLastCorruptContext[0] = '\0';
                saveQueue.bErrorDisplayed = true;
                if ( ensureContext( saveQueue.pContextName,
                                    saveQueue.pDisplayName ) == CST_Error ) {
                    strcpy( saveQueue.pLastCorruptContext,
                            saveQueue.pContextName );
                    saveQueue.LastError = SQE_CorruptedFile;
                    displaySaveQueuePrompt(
                        true, kSaveQueueCorruptedFileHashcode, 0xb,
                        kSaveQueuePromptCorruptedFile );
                } else {
                    displaySaveQueuePrompt( false,
                                            kSaveQueueWriteFailedHashcode, 0,
                                            kSaveQueuePromptWriteFailed );
                }
            }

            if ( saveQueue.LastError != SQE_CorruptedFile ) {
                if ( file.bReleaseContent ) {
                    TestDrive::FreeAligned( file.pContent );
                }
                file.pContent = nullptr;
                file.ContentSize = 0;
                file.pFilename[0] = '\0';
                file.bReleaseContent = false;
            }

            if ( !saveQueue.bErrorDisplayed ) {
                saveQueue.pLastCorruptContext[0] = '\0';
                saveQueue.LastError = SQE_Success;
            }
        }

        if ( bContextOpened ) {
            closeCurrentContext();
        }
        if ( saveQueue.LastError == SQE_CorruptedFile ) {
            return;
        }
    }

    saveQueue.pContextName[0] = '\0';
    wcscpy( saveQueue.pDisplayName, kSaveQueueNoName );
}

bool GSFile::openContextAsCurrent( const char *pContextName,
                                   const wchar_t * /*pDisplayName*/,
                                   const bool /*bCreateIfMissing*/ ) {
    // FUN_009a41b0
    if ( bIsCurrentContextReady || pContextName == nullptr ||
         strcmp( pContextName, "CURRENT" ) == 0 ) {
        return false;
    }

    currentContextDirectAccess.bNeedName = false;
    if ( !currentContextDirectAccess.openContext(
             gUserProfileRegister.getActiveProfileHashcode(), pContextName,
             eContextSource::CS_Savegame ) ) {
        return false;
    }

    bIsCurrentContextReady = true;
    return true;
}

void GSFile::closeCurrentContext() {
    if ( !bIsCurrentContextReady ) {
        return;
    }

    currentContextDirectAccess.reset();
    bIsCurrentContextReady = false;
}

eContextState GSFile::ensureContext( const char *pContextName,
                                     const wchar_t *pDisplayName ) {
    // FUN_009a4250
    const uint64_t profileHashcode =
        gUserProfileRegister.getActiveProfileHashcode();

    FileDirectAccess fileAccess;
    fileAccess.bNeedName = false;

    EnumerateContextItem items[16] = {};
    uint32_t numItems = 0u;
    fileAccess.enumerateContext(
        profileHashcode, eContextSource::CS_Savegame, pContextName, nullptr,
        (uint32_t)std::size( items ), items, &numItems );
    if ( numItems == 0u ) {
        return CST_Empty;
    }

    if ( !fileAccess.openContext( profileHashcode, pContextName,
                                  eContextSource::CS_Savegame ) ) {
        return CST_Error;
    }

    OTDU_LOG_DEBUG( "Save context '%s' ('%S') is ready\n", pContextName,
                    ( pDisplayName != nullptr ) ? pDisplayName : L"" );

    fileAccess.reset();
    return CST_Ready;
}

bool GSFile::visitContextFiles(
    const char *pContextName,
    const char *pPattern,
    const std::function<
        bool( FileDirectAccess &, const std::string &, int32_t )> &visitor ) {
    const bool bUseCurrentContext = ( pContextName != nullptr ) &&
                                    ( strcmp( pContextName, "CURRENT" ) == 0 );
    if ( bUseCurrentContext && !bIsCurrentContextReady ) {
        return false;
    }

    FileDirectAccess localAccess;
    FileDirectAccess *pFileAccess = &currentContextDirectAccess;

    if ( !bUseCurrentContext ) {
        localAccess.bNeedName = false;
        if ( !localAccess.openContext(
                 gUserProfileRegister.getActiveProfileHashcode(), pContextName,
                 eContextSource::CS_Savegame ) ) {
            return false;
        }
        pFileAccess = &localAccess;
    }

    std::string filename;
    int32_t index = 0;
    bool bFound = pFileAccess->findFirstFile( pPattern, filename );
    while ( bFound ) {
        if ( !visitor( *pFileAccess, filename, index ) ) {
            break;
        }
        index++;
        bFound = pFileAccess->findNextFile( filename );
    }
    pFileAccess->findClose();

    if ( !bUseCurrentContext ) {
        localAccess.reset();
    }

    return true;
}

int32_t GSFile::countFilesInContext( const char *pContextName,
                                     const char *pPattern ) {
    // FUN_009a3740
    int32_t numFiles = 0;
    visitContextFiles(
        pContextName, pPattern,
        [&numFiles]( FileDirectAccess &, const std::string &, int32_t ) {
            numFiles++;
            return true;
        } );
    return numFiles;
}

bool GSFile::getFileNameInContext( const char *pContextName,
                                   const char *pPattern,
                                   const int32_t index,
                                   char *pOutFilename,
                                   const size_t outFilenameSize ) {
    // FUN_009a2f30
    if ( pOutFilename == nullptr || outFilenameSize == 0u ) {
        return false;
    }
    pOutFilename[0] = '\0';

    bool bFound = false;
    visitContextFiles( pContextName, pPattern,
                       [&]( FileDirectAccess &, const std::string &filename,
                            int32_t fileIndex ) {
                           if ( fileIndex != index ) {
                               return true;
                           }
                           snprintf( pOutFilename, outFilenameSize, "%s",
                                     filename.c_str() );
                           bFound = true;
                           return false;
                       } );
    return bFound;
}

bool GSFile::deleteFileInContext( const char *pContextName,
                                  const char *pPattern,
                                  const int32_t index ) {
    // FUN_009a35a0
    bool bDeleted = false;
    visitContextFiles( pContextName, pPattern,
                       [&]( FileDirectAccess &fileAccess,
                            const std::string &filename, int32_t fileIndex ) {
                           if ( fileIndex != index ) {
                               return true;
                           }
                           fileAccess.findClose();
                           bDeleted = fileAccess.deleteFileInContext(
                               filename.c_str() );
                           return false;
                       } );
    return bDeleted;
}

bool GSFile::loadFileFromContext( const char *pContextName,
                                  const char *pFilename,
                                  void *pOutBuffer,
                                  const uint32_t bufferSize,
                                  uint32_t *pOutContentSize ) {
    // FUN_009a39c0
    if ( pOutContentSize != nullptr ) {
        *pOutContentSize = 0u;
    }

    const bool bUseCurrentContext = ( pContextName != nullptr ) &&
                                    ( strcmp( pContextName, "CURRENT" ) == 0 );
    if ( bUseCurrentContext ) {
        if ( !bIsCurrentContextReady ) {
            return false;
        }

        if ( !currentContextDirectAccess.openFile( pFilename, true, false ) ) {
            return false;
        }

        const uint32_t numBytesRead =
            currentContextDirectAccess.readFromFile( pOutBuffer, bufferSize );
        currentContextDirectAccess.close();

        if ( pOutContentSize != nullptr ) {
            *pOutContentSize = numBytesRead;
        }
        return numBytesRead != 0u;
    }

    FileDirectAccess fileAccess;
    fileAccess.bNeedName = false;

    if ( !fileAccess.openContext(
             gUserProfileRegister.getActiveProfileHashcode(), pContextName,
             eContextSource::CS_Savegame ) ) {
        return false;
    }

    bool bSuccess = false;
    if ( fileAccess.openFile( pFilename, true, false ) ) {
        const uint32_t numBytesRead =
            fileAccess.readFromFile( pOutBuffer, bufferSize );
        fileAccess.close();

        if ( pOutContentSize != nullptr ) {
            *pOutContentSize = numBytesRead;
        }
        bSuccess = numBytesRead != 0u;
    }

    fileAccess.reset();
    return bSuccess;
}

bool GSFile::loadFileFromContext( const char *pContextName,
                                  const char *pFilename,
                                  void **ppOutContent,
                                  uint32_t *pOutContentSize ) {
    // FUN_009a3b40
    *ppOutContent = nullptr;
    *pOutContentSize = 0u;

    const bool bUseCurrentContext = ( pContextName != nullptr ) &&
                                    ( strcmp( pContextName, "CURRENT" ) == 0 );
    if ( bUseCurrentContext && !bIsCurrentContextReady ) {
        return false;
    }

    FileDirectAccess localAccess;
    FileDirectAccess *pFileAccess = &currentContextDirectAccess;

    if ( !bUseCurrentContext ) {
        localAccess.bNeedName = false;
        if ( !localAccess.openContext(
                 gUserProfileRegister.getActiveProfileHashcode(), pContextName,
                 eContextSource::CS_Savegame ) ) {
            return false;
        }
        pFileAccess = &localAccess;
    }

    bool bSuccess = false;
    if ( pFileAccess->openFile( pFilename, true, false ) ) {
        const uint32_t fileSize = pFileAccess->getFileSize();

        *ppOutContent =
            ( fileSize != 0u ) ? TestDrive::Alloc( fileSize ) : nullptr;
        *pOutContentSize = fileSize;

        if ( *ppOutContent != nullptr ) {
            bSuccess = pFileAccess->readFromFile( *ppOutContent, fileSize ) ==
                       fileSize;
        }

        pFileAccess->close();
    }

    if ( !bUseCurrentContext ) {
        localAccess.reset();
    }

    return bSuccess;
}

void GSFile::syncAsyncWorkerThread() {
    if ( !workerThreadContext.bRunning ) {
        // Pick the next queued request and signal the worker to load it.
        AsyncFileOpen *pNext = nullptr;
        for ( AsyncFileOpen *pRequest : asyncData ) {
            if ( pRequest->bInUse && !pRequest->bCanceled ) {
                pNext = pRequest;
                break;
            }
        }

        if ( pNext == nullptr ) {
            return;
        }

        workerThreadContext.bRunning = true;
        workerThreadContext.bJobDone = false;
        workerThreadContext.pAsyncData = pNext;
        workerThreadContext.pEvent->reset(); // wake the worker (semaphore post)
    } else if ( workerThreadContext.bJobDone ) {
        AsyncFileOpen *pDone = workerThreadContext.pAsyncData;
        workerThreadContext.bRunning = false;
        workerThreadContext.bJobDone = false;
        bTaskInProgress = false;

        // The worker is back on waitForEvent(); fire the completion callback
        if ( pDone != nullptr ) {
            // Acquire-release pairing with the worker's filemap unlock so the
            // loaded buffer is fully visible before the callback parses it (the
            // original ran on x86; this matters on weak memory models).
            pFilemapMutex->lock();
            pFilemapMutex->unlock();

            if ( !pDone->bCanceled && pDone->pCompletionCallback != nullptr ) {
                pDone->pCompletionCallback( 4, 1 );
            }
            pDone->bInUse = false;
            asyncData.erase( pDone );
            workerThreadContext.pAsyncData = nullptr;
            delete pDone;
        }
    }
}

AsyncFileOpen::AsyncFileOpen()
    : Handle(),
      Filename( "" ),
      pUserData( nullptr ),
      pBuffer( nullptr ),
      pCompletionCallback( nullptr ),
      Size( 0 ),
      Offset( 0 ),
      bInUse( false ),
      bCanceled( false ),
      bCompressed( false ),
      bHighPriority( false ),
      pFilemap( nullptr ) {}

SaveQueue::SaveQueue()
    : pContextName(),
      pFilesToSave(),
      LastError( SQE_None ),
      pLastCorruptContext(),
      pMutex( new TestDriveMutex() ),
      bErrorDisplayed( false ),
      bDelay( false ) {
    wcscpy( pDisplayName, kSaveQueueNoName );
}

SaveQueue::~SaveQueue() {
    clear();
    delete pMutex;
}

void SaveQueue::clear() {
    // FUN_009a2670
    for ( FileToSave &file : pFilesToSave ) {
        if ( file.pContent == nullptr ) {
            continue;
        }
        if ( file.bReleaseContent ) {
            TestDrive::FreeAligned( file.pContent );
        }
        file.pContent = nullptr;
        file.ContentSize = 0;
        file.pFilename[0] = '\0';
        file.bReleaseContent = false;
    }
}

uint32_t SaveQueue::getNumPendingFiles() const {
    uint32_t numPending = 0;
    for ( const FileToSave &file : pFilesToSave ) {
        if ( file.pContent != nullptr ) {
            numPending++;
        }
    }
    return numPending;
}
