#pragma once

#include "game/gs_base.h"

#include "ai/ai_bot_manager.h"
#include "ai/ai_bot_personality.h"
#include "ai/ai_config.h"
#include "ai/ai_game_session.h"
#include "ai/ai_path.h"
#include "ai/ai_race.h"
#include "ai/ai_pedestrians.h"
#include "ai/ai_road_buckets.h"
#include "ai/ai_road_map.h"
#include "ai/ai_sim_bubble.h"

struct AiDebugDraw;
class AiPathOptimiser;
class AiPool;
struct AiLODSorter;
class Bank;
class RandomGen;

// DAT_00fa90a0
struct AiCullingShape {
    float FirstRadius;
    float SecondRadius;
    float FovDistance;
    float FovHalfAngle;
    float FirstRadiusSq;
    float SecondRadiusSq;
    float FovDistanceSq;
};

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
    void setCopsLevel( const float scale,
                       const bool bHighLevel,
                       const bool bMaxLevel );
    inline void setBotsActive( const bool bActive ) {
        pBotManager->setActive( bActive );
    }
    inline AiBotManager &getBotManager() { return *pBotManager; }
    inline AiPathFloodSolver &getFloodSolver() { return floodSolver; }
    inline bool areBotsDisabled() const { return bDisableBots; }
    inline bool isTrafficPaused() const { return bPauseTraffic; }
    inline void setCullFromCamera( const bool bEnabled ) {
        pSimBubble->setCullFromCamera( bEnabled );
    }
    inline AiSimBubble &getSimBubble() { return *pSimBubble; }
    inline AiLODSorter &getLODSorter() { return *pLODSorter; }
    inline AiPedestrianManager &getPedestrianManager() {
        return pedestrianManager;
    }
    inline float *getSignalSyncTime() const { return pSignalSyncTime; }
    inline const AiBotPersonalities &getBotPersonalities() const {
        return *pBotPersonalities;
    }
    inline RandomGen &getRandom() const { return *pRandom; }
    inline AiRaceDescriptor *getRace() const { return pRace; }
    inline GameSessionOffline &getGameSession() { return gameSession; }
    inline void setRace( AiRaceDescriptor *pNewRace ) { pRace = pNewRace; }
    inline AiDebugDraw *getDebugDraw() const { return pDebugDraw; }
    inline const AiCullingShape &getCullingShape() const {
        return *pCullingShape;
    }

public:
    GSAI();
    ~GSAI();

    bool initialize( TestDriveGameInstance * ) override;
    void tick( float totalTime, float deltaTime ) override;
    void draw( float deltaTime ) override;
    void terminate() override;
    void pause() override;
    void reset() override;

    void loadRoadMap( Bank &levelBank );
    void loadAIConfig();
    void loadBotPersonalities();
    void setTrafficCategories( const bool bResetPool,
                               const uint32_t categoryMask );
    void resumeTraffic( const Eigen::Vector4f *pBubblePosition );
    void drawDebug( float deltaTime ) override;
    void broadcastRaceMessage( const AiRaceMessage &message );
    bool spawnDebugTraffic();
    void spawnDebugParked();

private:
    AiRoadBuckets roadBuckets;
    AiPathPool pathPool;
    AiPathSolver pathSolver;
    AiPathFloodSolver floodSolver;
    AiPool *pAIPool;
    AiLODSorter *pLODSorter;
    AiBotPersonalities *pBotPersonalities;
    AiSimBubble *pSimBubble;
    RandomGen *pRandom;
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
    AiBotManager *pBotManager;
    bool bLocationsFromCarState; // Object locations are updated by GSCarState
                                 // instead
    float *pSignalSyncTime;
    AiRaceDescriptor *pRace;
    GameSessionOffline gameSession;
    AiPathOptimiser *pPathOptimiser;
    AiCullingShape *pCullingShape;
    AiDebugDraw *pDebugDraw;
};

// Returns the traffic signal bulb mask (red 0x1, orange 0x2, green 0x4).
uint32_t GetTrafficSignalState( int32_t synchro, float *pSyncTime );

extern GSAI *gpAI;
extern GSAI gGSAI;

extern std::vector<std::string> gForcedCarModels;

extern bool gbDisablePathOpt;
extern bool gbDisableObstacleParser;
