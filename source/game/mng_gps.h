#pragma once

#include <cfloat>

#include "game/mng_base.h"
#include "core/mutex.h"

class MngGPS : public Manager {
public:
    const char *getName() const override {
        // FUN_00406490
        return "GPS";
    }

public:
    MngGPS();
    ~MngGPS();

    bool initialize( TestDriveGameInstance * ) override;
    void tick( float deltaTime, float totalTime ) override;
    void reset() override;
    void draw() override;
    void pause() override;
    void terminate() override;

private:
    TestDriveMutex mutex;

    bool bLastArrowState = false;
    bool bHasValidObjective = false;

    Eigen::Vector4f lastObjectivePosition;
    Eigen::Vector4f lastSecondaryObjectivePosition;
    float currentRoadParam = -1.0f;
    Eigen::Vector4f hudTargetPosition;
    Eigen::Vector4f gpsTargetVector = { 0.0f, 0.0f, 0.0f, -1.0f };
    int32_t bTargetOnRoad = 0;
    float stateTimer = 0.0f;
    uint8_t bRerouteAnnounced = 0;
    int32_t objectiveRouteHandle = 0;
    int32_t field_0xe0 = -1;
    int32_t field_0xe4 = -1;
    uint8_t roadDirectionBit = 1;
    float roadParamOffset = 0.0f;
    int32_t secondaryObjectiveRouteHandle = 0;
    int32_t field_0xf4 = -1;
    int32_t field_0xf8 = -1;
    int32_t field_0xfc = 1;
    int32_t field_0x100 = 0;
    int32_t arrowDirectionCache = 8;
    uint8_t bBigArrowDisabled = 1;
    bool bAutoWaypointEnabled = true;
    bool bVoiceAnnounceEnabled = true;
    float roadParamMax = FLT_MAX;

    int32_t field_0x1cd4 = 0;
    uint8_t field_0x1cd8 = 0;
    float lastAnnouncedDistance = 0.0f;
    float field_0x1ce0 = FLT_MAX;
    float field_0x1ce4 = 0.0f;
    uint8_t bObjectiveInRange = 0;
    int32_t cachedSessionPlayerCount = 0;
    float updateAccumulator = 0.0f;
};
