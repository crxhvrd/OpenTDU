#include "shared.h"
#include "ige_file.h"

#include "ai/ai_road_query.h"
#include "config/gs_config.h"
#include "core/binary_helpers.h"
#include "entity/entity_car_player.h"
#include "entity/entity_check_point.h"
#include "event_parameters.h"
#include "filesystem/gs_file.h"
#include "game/game_cheats.h"
#include "game/gm_world.h"
#include "game/map/mng_trac_road.h"
#include "gs_scenaric.h"
#include "scenaric_bank.h"

IGEFileRequest gIGEFileRequest = {};

static constexpr uint32_t kMagicMDSC = 0x4d445343; // "MDSC"
static constexpr uint32_t kMagicMDWV = 0x4d445756; // "MDWV"
static constexpr uint32_t kDefaultMissionNameLength = 0x11;

static inline uint32_t ReadBigEndian32( const uint8_t *pData ) {
    return ReverseEndianness32( ReadUint32( pData ) );
}

static inline uint16_t ReadBigEndian16( const uint8_t *pData ) {
    return ReverseEndianness16( ReadU16( pData ) );
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
