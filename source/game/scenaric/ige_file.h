#pragma once

class Entity;
class EventParameters;
class MngActor;

// Called once an asynchronous IGE load is parsed (param_1 is always 0).
using IGEFileCallback_t = bool ( * )( int32_t param_1, void *pUserData );

// IGE ("In Game Editor") file load request (DAT_011f50a0).
struct IGEFileRequest {
    MngActor *pMngActor = nullptr;
    void *pUserData = nullptr;
    IGEFileCallback_t pCallback = nullptr;
    bool bParametersOnly = false;
    uint32_t Size = 0u;
    EventParameters *pEventParameters = nullptr;
    char pName[0x20] = {};
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
void ReadEntityControls( const uint8_t **ppCursor,
                         Entity *pEntity,
                         uint32_t *pVersion );
