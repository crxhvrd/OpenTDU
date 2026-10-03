#pragma once

#include "game/mng_base.h"

#include "render/2dm.h"
#include "render/draw_list.h"
#include "render/light.h"

class PostFXNodeTexSample;
class PostFXNodeCubemapToSphere;
class PostFXNodeEnvMapScene;
class Sector;
class VehicleBase;

struct EnvmapUnknown {
    virtual bool onEnvMapChanged( int32_t param_1 ) = 0;

    EnvmapUnknown *pNext = nullptr;
};
extern EnvmapUnknown *PTR_00fa9c0c;

void RegisterEnvMapListener( EnvmapUnknown *pListener );
void UnregisterEnvMapListener( EnvmapUnknown *pListener );

class MngEnvMap : public Manager {
public:
    enum eView : int32_t {
        EMV_Cockpit = 0,
        EMV_Hood = 1,
        EMV_Exterior = 2,
    };

    // Debug-only snapshot of what updateView/updateCameras fed to the env
    // capture camera. Nothing in the render path reads it back.
    struct DebugCaptureState {
        eView View = EMV_Exterior;
        Eigen::Vector3f Position = Eigen::Vector3f::Zero();
        Eigen::Vector3f LookTarget = Eigen::Vector3f::Zero();
        Eigen::Vector3f SourceEye = Eigen::Vector3f::Zero();
        Eigen::Vector3f SourceViewDir = Eigen::Vector3f::Zero();
        Eigen::Vector3f CarForward = Eigen::Vector3f::Zero();
        float FOV = 0.0f;
        float FarPlane = 0.0f;
    };

public:
    inline const DebugCaptureState &getDebugCaptureState() const {
        return debugCaptureState;
    }
    inline bool isCapturing() const { return bCompute && bInitialized; }

public:
    const char *getName() const override {
        // FUN_00430220
        return "EnvMap";
    }

public:
    MngEnvMap();
    ~MngEnvMap();

    bool initialize( TestDriveGameInstance * ) override;
    void tick( float, float ) override;
    void terminate() override;
    void draw() override;
    void reset() override;
    void pause() override;

    bool onCubemapResourcesChanged();

#ifdef OTDU_USE_IMGUI
    static void DrawEnvMapDebugPanel( float viewSize );
    static void DrawEnvMapDebugOverlay();
#endif

private:
    static constexpr uint32_t kNumGatheredSectors = 3;
    static constexpr uint32_t kNumGatheredVehicles = 3;

private:
    AmbientLight ambientLight;
    DirectionalLight directionalLight;

    void *pBuffer2DMCubemap;
    Render2DM render2DMCubemap;
    void *pBuffer2DMCubemapB;
    Render2DM render2DMCubemapB;

    DrawList drawList;

    uint8_t bCompute : 1;
    uint8_t bInitialized : 1;

    PostFXNodeTexSample *pPostFXTexSample;
    PostFXNodeCubemapToSphere *pPostFXCubemapToSphere;
    PostFXNodeEnvMapScene *pPostFXEnvMapScene;

    std::array<Sector *, kNumGatheredSectors> pSectors;

    float farPlaneCoeff;
    eView view;

    float camHeight;
    float heightBias;
    float hoodHeightBias;
    float camFOV;
    float lumFactor;
    float saturation;
    float globalAmbiant;
    float coefAmbiant;
    float coefDiffuse;
    float coefSpecular;
    float hmRed;
    float hmGreen;
    float hmBlue;
    float hmContrast;
    float cockpitSaturation;
    float cockpitGlobalAmbiant;
    float cockpitCoefAmbiant;
    float cockpitCoefDiffuse;
    float cockpitCoefSpecular;

    Eigen::Vector4f userVector;
    float lastDeltaTime;

    DebugCaptureState debugCaptureState;

private:
    bool createMaterials();
    bool setupScenes();
    bool createPostFXStack();
    void destroyPostFXStack();
    void bindRenderTargets();
    void popLights();
    void updateView();
    void updateCameras( bool bUseActiveCamera, bool bUpdateBackScene );
    void drawMiniQuad();
};

extern MngEnvMap *gpMngEnvMap;

extern bool gbDebugEnvMapDenseReflection;
extern float gDebugEnvMapFarPlaneScale;
extern float gDebugEnvMapFarPlaneCoeff;
extern float gDebugEnvMapObjectCullScale;
extern bool gbDebugEnvMapOverlay;
