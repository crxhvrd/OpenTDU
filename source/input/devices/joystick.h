#pragma once

#include "input/input_device.h"

class DeviceJoystick : public Device {
public:
    DeviceJoystick();
    ~DeviceJoystick();

    bool initialize( SDL_JoystickID param_1 );

    inline SDL_Joystick *getSDLJoystick() const { return pJoystick; }
    inline SDL_Haptic *getSDLHaptic() const { return pHaptic; }

    void setEnabled() override;
    void setDisabled() override;
    bool FUN_0042df00() override;
    bool tick() override;
    edGUID getInstanceGUID() const override;
    void getChannelDisplayName( const InputChannel &channel,
                                char *pOutName,
                                const size_t outNameLength ) const override;

private:
    bool createChannels();

    bool readButton( const int32_t buttonIndex ) const;
    int16_t readAxis( const int32_t axisIndex ) const;
    int32_t readPOVAngle( const int32_t hatIndex ) const;

private:
    int32_t field_0x70;
    int32_t field_0x74;
    bool field_0x78;
    int32_t field_0x7c;
    int32_t field_0x80;

    SDL_JoystickID joystickID;
    SDL_Joystick *pJoystick;
    SDL_Gamepad *pGamepad;
    SDL_Haptic *pHaptic;
};
