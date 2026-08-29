#pragma once

#include <array>

#include "cinematic_format.h"
#include "game/scenaric/entity/entity_factory.h"

class BankInstance;
class ClipInstance;

struct CinematicActor {
    enum eKind : int32_t {
        CAK_Entity = 0,    // pEntity is an Entity* (scene actor / ped / avatar)
        CAK_PlayerCar = 1, // the local player car; pEntity stays null
    };

    char Name[32];
    void *pEntity; // the bound EntityActor / avatar (null until spawned)
    const void *pSceneData; // this actor's CinScene media descriptor
    eKind Kind = CAK_Entity;
};

class CinResCollection {
public:
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
    inline void *getActorEntity( int32_t index ) const {
        return ( index >= 0 && index < numActors ) ? actors[index].pEntity
                                                   : nullptr;
    }
    inline CinematicActor::eKind getActorKind( int32_t index ) const {
        return ( index >= 0 && index < numActors ) ? actors[index].Kind
                                                   : CinematicActor::CAK_Entity;
    }
    inline void setActorEntity( int32_t index, void *pEntity ) {
        if ( index >= 0 && index < numActors ) {
            actors[index].pEntity = pEntity;
        }
    }

    inline static bool bQuiet = false; // TODO: REMOVE ME: Verbose debug flag

public:
    CinResCollection() = default;

    void debugDumpControllers() const; // TODO: REMOVE ME

    void reset();

    bool createInterface( const void *pBankData,
                          uint32_t size,
                          void *pBuffer,
                          BankInstance *pSourceBank );

    bool load( const void *pData, uint32_t size, void *pBuffer );
    bool initialize();

    void tick( float time, float deltaTime );

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

    void processActorMessages( const CinMediaContext *pCtx,
                               const uint16_t *pTrack,
                               float time );

    void playClipOnActorEntity( const CinematicActor *pActor,
                                ClipInstance *pClip );

    void applyActorTransform( const CinematicActor *pActor,
                              const Eigen::Vector3f &position,
                              const Eigen::Quaternionf &heading,
                              const Eigen::Vector3f &scale,
                              bool bHasTransformTrack );
    void applyPlayerCarTransform( const Matrix4x4f_t &transform );
    void applyCameraTransform( int32_t transformType,
                               const Eigen::Vector3f &position,
                               const Eigen::Quaternionf &heading,
                               float fov );

private:
    bool parseCinematic( const void *pData, uint32_t size );
    const uint8_t *resolveRef( int32_t index ) const;
    bool buildSourceMedia( const CinSourceEntry &source, void **&pSourceData );
    void buildRefMedia( const uint8_t *pRefEntry, int32_t slot );
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
    void *findSceneActorByName( const char *pName );
    void *spawnPlayerCar();

private:
    static constexpr int32_t kMaxActors = 128;

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
    int32_t lastMessageRef = -1;
};

extern uint32_t ExtractBufferSize( const void *pData, uint32_t size );