#pragma once

#include "ai/ai_road_iterator.h"

struct DBEntryBot;
struct GridPos2D;

// Free ride bot population
class AiBotManager {
public:
    static constexpr int32_t kNumBotSectors = 7;

public:
    inline bool isActive() const { return bActive; }
    inline void setActive( const bool bEnabled ) { bActive = bEnabled; }
    inline int32_t getWantedCount() const { return wantedCount; }

public:
    AiBotManager();

    void initialize();
    void tick();
    bool canSpawn() const;
    int32_t computeZone( const Eigen::Vector4f *pPosition ) const;
    int32_t computeRoadZone( const AiRoadIt &it ) const;
    void computeZoneTarget( const int32_t zone,
                            const Eigen::Vector4f *pPosition,
                            Eigen::Vector4f &outTarget,
                            GridPos2D *pOutArea ) const;
    bool pickSpawnRoad( AiRoadIt &outIt,
                        const int32_t zone,
                        const Eigen::Vector4f &position,
                        const float minDistance,
                        const float maxDistance ) const;

private:
    bool bActive;
    int32_t tickCounter;
    int32_t wantedCount;
};

int32_t GetBotSectorIndex( const GridPos2D &area );
const DBEntryBot *GetBotDBEntry( const int32_t botIndex );
int32_t GetBotColorIndex( const int32_t botIndex );
int32_t PickRandomBotInZone( const int32_t zone );

extern AiBotManager gAiBotManager;
extern bool gbForceBotsEnabled;
