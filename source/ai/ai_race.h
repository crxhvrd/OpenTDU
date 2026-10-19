#pragma once

#include "ai/ai_road_iterator.h"

enum eAiRaceMessage : int32_t {
    ARM_Prepare = 0,
    ARM_Countdown = 1,
    ARM_CountdownLast = 2,
    ARM_Go = 3,
    ARM_Restart = 6,
    ARM_Stop = 7,
};

struct AiRaceMessage {
    int32_t Type = 0;
    int32_t ID = 0;
};

// Race checkpoints as seen by the AI
class AiRaceDescriptor {
public:
    virtual ~AiRaceDescriptor() = default;

    // FUN_00801080
    virtual int32_t getNumCheckpoints() const = 0;
    // FUN_00801310
    virtual bool getCheckpointRoadLocation( const int32_t index,
                                            AiRoadIt &outIt ) = 0;
    // FUN_00801050 + FUN_00408150
    virtual void setCheckpointRoadLocation( const int32_t index,
                                            const AiRoadIt &it ) = 0;
};
