#include "shared.h"
#include "ai_pool.h"

AiRoadShape *AiPool::getUnusedShape() {
    // FUN_0091e170
    int32_t leastRecentIndex = 0;
    int32_t leastRecentUse = static_cast<int32_t>( roadShapes[0].LastUse );
    for ( int32_t i = 1; i < kNumRoadShapes; i++ ) {
        const int32_t lastUse = static_cast<int32_t>( roadShapes[i].LastUse );
        if ( lastUse < leastRecentUse ) {
            leastRecentUse = lastUse;
            leastRecentIndex = i;
        }
    }

    AiRoadShape *pShape = &roadShapes[leastRecentIndex];
    if ( leastRecentUse != 0 ) {
        pShape->clear();
    }
    return pShape;
}

void AiPool::removeAllVehicles( const bool bResetPool ) {
    // FUN_0091f9e0
    OTDU_UNIMPLEMENTED_ONCE; // TODO: AI vehicles ("all AI removed")
    if ( bResetPool ) {
        OTDU_UNIMPLEMENTED_ONCE; // TODO: FUN_0091d030
    }
}

void AiPool::removeBots() {
    // FUN_0091f5f0
    OTDU_UNIMPLEMENTED_ONCE; // TODO: AI vehicles ("bots disabled")
}

void AiPool::removeCops() {
    // FUN_0091f630
    OTDU_UNIMPLEMENTED_ONCE; // TODO: AI vehicles ("cops disabled", "cop dams
                             // disabled")
}

void AiPool::removeParkedVehicles() {
    // FUN_0091f6b0
    OTDU_UNIMPLEMENTED_ONCE; // TODO: AI vehicles ("parked disabled")
}

void AiPool::removeOpponents() {
    // FUN_0091f6f0
    OTDU_UNIMPLEMENTED_ONCE; // TODO: AI vehicles ("opponent disabled")
}

void AiPool::removeFlaggedVehicles() {
    // FUN_0091f730
    OTDU_UNIMPLEMENTED_ONCE; // TODO: AI vehicles
}
