#pragma once

#include "game/mng_base.h"

struct FlashMessage;
class GMLiving;

// Rankings/leaderboard panel
class MngRankings : public Manager {
public:
    const char *getName() const override {
        // FUN_0042d740
        return "Rankings";
    }
    inline void setGameMode( GMLiving *pGameMode ) { pOwner = pGameMode; }

public:
    MngRankings();
    ~MngRankings();

    void terminate() override;
    bool onMessage( FlashMessage *pMessage );

private:
    GMLiving *pOwner = nullptr;
};
