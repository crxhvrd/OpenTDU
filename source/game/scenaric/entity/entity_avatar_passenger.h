#pragma once

#include "entity_avatar.h"
#include "entity_driver.h"
#include "render/animation/animation.h"
#include "render/animation/anim_expression.h"

class CarState;
struct HiearchyNode;

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

    void setCarState( CarState *pNewCarState );
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
    RenderAnm AnimHi;
    RenderAnm AnimPeepin;
};
