#pragma once

#include "game/mng_base.h"
#include "game/map/map_screenshot_bank.h"

class DrawList;
class FlashPlayer;
class FlashResource;
struct FlashRECT;
struct SpotMap;

// Streams a spot screenshot into a Flash clip
class MngSpotPicture : public Manager {
public:
    static constexpr size_t kClipPathLength = 0x32;

public:
    inline void setTarget( FlashPlayer *pPlayer, const char *pPath ) {
        pMovie = pPlayer;
        strncpy( pClipPath, pPath, kClipPathLength );
        pClipPath[kClipPathLength - 1] = '\0';
    }

public:
    MngSpotPicture();
    ~MngSpotPicture();

    bool initialize( TestDriveGameInstance * ) override;
    void terminate() override;

    void showSpot( const SpotMap *pSpot );

    static void PictureDrawCallback( FlashRECT *pViewport,
                                     float depth,
                                     DrawList *pDrawList,
                                     void *pUserData,
                                     FlashResource *pResource,
                                     const char *pClipName );

private:
    FlashPlayer *pMovie;
    char pClipPath[kClipPathLength];
    MapScreenshotBank bank;

private:
    static bool OnPictureLoaded( MapScreenshotBank *pBank, void *pUserData );
};
