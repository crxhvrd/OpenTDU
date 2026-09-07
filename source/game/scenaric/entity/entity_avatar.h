#pragma once

#include "entity_actor.h"
#include "game/avatar/avatar_clothes.h"

class GSAvatar;
class EntityDriver;

extern float gAvatarLODDistances[3];

class EntityAvatar : public EntityActor {
    friend class GSAvatar;

public:
    const char *GetName() override { return "Avatar"; /* FUN_00424660 */ }

public:
    inline int32_t getAvatarType() const { return avatarType; }
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
    inline void setInstanceRefCount( const int32_t value ) {
        InstanceRefCount = value;
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
    void tick() override;
    bool draw( RenderScene *pScene ) override;
    void deserialize( BinaryReader *param_2 ) override;
    void instanciate( MngActor *pMngActor, Entity *param_1 ) override;
    void deinstanciate() override;
    void relight() override;
    void unknownActorFunc() override;

    // Vtable entries specific to avatar entities
    virtual int32_t getRenderSceneID();
    virtual void setHeadVisible( bool bVisible );
    virtual void setAvatarType( int32_t type );
    virtual EntityDriver *getDriver();
    virtual int32_t unknownAvatarFunc2();
    virtual void getLightingFactor( Eigen::Vector4f &outFactor );
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

private:
    void onCarAlphaTerm();
    void freeAlphaSlot();

protected:
    // 'Head' bone name hashcode
    static constexpr uint32_t kHeadBoneHashcode = 0x64616548;

protected:
    int32_t avatarType; // 1 = driver
    int32_t PlayerType;
    const float *pLightingAttenuation;
    float currentEnvFactor;
    float currentEnvFactorBackFace;
    void *pSerializedData;
    PooledAvatarEntry *pPooledAvatarEntry;
    int32_t FaceParams[kNumFaceParams];
    void *pBehavior;
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
    uint32_t HelmetLinkBoneHC;
    bool bNeedToRefreshFace;
    bool bForceShowHelmet;
    bool bLastShowHead;
    bool bIsToUpdate;
    bool bLowResShadow;
};
