#pragma once

#include "flash/entities/flash_list.h"
#include "game/scenaric/mng_spots.h"

class FlashPlayer;
class DrawList;
class FlashResource;
struct Material;
struct FlashRECT;

class FlashListSpots : public FlashList {
public:
    inline void setFlashPlayer( FlashPlayer *pFlashPlayer ) {
        pPlayer = pFlashPlayer;
    }
    inline struct SpotMap *getSelectedSpot() const { return pSelectedSpot; }

    static void PictureDrawCallback( FlashRECT *pViewport,
                                     float depth,
                                     DrawList *pDrawList,
                                     void *pUserData,
                                     FlashResource *pResource,
                                     const char *pClipName );

public:
    FlashListSpots();
    ~FlashListSpots();

    int32_t getListLength() const override;
    void open() override;
    void close() override;
    void update( const int32_t param_2, const int32_t param_3 ) override;
    void selectedLineChanged() override;

private:
    void loadSpotScreens();
    void releaseSpotScreens();

private:
    static constexpr int32_t kGarageListLength = 512;

private:
    Material *pPicture;
    FlashPlayer *pPlayer;
    char garageList[kGarageListLength];
    SpotScreensBank spotScreens;
    bool bSpotScreensLoaded;
    int32_t spotType;
    int32_t spotSubType;
    struct SpotMap *pSelectedSpot;
};
