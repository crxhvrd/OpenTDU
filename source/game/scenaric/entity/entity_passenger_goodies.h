#pragma once

#include "entity_actor.h"

class CarState;
struct HiearchyNode;
struct IKOffset;

enum ePassengerGoodies : int32_t {
    PG_MovementFree = 0,
    PG_MovementInCar = 1,
};

// One car-relative IK point
struct IKPoint {
    HiearchyNode *pHiearchNode;
    IKOffset *pOffset;
};

class EntityPassengerGoodies : public EntityActor {
public:
    const char *GetName() override {
        return "PassengerGoodies"; /* FUN_00425820 */
    }

public:
    EntityPassengerGoodies();
    ~EntityPassengerGoodies();

    void tick( PlayerGameMech *pGameMech,
               float deltaTime,
               float totalTime ) override;
    bool draw( RenderScene *pScene ) override;
    void deserialize( BinaryReader *param_2 ) override;

protected:
    static constexpr int32_t kNumCarIKPoints = 27;

protected:
    ePassengerGoodies CurrentBehavior;
    CarState *pCarState;
    Eigen::Vector4f Translation;
    IKPoint CarIKPoints[kNumCarIKPoints];
    float *pCarPsgSeatHeight;
    float *pCarPsgSeatFrontBack;
    Matrix4x4f_t PsgSeatModelInitMatrix;
    HiearchyNode *pPsgSeatModel;
    HiearchyNode *pPsgSeatModelRoot;
};
