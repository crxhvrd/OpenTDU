#pragma once

class AiVehicle;
class CarState;

class AiVehBehaviour {
public:
    virtual ~AiVehBehaviour() = default;

    virtual void attach( AiVehicle *pOwner ) = 0;
    virtual void reset( const bool bFull ) = 0;
    virtual void tick( const float deltaTime, const float totalTime ) = 0;
    virtual void release() = 0;
    virtual void
    onCollision( CarState *pCarA, CarState *pCarB, const float impulse ) = 0;
};

// Behaviour type 2: traffic gives up its navigation when stuck or crashed
class AiVehBhvTraffic : public AiVehBehaviour {
public:
    AiVehBhvTraffic();

    void attach( AiVehicle *pOwner ) override;
    void reset( const bool bFull ) override;
    void tick( const float deltaTime, const float totalTime ) override;
    void release() override;
    void onCollision( CarState *pCarA,
                      CarState *pCarB,
                      const float impulse ) override;

private:
    AiVehicle *pVehicle;
};
