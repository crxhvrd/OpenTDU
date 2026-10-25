#pragma once

#include "entity_actor.h"
#include "entity_driver.h"

class CarState;
struct HiearchyNode;

enum ePassengerGoodies : int32_t {
    PG_MovementFree = 0,
    PG_MovementInCar = 1,
};

class EntityPassengerGoodies : public EntityActor {
public:
    const char *GetName() override {
        return "PassengerGoodies"; /* FUN_00425820 */
    }

public:
    EntityPassengerGoodies();
    ~EntityPassengerGoodies();

    void
    tick( GameMechanism *pGameMech, float deltaTime, float totalTime ) override;
    bool draw( RenderScene *pScene ) override;
    void deserialize( BinaryReader *param_2 ) override;

    void setCarState( CarState *pNewCarState );
    void setSeatModel( HiearchyNode *pNode );
    void updateTranslation();

    inline void setBehavior( const ePassengerGoodies behavior ) {
        CurrentBehavior = behavior;
    }

protected:
    static constexpr int32_t kCarIKPointCargo = 14; // "PsgSeat_HR"

protected:
    ePassengerGoodies CurrentBehavior;
    CarState *pCarState;
    Eigen::Vector4f Translation;
    SCIKPoint CarIKPoints[kNumCarIKPoints];
    float *pCarPsgSeatHeight;
    float *pCarPsgSeatFrontBack;
    Matrix4x4f_t PsgSeatModelInitMatrix;
    HiearchyNode *pPsgSeatModel;
    HiearchyNode *pPsgSeatModelRoot;
};
