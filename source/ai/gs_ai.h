#pragma once

#include "game/gs_base.h"

#include "ai/ai_path.h"
#include "ai/ai_road_buckets.h"
#include "ai/ai_road_map.h"

class AiPool;
class Bank;

struct AIConfig {
    int32_t UnknownDword = 0;
    int32_t MaxNumVehicle = 0;
    int32_t MaxNumTrafficVehicle = 0;
    int32_t MaxNumParkedVehicle = 0;
    int32_t MaxNumPoliceVehicle = 0;
    int32_t MaxNumPoliceDam = 0;
    int32_t MaxNumBots = 0;
    int32_t MaxNumBotsCity = 0;
    int32_t MaxNumPhysicsVehicle = 0;
    int32_t NumOpponentsOverride = 0;
    int32_t BotsWantedDifficulty = 0;
    int32_t BotsWantedBehavior = 0;
    float OpponentDifficulty = 0.0f;
    float OpponentSharpTurnLimit = 0.0f;
    float OpponentSharpTurnMaxSpeed = 0.0f;
    float OpponentSharpTurnClampValue = 0.0f;
    bool bEnablePolice = false;
    bool bTraceModeEnabled = false;
    bool bAIReplacePlayer = false;
    bool bSameCarAsPlayer = false;
    bool bEnableJail = false;
    bool bEnableBots = false;
    bool bBotBehaviorOverride = false;
    bool bPoliceForceChase = false;
    bool bPoliceForceDensity = false;
};
extern AIConfig gAIConfig;

class GSAI : public GameSystem {
public:
    const char *getName() const override { return "Service : AI"; }
    inline AiRoadMap &getRoadMap() { return roadMap; }
    inline AiPool *getAIPool() const { return pAIPool; }
    inline AiPathPool &getPathPool() { return pathPool; }
    inline AiPathSolver &getPathSolver() { return pathSolver; }
    inline AiRoadBuckets &getRoadBuckets() { return roadBuckets; }
    inline bool isRoadMapLoaded() const { return bRoadMapLoaded; }
    inline void setDisableCops( const bool bDisable ) {
        bDisableCops = bDisable;
    }
    inline void setDisableBots( const bool bDisable ) {
        bDisableBots = bDisable;
    }
    inline void setPauseTraffic( const bool bPause ) { bPauseTraffic = bPause; }
    inline void setCopsActive( const bool bActive ) { bCopsActive = bActive; }
    inline void setBotsActive( const bool bActive ) { bBotsActive = bActive; }
    inline void setCullFromCamera( const bool bEnabled ) {
        bCullFromCamera = bEnabled;
    }

public:
    GSAI();
    ~GSAI();

    bool initialize( TestDriveGameInstance * ) override;
    void tick( float totalTime, float deltaTime ) override;
    void terminate() override;

    void loadRoadMap( Bank &levelBank );
    void setTrafficCategories( const bool bResetPool,
                               const uint32_t categoryMask );

private:
    AiRoadBuckets roadBuckets;
    AiPathPool pathPool;
    AiPathSolver pathSolver;
    AiPool *pAIPool;
    AiRoadMap roadMap;
    bool bRoadMapLoaded;
    void *pRoadMapStream;
    void *pRoadDetailsStream;

    void *pUpgradedRoadMap;
    void *pUpgradedRoadDetails;

    bool bDisableCops;
    bool bDisableBots;
    bool bPauseTraffic;

    bool bCopsActive;
    bool bBotsActive;
    bool bCullFromCamera; // Culling/spawn centered on the camera instead of the
                          // player car
    bool bLocationsFromCarState; // Object locations are updated by GSCarState
                                 // instead
};

extern GSAI *gpAI;
extern GSAI gGSAI;

extern bool gbDisablePathOpt;
extern bool gbDisableObstacleParser;
