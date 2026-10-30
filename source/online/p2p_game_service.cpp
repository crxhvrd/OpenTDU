#include "shared.h"
#include "p2p_game_service.h"

static CP2PGameService gP2PGameService;
CP2PGameService *gpCP2PGameService = &gP2PGameService; // DAT_010e75f8

CP2PFreeRideMgr::CP2PFreeRideMgr()
    : bUnknownBool1( true ),
      bUnknownBool2( true ),
      bUnknownBool3( true ),
      BotNameLabels{} {}

CP2PGameService::CP2PGameService() : FreeRideMgr() {}
