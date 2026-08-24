#pragma once

#include "game/vehicle/vehicle_base.h"

class CarState;
class Driver;
class PlayerCarStateHandler;

class LocalPlayer : public CarStateEventListener {
public:
    inline CarState *getCarState() const { return pCarState; }
    inline Driver *getDriver() const { return pDriver; }
    inline void setDriver( Driver *pNewDriver ) { pDriver = pNewDriver; }
    inline void setGameID( int32_t id ) { gameID = id; }
    inline int32_t getGameID() const { return gameID; }
    inline const Eigen::Vector4f &getLastPosition() const {
        return lastPosition;
    }
    inline void setLastPosition( const Eigen::Vector4f &position ) {
        lastPosition = position;
    }

public:
    LocalPlayer();
    ~LocalPlayer();

    void setCarState( CarState *pNewCarState );
    void
    onEvent( class CarState *param_1, float param_2, void *param_3 ) override;

private:
    int32_t gameID;
    Driver *pDriver;
    CarState *pCarState;
    Eigen::Vector4f lastPosition;
};
