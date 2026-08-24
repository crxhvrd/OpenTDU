#pragma once

#include "game/mng_base.h"

struct FlashMessage;
class GMLiving;

// Car/house collection panel
class MngCollection : public Manager {
public:
    const char *getName() const override {
        // FUN_0042d6a0
        return "MngCollection";
    }

    inline void setGameMode( GMLiving *pGameMode ) { pOwner = pGameMode; }

public:
    MngCollection();
    ~MngCollection();

    void terminate() override;
    bool onMessage( FlashMessage *pMessage );

private:
    GMLiving *pOwner = nullptr;
};
