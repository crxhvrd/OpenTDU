#pragma once

#include "entity_actor.h"
#include "game/vehicle/mng_car_alpha.h"
#include "game/avatar/avatar_clothes.h"
#include "render/animation/skeleton.h"

class AvatarLayer;
class GSAvatar;
class EntityDriver;

extern float gAvatarLODDistances[3];
extern bool gbDisableFaceMorph;

class EntityAvatar : public EntityActor, public CarAlphaListener {
    friend class GSAvatar;

public:
    const char *GetName() override { return "Avatar"; /* FUN_00424660 */ }

public:
    inline int32_t getAvatarType() const { return avatarType; }
    inline void resetCineAvatarMode() { CineAvatarMode = 0; }
    inline int32_t getCineAvatarMode() const { return CineAvatarMode; }
    inline void setCineAvatarMode( int32_t mode ) { CineAvatarMode = mode; }
    inline PooledAvatarEntry *getPooledEntry() const {
        return pPooledAvatarEntry;
    }
    inline void addInstanceReference() { InstanceRefCount++; }
    inline void removeInstanceReference() {
        if ( InstanceRefCount > 0 ) {
            InstanceRefCount--;
        }
    }
    inline int32_t getInstanceRefCount() const { return InstanceRefCount; }
    inline void setAlpha( const float value ) { Alpha = value; }
    inline void setInstanceRefCount( const int32_t value ) {
        InstanceRefCount = value;
    }
    inline void setForceShowHelmet( const bool bEnabled ) {
        bForceShowHelmet = bEnabled;
    }
    inline void setLowResShadow( const bool bEnabled ) {
        bLowResShadow = bEnabled;
    }
    inline void requestFaceRefresh() { bNeedToRefreshFace = true; }
    inline int32_t getFaceParam( const int32_t index ) const {
        return FaceParams[index];
    }
    inline void setFaceParam( const int32_t index, const uint32_t value ) {
        FaceParams[index] = static_cast<int32_t>( std::min( value, 15u ) );
        bNeedToRefreshFace = true;
    }

public:
    EntityAvatar();
    ~EntityAvatar();

    // Entity / EntityActor overrides
    bool onMessage( FlashMessage *pMessage ) override;
    void initializeForCinematic( void *param_2, void *param_3 ) override;
    void initializeForCinematicKeyframed( void *param_2,
                                          void *param_3 ) override;
    bool isValid() const override;
    void initialize() override;
    void loadResources( ScenaricBank *param_2 ) override;
    void unloadResources() override;
    void tick( PlayerGameMech *pGameMech,
               float deltaTime,
               float totalTime ) override;
    bool draw( RenderScene *pScene ) override;
    void deserialize( BinaryReader *param_2 ) override;
    void instanciate( MngActor *pMngActor, Entity *param_1 ) override;
    void deinstanciate() override;
    void relight() override;
    void unknownActorFunc() override;

    void onAlphaChange() override;

    // Vtable entries specific to avatar entities
    virtual int32_t getRenderSceneID();
    virtual void setHeadVisible( bool bVisible );
    virtual void setAvatarType( int32_t type );
    virtual EntityDriver *getDriver();
    virtual class EntityAvatarPassenger *getPassenger();
    virtual void getCarAlphaScreenRect( Eigen::Vector4f &outRect );
    virtual int32_t chooseLODLevel();
    virtual void onSkeletonChange( void *pSkeleton );
    virtual void onResourceChange();
    virtual void relightFromWeather( DirectionalLight *pLight );
    virtual void onHeadUpdate( Matrix4x4f_t &headTransform );
    virtual void updateCarAlpha();
    virtual bool isShadowCaster();

public:
    void setEnvMap( bool bRealTime, Render2DB *p2DB );
    void installConfiguration( const AvatarConfiguration *pConfig,
                               int32_t clotheSet,
                               bool bAsync,
                               bool bCritical );
    void endCinematic( bool bStillReferenced,
                       class EntityCinematic *pLifeCinematic );
    void endLifeSetCinematic();
    void addLayer( AvatarLayer *pLayer );
    void removeLayer( AvatarLayer *pLayer );
    void setSkin( struct AvatarSkinPart *pSkin );

private:
    void freeAlphaSlot();
    void refreshFace();

protected:
    // 'Head' bone name hashcode
    static constexpr uint32_t kHeadBoneHashcode = 0x64616548;

protected:
    int32_t avatarType; // 1 = driver
    int32_t PlayerType;
    const float *pLightingAttenuation;
    float currentEnvFactor;
    float currentEnvFactorBackFace;
    float ShadowIntensity;
    PooledAvatarEntry *pPooledAvatarEntry;
    int32_t FaceParams[kNumFaceParams];
    AvatarLayer *pLayers;
    void *pVoiceEmitter;
    void *pCineInterface;
    float Alpha;
    int32_t RenderLOD;
    int32_t LodLevel;
    float LODDistances[3];
    int32_t AlphaSceneSlot;
    int32_t CineAvatarMode;
    int32_t InstanceRefCount;
    int32_t HeadBoneIndex;
    void *pUseSkeleton;
    SkeletonInstance avatarSkeleton;
    AnimPoseBuffer facePose; // +0x200
    uint32_t HelmetLinkBoneHC;
    bool bNeedToRefreshFace;
    bool bForceShowHelmet;
    bool bLastShowHead;
    bool bIsToUpdate;
    bool bLowResShadow;
};
