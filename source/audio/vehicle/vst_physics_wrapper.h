#pragma once

#include "gs_vehicle_sound_model.h"

class CarState;

class VSTPhysicsWrapper {
public:
    inline const VSTPhysicsSettings &getSettings() const { return settings; }
    inline VSTPhysicsSettings &getSettings() { return settings; }

    inline const float *getPanelVolumeFactors() const {
        return panelVolumeFactors;
    }

public:
    VSTPhysicsWrapper() = default;

    void refresh( const CarState &carState );
    void computeEvents();

private:
    VSTPhysicsSettings settings = {};
    float panelVolumeFactors[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
    int32_t previousGear = 0;
    float previousWindowAperture = 0.0f;
    bool bPreviousHornDown = false;
    bool bPreviousEngineIgnition = false;
    bool bPreviousViewInternal = false;
    bool bPreviousDoorLeftOpened = false;
    bool bPreviousDoorRightOpened = false;
    bool bPreviousWindowMoving = false;

private:
    void refreshWheels( const CarState &carState, bool bIsPlayer );
};
