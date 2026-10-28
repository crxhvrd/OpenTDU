#pragma once

#include "ai/ai_veh_nav_traffic.h"

class AiVehicle;

// Navigation type 2: holds a car handed over to the AI by a cinematic ("ctia")
class AiVehNavCinematic : public AiVehNavigation {
public:
    AiVehNavCinematic();

    void attach( AiVehicle *pVehicle ) override;
    void reset( const bool bFull ) override;
    void tick( const float deltaTime, const float totalTime ) override;
    void detach() override;
    bool getTarget( Eigen::Vector4f &outDirection,
                    float &outSpeed ) const override;
    void getHeightAndTwist( float &outHeight, float &outTwist ) const override;

private:
    AiVehicle *pOwner;
    Eigen::Vector4f anchor;
    Eigen::Vector4f target;
    float speed;
    float holdTime;
    float unknown0x58;
};
