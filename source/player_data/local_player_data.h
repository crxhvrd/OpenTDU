#pragma once

#include "config/gs_config.h"
#include "render/gs_render_helper.h"
#include "saved_input_bindings.h"

class edStore;

enum eUnitSystem : int32_t {
    US_Metric = 0,
    US_Imperial = 1,
};

struct LocalPlayerData {
    char Language[16] = {};
    eUnitSystem UnitSystem = US_Metric;
    bool bAuto = true;
    float PadSensitivity = 0.0f;
    float BarSensitivity = 0.5f;
    float MouseSensitivity = 0.5f;
    float QuiverSensitivity = 0.5f;
    uint32_t PadConfig = 1u;
    int32_t DrivingHelpLevel = 1;
    uint32_t field10_0x30 = 0u;
    uint32_t StakeValue = 1000u;
    bool bStakeEnabled = false;
    float SeatPositionHeight = 0.0f;
    float SeatPositionPosition = 0.0f;
    bool bUseWheel = false;
    bool field17_0x45 = false;
    bool field18_0x46 = false;
    bool field19_0x47 = false;
    float Linearity = 0.5f;
    bool bSubtitles = false;
    bool bInvertMouse = false;
    bool bAutoClutch = true;
    bool bHShifterEnabled = false;
    bool bTrackIREnabled = false;
    bool bFFBInvert = false;
    bool bDispOP1 = true;
    bool bDisplayPlayerNames = true;
    bool bDisplayGPS = true;
    bool bGPSRotation = true;
    bool bHelpSystem = false;
    bool field32_0x57 = false;
    float Gamma = 0.5f;
    float RadialBlurIntensity = 0.5f;
    float CameraShake = 0.0f;
    int32_t field36_0x64 = 0;
    bool bOnlineAccount = false;
    bool bOffline = false;
    char Nickname[32] = {};
    char Email[52] = {};
    char Password[32] = {};
    SavedInputBindings InputBindings = {};
    bool bOnlineOps[9] = {};
    bool field89274_0x15d05 = false;
    bool field89275_0x15d06 = false;
    bool field89276_0x15d07 = false;
    bool field89277_0x15d08 = false;
    uint32_t RenderWidth = 0u;
    uint32_t RenderHeight = 0u;
    uint32_t RefreshRate = 0u;
    eAntiAliasingMethod AntiAliasing = eAntiAliasingMethod::AAM_Disabled;
    bool bHDREnabled = false;
    uint32_t LODQuality = 0u;
    uint32_t GPUDeviceId = 0u;
    uint32_t GPUSubSysId = 0u;
    uint32_t GPUDriverRevision = 0u;
    uint32_t GPUVendorId = 0u;
    float VoicesVolume = 1.0f;
    float VoicesThreshold = 0.16f;
    float VoicesInput = 0.5f;
    bool bEnablePlayerVoices = true;
    bool bUseLoopBack = false;

    void clear();
    bool loadFromStream( edStore &stream );
    void serialize( edStore &stream );
};
