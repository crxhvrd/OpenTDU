#pragma once

#include "game/mng_base.h"
#include "mng_screenshots.h"
#include "photo_mode_effect.h"

struct Material;
struct FlashMessage;
class FlashPlayer;
class CameraGame;
class CarState;

class MngPhotoMode : public Manager {
public:
    const char *getName() const override {
        // FUN_00431f10
        return "MngPhotoMode";
    }

    inline void setFlashPlayer( FlashPlayer *pPlayer ) {
        pFlashPlayer = pPlayer;
        PhotoFilters.setFlashPlayer( pPlayer );
    }

    inline MngScreenshots &getScreenshots() { return MngScrenshots; }
    inline bool isFilterPopupOpen() const { return PhotoFilters.isPopupOpen(); }

public:
    MngPhotoMode();
    ~MngPhotoMode();

    bool initialize( TestDriveGameInstance * ) override;
    void tick( float, float ) override;
    void terminate() override;

    bool onMessage( FlashMessage *pMessage );

private:
    static constexpr size_t kMaxCaptionLength = 0x20;

private:
    void enterPhotoMode();
    void leavePhotoMode();
    void takeShot();
    void saveShot();
    void showLiveView();
    void cancelShot();

    void setShowcaseCameraInput( bool bEnabled );

    static void ShotDrawCallback( struct FlashRECT *pViewport,
                                  float depth,
                                  class DrawList *pDrawList,
                                  void *pUser,
                                  class FlashResource *pResource,
                                  const char *pClipName );
    static void OnSavePopupClosed( void *pUser );

private:
    PhotoModeEffect PhotoFilters;
    bool bPhotoModeActive;
    Matrix4x4f_t carTransform;
    MngScreenshots MngScrenshots;
    Material *pShotMaterial;
    FlashPlayer *pFlashPlayer;
    bool bWaitingSaveResult;
    bool bFXWasOn;

    bool bGlowPauseWasEnabled = false;
    char caption[kMaxCaptionLength] = {};
};

extern MngPhotoMode *gpMngPhotoMode;
