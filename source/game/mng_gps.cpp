#include "shared.h"
#include "mng_gps.h"

MngGPS::MngGPS() : Manager(), mutex( "MngGPS" ) {
    // FUN_006c2280
    lastObjectivePosition.setZero();
    lastSecondaryObjectivePosition.setZero();
    hudTargetPosition.setZero();
}

MngGPS::~MngGPS() {}

bool MngGPS::initialize( TestDriveGameInstance * ) {
    // FUN_006c1d50
    mutex.initialize( "MngGPS" );
    cachedSessionPlayerCount = 0;
    return true;
}

void MngGPS::tick( float, float ) {
    // FUN_006c38d0
    OTDU_UNIMPLEMENTED;
}

void MngGPS::reset() {
    // FUN_006c1d40
    // NOP
}

void MngGPS::draw() {
    // FUN_0063f2b0
    // NOP
}

void MngGPS::pause() {
    // FUN_006c1d30
    // NOP
}

void MngGPS::terminate() {
    // FUN_006c1d20
    mutex.terminate();
}
