#pragma once

class AiVehicle;
class CarState;
struct CollisionEvent;

class AiVehBehaviour {
public:
    virtual ~AiVehBehaviour() = default;

    virtual void attach( AiVehicle *pOwner ) = 0;
    virtual void reset( const bool bFull ) = 0;
    virtual void tick( const float deltaTime, const float totalTime ) = 0;
    virtual void release() = 0;
    virtual void onCollision( CarState *pCarA,
                              CarState *pCarB,
                              const CollisionEvent &event ) = 0;
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
                      const CollisionEvent &event ) override;

private:
    AiVehicle *pVehicle;
};
