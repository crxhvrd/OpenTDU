#pragma once

#include <atomic>

#include "file_direct_access.h"
#include "file_handle.h"
#include "game/gs_base.h"
#include "virtual_file_system.h"

class TestDriveMutex;
class TestDriveEvent;
class TestDriveThread;
class Bank;

struct FileHandle;
struct FileMap;

struct AsyncFileOpen {
    using CompletionCallback_t = std::function<void( int32_t, int32_t )>;
    FileHandle Handle;
    std::string Filename;
    void *pUserData;
    void *pBuffer;
    CompletionCallback_t pCompletionCallback;

    uint32_t Size;
    uint32_t Offset;
    uint8_t bInUse : 1;
    uint8_t bCanceled : 1;
    uint8_t bCompressed : 1;
    uint8_t bHighPriority : 1;

    FileMap *pFilemap;

    AsyncFileOpen();
};

enum eSaveQueueError : int32_t {
    SQE_None = 0,
    SQE_Success = 1,
    SQE_NotEnoughSpace = 2,
    SQE_CorruptedFile = 3,
    SQE_NotEnoughSpaceAcknowledged = 4,
    SQE_NoProfile = 5,
    SQE_Blocked = 6,
    SQE_WriteFailed = 7,
};

struct SaveQueue {
    static constexpr int32_t kNumFiles = 10;

    struct FileToSave {
        char pFilename[32];
        void *pContent;
        uint32_t ContentSize;
        bool bReleaseContent;
    };

    char pContextName[32];
    wchar_t pDisplayName[32];
    FileToSave pFilesToSave[kNumFiles];
    eSaveQueueError LastError;
    char pLastCorruptContext[128];
    TestDriveMutex *pMutex;
    bool bErrorDisplayed;
    bool bDelay;

    SaveQueue();
    ~SaveQueue();

    void clear();
    uint32_t getNumPendingFiles() const;
};

struct WorkerThreadContext {
    TestDriveEvent *pEvent;
    AsyncFileOpen *pAsyncData;
    uint8_t bRunning : 1;
    std::atomic<bool> bJobDone;

    WorkerThreadContext()
        : pEvent( nullptr ),
          pAsyncData( nullptr ),
          bRunning( false ),
          bJobDone( false ) {}
};

enum eContextState : int32_t {
    CST_Empty = 1, // the profile has no such context yet
    CST_Error = 2, // the context exists but could not be opened
    CST_Ready = 3, // opened (and immediately released again)
};

class GSFile : public GameSystem {
public:
    const char *getName() const override { return "Service : File"; }
    const std::wstring &getSaveFolder() {
        if ( savegameFolder.empty() ) {
            retrieveSaveFolder();
        }
        return savegameFolder;
    }

    inline bool hasTaskPending() const { return bTaskInProgress; }
    inline bool isFilemapActive() const { return bFilemapActive; }
    inline bool hasPendingFileToSave() const {
        return saveQueue.getNumPendingFiles() != 0;
    }
    inline void setSaveQueueDelayed( const bool bDelayed ) {
        saveQueue.bDelay = bDelayed;
    }

public:
    GSFile();
    ~GSFile();

    bool initialize( TestDriveGameInstance * ) override;
    void tick( float totalTime, float deltaTime ) override;
    void terminate() override;

    bool loadFile( const char *pFilename,
                   void **pOutContent,
                   uint32_t *pOutContentSize );
    bool loadBank( const char *pFilename,
                   void **pOutContent,
                   uint32_t *pOutContentSize,
                   uint32_t param_5 );
    bool loadBank( const char *pFilename,
                   void **pOutContent,
                   uint32_t *pOutContentSize,
                   void *pUserData,
                   AsyncFileOpen::CompletionCallback_t onComplete );

    bool findMapEntriesForFile( const char *pEntryName,
                                FileMapEntry **ppOutMapEntry,
                                FileMap **ppOutMap );
    bool loadFileRaw( const char *pFilename,
                      FileMap *pFilemap,
                      FileMapEntry *pEntry,
                      const uint32_t size,
                      void **ppOutContent );

    bool loadBootFile( const char *pFilename,
                       void **pOutContent,
                       uint32_t *pOutContentSize );

    void initCarPacks( bool bApplyContent );
    int32_t appendMapFile( const char *pVFSName,
                           const char *pPackBigName,
                           const char *pPackMapName,
                           FileDirectAccess *pContext,
                           const int32_t maxFilemapCount,
                           const bool bReadFromVFS,
                           int32_t filemapIndex );
    std::wstring getSaveLocation( eContextSource source );
    bool deleteProfileSaveFolder( const uint64_t profileHashcode,
                                  const eContextSource source );

    bool hasPendingAsyncRequests();
    bool cancelAsyncRequest( void *pUserData );
    void flushPendingAsync();

    bool fileExists( const char *pFilename );
    bool isUsingFilemap() const;

    static bool SaveFile( const char *pFilename,
                          const void *pContent,
                          uint32_t contentSize );

    bool deleteFile( const char *pContextName,
                     const char *pFilename,
                     const wchar_t *pDisplayName );
    bool pushSaveRequest( const char *pContextName,
                          const char *pFilename,
                          const wchar_t *pDisplayName,
                          void *pContent,
                          uint32_t contentSize,
                          bool bReleaseContent );

    bool loadFileFromContext( const char *pContextName,
                              const char *pFilename,
                              void *pOutBuffer,
                              const uint32_t bufferSize,
                              uint32_t *pOutContentSize );

    bool loadFileFromContext( const char *pContextName,
                              const char *pFilename,
                              void **ppOutContent,
                              uint32_t *pOutContentSize );

    bool openContextAsCurrent( const char *pContextName,
                               const wchar_t *pDisplayName,
                               const bool bCreateIfMissing );
    void closeCurrentContext();

    eContextState ensureContext( const char *pContextName,
                                 const wchar_t *pDisplayName );

    int32_t countFilesInContext( const char *pContextName,
                                 const char *pPattern );
    bool getFileNameInContext( const char *pContextName,
                               const char *pPattern,
                               const int32_t index,
                               char *pOutFilename,
                               const size_t outFilenameSize );
    bool deleteFileInContext( const char *pContextName,
                              const char *pPattern,
                              const int32_t index );

private:
    std::set<AsyncFileOpen *> asyncData;
    std::vector<FileMap> filemaps;
    TestDriveMutex *pFilemapMutex;
    TestDriveThread *pWorkerThread;
    WorkerThreadContext workerThreadContext;
    FileDirectAccess directAccess;
    FileDirectAccess currentContextDirectAccess;
    SaveQueue saveQueue;

    VirtualFileSystem hardDriveVFS;

    std::string tmpFilename;
    std::wstring savegameFolder;

    int32_t initStep;

    uint8_t bFilemapActive : 1;
    uint8_t bTaskInProgress : 1;
    uint8_t bIsCurrentContextReady : 1;
    uint8_t bDeletingInProgress : 1;
    uint8_t bSaveDeviceError : 1;

private:
    bool retrieveSaveFolder();
    bool writeFileInContext( const char *pContextName,
                             const char *pFilename,
                             const void *pContent,
                             uint32_t contentSize );
    void tickSaveQueue();
    void displaySaveQueuePrompt( bool bHasCancel,
                                 uint64_t messageHashcode,
                                 int32_t messageDatabaseIndex,
                                 int32_t promptCode );
    bool findFileOutsideFilemap( const char *pFilename,
                                 uint32_t *pOutFilesize );
    bool resolveFileInFilemaps( const char *pFilename,
                                FileMapEntry **ppOutEntry,
                                FileMap **ppOutFilemap,
                                uint32_t *pOutFileSize );
    void syncAsyncWorkerThread();
    bool visitContextFiles(
        const char *pContextName,
        const char *pPattern,
        const std::function<bool(
            FileDirectAccess &, const std::string &, int32_t )> &visitor );
};

extern GSFile *gpFile;

bool FUN_009a6d50( Bank *param_1,
                   FileHandle *param_2,
                   int32_t *param_3,
                   bool param_4 );
