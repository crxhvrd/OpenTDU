#pragma once

#include <array>

#include "cinematic_format.h"
#include "game/scenaric/entity/entity_factory.h"

struct AvatarConfiguration;
class BankInstance;
class ClipInstance;
class CameraCinematic;
class CarState;
class Entity;
class EntityActor;
class EntityAvatar;
class EntityCinematic;

struct CinematicActor {
    enum eKind : int32_t {
        CAK_Entity = 0,    // pEntity is an Entity* (scene actor / ped / avatar)
        CAK_PlayerCar = 1, // PLAYER_CAR; pEntity stays null, see pCarState
        CAK_Plane = 2,     // pEntity is the live GSAircraft instance (Interface
                       // pPlane); owned by GSAircraft, never cloned or freed
        CAK_TrafficCar = 3, // TRAFFICCAR_<id>; spawned for the cinematic,
                            // see pCarState
    };

    char Name[32];
    void *pEntity; // the bound EntityActor / avatar (null until spawned)
    CarState *pCarState = nullptr; // CAK_PlayerCar / CAK_TrafficCar: the car
                                   // driven (+0x1c)
    const void *pSceneData;        // this actor's CinScene media descriptor
    eKind Kind = CAK_Entity;
    bool bInstanced = false;
    bool bCarTeleport = true;
    int32_t AvatarRefCount = 0;
    uint64_t Hashcode = 0;
    Matrix4x4f_t TrackMatrix = Matrix4x4f_t::Identity();
    Matrix4x4f_t DisplayMatrix = Matrix4x4f_t::Identity();
};

struct CinActorMessage {
    void *pRef;          // the scene ref's runtime handle
    const void *pKey;    // {refIdx, duration} key pair
    const char *pString; // msg
    int32_t Length;
    float Duration;
    float Elapsed;
};

struct CinActorState {
    static constexpr int32_t kMaxMessages = 15;

    Eigen::Vector3f Position = Eigen::Vector3f::Zero();
    Eigen::Quaternionf Heading = Eigen::Quaternionf::Identity();
    bool bVisible = true;
    int32_t ClipRefA = -1;
    int32_t ClipColumnA = -1;
    float TimeA = 0.0f;
    float Ratio = 0.0f;
    int32_t ClipRefB = -1;
    int32_t ClipColumnB = -1;
    float TimeB = 0.0f;
    std::array<CinActorMessage, kMaxMessages> Messages{};
    int32_t NumMessages = 0;
};

class CinResCollection {
public:
    struct CameraShakeState {
        bool bEnabled = false;
        float Amplitude = 0.002f; // DAT_00f41754
        float Speed = 0.2f;       // FLOAT_00f41730
        float TargetAngleX = 0.0f;
        float TargetAngleY = 0.0f;
        float AngleX = 0.0f;
        float AngleY = 0.0f;
    };

    struct CameraView {
        Eigen::Vector3f Position = Eigen::Vector3f::Zero();
        Eigen::Vector3f Target = Eigen::Vector3f::Zero();
        Eigen::Vector3f Up = Eigen::Vector3f::Zero();
        float Fov = 0.0f;
        bool bValid = false;
    };

public:
    inline bool isLoaded() const { return pCinematicData != nullptr; }
    inline bool isCreated() const { return bCreated; }
    inline int32_t getActorCount() const { return numActors; }
    inline const CinematicHeader *getHeader() const { return pCinematicData; }
    inline const void *getRefData() const { return pRefData; }
    inline float getTotalTime() const {
        return pCinematicData != nullptr ? pCinematicData->TotalLength[0]
                                         : 0.0f;
    }
    inline const CameraView &getCameraView() const { return cameraView; }
    inline void setBaseTranslation( const Eigen::Vector3f &t ) {
        baseMatrix = Matrix4x4f_t::Identity();
        baseMatrix.row( 3 ) << t.x(), t.y(), t.z(), 1.0f;
        bHasBaseMatrix = true;
    }
    inline void setBaseMatrix( const Matrix4x4f_t &m ) {
        baseMatrix = m;
        bHasBaseMatrix = true;
    }
    inline void clearBaseMatrix() {
        baseMatrix = Matrix4x4f_t::Identity();
        bHasBaseMatrix = false;
    }
    inline bool hasBaseMatrix() const { return bHasBaseMatrix; }
    inline const Matrix4x4f_t &getBaseMatrix() const { return baseMatrix; }
    inline bool isLifeSetCinematic() const { return bIsLifeSetCinematic; }
    inline EntityCinematic *getOwner() const { return pOwner; }
    inline bool canActorsDriveStreaming() const {
        return bActorsDriveStreaming;
    }
    inline bool isStreamTarget() const { return bStreamTarget; }
    inline void setStreamTarget( bool bEnabled ) { bStreamTarget = bEnabled; }
    inline Entity *getCurrentMicro() const { return pCurrentMicro; }
    inline void setCurrentMicro( Entity *pMicro ) { pCurrentMicro = pMicro; }
    inline void setAvatarShadowTarget( bool bEnabled ) {
        bAvatarShadowTarget = bEnabled;
    }
    inline void clearPlayerAvatar() {
        bPlayerAvatarSpawned = false;
        pPlayerAvatar = nullptr;
    }
    inline uint64_t getActorHashcode( int32_t index ) const {
        return ( index >= 0 && index < numActors ) ? actors[index].Hashcode : 0;
    }
    inline int32_t getActorAvatarRefCount( int32_t index ) const {
        return ( index >= 0 && index < numActors )
                   ? actors[index].AvatarRefCount
                   : 0;
    }
    inline void setLifeSetCinematic( bool bEnabled ) {
        bIsLifeSetCinematic = bEnabled;
    }
    inline void setPlane( EntityActor *pActor ) { pPlane = pActor; }
    inline void setAsyncActorLoad( bool bEnabled ) {
        bAsyncActorLoad = bEnabled;
    }
    inline EntityActor *getPlane() const { return pPlane; }
    inline void *getActorEntity( int32_t index ) const {
        return ( index >= 0 && index < numActors ) ? actors[index].pEntity
                                                   : nullptr;
    }
    inline CinematicActor::eKind getActorKind( int32_t index ) const {
        return ( index >= 0 && index < numActors ) ? actors[index].Kind
                                                   : CinematicActor::CAK_Entity;
    }
    inline const char *getActorName( int32_t index ) const {
        return ( index >= 0 && index < numActors ) ? actors[index].Name : "";
    }
    inline bool isActorInstanced( int32_t index ) const {
        return index >= 0 && index < numActors && actors[index].bInstanced;
    }
    inline void setActorInstanced( int32_t index, bool bInstanced ) {
        if ( index >= 0 && index < numActors ) {
            actors[index].bInstanced = bInstanced;
        }
    }
    inline void setMusicEmitter( class CinematicMusicEmitter *pEmitter ) {
        pMusicEmitter = pEmitter;
    }
    inline void setOwner( EntityCinematic *pCinematic ) { pOwner = pCinematic; }
    inline CameraCinematic *getCinematicCamera() const {
        return pCinematicCamera;
    }
    inline void setCinematicCamera( CameraCinematic *pCamera ) {
        pCinematicCamera = pCamera;
    }
    inline void setActorEntity( int32_t index, void *pEntity ) {
        if ( index >= 0 && index < numActors ) {
            actors[index].pEntity = pEntity;
        }
    }
    inline void clearFiredMessages() { firedMessages.clear(); }

    inline static bool bQuiet = false; // TODO: REMOVE ME: Verbose debug flag

public:
    CinResCollection() = default;

    void debugDumpControllers() const; // TODO: REMOVE ME

    void reset();
    void releasePlayerCarActors();
    void releaseTrafficCarActors();
    void bindPlayerCar( CarState *pCarState );
    void releasePlayerCarActor( const int32_t index );

    bool createInterface( const void *pBankData,
                          uint32_t size,
                          void *pBuffer,
                          BankInstance *pSourceBank );

    bool load( const void *pData, uint32_t size, void *pBuffer );
    bool initialize();

    Matrix4x4f_t buildActorMatrix( const CinActorState &state ) const;
    void checkForRegisterActor( Entity *pEntity );
    void checkForUnregisterActor( Entity *pEntity );
    void animActor( EntityActor *pActor, const CinActorState *pState );

    void tick( float time, float deltaTime );
    void resolveRefCamera();

    bool areActorsValid() const;
    void *findActorEntity( const char *pName );
    void setVeryFarScene( const bool bEnabled );
    inline void setCameraShake( const float amplitude, const float speed ) {
        cameraShake.Amplitude = amplitude;
        cameraShake.Speed = speed;
        cameraShake.bEnabled = true;
    }

private:
    struct CinRefSlot {
        const void *pDescriptor;
        void *pHandle;
    };

    struct CinSourceContext {
        CinResCollection *pPlayer;
        const void *pSceneData;
        void **pHandles;
        CinRefSlot *pSceneRefs;
    };

    struct CinMediaContext {
        const CinSceneMediaHeader *pEntry;
        const CinSourceContext *pSource;
        void **pHandleSlot;
    };

private:
    void tickController( const uint8_t *pController, float time );
    void
    tickSource( const CinSourceContext *pCtx, float time, uint32_t mediaId );
    void tickActorMedia( const CinMediaContext *pCtx, float time );
    void tickCameraMedia( const CinMediaContext *pCtx, float time );
    Eigen::Vector3f tickCameraShake( CameraCinematic *pCamera,
                                     float deltaTime );

    bool onCinematicMessage( const void *pKey, const char *pMessage );
    void processCinematicMessages( const uint16_t *pTrack, float time );
    void initializePlayerCarForCinematic();
    Entity *findMicroByName( const char *pName ) const;
    bool onPlayerCarMessage( CarState *pCarState, const char *pMessage );
    void collectActorClips( const CinMediaContext *pCtx,
                            const uint16_t *pTrack,
                            float time,
                            CinActorState &state );
    void collectActorMessages( const CinMediaContext *pCtx,
                               const uint16_t *pTrack,
                               float time,
                               CinActorState &state );
    void
    onShotEvaluated( int32_t shotIndex, const float *pShotKey, float shotTime );
    bool isMessageFired( const void *pKey ) const;
    void recordFiredMessage( const char *pMessage, const void *pKey );
    void removeFiredMessages( uint32_t tag );

    void dispatchActor( CinematicActor *pActor,
                        CinActorState &state,
                        const Eigen::Vector3f &scale,
                        bool bHasTransformTrack );
    void applySpawnTail( Entity *pEntity );
    void applyCarTransform( CarState *pCarState,
                            const Matrix4x4f_t &transform,
                            bool &bTeleport );
    void applyCameraTransform( const CinMediaContext *pCtx,
                               int32_t transformType,
                               const Eigen::Vector3f &position,
                               const Eigen::Quaternionf &heading,
                               float fov,
                               const Eigen::Vector3f &target,
                               const Eigen::Vector3f &up,
                               int32_t posRef,
                               int32_t targetRef,
                               float time );
    static const char *GetActorRefString( const CinMediaContext *pCtx,
                                          int32_t refIdx,
                                          CinematicActor *&pOutActor );
    bool resolveRefPoint( const CinMediaContext *pCtx,
                          int32_t refIdx,
                          const Eigen::Vector3f &spec,
                          Eigen::Vector4f &outPoint,
                          bool &bOutHasRef,
                          const CinematicActor *&pOutDeferredActor ) const;
    bool resolveActorRefPoint( const CinematicActor *pActor,
                               const char *pRefString,
                               const Eigen::Vector3f &spec,
                               Eigen::Vector4f &outPoint,
                               bool &bOutDeferred ) const;
    void resolveDeferredRefPoint( const CinematicActor *pActor,
                                  const Eigen::Vector4f &spec,
                                  Eigen::Vector4f &inOutPoint ) const;
    void applyViewToCinematicCamera( CameraCinematic *pCamera,
                                     float time,
                                     bool bApplyShake );
    void releaseCinematicCamera();

private:
    bool parseCinematic( const void *pData, uint32_t size );
    const uint8_t *resolveRef( int32_t index ) const;
    bool buildSourceMedia( const CinSourceEntry &source, void **&pSourceData );
    void *buildRefMedia( const uint8_t *pRefEntry );
    void *loadSourceByName( const char *pName,
                            const uint64_t *pId,
                            uint32_t *pOutSize );
    bool
    parseSourceScene( void *pSceneBlob, uint32_t size, void **&pSourceData );
    void *createCameraMedia( const CinSceneMediaHeader *pMedia );
    void *createActorMedia( const CinSceneMediaHeader *pMedia );
    CinematicActor *findActor( const char *pName );
    CinematicActor *registerActor( const char *pName,
                                   const void *pSceneData,
                                   void *pEntity,
                                   CinematicActor::eKind kind );
    void *findActorByHashcode( uint64_t key );
    void *spawnActorEntity( const char *pName,
                            uint64_t hashcode,
                            CinematicActor::eKind &outKind );
    void *spawnPedestrian( int32_t gender );
    void *spawnAvatar( eEntityType type, uint64_t hashcode, const char *pName );
    void *spawnNamedAvatar( const struct AvatarConfiguration *pConfig,
                            uint64_t hashcode,
                            bool bBikeClothes );
    void *findSceneActorByName( const char *pName );
    void *spawnPlayerCar();
    void *spawnTrafficCar( const char *pName );
    void *spawnPnjAvatar( const char *pPnjName, uint64_t hashcode );
    void registerLifeSetActor( uint64_t hashcode );

private:
    static constexpr int32_t kMaxActors = 128;
    static constexpr size_t kMaxFiredMessages = 0x20;

    struct FiredMessage {
        uint32_t Tag;
        const void *pKey;
    };

private:
    const CinematicHeader *pCinematicData = nullptr;
    const void *pRefData = nullptr;
    void *pBuffer = nullptr;
    void **pSourceTable = nullptr;
    CinRefSlot *pRefTable = nullptr;
    BankInstance *pBank = nullptr;
    std::array<CinematicActor, kMaxActors> actors{};
    int32_t numActors = 0;
    bool bCreated = false;
    CameraView cameraView{};
    Matrix4x4f_t baseMatrix = Matrix4x4f_t::Identity();
    bool bHasBaseMatrix = false;
    bool bIsLifeSetCinematic = false;
    CinematicActor *pPlayerCarActor = nullptr;
    CarState *pPendingPlayerCar = nullptr;
    bool bActorPoolActive = false;
    struct ZConfig {
        float Near;
        float Far;
    };
    void applyZConfig( const ZConfig &config );

    bool bStreamTarget = false;
    ZConfig zConfigNear{ 0.1f, -1.0f };
    ZConfig zConfigFar{ -1.0f, -1.0f };
    ZConfig zConfigDefault{ -1.0f, -1.0f };
    ZConfig zConfigVeryFar{ -1.0f, -1.0f };
    ZConfig zConfigVeryNear{ 0.01f, -1.0f };
    bool bVeryFarScene = false;
    bool bMustCloseWindowsAndDoors = false;
    bool bPlayerCarTeleport = true;
    int32_t numAvatars = 0;
    int32_t numHostesses = 0;
    Entity *pCurrentMicro = nullptr;
    bool bPlayerAvatarSpawned = false;
    bool bPlayerCarSpawned = false;
    EntityAvatar *pPlayerAvatar = nullptr;
    bool bActorsDriveStreaming = true;
    bool bAsyncActorLoad = false;
    bool bAvatarShadowTarget = false;
    EntityActor *pPlane = nullptr;
    std::vector<FiredMessage> firedMessages;
    bool bFirstShotPending = true;
    bool bFirstShotConsumed = false;
    int32_t currentShot = -1;
    bool bFadeOutNotDone = false;
    float lastDeltaTime = 0.0f;
    class CinematicMusicEmitter *pMusicEmitter = nullptr;
    EntityCinematic *pOwner = nullptr;
    CameraCinematic *pCinematicCamera = nullptr;
    CameraShakeState cameraShake{};

    bool bRefCameraArmed = false;
    bool bRefCameraHasPositionRef = false;
    bool bRefCameraHasTargetRef = false;
    float refCameraFov = 0.0f;
    Eigen::Vector4f refCameraUp = Eigen::Vector4f::Zero();
    Eigen::Vector4f refCameraTarget = Eigen::Vector4f::Zero();
    Eigen::Vector4f refCameraPosition = Eigen::Vector4f::Zero();
    Eigen::Vector4f refCameraPositionSpec = Eigen::Vector4f::Zero();
    Eigen::Vector4f refCameraTargetSpec = Eigen::Vector4f::Zero();
    const CinematicActor *pRefCameraPositionActor = nullptr;
    const CinematicActor *pRefCameraTargetActor = nullptr;

public:
    char debugName[64] = {}; // the owning cinematic's name, for logs
};

extern uint32_t ExtractBufferSize( const void *pData, uint32_t size );

void ReadAvatarConfigFromPNJ( AvatarConfiguration &outConfig,
                              const char *pPNJName );
