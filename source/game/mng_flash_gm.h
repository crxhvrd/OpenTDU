#pragma once

#include "flash/mng_flash.h"

class GameModeScene;

class MngFlashGM : public MngFlash {
public:
    inline bool isTransitioningToGPS() const { return bTransitionToGPS; }
    inline void requestGPSTransition() { bTransitionToGPS = true; }

public:
    MngFlashGM( GameModeScene *pGameMode );
    ~MngFlashGM();

    void tick( float, float ) override;
    bool onMessage( FlashMessage *pMessage, FlashPlayer *pPlayer ) override;

protected:
    GameModeScene *pGMScene;
    bool bTransitionToGPS;
};
