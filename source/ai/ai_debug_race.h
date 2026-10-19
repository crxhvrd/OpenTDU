#pragma once

#include "ai/ai_race.h"

class AiVehicle;
class CarState;
struct VehicleBaseResources;

// Debug helper to create a fake race in freeride
// (helper to test AI bots before Lobby/Missions are implemented)
class AiDebugRace : public AiRaceDescriptor {
public:
    static constexpr int32_t kMaxNumOpponents = 3;

public:
    AiDebugRace();

    int32_t getNumCheckpoints() const override;
    bool getCheckpointRoadLocation( const int32_t index,
                                    AiRoadIt &outIt ) override;
    void setCheckpointRoadLocation( const int32_t index,
                                    const AiRoadIt &it ) override;

    void tick();
    void drawDebug();

private:
    enum eOpponentState : int32_t {
        OS_Free = 0,
        OS_Loading,
        OS_Installed,
        OS_Racing,
    };

    struct Opponent {
        eOpponentState State = OS_Free;
        VehicleBaseResources *pResource = nullptr;
        CarState *pCarState = nullptr;
        AiVehicle *pVehicle = nullptr;
        Matrix4x4f_t StartFrame = Matrix4x4f_t::Identity();
    };

private:
    std::array<Opponent, kMaxNumOpponents> opponents;
    std::vector<AiRoadIt> checkpoints;
    Matrix4x4f_t startLineFrame;
    int32_t carID;
    float finishDistance;
    bool bReverseGrid;

private:
    bool setupCheckpoints();
    void spawnOpponent();
    void sendMessage( const int32_t id );
    void releaseAll();
};

extern AiDebugRace gAiDebugRace;
