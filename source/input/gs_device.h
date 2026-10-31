#pragma once

#include "game/gs_base.h"
#include "head_tracking.h"

#include <atomic>

class Device;
class DeviceJoystick;
struct InputChannel;

enum class eForceFeedbackMode : int32_t {
    FFBM_None = 0,
    FFBM_Wheel = 1,
    FFBM_Pad = 2
};

// Written by the physics step (FUN_0081cd10), sent to the device by
// GSDevice::tick
struct ForceFeedbackOutput {
    float ConstantForce = 0.0f;                              // DAT_010e72a4
    float Damper = 0.0f;                                     // DAT_010e72a8
    float SecondaryForce = 0.0f;                             // DAT_010e72b0
    eForceFeedbackMode Mode = eForceFeedbackMode::FFBM_None; // DAT_010e72b4

    // NOTE: the physics thread steps faster than the device tick; keep the
    // pad peaks so single-step impact pulses are not lost between frames.
    std::atomic<float> PeakConstantForce{ 0.0f };
    std::atomic<float> PeakSecondaryForce{ 0.0f };

    void latchPadPeaks() {
        LatchPeak( PeakConstantForce, ConstantForce );
        LatchPeak( PeakSecondaryForce, SecondaryForce );
    }

private:
    static void LatchPeak( std::atomic<float> &peak, const float value ) {
        float current = peak.load();
        while ( value > current &&
                !peak.compare_exchange_weak( current, value ) ) {
        }
    }
};

struct MouseState {
    Device *pInstance = nullptr;
    InputChannel *pChannelLeft = nullptr;
    InputChannel *pChannelUp = nullptr;
    InputChannel *pChannelWheelUp = nullptr;
    InputChannel *pChannelWheelDown = nullptr;
    InputChannel *pChannelButton0 = nullptr;
    InputChannel *pChannelButton2 = nullptr;
    InputChannel *pChannelButton1 = nullptr;

    void initialize();
    bool FUN_00980000() const;
};

class GSDevice : public GameSystem {
public:
    const char *getName() const override {
        return "Service : Device"; /* FUN_00406430 */
    }

    // If false, GSDevice will skip keyboard key presses and assume text input
    // (e.g. for Flash)
    inline bool isKeyboardInputEnabled() { return field14_0x54; }
    inline void setKeyboardInputEnabled( bool state ) { field14_0x54 = state; }

    inline const MouseState &getMouseState() const { return mouseState; }
    inline bool isHeadTrackingAvailable() const {
        return bUseNaturalPointHeadTracking;
    }
    inline float getHeadTrackingYaw() const { return headTrackingYaw; }
    inline float getHeadTrackingPitch() const { return headTrackingPitch; }

    void setHeadTrackingEndpoint( const char *pAddress, const uint16_t port );

    inline void requestReplay( const char *pFilename,
                               const uint32_t size,
                               const bool bSave ) {
        strncpy( replayFilename, pFilename, sizeof( replayFilename ) - 1 );
        replayFilename[sizeof( replayFilename ) - 1] = '\0';
        replaySize = size;
        if ( bSave ) {
            bReplaySaving = true;
        } else {
            bReplayLoading = true;
        }
    }

public:
    GSDevice();
    ~GSDevice();

    bool initialize( TestDriveGameInstance * ) override;
    void tick( float totalTime, float deltaTime ) override;
    void terminate() override;

    void retrieveFlashNavKeyStates( bool *param_1,
                                    bool *param_2,
                                    const bool param_3,
                                    const bool param_4,
                                    const int32_t param_5 );

private:
    void processJoystickHotplug();
    void createForceFeedbackEffects( DeviceJoystick *pDevice );
    void updateForceFeedbackEffects();
    void releaseForceFeedbackEffects();
    void updateHeadTracking( const float deltaTime );

private:
    char replayFilename[32];
    uint32_t replaySize;
    bool bReplayLoading;
    bool bReplaySaving;
    MouseState mouseState;
    float headTrackingYaw;
    float headTrackingPitch;
    bool bUseNaturalPointHeadTracking;
    bool field14_0x54;

    HeadTrackingReceiver headTrackingReceiver;
    float headTrackingIdleTime;
};

extern GSDevice gDevice;
extern GSDevice *gpDevice;

extern uint32_t gDefaultSensitivity;
extern float gFFBFactor;
extern float gSteeringDeadZone;
extern float gSteeringLinearity;
extern float gSteeringTurn;
extern float gBrakeDeadZone;
extern float gAccelDeadZone;
extern float gFFBLoadFactor[3];
extern float gFFBLoadFromAccel[3];
extern float gFFBSlipAngle[3][2];
extern float gFFBSpeedFactor[4][2];
extern bool gPedalsSeparated;
extern ForceFeedbackOutput gForceFeedbackOutput;