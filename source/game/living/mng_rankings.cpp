#include "shared.h"
#include "flash/entities/flash_entity.h"
#include "mng_rankings.h"

MngRankings::MngRankings() : Manager() {
    // FUN_00764640
}

MngRankings::~MngRankings() {
    // FUN_0042d740
}

void MngRankings::terminate() {
    // FUN_00765760
    OTDU_UNIMPLEMENTED; // TODO: GMLIVING (rankings panel)
}

bool MngRankings::onMessage( FlashMessage * /*pMessage*/ ) {
    // FUN_00764d90
    OTDU_UNIMPLEMENTED_ONCE; // TODO: GMLIVING (rankings panel)
    return false;
}
