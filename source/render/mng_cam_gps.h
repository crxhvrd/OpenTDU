#pragma once

#include "game/mng_base.h"

#include "render/2dm.h"
#include "render/2db.h"
#include "render/setup_node.h"
#include "world/mng_ocean.h"

class DrawList;
struct HDRIConfig;
class WeatherConfig;
struct Material;

class MngCamGPS : public Manager {
public:
    const char *getName() const override {
        // FUN_00432640
        return "MngCamGPS";
    }

    inline void setDrawEarth( const bool bEnabled ) { bDrawEarth = bEnabled; }
    inline void setUpdateStreaming( const bool bEnabled ) {
        bUpdateStreaming = bEnabled;
    }
    inline void setForceStreaming( const bool bEnabled ) {
        bForceStreaming = bEnabled;
    }
    inline void setForceStreamingFlush( const bool bEnabled ) {
        bForceStreamingFlush = bEnabled;
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
    float *pStaticParamsSpaceBackground = nullptr;

    Material *pMatFadeMap = nullptr;
    Render2DM dmFadeMap;
    void *p2DMFadeMapData = nullptr;

    float noiseWindStart = 0.0f;
    float noiseWindSize = 0.05f;
    float noiseWindSpeed = 0.01f;

    bool bDrawEarth = true;
    bool bUpdateStreaming = false;
    bool bForceStreaming = false;
    bool bForceDraw = false;
    bool bForceStreamingFlush = false;
    bool bFirstFrameInGPS = true;
    bool bClipPlanesLatched = false;
    float earthFadeNearDist = 0.0f;
    float earthFadeFarDist = 0.0f;

private:
    static void OnWeatherConfigUpdate( WeatherConfig *pConfig );
    static void OnHDRConfigUpdate( HDRIConfig *pConfig );

    bool allocateResources();
    void releaseResources();
    void toggleGPSClipPlanes( bool bGPSRendering, bool bClipPlanes );
    void updateCameraForCapture();
    void updateSpaceViewRotation( float deltaTime );
    void updateRadialBlur();
    void updateHeightmapLOD();
    void updateBackground();
    void updateOcean();
};

extern MngCamGPS *gpMngCamGPS;
