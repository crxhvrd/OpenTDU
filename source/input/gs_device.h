#pragma once

#include "game/gs_base.h"

class Device;
struct InputChannel;

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
    char replayFilename[32];
    uint32_t replaySize;
    bool bReplayLoading;
    bool bReplaySaving;
    MouseState mouseState;
    bool bUseNaturalPointHeadTracking;
    bool field14_0x54;
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
extern bool gPedalsSeparated;