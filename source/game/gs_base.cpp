#include "shared.h"
#include "gs_base.h"

#include "world/gs_world.h"
#include "world/streaming_manager.h"

GameSystem::GameSystem()
    : index( 0xffffffff ), bPaused( false ), bStaticallyAllocated( false ) {
#ifdef OTDU_USE_IMGUI
    bDebugMenuImplemented = true;
#endif
}

bool GameSystem::initialize( TestDriveGameInstance *param_2 ) {
    // FUN_009b3f80
    this->pGameInstance = param_2;
    this->bPaused = false;
    return true;
}

void GameSystem::reset() {
    // FUN_009b4120
    gpWorld->getStreamingManager()->reinit();
}

bool GameSystem::isPaused() const { return bPaused; }

uint32_t GameSystem::getIndex() const { return index; }
