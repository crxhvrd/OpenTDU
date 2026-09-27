#pragma once

#include "filesystem/bank.h"

class DrawList;
class FlashResource;
class Render2DB;
class Render2DM;
struct FlashRECT;
struct Material;

class RadioTitle {
public:
    enum eScrollMode : int32_t {
        RTSM_PingPong = 0,
        RTSM_Marquee = 1,
    };

    inline float getVolume() const { return Volume; }
    inline void setVisibleChars( const uint32_t numChars ) {
        VisibleChars = numChars;
    }

public:
    RadioTitle();
    ~RadioTitle();

    void initialize();
    void unload();

    void setFlashVariable( const char *pVariableName );
    void resetScroll();
    void setText( const char *pText );
    void setLogo( const char *pLogoName );
    void setVolume( const float volume );
    void update( const float deltaTime );

private:
    char LogoName[256];
    bool bInitialized;
    void *p2DMData;
    Render2DM *pRender2DM;
    Render2DB *pRender2DBs;
    Material *pLogoMaterial;
    int32_t Num2DB;
    int32_t Max2DB;
    float ScrollTimer;
    uint32_t VisibleChars;
    char FlashVariable[32];
    char Text[256];
    char ScrollText[256];
    char UTF8Text[256];
    eScrollMode ScrollMode;
    uint32_t ScrollOffset;
    bool bScrollForward;
    float Volume;
    bool bDirty;
    Bank LogoBank;

private:
    static void DrawLogoCallback( FlashRECT *pViewport,
                                  float depth,
                                  DrawList *pDrawList,
                                  void *pUser,
                                  FlashResource *pResource,
                                  const char *pClipName );
};
