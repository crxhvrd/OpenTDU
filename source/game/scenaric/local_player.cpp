#include "shared.h"
#include "local_player.h"

#include "game/vehicle/car_state.h"
#include "game/vehicle/gs_car_state.h"
#include "game/vehicle/vehicle_base.h"

void LocalPlayerInfringementCheck::check( CarState *pCarState,
                                          float deltaTime ) {
    // FUN_00805db0
    pCarState->getPoliceState().checkInfringements( pCarState, deltaTime );
}

LocalPlayer::LocalPlayer()
    : CarStateEventListener(),
      gameID( -1 ),
      pDriver( nullptr ),
      pCarState( nullptr ),
      lastPosition( 0.0f, 0.0f, 0.0f, 1.0f ) {
    // FUN_004238e0
}

LocalPlayer::~LocalPlayer() = default;

void LocalPlayer::setCarState( CarState *pNewCarState ) {
    // FUN_00805dd0
    if ( pCarState == pNewCarState ) {
        return;
    }
    if ( gpCarState == nullptr ) {
        return;
    }

    if ( pCarState != nullptr ) {
        gpCarState->unregisterCallback( pCarState, this );
        gpCarState->setPoliceStateJob( pCarState, nullptr );
    }

    pCarState = pNewCarState;

    if ( pNewCarState != nullptr ) {
        gpCarState->registerCallback( pNewCarState, this, nullptr );
        gpCarState->setPoliceStateJob( pNewCarState, &checkInfringement );
    }
}

void LocalPlayer::onEvent( class CarState *param_1,
                           float param_2,
                           void *param_3 ) {
    // FUN_00803900
    if ( gpCarState->isCarStateUsed( param_1->getCarInstanceID() ) ) {
        lastPosition = param_1->getWorldPosition();
    }
}
