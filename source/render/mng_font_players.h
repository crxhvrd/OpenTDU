#pragma once

#include "game/mng_base.h"

class DrawList;
struct Font;

class MngFontPlayers : public Manager {
public:
    const char *getName() const override {
        // FUN_00431f30
        return "MngFontPlayers";
    }

public:
    MngFontPlayers();
    ~MngFontPlayers();

    bool initialize( TestDriveGameInstance * ) override;
    void draw() override;
    void terminate() override;
    void drawText( float x,
                   float y,
                   float scale,
                   uint8_t r,
                   uint8_t g,
                   uint8_t b,
                   uint8_t a,
                   const char *pFormat,
                   ... );
    void drawText3D( float x,
                     float y,
                     float z,
                     float scale,
                     uint8_t r,
                     uint8_t g,
                     uint8_t b,
                     uint8_t a,
                     const char *pFormat,
                     ... );

    float getTextWidth( float scale, const char *pText ) const;
    float getTextWidthFormatted( float scale, const char *pFormat, ... );

private:
    bool bInitialized;
    char pString[256];

    DrawList *pDrawList;

    int32_t numPrimitives;
    uint32_t numVertices;
    uint32_t numIndices;

private:
    float pushGlyph( char character, Eigen::Vector4f &pen, float scale );
    void drawPlayerNames();
    void drawBotNames();
};

extern MngFontPlayers *gpMngFontPlayers;
