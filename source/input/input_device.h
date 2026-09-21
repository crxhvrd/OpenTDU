#pragma once

#include <SDL3/SDL.h>

#define OTDU_MOUSE_LEFT 0
#define OTDU_MOUSE_RIGHT 1
#define OTDU_MOUSE_MIDDLE 2
#define OTDU_MOUSE_SCROLL_UP 3
#define OTDU_MOUSE_SCROLL_DOWN 4
#define OTDU_MOUSE_MOUSE_X 5
#define OTDU_MOUSE_MOUSE_Y 6

enum class eDeviceType : uint32_t {
    DT_Keyboard = 0,
    DT_Mouse,
    DT_Other, // Either gamepad or steeering wheel (depending on
              // eDeviceSpecialization)
    DT_Count
};

enum class eDeviceSpecialization : uint32_t {
    DS_None = 0,
    DS_Gamepad = 1,
    DS_SteeringWheel = 2,
    DS_Count
};

enum class eInputChannelType : int32_t {
    ICT_Digital = 0,
    ICT_Analog = 1,
    ICT_Keyboard = 2
};

struct InputChannel {
    int32_t Unknown = 0;
    int32_t Index = 0;
    uint16_t Flags = 0;
    uint16_t Flags2 = 0;
    char pName[32];
    uint32_t field_0x2c = 0;
    float InputValue = 0.0f;

    uint8_t Unknown1 = 0;
    uint8_t Unknown2 = 0;
    uint16_t Unknown3 = 0;

    float Unknown4 = 0.0f;
    float Unknown5 = 0.0f;

    float Unknown6 = 0.0f;
    float Unknown7 = 0.0f;
    float Unknown8 = 0.0f;
    float Unknown9 = 0.0f;

    class Device *pParent = nullptr;
    int32_t field13_0x54 = 0;
};

struct InputChanneMapping {
    InputChannel *pChannel = nullptr;
    uint16_t Inverted = 0;
    uint16_t Combination = 0;
};

struct DeviceAction {
    uint32_t bIndividualAction;
    uint32_t InputInterpolationMode;
    uint8_t Flags;
    float TimerCounter;
    int32_t NumChannels;
    int32_t NumAddedChannels;
    InputChanneMapping *pChannel;
    char pName[32];

    DeviceAction();

    void tick( float param_2 );
    bool addChannel( uint8_t param_2, uint8_t param_3, InputChannel *param_4 );
    float FUN_00434bc0( float param_2, float param_3 );
    float FUN_00434e40( float param_2 );
};

inline bool IsActionPressed( DeviceAction *param_1 ) {
    // FUN_0099a000
    if ( ( param_1 != nullptr ) && ( ( param_1->Flags & 8 ) != 0 ) ) {
        return true;
    }
    return false;
}

inline bool IsActionReleased( DeviceAction *param_1 ) {
    if ( ( param_1 != nullptr ) && ( ( param_1->Flags & 0x10u ) != 0 ) ) {
        return true;
    }
    return false;
}

inline bool IsChannelJustPressed( const InputChannel *param_1 ) {
    return param_1 != nullptr &&
           ( static_cast<uint8_t>( param_1->Flags ) & 0x80u ) != 0;
}

inline void ClearActionFlags( DeviceAction *pAction ) {
    if ( pAction != nullptr ) {
        pAction->Flags = static_cast<uint8_t>(
            ( ( pAction->Flags & 1u ) << 1 ) | ( pAction->Flags & 0xe1u ) );
    }
}

inline bool IsActionDown( DeviceAction *param_1 ) {
    if ( ( param_1 != nullptr ) && ( ( param_1->Flags & 1 ) != 0 ) ) {
        return true;
    }
    return false;
}

static float ReadActionAxis( DeviceAction *pAction ) {
    if ( pAction == nullptr || pAction->NumAddedChannels == 0 ) {
        return 0.0f;
    }
    return pAction->FUN_00434e40( 0.0f );
}

static float ApplyInputDeadZone( float value, float deadZone ) {
    return value >= deadZone ? ( value - deadZone ) / ( 1.0f - deadZone )
                             : 0.0f;
}

class Device {
public:
    inline const char *getName() const { return pName; }
    inline eDeviceType getType() { return deviceType; }
    inline eDeviceSpecialization getSpecialization() const {
        return specializedType;
    }
    inline bool isEnabled() const { return ( flags & 1 ) != 0; }
    inline uint32_t getNumAxis() { return numChannelAxis; }
    inline uint32_t getNumChannels() { return channelCount; }
    inline InputChannel *getChannels() const { return pChannels; }
    inline uint32_t getNumChannelsDown() const { return numChannelsDown; }
    inline int32_t getPressedChannelIndex() const { return channelIsPressed; }
    inline bool hasChannelBeenPressed() const {
        return channelHasBeenPressed >= 0;
    }

public:
    Device();
    virtual ~Device();

    virtual int32_t getChannelIndexFromAscii( uint8_t param_1 );
    virtual void setEnabled() = 0;
    virtual void setDisabled() = 0;

    // TODO: Figure out what the interface func is (inherited classes always
    // impl this using FUN_0042df00)
    virtual bool FUN_0042df00() = 0;

    virtual bool tick() = 0;

    void setName( const char *param_2 );
    InputChannel *allocateChannels( int32_t param_1 );

    int32_t getChannelIndex( const std::string &param_1 );
    int32_t getChannelIndex( const int32_t param_1, const int32_t param_3 );
    void postDeviceCreation();

    InputChannel *getChannel( const std::string &param_1 );
    void FUN_00434ab0();

    bool sampleData( float param_2 );

    void freeChannels();

    float getChannelValue( const std::string &param_1 );
    bool getChannelValueBinary( const std::string &param_1 );

protected:
    char pName[32];
    uint8_t flags;
    uint32_t field_0x68;
    InputChannel *pChannels;
    InputChannel *pScancodeChannel;
    uint32_t channelCount;
    uint32_t numChannelAxis;
    uint32_t numChannelButtons;
    uint32_t unknown;
    uint32_t numChannelsDown;
    int32_t channelIsPressed;
    int32_t channelHasBeenPressed;
    float delay;
    float repeat;
    float delayTimer;
    float repeatCounter;
    eDeviceType deviceType;
    eDeviceSpecialization specializedType;
    // IDirectInputDevice8A* field_0x6c;
};
