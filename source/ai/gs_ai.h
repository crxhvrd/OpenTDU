#pragma once

#include "game/gs_base.h"

#include "ai/ai_config.h"
#include "ai/ai_path.h"
#include "ai/ai_pedestrians.h"
#include "ai/ai_road_buckets.h"
#include "ai/ai_road_map.h"
#include "ai/ai_sim_bubble.h"

class AiPool;
struct AiLODSorter;
class Bank;

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
        pSimBubble->setCullFromCamera( bEnabled );
    }
    inline AiSimBubble &getSimBubble() { return *pSimBubble; }
    inline AiLODSorter &getLODSorter() { return *pLODSorter; }
    inline AiPedestrianManager &getPedestrianManager() {
        return pedestrianManager;
    }
    inline float *getSignalSyncTime() const { return pSignalSyncTime; }

public:
    GSAI();
    ~GSAI();

    bool initialize( TestDriveGameInstance * ) override;
    void tick( float totalTime, float deltaTime ) override;
    void terminate() override;
    void pause() override;
    void reset() override;

    void loadRoadMap( Bank &levelBank );
    void loadAIConfig();
    void setTrafficCategories( const bool bResetPool,
                               const uint32_t categoryMask );
    void resumeTraffic( const Eigen::Vector4f *pBubblePosition );
    void drawDebug( float deltaTime ) override;
    bool spawnDebugTraffic();
    void spawnDebugParked();

private:
    AiRoadBuckets roadBuckets;
    AiPathPool pathPool;
    AiPathSolver pathSolver;
    AiPool *pAIPool;
    AiLODSorter *pLODSorter;
    AiSimBubble *pSimBubble;
    AiRoadMap roadMap;
    AiPedestrianManager pedestrianManager;
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
    bool bLocationsFromCarState; // Object locations are updated by GSCarState
                                 // instead
    float *pSignalSyncTime;
};

// Returns the traffic signal bulb mask (red 0x1, orange 0x2, green 0x4).
uint32_t GetTrafficSignalState( int32_t synchro, float *pSyncTime );

extern GSAI *gpAI;
extern GSAI gGSAI;

extern std::vector<std::string> gForcedCarModels;

extern bool gbDisablePathOpt;
extern bool gbDisableObstacleParser;
