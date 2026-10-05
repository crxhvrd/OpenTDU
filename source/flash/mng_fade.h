#pragma once

#include "core/color.h"
#include "game/mng_base.h"
#include "render/2dm.h"
#include "render/file_collection_2d.h"

class DrawList;
struct Material;

class MngFade : public Manager {
public:
    using PostTransitionCallback_t = std::function<void( void *, void * )>;

public:
    const char *getName() const override { return "MngFade"; }
    inline uint32_t getTransitionFlags() const { return flags; }
    inline void setTransitionFlags( const uint32_t newFlags ) {
        flags = newFlags;
    }
    inline void setTransitionColorRGB( const uint8_t r,
                                       const uint8_t g,
                                       const uint8_t b ) {
        transitionColor.R = r;
        transitionColor.G = g;
        transitionColor.B = b;
    }
    inline float getTransitionDuration() const { return transitionDuration; }
    inline void setRenderLogo( const bool bEnabled ) { bRenderLogo = bEnabled; }
    inline void restartTransition( const uint32_t newFlags,
                                   const float durationInSeconds ) {
        transitionDuration = durationInSeconds;
        flags = newFlags;
        elapsedTime = 0.0f;
    }

public:
    MngFade();
    ~MngFade();

    void startTransition( uint32_t newFlags,
                          float durationInSeconds,
                          PostTransitionCallback_t callback = nullptr,
                          void *pSource = nullptr,
                          void *pData = nullptr );
    bool initialize( TestDriveGameInstance * ) override;
    void tick( float, float ) override;
    void terminate() override;
    void draw() override;
    void pause() override;

private:
    FileCollection2D fileCollection;

    DrawList *pDrawListTransition;
    ColorRGBA transitionColor;
    uint32_t flags;
    float transitionDuration;

    PostTransitionCallback_t pCallback;
    void *pCallbackSource;
    void *pCallbackData;

    Eigen::Vector2f position;
    float width;
    float height;
    float elapsedTime;
    float logoElapsedTime;

    DrawList *pDrawListLogo;
    Render2DM transition2DM;
    void *p2DMBuffer;

    Material *pMaterial;

    uint8_t bRenderLogo : 1;
    uint8_t bWaitForPopup : 1;

private:
    void encodeTransitionDrawList( const uint8_t alpha, const float fVar7 );
    void encodeLogoDrawList();
};

extern MngFade *gpMngFade;
