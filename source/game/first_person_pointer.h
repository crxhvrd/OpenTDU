#pragma once

#include "render/2db.h"
#include "render/2dm.h"
#include "render/draw_list.h"

struct Material;

// First person mode cursor. Independant from FlashMouse cursor
// (since this is an addition and does not live in the GENERAL flash movie)
class FirstPersonPointer {
public:
    inline bool isLoaded() const { return bLoaded; }
    inline bool isHighlighted() const { return bHighlighted; }
    inline void setHighlighted( bool bValue ) { bHighlighted = bValue; }

public:
    FirstPersonPointer();
    ~FirstPersonPointer();

    bool initialize();
    void destroy();
    void draw( float deltaTime );

private:
    bool loadBitmap();

private:
    void *pBitmapResource;
    Render2DB bitmap;
    Texture *pTexture;

    void *pMaterialMemory;
    Render2DM material2DM;
    Material *pMaterial;

    DrawList drawList;

    float highlightBlend;
    float pulsePhase;

    bool bLoaded;
    bool bHighlighted;
};
