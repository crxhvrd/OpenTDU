#include "shared.h"
#include "road_phantom_manager.h"

RoadPhantomManager::RoadPhantomManager()
    : numMaxRoadPhantoms( 0 ),
      numRoadPhantoms( 0 ),
      roadPhantoms(),
      numRoadPhantomEvents( 0 ),
      roadPhantomEvents(),
      numMaxCallbacks( 0 ),
      numCallbacks( 0 ),
      callbacks() {}

RoadPhantomManager::~RoadPhantomManager() {}

void RoadPhantomManager::initialize( int32_t param_1, int32_t param_3 ) {
    // FUN_00819d80
    numMaxRoadPhantoms = param_1;
    numRoadPhantoms = 0;
    roadPhantoms.assign( static_cast<size_t>( param_1 ), nullptr );

    numMaxCallbacks = param_3;
    numCallbacks = 0;
    callbacks.assign( static_cast<size_t>( param_3 ), nullptr );

    numRoadPhantomEvents = 0;
    roadPhantomEvents.assign( static_cast<size_t>( kNumMaxRoadPhantomEvents ),
                              RoadPhantomEvt{} );
}

void RoadPhantomManager::dispatchEvents() {
    // FUN_00819b40
    const int32_t numEvents =
        Min( numRoadPhantomEvents,
             static_cast<int32_t>( roadPhantomEvents.size() ) );

    for ( int32_t i = 0; i < numEvents; i++ ) {
        const RoadPhantomEvt &evt = roadPhantomEvents[i];
        for ( RoadPhantomCallback *pCallback : callbacks ) {
            if ( pCallback == nullptr ) {
                continue;
            }
            if ( evt.bAdding ) {
                pCallback->onRoadPhantomAdd( evt );
            } else {
                pCallback->onRoadPhantomRemove( evt );
            }
        }
    }
}
