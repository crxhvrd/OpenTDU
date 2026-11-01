#pragma once

#include "game/gm_world.h"
#include "game/scenaric/scenaric_bank.h"

class EntityCinematic;
class FlashPlayer;

class GMJail : public GameModeWorldBase {
public:
    GMJail();
    ~GMJail();

    void initialize() override;
    void reset() override;
    void tick( float deltaTime, float totalTime ) override;
    void draw( float deltaTime, float totalTime ) override;
    void pause() override;
    void destroy() override;
    bool onMessage( FlashMessage *pMessage, FlashPlayer *pPlayer ) override;
    bool executeAction( int32_t param_2,
                        void *param_3,
                        void *pData = nullptr ) override;

private:
    void endJail();

private:
    ScenaricBank jailBank;
    EntityCinematic *pJailCinematic;
    EntityCinematic *pPendingCinematic;
    float elapsedTime;
    float jailDuration;
    FlashPlayer *pFlashPlayer;
    bool bPopupShown;
};
