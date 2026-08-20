#pragma once

#include "game/scenaric/scenaric_bank.h"

class ExtraChallengeCineBank {
public:
    enum eState : int32_t {
        ECBS_Unloaded = 0,
        ECBS_Loading = 1,
        ECBS_Loaded = 2,
    };

    enum eMode : int32_t {
        ECBM_None = 0,
        ECBM_Intro = 1,
        ECBM_Outro = 2,
    };

public:
    inline eState getState() const { return state; }
    inline eMode getMode() const { return mode; }
    inline ScenaricBank &getBank() { return bank; }

public:
    bool load( eMode requestedMode );
    void unload();

private:
    eState state = ECBS_Unloaded;
    eMode mode = ECBM_None;
    ScenaricBank bank;
};
