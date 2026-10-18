#pragma once

#include "gm_demo_base.h"

class GMOutroDemo : public GMDemoBase {
public:
    GMOutroDemo();
    ~GMOutroDemo();

    void reset() override;
    void tick( float deltaTime, float totalTime ) override;
    void pause() override;
    bool onMessage( FlashMessage *pMessage ) override;

protected:
    void changeGameMode() override;

private:
    float pictureTimer;
};
