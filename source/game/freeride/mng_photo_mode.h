#pragma once

#include "game/mng_base.h"
#include "mng_screenshots.h"
#include "photo_mode_effect.h"

struct Material;
struct FlashMessage;
class FlashPlayer;

class MngPhotoMode : public Manager {
public:
    const char *getName() const override {
        // FUN_00431f10
        return "MngPhotoMode";
    }

public:
    MngPhotoMode();
    ~MngPhotoMode();

    bool initialize( TestDriveGameInstance * ) override;
    void tick( float, float ) override;
    void terminate() override;

    bool onMessage( FlashMessage *pMessage );

    inline void setFlashPlayer( FlashPlayer *pPlayer ) {
        pFlashPlayer = pPlayer;
        PhotoFilters.setFlashPlayer( pPlayer );
    }

private:
    PhotoModeEffect PhotoFilters;
    bool bPhotoModeActive;
    float nullMatrix[4][4];
    MngScreenshots MngScrenshots;
    Material *pShotMaterial;
    FlashPlayer *pFlashPlayer;
    bool bWaitingSaveResult;
    bool bFXWasOn;
};

extern MngPhotoMode *gpMngPhotoMode;
