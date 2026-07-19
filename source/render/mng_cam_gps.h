#pragma once

#include "game/mng_base.h"

#include "render/2dm.h"
#include "render/2db.h"
#include "render/setup_node.h"
#include "world/mng_ocean.h"

class DrawList;
struct Material;

class MngCamGPS : public Manager {
public:
    const char *getName() const override {
        // FUN_00432640
        return "MngCamGPS";
    }

public:
    MngCamGPS();
    ~MngCamGPS();

    bool initialize( TestDriveGameInstance * ) override;
    void tick( float deltaTime, float totalTime ) override;
    void reset() override;
    void draw() override;
    void pause() override;
    void terminate() override;

private:
    OceanSetup oceanSetup;
    ClipPlaneSetupNode clipPlaneSetup;

    Eigen::Vector4f earthNodePosBackup = { 0.0f, 0.0f, 0.0f, 1.0f };

    DrawList *pSpaceBkgDrawList = nullptr;
    DrawList *pFadeMapDrawList = nullptr;

    RuntimeRender2DB spaceNoise2DB;

    Material *pMatSpaceBackground = nullptr;
    Render2DM dmSpaceBackground;
    void *p2DMSpaceBackgroundData = nullptr;
    Eigen::Vector4f *pStaticParamsSpaceBackground = nullptr;

    Material *pMaterialOceanReflection = nullptr;
    Render2DM dmOceanReflection;
    void *p2DMBufferOceanReflection = nullptr;

    float noiseWindStart = 0.0f;
    float noiseWindSize = 0.05f;
    float noiseWindSpeed = 0.01f;

    bool bDrawEarth = true;
    bool bUpdateStreaming = false;
    bool bForceStreaming = false;
    bool bForceDraw = false;
    bool bForceStreamingFlush = false;
    bool bFirstFrameInGPS = true;
    uint8_t oceanTransitionLatch = 0;
    float earthFadeNearDist = 0.0f;
    float earthFadeFarDist = 0.0f;

private:
    bool allocateResources();
    void releaseResources();
    void toggleGPSClipPlanes( bool bEnable, bool bResetOTMask );
    void updateCameraForCapture( float progress );
};

extern MngCamGPS *gpMngCamGPS;
