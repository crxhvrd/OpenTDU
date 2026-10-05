#pragma once

#include "filesystem/bank.h"
#include "game/mng_base.h"

class FlashPlayer;
class FlashResource;
class DrawList;
struct DBEntryBrand;
struct FlashRECT;
struct MinimapSpotIcon;
struct Material;

// One brand-logo slot. The display caches the SWF path of a logo, the flash
// player it has been bound to and the brand it belongs to.
struct LogoEntry {
    char pPath[512];
    FlashPlayer *pFlash;
    DBEntryBrand *pBrand;
};

class MngLogoDisplay : public Manager, public Bank {
public:
    static constexpr int32_t kMaxNumCarShopBrands = 16;

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
    void clearLogo( FlashPlayer *pFlash, char *pPath );

    static void GetCarShopBrands( DBEntryBrand **ppOutBrands,
                                  uint64_t spotHashcode );
    static DBEntryBrand *PickCarShopBrand( DBEntryBrand **ppBrands,
                                           uint64_t spotHashcode,
                                           float cycleTimer );

    static Material *GetSpotIconUVs( const MinimapSpotIcon *pIcon,
                                     int32_t iconSize,
                                     float &u0,
                                     float &u1,
                                     float &v0,
                                     float &v1 );

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
