#pragma once

#include "input/input_device.h"

class DeviceKeyboard : public Device {
public:
    DeviceKeyboard();
    ~DeviceKeyboard();

    bool initialize();

    int32_t getChannelIndexFromAscii( uint8_t param_1 ) override;
    void setEnabled() override;
    void setDisabled() override;
    bool FUN_0042df00() override;
    bool tick() override;
    edGUID getInstanceGUID() const override;
    void getChannelDisplayName( const InputChannel &channel,
                                char *pOutName,
                                const size_t outNameLength ) const override;
};