#pragma once

class RandomGen;

struct AiVehIndDPCPattern {
    float AvoidJumpFactorDeadZone;
    float BrakingDistanceFactorDeadZone;
    float CurveSpeedFactorDeadZone;
    float RoadWidthFactorDeadZone;
    float SteeringSecurityFactorDeadZone;
    bool bAvoidCollision;
    float AvoidJumpFactorMin;
    float AvoidJumpFactorMax;
    float AvoidJumpLengthMin;
    float AvoidJumpLengthMax;
    float BetLevel;
    bool bBlockPlayer;
    float BrakingDistFactorMin;
    float BrakingDistFactorMax;
    float ChaseDistMin;
    float ChaseDistMax;
    float CurveSpeedFactorMin;
    float CurveSpeedFactorMax;
    bool bKeepOneLane;
    float FrontABSFactorMin;
    float FrontABSFactorMax;
    float RearABSFactorMin;
    float RearABSFactorMax;
    float MaxAccel;
    int32_t PathOptLevel;
    bool bPreferLinearPath;
    bool bRespectRoadSign;
    bool bRespectWay;
    float RoadWidthFactorMin;
    float RoadWidthFactorMax;
    float RoaringLevelMin;
    float RoaringLevelMax;
    int32_t RPMToChangeGearIndex;
    float SteeringSecurityFactorMin;
    float SteeringSecurityFactorMax;
    bool bTaunt;
    float TractionControlFactorMin;
    float TractionControlFactorMax;
    bool bWaitForPlayer;

    AiVehIndDPCPattern();

    void reset();
};
OTDU_SIZE_MUST_MATCH( AiVehIndDPCPattern, 0x94 );

// Per-vehicle driver personality
struct AiVehIndDPC {
    static constexpr int32_t kNameLength = 32;

    int32_t BotID;
    char pName[kNameLength];
    int32_t BotSectorIndex;
    int32_t Difficulty;
    int32_t Personality;
    float AvoidJumpFactor;
    float BrakingDistanceFactor;
    float CurveSpeedFactor;
    float FrontABSFactor;
    float RearABSFactor;
    float RoadWidthFactor;
    float SteeringSecurityFactor;
    float TCFactor;
    int32_t NumOutsideWanted;
    AiVehIndDPCPattern DPCPattern;

    AiVehIndDPC();

    void reset();
    void randomizeFactors( RandomGen &random );
    float applySteerDeadZone( const float steer ) const;
};
OTDU_SIZE_MUST_MATCH( AiVehIndDPC, 0xe8 );

// AIBotPers.xml instance
class AiBotPersonalities {
public:
    static constexpr int32_t kNumDifficulties = 3;
    static constexpr int32_t kNumPersonalities = 4;
    static constexpr int32_t kNumContexts = 3;

public:
    AiBotPersonalities();

    int32_t parse( const char *pDocument );
    const AiVehIndDPCPattern *getPattern( const int32_t difficulty,
                                          const int32_t personality,
                                          const int32_t context ) const;

private:
    AiVehIndDPCPattern patterns[kNumDifficulties][kNumPersonalities]
                               [kNumContexts];
};
