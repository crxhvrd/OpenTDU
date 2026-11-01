#pragma once

class Entity;
class EventParameters;
class MngActor;

// Called once an asynchronous IGE load is parsed (param_1 is always 0).
using IGEFileCallback_t = bool ( * )( int32_t param_1, void *pUserData );

struct IGEFileEntry {
    int32_t GameType;
    int32_t SpotType;
    char pName[0x41];
    char pDescription[0x41];
    char pFileName[0x20];
    uint16_t Year;
    uint8_t Month;
    uint8_t Day;
    bool bUserFile;
    bool bTested;
};
OTDU_SIZE_MUST_MATCH( IGEFileEntry, 0xb0 );

// IGE ("In Game Editor") file load request (DAT_011f50a0).
struct IGEFileRequest {
    static constexpr int32_t kMaxNumFiles = 0x200;

    MngActor *pMngActor = nullptr;
    void *pUserData = nullptr;
    IGEFileCallback_t pCallback = nullptr;
    bool bParametersOnly = false;
    uint32_t Size = 0u;
    EventParameters *pEventParameters = nullptr;
    char pName[0x20] = {};
    int32_t NumFiles = 0;                   // +0x3c
    IGEFileEntry Files[kMaxNumFiles] = {};  // +0x40
    int32_t NumSortedFiles = 0;             // +0x16044
    int32_t SortedFiles[kMaxNumFiles] = {}; // +0x16048

    inline IGEFileEntry *getSortedFile( const int32_t index ) {
        return &Files[SortedFiles[index]];
    }
};

extern IGEFileRequest gIGEFileRequest;

bool LoadIGEFile( IGEFileRequest *pRequest,
                  const char *pName,
                  MngActor *pMngActor,
                  const bool bParametersOnly,
                  EventParameters *pEventParameters,
                  const bool bAsync,
                  IGEFileCallback_t pCallback,
                  void *pUserData );
bool ParseIGEFile( EventParameters *pEventParameters,
                   const uint8_t *pBuffer,
                   IGEFileRequest *pRequest,
                   MngActor *pMngActor,
                   const uint32_t size,
                   const bool bParametersOnly );
void SetIGEFileName( EventParameters *pEventParameters, const char *pName );
bool WriteIGEFile( IGEFileRequest *pRequest,
                   MngActor *pMngActor,
                   uint8_t *pBuffer,
                   uint32_t *pOutSize );
void WriteEntityControls( uint8_t **ppCursor, Entity *pEntity );
void RefreshIGEFileList( IGEFileRequest *pRequest,
                         const bool bOpenContext,
                         const bool bNamesOnly );
void SortIGEFileList( IGEFileRequest *pRequest,
                      const int32_t sortMode,
                      const bool bInverted );
bool LoadUserIGEFile( IGEFileRequest *pRequest,
                      MngActor *pMngActor,
                      const char *pName,
                      EventParameters *pEventParameters,
                      const bool bParametersOnly );
bool SaveIGEFile( IGEFileRequest *pRequest,
                  MngActor *pMngActor,
                  const char *pName );
void ReadEntityControls( const uint8_t **ppCursor,
                         Entity *pEntity,
                         uint32_t *pVersion );
