#pragma once

#include "input/input_device.h"

class DeviceMouse : public Device {
public:
    DeviceMouse();
    ~DeviceMouse();

    bool initialize();

    void addScrollDelta( float deltaY );

    void setEnabled() override;
    void setDisabled() override;
    bool FUN_0042df00() override;
    bool tick() override;
    edGUID getInstanceGUID() const override;
    void getChannelDisplayName( const InputChannel &channel,
                                char *pOutName,
                                const size_t outNameLength ) const override;

private:
    float pendingScrollY = 0.0f;
};
