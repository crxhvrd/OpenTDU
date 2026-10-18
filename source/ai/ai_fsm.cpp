#include "shared.h"
#include "ai_fsm.h"

AiFsm::AiFsm()
    : currentState( 0 ),
      nextState( 0 ),
      bTransitionPending( false ),
      stateStack{ 0, 0, 0, 0 },
      stackSize( 0 ) {}

void AiFsm::process( const int32_t event ) {
    // FUN_00407fb0
    handle( event, currentState );
    for ( int32_t i = kMaxChainedTransitions; bTransitionPending && i > 0;
          i-- ) {
        bTransitionPending = false;
        handle( FE_Exit, currentState );
        currentState = nextState;
        handle( FE_Enter, currentState );
    }
}
