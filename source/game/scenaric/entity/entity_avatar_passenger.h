#pragma once

#include "entity_avatar.h"
#include "entity_driver.h"
#include "core/random/bcpl.h"
#include "filesystem/bank.h"
#include "render/animation/animation.h"
#include "render/animation/anim_expression.h"

#include <memory>

class AudioClient;
class CarState;
class edAudioSample;
class edAudioStreamEntry;
struct HiearchyNode;

// Voice lines of a hitch-hiker / shopping girl
// ("Sound/Voices/<language>/<prefix>_<voice>.bnk")
class PassengerVoiceBank : public Bank {
public:
    enum eStatus : int32_t {
        PVBS_None = 0,
        PVBS_Loading = 1,
        PVBS_Ready = 2,
    };

    static constexpr int32_t kMaxSounds = 6;
    static constexpr int32_t kSoundNameLength = 0x80;
    static constexpr int32_t kVoiceNameLength = 0x40;

public:
    inline bool isReady() const { return Status == PVBS_Ready; }
    inline const edAudioSample *getSound( const int32_t index ) const {
        return pSounds[index].get();
    }
    inline const char *getSoundName( const int32_t index ) const {
        return SoundNames[index];
    }

public:
    PassengerVoiceBank( const char *pInBankPrefix,
                        const char *pInSoundNameFormat,
                        const int32_t numSounds );
    ~PassengerVoiceBank();

    bool load( const char *pVoiceName );
    void loadContent() override;
    void release();

private:
    const char *pBankPrefix;
    const char *pSoundNameFormat;
    int32_t NumSounds;
    eStatus Status;
    char VoiceName[kVoiceNameLength];
    std::unique_ptr<edAudioSample> pSounds[kMaxSounds];
    char SoundNames[kMaxSounds][kSoundNameLength];
};

// Voice line playback shared by the hitch-hiker and shopping girl
class PassengerVoicePlayer {
public:
    inline bool isPlaying() const { return bPlaying; }
    inline bool hasPendingSound() const { return pPendingSound != nullptr; }

public:
    PassengerVoicePlayer();
    ~PassengerVoicePlayer();

    void create( const char *pDescription );
    void destroy();
    void requestStop();
    void clear();
    void queue( const edAudioSample *pSound, const char *pSoundName );
    void tick();

private:
    AudioClient *pClient;
    edAudioStreamEntry *pEntry;
    const edAudioSample *pPendingSound;
    const char *pPendingSoundName;
    bool bPlaying;
};

static constexpr int32_t kNumPassengerDrivingSounds = 4;

int32_t PickLeastPlayedDrivingSound(
    uint32_t ( &playCounts )[kNumPassengerDrivingSounds], RandomGen &random );

// Base class for passenger
class EntityAvatarPassenger : public EntityAvatar {
public:
    enum ePassengerBehavior : int32_t {
        PB_MovementFree = 0,
        PB_PassengerNoAnims = 1,
        PB_Passenger = 2,
        PB_Idle = 3,
    };

public:
    inline CarState *getCarState() const { return pCarState; }
    inline bool isSeated() const {
        // FUN_00413b70
        return CurrentBehavior == PB_Passenger ||
               CurrentBehavior == PB_PassengerNoAnims;
    }

public:
    EntityAvatarPassenger();
    ~EntityAvatarPassenger();

    bool onMessage( FlashMessage *pMessage ) override;
    void initializeForCinematic( void *param_2, void *param_3 ) override;
    void initialize() override;
    void loadResources( ScenaricBank *param_2 ) override;
    void unloadResources() override;
    void
    tick( GameMechanism *pGameMech, float deltaTime, float totalTime ) override;
    bool draw( RenderScene *pScene ) override;
    void instanciate( MngActor *pMngActor, Entity *param_1 ) override;

    int32_t getRenderSceneID() override;
    EntityAvatarPassenger *getPassenger() override;
    void getCarAlphaScreenRect( Eigen::Vector4f &outRect ) override;
    void onSkeletonChange( void *pSkeleton ) override;
    void relightFromWeather( DirectionalLight *pLight ) override;
    bool isShadowCaster() override;

    virtual void resetVoice() { /* NOP */ }
    virtual void resetFreerideAnim() { /* NOP */ }

    void setCarState( CarState *pNewCarState );
    void requestStandAnim( RenderAnm *pAnim );
    void seatInCar();
    void unseat();
    int32_t setBehavior( const int32_t behavior );

protected:
    void deserializeAnims( BinaryReader *pReader, uint32_t minVersion );

private:
    void setSeatModel( HiearchyNode *pNode );
    void updateSeatTransform();
    void updateCarAnims( float deltaTime );
    void updateIKTargets();
    void updateLightingAttenuation();
    void playCarAnimState( RenderAnm &anim,
                           const AnimStateBlendEntry *pRatio,
                           const uint32_t clipID );
    void playCarAnimStateFromPosture( RenderAnm &anim,
                                      const AnimStateBlendEntry *pRatio,
                                      const uint32_t clipID,
                                      const bool bLooped );
    void setSeatBeltVisible( const bool bVisible );

protected:
    static constexpr uint32_t kNumAnimCar = 2;

protected:
    SCIKPoint CarIKPoints[kNumCarIKPoints];
    const float *pCarPassengerSeatHeight;
    const float *pCarPassengerSeatFrontBack;
    RenderAnm pAnimCar[kNumAnimCar];
    uint32_t NumAnimCar;
    AnimStateBlendEntry AnimCarStand;
    AnimStateBlendEntry FrontBackRatio;
    float LastAccelerationRatio;
    AnimExpressionList ExpressionList;
    AnimIK3NParams IKLeftFoot;
    AnimIK3NParams IKRightFoot;
    AnimTarget2NParams IKBack;
    AnimLinkParams IKLeftShoulderBelt1;
    AnimLinkParams IKLeftShoulderBelt2;
    AnimLinkParams IKRightShoulderBelt1;
    AnimLinkParams IKRightShoulderBelt2;
    AnimLinkParams IKRightHipsBelt;
    AnimLinkParams IKLeftHipsBelt;
    Matrix4x4f_t PassengerSeatModelInit;
    HiearchyNode *pPassengerSeatModel;
    HiearchyNode *pPassengerSeatModelRoot;
    CarState *pCarState;
    Eigen::Vector4f Translation;
    int32_t CurrentBehavior;
    bool bHasInteriorHR;
    bool bMustPlayCarAnims;
    bool bSkeletonHasChanged;
    bool bMustPlayPassengerStandAnim;
    bool bIsPlayingPassengerStandAnim;
    bool bForceNotUpdatePos;
    RenderAnm *pStandAnim;
    RenderAnm AnimHi;
    RenderAnm AnimPeepin;
};
