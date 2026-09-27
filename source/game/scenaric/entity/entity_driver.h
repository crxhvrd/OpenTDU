#pragma once

#include "core/response_curve.h"
#include "entity_avatar.h"
#include "game/scenaric/ik_offset_db.h"
#include "game/vehicle/vehicle_base.h"
#include "render/animation/animation.h"
#include "render/animation/anim_expression.h"

class CarState;
struct HiearchyNode;

enum eRideType : int32_t {
    RT_Bike = 0x63,
    RT_Car = 0x64,
};

struct TimeRatio {
    float CurrentTime = 0.0f;
    float TimeEndInv = 3.402823466e+38f;
};
struct IKInterpolator {
    bool bHorizontalTransitionFirst = false;
    float TransitionZOffset = -0.15f; // _DAT_00f41ed8
    float TangentLength = 0.66f;      // DAT_00f03f00
    Eigen::Vector4f LastTarget = Eigen::Vector4f::Zero();
    Eigen::Vector4f CurrentTarget = Eigen::Vector4f::Zero();

    void restart( bool bHorizontalFirst,
                  float deltaTime,
                  float duration,
                  TimeRatio &timeRatio );
    const Eigen::Vector4f &evaluate( const Eigen::Vector4f &target,
                                     const TimeRatio &timeRatio );
};

enum eDriverBodyPosition : int32_t {
    DBP_2HandsOnWheel = 999,
    DBP_LeverUp = 1000,
    DBP_LeverDown = 1001,
    DBP_LeverNeutral = 1002,
};

enum eHandsTarget : int32_t {
    HT_Wheel = 1,
    HT_GearLever = 2,
    HT_Radio = 3,
};

enum eGearBoxType : int32_t {
    GBT_Auto = 0,
    GBT_SemiAuto = 1,
    GBT_Sequential = 2,
    GBT_Mechanical = 3,
};

struct SCIKPoint {
    HiearchyNode *pNode;
    const SCIKOffset *pOffset;
};

static constexpr int32_t kNumCarIKPoints = kNumCarIKOffsets;
static constexpr int32_t kNumBikeIKPoints = kNumBikeIKOffsets;
static constexpr int32_t kNumDriverAnims = 7;
static constexpr int32_t kCarIKPointDriverSeat = 0;
static constexpr int32_t kCarIKPointHandRight = 1; // "Hand_HR_R"
static constexpr int32_t kCarIKPointHandLeft = 2;  // "Hand_HR_L"
static constexpr int32_t kCarIKPointFootRight = 3; // "Foot_HR_R"
static constexpr int32_t kCarIKPointFootLeft = 4;  // "Foot_HR_L"
static constexpr int32_t kCarIKPointRadio = 5;     // "Radio_HR"
static constexpr int32_t kCarIKPointBackLeft = 6;  // "Back_HR_L"
static constexpr int32_t kCarIKPointRightShoulderBelt1 = 15;
static constexpr int32_t kCarIKPointRightShoulderBelt2 = 16;
static constexpr int32_t kCarIKPointLeftShoulderBelt1 = 17;
static constexpr int32_t kCarIKPointLeftShoulderBelt2 = 18;
static constexpr int32_t kCarIKPointRightHipsBelt = 19;
static constexpr int32_t kCarIKPointLeftHipsBelt = 20;
static constexpr int32_t kCarIKPointGearLever = 7;
static constexpr int32_t kCarIKPointWindscreen = 9;
static constexpr int32_t kBikeIKPointDriverSeat = 0;

class EntityDriver : public EntityAvatar, public CarStateEventListener {
public:
    const char *GetName() override { return "DrivEr"; /* FUN_00425220 */ }

    inline CarState *getCarState() const { return pCarState; }
    inline bool isSeating() const { return bIsSeating; }
    inline bool isFreeMovement() const { return bIsFreeMovement; }
    inline void setFreeMovement( bool bEnabled ) { bIsFreeMovement = bEnabled; }
    inline void setUsedIK( bool bEnabled ) { bIsUsedIK = bEnabled; }
    inline void requestSeatRefresh() {
        bMustPlayAnims = true;
        bMustRecalculateDriverSeatPos = true;
    }
    inline eRideType getDriveType() const { return DriveType; }

public:
    EntityDriver();
    ~EntityDriver();

    bool onMessage( FlashMessage *pMessage ) override;
    void tick( PlayerGameMech *pGameMech,
               float deltaTime,
               float totalTime ) override;
    bool draw( RenderScene *pScene ) override;
    void loadResources( ScenaricBank *param_2 ) override;
    void instanciate( MngActor *pMngActor, Entity *param_1 ) override;
    void deserialize( BinaryReader *param_2 ) override;
    void setHeadVisible( bool bVisible ) override;
    int32_t getRenderSceneID() override;
    EntityDriver *getDriver() override;

    void
    onEvent( class CarState *param_1, float param_2, void *param_3 ) override;

    void setCarState( CarState *pNewCarState );
    void setSeating( bool bState );
    void leaveSeatForCinematic();
    void returnToSeatAfterCinematic();
    bool placeInLocalPlayerCar();
    void resetAnimationChannels();

private:
    void buildCarIKPoints();
    void buildBikeIKPoints();
    void setSeatModel( HiearchyNode *pNode );
    void setSteer( HiearchyNode *pNode );
    void setGearLever( HiearchyNode *pNode );
    void updateSeatTransform();
    void playDriveState( int32_t slot );
    void applyDriveBaseAnim();
    void updateAnimChannels( float deltaTime );
    void buildIKChains();
    void updateIKTargets();
    void updateBeltTargets();
    void updateBodyPositionFSM( float deltaTime );
    void updateGearRatios( float deltaTime );
    void updateGearLeverNode( float angle );
    float updateSteerValue( float deltaTime );
    void updateSteeringWheel( float steerNormalised );
    void updateStandDriveRatio( float deltaTime );

protected:
    RenderAnm AnimRide[kNumDriverAnims];
    uint32_t NumAnimRide;
    RenderAnm AnimDrive[kNumDriverAnims];
    int32_t NumAnimDrive;
    RenderAnm AnimFalls[kNumDriverAnims];

    AnimExpressionList ExpressionList;
    AnimIK3NParams IKLeftHand;
    AnimIK3NParams IKRightHand;
    AnimIK3NParams IKLeftFoot;
    AnimIK3NParams IKRightFoot;
    AnimTarget2NParams IKBack;
    AnimLinkParams IKRightShoulderBelt1;
    AnimLinkParams IKRightShoulderBelt2;
    AnimLinkParams IKLeftShoulderBelt1;
    AnimLinkParams IKLeftShoulderBelt2;
    AnimLinkParams IKRightHipsBelt;
    AnimLinkParams IKLeftHipsBelt;
    AnimStateBlendEntry StandDriveRatio[2];
    float LastStandDriveWeight;
    float KeepAccelRatio;
    eDriverBodyPosition CurrentBodyPosition;
    eHandsTarget HandsTarget;
    eHandsTarget PrevHandsTarget;
    float HandsTargetTransitionTime;
    int32_t LastGear;
    float TimeInLeverNeutralPos;
    float IKRatioGear;
    float LastIKRatioGear;
    float RatioGearUp;
    float PrevRatioGearUp;
    float RatioGearDown;
    float PrevRatioGearDown;
    float RatioRadioAnim;
    float PrevRatioRadioAnim;
    float RadioAnimTotalTimeInv;
    AnimStateBlendEntry HandOpenRatio;
    float HandOpenLayerWeight;
    AnimStateBlendEntry GearLeverRatio[5];
    float GearLeverLayerWeight;
    AnimStateBlendEntry RadioTurnRatio;
    float RadioTurnLayerWeight;
    ResponseCurve<3> GearingHandOpenResponseCurve;
    ResponseCurve<6> RatioHandOpenResponseCurve;
    ResponseCurve<2> GearingLayerResponseCurve;
    ResponseCurve<2> RadioTurnRatioResponseCurve;
    ResponseCurve<4> RadioTurnWeightResponseCurve;
    TimeRatio SteerTimeRatio;
    IKInterpolator IKSteerInterpolator;

    float SteerSM;
    float SteerStack[4];
    int32_t SteerStackIndex;
    float LastWheelRatio;

    SCIKPoint CarIKPoints[kNumCarIKPoints];
    const float *pCarHandSize;
    const float *pCarSeatHeight;
    const float *pCarSeatFrontBack;

    SCIKPoint BikeIKPoints[kNumBikeIKPoints];

    const float *pBikeHandSize;
    const float *pBikeBikerAngle;
    const float *pBikeFrontLimit;
    const float *pBikeBackLimit;

    Matrix4x4f_t SteerInit;
    HiearchyNode *pSteer;
    Matrix4x4f_t GearLeverInit;
    HiearchyNode *pGearLever;
    Matrix4x4f_t SeatModelInit;
    HiearchyNode *pSeatModel;
    HiearchyNode *pHandLeft;
    HiearchyNode *pHandRight;

    Eigen::Vector4f VTranslation;

    Matrix4x4f_t TransformRelativeToVehicle;

    float BikeLayersCoeff;
    float LastBikeLayersCoeff;

    float AnimationDeltaTime;

    CarState *pCarState;
    eRideType DriveType;
    eGearBoxType GearboxType;

    float GearLeverAngleUp;
    float GearLeverAngleDown;

    bool bHasInteriorHR;
    bool bIsSeating;
    bool bIsFreeMovement;
    bool bIsUsedIK;
    bool bMustPlayAnims;
    bool bMustPlayRadioAnim;
    bool bIsAccelerationAnimPlayed;
    bool bMustPlayGearUpAnim;
    bool bMustPlayGearDownAnim;
    bool bMustRecalculateDriverSeatPos;

    bool bRiderHasFalled;

    Entity *pLinkedExternalHelmet;
};
