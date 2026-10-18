#pragma once

#include "game/gm_scene.h"
#include "game/freeride/mng_screenshots.h"
#include "flash/implementation/flash_resource.h"

class FlashPlayer;
struct Material;

// Shared base of the demo-build bumper modes
class GMDemoBase : public GameModeScene {
public:
    GMDemoBase();
    ~GMDemoBase();

    void initialize() override;
    void tick( float deltaTime, float totalTime ) override;
    void destroy() override;
    bool onMessage( FlashMessage *pMessage ) override;

protected:
    virtual void changeGameMode() = 0;

    void setPicturePrefix( const char *pPrefix );

private:
    void loadNextPicture();

    static void LureDrawCallback( FlashRECT *pViewport,
                                  float depth,
                                  DrawList *pDrawList,
                                  void *pUserData,
                                  FlashResource *pResource,
                                  const char *pClipName );

protected:
    FlashPlayer *pDemoPlayer;

private:
    MngScreenshots photo;
    Material *pPicture;
    char picturePrefix[32];
};
