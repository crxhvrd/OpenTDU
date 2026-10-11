#include "shared.h"
#include "gs_intro_pool.h"

#include "ai/ai_pool.h"
#include "ai/gs_ai.h"

#include "game/vehicle/gs_car_display.h"
#include "world/gs_world.h"
#include "world/spot_stream.h"
#include "world/streaming_manager.h"

GSIntroPool gIntroPool = {};
GSIntroPool *gpIntroPool = nullptr;

GSIntroPool::GSIntroPool()
    : GameSystem(), pIntroPool( nullptr ) { // FUN_009de8d0
    gpIntroPool = this;
}

GSIntroPool::~GSIntroPool() {
    // FUN_009eab40
    gpIntroPool = nullptr;
}

bool GSIntroPool::initialize( TestDriveGameInstance *pGameInstance ) {
    // FUN_008a4e50
    // MOV        EAX, [UINT_00f7f16c] = 330986Ch
    // ADD        puVar1, DAT_00a80000 = 24h    $
    static constexpr uint32_t kPoolSize = 64526444;

    bPaused = false;
    pIntroPool = TestDrive::Alloc( kPoolSize );
    return true;
}

void GSIntroPool::terminate() {
    // FUN_008a4de0
    if ( pIntroPool != nullptr ) {
        TestDrive::Free( pIntroPool );
        pIntroPool = nullptr;
    }
}

void EnableWorldSpotStreaming() {
    // FUN_008a4d90
    if ( gpCarDisplay != nullptr ) {
        gpCarDisplay->freeResources();
    }

    gpAI->getAIPool()->setTrafficPaused( false );

    if ( gpWorld == nullptr || gpWorld->getStreamingManager() == nullptr ) {
        return;
    }

    SpotStream &spotStream = gpWorld->getStreamingManager()->spotStream;

    // TODO: FIXME: REMOVE
    spotStream.arm();

    spotStream.bStream = true;
    spotStream.bDraw = true;
}
