#pragma once

#include "game/gm_world.h"

struct AvatarConfiguration;
class EntityAvatarPassenger;

class GMTestCar : public GameModeWorld {
public:
    static void SetTestCar( const int32_t carID,
                            const int32_t paintIndex,
                            const int32_t rimID,
                            const int32_t interiorID );
    static void SetTestDuration( const float duration );
    static AvatarConfiguration &GetPassengerConfiguration();

public:
    GMTestCar();
    ~GMTestCar();

    void initialize() override;
    void reset() override;
    void tick( float deltaTime, float totalTime ) override;
    void draw( float deltaTime, float totalTime ) override;
    void pause() override;
    void destroy() override;

    bool onMessage( FlashMessage *pMessage ) override;

private:
    void loadPassenger();
    void releasePassenger();
    void endTestDrive();

private:
    float startTime;
    float endTime;
    EntityAvatarPassenger *pPassenger;
};
