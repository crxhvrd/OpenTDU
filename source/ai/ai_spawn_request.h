#pragma once

#include "ai/ai_road_iterator.h"

class EntityCarPlayer;

class AiVehicle;
class CarState;

class AiSpawnRequest {
public:
    enum eKind : int32_t {
        ASK_Traffic = 0,
        ASK_Network = 1,
        ASK_Parked = 2,
        ASK_CopDam = 3,
        ASK_Bot = 4,
    };

    enum eState : int32_t {
        ASS_Idle = 0,
        ASS_WaitResource = 1,
        ASS_Ready = 2,
        ASS_Executing = 3,
        ASS_WaitPhysics = 4,
        ASS_Finalized = 5,
    };

public:
    inline bool isIdle() const { return state == ASS_Idle; }
    inline eState getState() const { return state; }
    inline const Matrix4x4f_t &getSpawnMatrix() const { return spawnMatrix; }

public:
    AiSpawnRequest();

    void fillTraffic( AiVehicle *pVehicle,
                      const Matrix4x4f_t &matrix,
                      const AiRoadIt &roadIt,
                      const int32_t laneIndex,
                      const bool bPoliceCar );
    void fillParked( AiVehicle *pVehicle,
                     const Matrix4x4f_t &matrix,
                     const int32_t newColorIndex,
                     EntityCarPlayer *pOwner );
    void execute();
    void tick( const bool bCancel );
    void flush();

private:
    eKind kind;
    eState state;
    AiVehicle *pAiVehicle;
    CarState *pCarState;
    Matrix4x4f_t spawnMatrix;
    AiRoadIt roadIt;
    int32_t lane;
    bool bPolice;
    int32_t botDBIndex;
    int32_t colorIndex;
    EntityCarPlayer *pParkedOwner;

private:
    bool executeTraffic();
    bool executeParked();
    void finalizeParked();
    void finalize();
    void finalizeTraffic();
    void applyRandomTrafficColor();
};
