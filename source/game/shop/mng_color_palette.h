#pragma once

#include "core/color.h"
#include "flash/implementation/ed_flash.h"
#include "game/mng_base.h"
#include "render/2db.h"
#include "render/2dm.h"
#include "render/file_collection_2d.h"

class DrawList;
class FlashPlayer;
class FlashResource;
struct Material;

// Colour picker of the deluxe paint shop.
class MngColorPalette : public Manager {
public:
    static constexpr uint32_t kPaletteSize = 256;
    static constexpr int32_t kNumKeyColors = 7;
    static constexpr int32_t kPathLength = 0x40;
    static constexpr int32_t kClipNameLength = 0x20;

public:
    const char *getName() const override { return "MngColorPalette"; }

    inline void setFlashPlayer( FlashPlayer *pPlayer ) {
        pFlashPlayer = pPlayer;
    }
    inline void setCursorPath( const char *pPath ) {
        strncpy( cursorPath, pPath, kPathLength );
    }
    inline const FlashRECT &getClipViewport() const { return clipViewport; }

public:
    MngColorPalette();
    ~MngColorPalette();

    bool initialize( TestDriveGameInstance * ) override;
    void tick( float deltaTime, float totalTime ) override;
    void terminate() override;

    void bindClip( const char *pPath, const char *pClipName );
    bool setCursorFromColor( const ColorRGBA &color );
    ColorRGBA getColorAtCursor();

private:
    static void PaletteCallback( FlashRECT *pRect,
                                 float deltaTime,
                                 DrawList *pDrawList,
                                 void *pUserData,
                                 FlashResource *pResource,
                                 const char *pClipName );

    void create( const uint32_t width, const uint32_t height );
    void fillPalette();
    ColorRGBA getPaletteColor( const float hue, const float brightness ) const;
    void readClipBounds();

private:
    Material *pMaterial;
    void *p2DMContent;
    Render2DM render2DM;
    FileCollection2D collection2D;
    RuntimeRender2DB paletteBitmap;
    ColorRGBA keyColors[kNumKeyColors];
    char clipPath[kPathLength];
    char clipName[kClipNameLength];
    char cursorPath[kPathLength];
    FlashPlayer *pFlashPlayer;
    float clipWidth;
    float clipHeight;
    float cursorX;
    float cursorY;
    FlashRECT clipViewport;
};

extern MngColorPalette *gpMngColorPalette;
