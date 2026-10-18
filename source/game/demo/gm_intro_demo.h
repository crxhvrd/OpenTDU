#pragma once

#include "gm_demo_base.h"

class GMIntroDemo : public GMDemoBase {
public:
    GMIntroDemo();
    ~GMIntroDemo();

    void reset() override;

protected:
    void changeGameMode() override;
};
