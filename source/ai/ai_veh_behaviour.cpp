#include "shared.h"
#include "ai_veh_behaviour.h"

#include "ai/ai_config.h"
#include "ai/ai_vehicle.h"
#include "physics/collision_event.h"

static constexpr float kMaxBlockedTime = 6.0f; // DAT_00f41840

AiVehBhvTraffic::AiVehBhvTraffic() : pVehicle( nullptr ) {}

void AiVehBhvTraffic::attach( AiVehicle *pOwner ) {
    // FUN_004adc30
    pVehicle = pOwner;
}

void AiVehBhvTraffic::reset( const bool bFull ) {
    // FUN_00614df0
    (void)bFull;
}

void AiVehBhvTraffic::tick( const float deltaTime, const float totalTime ) {
    // FUN_009cb960
    (void)totalTime;
    if ( deltaTime <= 0.0f ) {
        return;
    }
    if ( pVehicle->getInfos().BlockedTime > kMaxBlockedTime ) {
        pVehicle->onNavigationLost();
    }
}

void AiVehBhvTraffic::release() {
    // FUN_0063f2b0
}

void AiVehBhvTraffic::onCollision( CarState *pCarA,
                                   CarState *pCarB,
                                   const CollisionEvent &event ) {
    // FUN_009cb990
    (void)pCarA;
    (void)pCarB;
    if ( event.Energy > gAIConfig.Unknown0x1BC ) {
        pVehicle->onNavigationLost();
    }
}
