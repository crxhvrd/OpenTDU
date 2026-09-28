#pragma once

#include "filesystem/bank.h"
#include "game/mng_base.h"

class FlashPlayer;
class FlashResource;
class DrawList;
struct DBEntryBrand;
struct FlashRECT;

// One brand-logo slot. The display caches the SWF path of a logo, the flash
// player it has been bound to and the brand it belongs to.
struct LogoEntry {
    char pPath[512];
    FlashPlayer *pFlash;
    DBEntryBrand *pBrand;
};

class MngLogoDisplay : public Manager, public Bank {
public:
    const char *getName() const override {
        // FUN_0042e8a0
        return "MngLogoDisplay";
    }

public:
    MngLogoDisplay();
    ~MngLogoDisplay();

    void terminate() override;

    void setLogo( FlashPlayer *pFlash, const char *pBrandName, char *pPath );

private:
    static void DrawLogoCallback( FlashRECT *pViewport,
                                  float depth,
                                  DrawList *pDrawList,
                                  void *pUser,
                                  FlashResource *pResource,
                                  const char *pClipName );

private:
    LogoEntry pLogoPaths[4];
};

extern MngLogoDisplay *gpMngLogoDisplay;
