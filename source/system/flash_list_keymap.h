#pragma once

#include "flash/entities/flash_list.h"
#include "input/input_templates.h"
#include "player_data/saved_input_bindings.h"

class Device;
struct InputChannel;
struct PlayerConfiguration;

// '/BindKey' popup
struct KeyBindCapture {
public:
    enum eState : int32_t {
        S_Idle = 0,
        S_Calibrating = 1, // wait until nothing moves for kCalibrationDelay
        S_WaitRelease = 2, // wait until everything is back to the snapshot
        S_Capturing = 3,   // wait for a key/button/axis
        S_WaitAxisRelease = 4
    };

    static constexpr int32_t kMaxSnapshots = 0x400;
    static constexpr int32_t kMaxChangedChannels = 8;

    struct ChannelSnapshot {
        char DeviceName[0x20];
        char ChannelName[0x20];
        float AxisValue;
        uint32_t ButtonState;
    };

    int32_t State = S_Idle;
    FlashPlayer *pPlayer = nullptr;
    InputChannel *pCapturedChannel = nullptr;
    bool bCapturedInverted = false;
    bool bCapturedCombination = false;
    ChannelSnapshot Snapshots[kMaxSnapshots] = {};
    int32_t NumSnapshots = 0;
    Device *pChangedDevices[kMaxChangedChannels] = {};
    InputChannel *pChangedChannels[kMaxChangedChannels] = {};
    uint8_t ChangedDirections[kMaxChangedChannels] = {};
    int32_t NumChangedChannels = 0;
    float Timer = 0.0f;

    void openCalibration();
    void start( const char *pTitle );
    void close();
    int32_t tick( const float deltaTime );

private:
    void takeSnapshot();
    int32_t detectChanges();
    void setMessage( const uint64_t messageHashcode );
};

// '/keymap/list' in CONFIGPC
class FlashListKeymap : public FlashList {
public:
    static constexpr FlashMessage kTab = FlashMessage( "ongl" );
    static constexpr FlashMessage kCancel = FlashMessage( "canc" );
    static constexpr FlashMessage kDefaults = FlashMessage( "dflt" );
    static constexpr FlashMessage kGetKey = FlashMessage( "getk" );
    static constexpr FlashMessage kDeleteKey = FlashMessage( "supk" );
    static constexpr FlashMessage kValidate = FlashMessage( "vald" );

public:
    FlashListKeymap();

    void bind( FlashPlayer *pPlayer,
               SavedInputBindings *pLiveBindings,
               PlayerConfiguration *pInputConfig );

    bool onMessage( FlashMessage *pMessage, FlashPlayer *pPlayer ) override;
    void tick( float deltaTime, float totalTime ) override;
    int32_t getListLength() const override;
    void open() override;
    void close() override;
    void update( const int32_t entryIndex, const int32_t lineIndex ) override;

private:
    int32_t getBindingIndex( const int32_t entryIndex ) const;
    void selectTab( const int32_t tabIndex );
    void startKeyCapture();
    void deleteSelectedKey();
    void onKeyCaptured();
    void writeDeviceLabel( const SavedInputBinding &binding,
                           char *pOutLabel,
                           const size_t outLabelLength ) const;
    const char *findConflictingAction( const int32_t bindingIndex,
                                       const InputChannel &channel ) const;

private:
    FlashPlayer *pConfigPlayer;
    SavedInputBindings *pBindings;
    PlayerConfiguration *pPlayerConfig;
    SavedInputBindings backupBindings;
    bool bCapturePending;
    bool bCalibrating;
    KeyBindCapture capture;
    int32_t currentTab;
    int32_t numTabs;
    InputTemplates::Selection backupTemplateSelection; // OTDU_ADDITION
};
