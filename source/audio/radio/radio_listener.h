#pragma once

class RadioListener {
public:
    virtual float getTurnOnDelay() = 0;
    virtual float getTurnOffDelay() = 0;
    virtual void onVolumeUp() = 0;
    virtual void onVolumeDown() = 0;
    virtual void onNextStation() = 0;
    virtual void onPreviousStation() = 0;
};
