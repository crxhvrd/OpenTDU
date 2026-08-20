#pragma once

#include "entity_avatar.h"
#include "game/scenaric/ik_offset_db.h"
#include "game/vehicle/vehicle_base.h"
#include "render/animation/animation.h"

class CarState;
struct HiearchyNode;

enum eRideType : int32_t {
    RT_Bike = 0x63,
    RT_Car = 0x64,
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
static constexpr int32_t kCarIKPointDriverSeat = 0;
static constexpr int32_t kCarIKPointGearLever = 7;
static constexpr int32_t kCarIKPointWindscreen = 9;
static constexpr int32_t kBikeIKPointDriverSeat = 0;

class EntityDriver : public EntityAvatar, public CarStateEventListener {
public:
    const char *GetName() override { return "DrivEr"; /* FUN_00425220 */ }

    inline CarState *getCarState() const { return pCarState; }
    inline bool isSeating() const { return bIsSeating; }
    inline eRideType getDriveType() const { return DriveType; }

public:
    EntityDriver();
    ~EntityDriver();

    void tick() override;
    bool draw( RenderScene *pScene ) override;
    void deserialize( BinaryReader *param_2 ) override;
    void setHeadVisible( bool bVisible ) override;
    EntityDriver *getDriver() override;

    void
    onEvent( class CarState *param_1, float param_2, void *param_3 ) override;

    void setCarState( CarState *pNewCarState );
    void setSeating( bool bState );

private:
    void buildCarIKPoints();
    void buildBikeIKPoints();
    void setSeatModel( HiearchyNode *pNode );
    void setSteer( HiearchyNode *pNode );
    void setGearLever( HiearchyNode *pNode );
    void updateSeatTransform();

protected:
    uint32_t NumAnimRide;
    int32_t NumAnimDrive;
    RenderAnm AnimRide[7];

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
    bool bMustRecalculateDriverSeatPos;

    bool bRiderHasFalled;

    Entity *pLinkedExternalHelmet;
};
