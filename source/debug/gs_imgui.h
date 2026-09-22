#pragma once

#ifdef OTDU_USE_IMGUI
#include "game/gs_base.h"

#include "render/2dm.h"
#include "render/2db.h"
#include "render/setup_node.h"

struct Material;
class DrawList;
struct ImDrawData;
struct ImTextureData;
template <typename T> struct ImVector;

class GSImgui : public GameSystem {
public:
    const char *getName() const override { return "Service : imgui"; }
    inline bool hasFocus() const { return bVisible; }
    inline void requestOverlayFrame() { bOverlayRequested = true; }
    inline void setFocus( const bool bState ) { bVisible = bState; }

public:
    GSImgui();
    ~GSImgui();

    bool initialize( TestDriveGameInstance * ) override;
    void tick( float totalTime, float deltaTime ) override;
    void draw( float deltaTime ) override;
    void terminate() override;
    void drawDebug( float deltaTime ) override;
    Material *getDebugViewMaterial( Render2DB *p2DB );

private:
    static constexpr int32_t kMaxNumTextures = 0x20; // Arbitrary
    static constexpr int32_t kMaxNumDrawCmds = 128;  // Arbitrary
    static constexpr int32_t kMaxNumDebugViews = 16;

    // We must allocate one material per texture created by imgui (due to the
    // way TDU1 render pipeline works...).
    struct TextureEntry {
        void *pMaterialMemory = nullptr;
        Material *pMaterial = nullptr;
        Render2DM render2DM;
        RuntimeRender2DB render2DB;
    };

    struct DrawCommandSetup {
        SetupGraph Graph;
        ScissorSetupNode ScissorNode;
    };

private:
    DrawList *pDrawList;
    std::queue<TextureEntry *> availableTextures;

    uint32_t setupUsageIndex;
    std::array<TextureEntry, kMaxNumTextures> textures;
    std::array<DrawCommandSetup, kMaxNumDrawCmds> setups;

    // Dedicated materials for getDebugViewMaterial()
    std::array<TextureEntry, kMaxNumDebugViews> debugViews;
    uint32_t debugViewIndex = 0;

    bool bVisible;
    bool bOverlayRequested;

private:
    void processDrawData( ImDrawData *pDrawData );
    void processTextureUpdates( ImVector<ImTextureData *> *pTexUpdates );
    void uploadGeometryBuffers( ImDrawData *pDrawData );
    void enqueueDrawCommands( ImDrawData *pDrawData );

    void allocateTextureEntries();
    void releaseTextureEntries();
    void initializeSetups();
    void setupImguiMaterial( TextureEntry *entry, bool bOpaqueBlend );
};

extern GSImgui *gpImgui;
#endif