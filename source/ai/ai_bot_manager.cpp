#include "shared.h"
#include "ai_bot_manager.h"

#include "ai/ai_config.h"
#include "ai/ai_pool.h"
#include "ai/ai_vehicle.h"
#include "ai/gs_ai.h"
#include "core/random/bcpl.h"
#include "database/gs_database.h"
#include "game/scenaric/game_intro_tutorial.h"
#include "game/scenaric/gs_scenaric.h"
#include "game/vehicle/car_state.h"
#include "player_data/saved_road_discovery.h"
#include "world/gs_world.h"
#include "world/sector_manager.h"
#include "world/streaming_manager.h"

AiBotManager gAiBotManager; // DAT_00fa9424

bool gbForceBotsEnabled = false;

static constexpr int32_t kWantedCountPeriod = 1000;
static constexpr int32_t kLimitedBotZone = 5;
static constexpr int32_t kMaxBotCandidates = 16;
static constexpr int32_t kNumZoneSubSectors = 8;
static constexpr int32_t kSectorCellSize = 0x300;
static constexpr float kMaxZoneDistanceSq = 1.0e10f; // DAT_00f41b80
static constexpr float kRemoveNow = -1.0f;           // DAT_00f41664
static constexpr float kRoadEndAdvance = 10.0f;

struct BotSectorArea {
    int32_t X;
    int32_t Y;
};

static constexpr BotSectorArea kBotSector0Areas[] = {
    {3, 0},
    {4, 0},
    {5, 0},
    {5, 1},
    {6, 1},
    {6, 2},
    {6, 3},
    {7, 3},
    {6, 4},
    {7, 4}
}; // DAT_00f03ae8
static constexpr BotSectorArea kBotSector1Areas[] = {
    {3, 1},
    {4, 1},
    {1, 2},
    {2, 2},
    {3, 2},
    {4, 2},
    {1, 3},
    {2, 3},
    {3, 3},
    {4, 3}
}; // DAT_00f03b38
static constexpr BotSectorArea kBotSector2Areas[] = {
    {0, 2},
    {0, 3},
    {0, 4},
    {1, 4},
    {1, 5},
    {2, 5}
}; // DAT_00f03b88
static constexpr BotSectorArea kBotSector3Areas[] = {
    {2, 4},
    {3, 4},
    {4, 4},
    {3, 5},
    {4, 5},
    {5, 5},
    {6, 5},
    {7, 5}
}; // DAT_00f03bb8
static constexpr BotSectorArea kBotSector4Areas[] = {
    {1, 6},
    {2, 6},
    {3, 6},
    {4, 6},
    {2, 7},
    {3, 7},
    {4, 7}
}; // DAT_00f03bf8
static constexpr BotSectorArea kBotSector5Areas[] = {
    {5, 6},
    {6, 6},
    {7, 6},
    {5, 7},
    {6, 7},
    {7, 7},
    {7, 8}
}; // DAT_00f03c30
static constexpr BotSectorArea kBotSector6Areas[] = {
    {8,  4},
    {9,  4},
    {8,  5},
    {9,  5},
    {8,  6},
    {9,  6},
    {8,  7},
    {9,  7},
    {10, 7},
    {8,  8},
    {9,  8},
    {10, 8}
}; // DAT_00f03c68

struct BotSectorAreas {
    const BotSectorArea *pAreas;
    int32_t NumAreas;
};

static constexpr BotSectorAreas kBotSectors[AiBotManager::kNumBotSectors] = {
    {kBotSector0Areas, 10},
    {kBotSector1Areas, 10},
    {kBotSector2Areas, 6 },
    {kBotSector3Areas, 8 },
    {kBotSector4Areas, 7 },
    {kBotSector5Areas, 7 },
    {kBotSector6Areas, 12},
};

int32_t GetBotSectorIndex( const GridPos2D &area ) {
    // FUN_0090b9b0
    for ( int32_t sector = 0; sector < AiBotManager::kNumBotSectors;
          sector++ ) {
        const BotSectorAreas &areas = kBotSectors[sector];
        for ( int32_t i = 0; i < areas.NumAreas; i++ ) {
            if ( areas.pAreas[i].X == area.posX &&
                 areas.pAreas[i].Y == area.posY ) {
                return sector;
            }
        }
    }
    return -1;
}

const DBEntryBot *GetBotDBEntry( const int32_t botIndex ) {
    const GameDatabase &database = gpDatabase->getGameDB();
    const int32_t numBots = static_cast<int32_t>( database.pNumItems[0xe] );
    if ( numBots == 0 || botIndex >= numBots || database.DBBot == nullptr ) {
        return nullptr;
    }
    return ( botIndex > 0 ) ? &database.DBBot[botIndex] : database.DBBot;
}

int32_t GetBotColorIndex( const int32_t botIndex ) {
    // FUN_0090c390
    const DBEntryBot *pBot = GetBotDBEntry( botIndex );
    return ( pBot != nullptr ) ? pBot->ColorIndex : -1;
}

int32_t GetBotCarID( const int32_t botIndex ) {
    // FUN_0090c3d0
    const DBEntryBot *pBot = GetBotDBEntry( botIndex );
    if ( pBot == nullptr || pBot->BotCar.pAsRawPointer == nullptr ) {
        return -1;
    }
    return static_cast<const DBEntryCarPhysicsData *>(
               pBot->BotCar.pAsRawPointer )
        ->CarID;
}

int32_t GetBotTier( const int32_t botIndex ) {
    // FUN_0090c2e0
    static constexpr uint64_t kRookieBehaviours[] = { 0x34e7468, 0x35c3008,
                                                      0x389f6c8, 0x3b7bd88 };
    static constexpr uint64_t kExpertBehaviours[] = { 0x32fefe8, 0x33dab88,
                                                      0x36b7248, 0x3993908 };

    const DBEntryBot *pBot = GetBotDBEntry( botIndex );
    if ( pBot == nullptr ) {
        return 1;
    }
    const uint64_t behaviour =
        gpDatabase->getGameDB().Databases[0xe].getHashcodeForString(
            pBot->Behaviour.pAsString );
    for ( const uint64_t hashcode : kRookieBehaviours ) {
        if ( hashcode == behaviour ) {
            return 0;
        }
    }
    for ( const uint64_t hashcode : kExpertBehaviours ) {
        if ( hashcode == behaviour ) {
            return 2;
        }
    }
    return 1;
}

int32_t FindBotOwningCar( const int32_t carID ) {
    // FUN_0090c140
    const GameDatabase &database = gpDatabase->getGameDB();
    const int32_t numBots = static_cast<int32_t>( database.pNumItems[0xe] );
    for ( int32_t i = 0; i < numBots; i++ ) {
        if ( GetBotCarID( i ) == carID ) {
            return i;
        }
    }
    return -1;
}

static bool IsBotSpawned( const int32_t botIndex ) {
    AiVehicleList &bots = gpAI->getAIPool()->getList(
        AiPool::AVL_FirstCategory +
        static_cast<int32_t>( eAiVehicleCategory::AVC_Bot ) );
    bool bSpawned = false;
    for ( AiVehicle *pBot : bots ) {
        if ( pBot->getDPC().BotID == botIndex ) {
            bSpawned = true;
        }
    }
    return bSpawned;
}

int32_t PickRandomBotInZone( const int32_t zone ) {
    // FUN_009aa260
    struct Candidate {
        int32_t BotIndex = -1;
        float Weight = -1.0f;
        float RangeMin = -1.0f;
        float RangeMax = -1.0f;
    };

    const GameDatabase &database = gpDatabase->getGameDB();
    const uint32_t numBots = database.pNumItems[0xe];
    if ( numBots == 0 || database.DBBot == nullptr ) {
        return -1;
    }

    Candidate candidates[kMaxBotCandidates];
    for ( uint32_t i = 0; i < numBots; i++ ) {
        // The original pcode does not advance its entry cursor past a bot that
        // is already spawned, so the following rows were read one entry behind
        const DBEntryBot &entry = database.DBBot[i];
        if ( entry.ZoneIndex != zone ||
             IsBotSpawned( static_cast<int32_t>( i ) ) ) {
            continue;
        }
        for ( Candidate &candidate : candidates ) {
            if ( candidate.BotIndex == -1 ) {
                candidate.BotIndex = static_cast<int32_t>( i );
                candidate.Weight = entry.Rarety;
                break;
            }
        }
    }

    int32_t numCandidates = 0;
    float weightSum = 0.0f;
    while ( numCandidates < kMaxBotCandidates &&
            candidates[numCandidates].BotIndex != -1 ) {
        weightSum += candidates[numCandidates].Weight;
        numCandidates++;
    }

    for ( int32_t i = 0; i < numCandidates; i++ ) {
        Candidate &candidate = candidates[i];
        if ( i == 0 ) {
            candidate.RangeMin = 0.0f;
            candidate.RangeMax = candidate.Weight / weightSum;
        } else if ( i == numCandidates - 1 ) {
            candidate.RangeMin = candidates[i - 1].RangeMax;
            candidate.RangeMax = 1.0f;
        } else {
            candidate.RangeMin = candidates[i - 1].RangeMax;
            candidate.RangeMax =
                candidate.Weight / weightSum + candidates[i - 1].RangeMax;
        }
    }

    const float r = RandomUnit( gpAI->getAIPool()->getRandom() );
    for ( int32_t i = 0; i < numCandidates; i++ ) {
        if ( candidates[i].RangeMin <= r && r <= candidates[i].RangeMax ) {
            return candidates[i].BotIndex;
        }
    }
    return -1;
}

AiBotManager::AiBotManager()
    : bActive( false ), tickCounter( 0 ), wantedCount( 3 ) {}

void AiBotManager::initialize() {
    // FUN_00969900 (inlined)
    bActive = false;
    tickCounter = 0;
    wantedCount = 3;
}

void AiBotManager::tick() {
    // FUN_0090c760
    tickCounter++;
    if ( static_cast<uint32_t>( tickCounter ) >= kWantedCountPeriod ) {
        tickCounter = 0;
        if ( wantedCount == 0 ) {
            wantedCount = 1;
        } else if ( wantedCount == gAIConfig.MaxNumBots ||
                    gpAI->getRandom().getRandomUint32() >= 0x7fffffffu ) {
            wantedCount--;
        } else {
            wantedCount++;
        }
    }

    CarState *pLocalCar = gpScenaric->getLocalPlayer().getCarState();
    if ( pLocalCar != nullptr ) {
        const Eigen::Vector4f position = pLocalCar->getWorldPosition();
        if ( computeZone( &position ) == kLimitedBotZone ) {
            wantedCount = std::min( wantedCount, gAIConfig.MaxNumBotsLimited );
        }
    }

    AiVehicleList &bots = gpAI->getAIPool()->getList(
        AiPool::AVL_FirstCategory +
        static_cast<int32_t>( eAiVehicleCategory::AVC_Bot ) );
    if ( bots.Count <= wantedCount ) {
        return;
    }

    AiVehicle *pFarthest = nullptr;
    float farthestDistance = 0.0f;
    for ( AiVehicle *pBot : bots ) {
        if ( pBot->getCarState() == nullptr ||
             gpScenaric->getLocalPlayer().getCarState() == nullptr ) {
            continue;
        }
        const float distance = pBot->getBotBehaviour().getDistanceToPlayer();
        if ( farthestDistance < distance ) {
            farthestDistance = distance;
            pFarthest = pBot;
        }
    }
    if ( pFarthest == nullptr ) {
        return;
    }
    if ( farthestDistance > pFarthest->getDPC().DPCPattern.ChaseDistMax &&
         pFarthest->getBotBehaviour().getState() == AiVehBhvBot::BS_Cruise ) {
        pFarthest->setRemovable( true );
        pFarthest->requestRemoval( kRemoveNow, "too many bots" );
    }
}

bool AiBotManager::canSpawn() const {
    // FUN_0090c710
    static constexpr bool bOnlineSession = false; // TODO: ONLINE

    const bool bForced = gbForceBotsEnabled;
    if ( !bActive || ( !gAIConfig.bEnableBots && !bForced ) ||
         bOnlineSession ) {
        return false;
    }
    const int32_t tutorialStep = gpScenaric->getIntroTutorial().getStep();
    if ( tutorialStep != kTutorialStatePending &&
         tutorialStep != kTutorialStateRunning && !bForced ) {
        return false;
    }
    return gpAI->getAIPool()->getNumBots() < wantedCount;
}

int32_t AiBotManager::computeZone( const Eigen::Vector4f *pPosition ) const {
    // FUN_0090c410
    if ( pPosition == nullptr ) {
        return -1;
    }
    GridPos2D sector;
    GridPos2D area;
    gpWorld->getStreamingManager()->sectorManager.computeCell( *pPosition,
                                                               sector, area );
    return GetBotSectorIndex( area );
}

int32_t AiBotManager::computeRoadZone( const AiRoadIt &it ) const {
    // FUN_0090cc30
    float lateral = 0.0f;
    float height = 0.0f;
    it.getEntity()->pProfile->computeWidthOffset( 0.0f, lateral, height );
    const Eigen::Vector4f position =
        it.computeOffsetPosition( lateral, height );
    return computeZone( &position );
}

void AiBotManager::computeZoneTarget( const int32_t zone,
                                      const Eigen::Vector4f *pPosition,
                                      Eigen::Vector4f &outTarget,
                                      GridPos2D *pOutArea ) const {
    // FUN_0090c8d0
    const StreamingManager *pStreaming = gpWorld->getStreamingManager();
    if ( zone == computeZone( pPosition ) ) {
        outTarget = *pPosition;
        if ( pOutArea != nullptr ) {
            GridPos2D sector;
            pStreaming->sectorManager.computeCell( *pPosition, sector,
                                                   *pOutArea );
        }
        return;
    }

    if ( zone < 0 || zone >= kNumBotSectors ) {
        outTarget = Eigen::Vector4f::Zero();
        return;
    }

    const BotSectorAreas &areas = kBotSectors[zone];
    const GridPos2D centreSector = { kNumZoneSubSectors / 2,
                                     kNumZoneSubSectors / 2 };
    int32_t bestArea = -1;
    float bestDistanceSq = kMaxZoneDistanceSq;
    Eigen::Vector4f best = Eigen::Vector4f::Zero();
    for ( int32_t i = 0; i < areas.NumAreas; i++ ) {
        const GridPos2D area = { areas.pAreas[i].X, areas.pAreas[i].Y };
        Eigen::Vector4f centre;
        pStreaming->computeCellWorldPosition( centre, centreSector, area );
        const float distanceSq = ( centre - *pPosition ).squaredNorm();
        if ( distanceSq < bestDistanceSq ) {
            bestDistanceSq = distanceSq;
            best = centre;
            bestArea = i;
        }
    }

    if ( bestArea < 0 ) {
        outTarget = best;
        return;
    }

    const BotSectorArea &area = areas.pAreas[bestArea];
    const SectorManager &sectorManager = pStreaming->sectorManager;
    const int32_t baseZ = area.Y *
                          static_cast<int32_t>( sectorManager.numSubSectors ) *
                          kSectorCellSize;
    int32_t x = area.X * static_cast<int32_t>( sectorManager.numSectors ) *
                kSectorCellSize;
    for ( int32_t column = 0; column < kNumZoneSubSectors; column++ ) {
        int32_t z = baseZ;
        for ( int32_t row = 0; row < kNumZoneSubSectors; row++ ) {
            const int32_t cellX =
                ( ( row % 2 ) != 0 ) ? x + kSectorCellSize / 2 : x;
            const Eigen::Vector4f corner( static_cast<float>( cellX ), 0.0f,
                                          static_cast<float>( z ), 1.0f );
            const float distanceSq = ( corner - *pPosition ).squaredNorm();
            if ( distanceSq < bestDistanceSq ) {
                bestDistanceSq = distanceSq;
                best = corner;
            }
            z += kSectorCellSize;
        }
        x += kSectorCellSize;
    }

    outTarget = best;
    if ( pOutArea != nullptr ) {
        pOutArea->posX = area.X;
        pOutArea->posY = area.Y;
    }
}

static int32_t ClampCellIndex( const float coordinate,
                               const float cellSize,
                               const int32_t numCells ) {
    const int32_t index = static_cast<int32_t>( coordinate / cellSize );
    if ( index < 0 ) {
        return 0;
    }
    return std::min( index, numCells - 1 );
}

bool AiBotManager::pickSpawnRoad( AiRoadIt &outIt,
                                  const int32_t zone,
                                  const Eigen::Vector4f &position,
                                  const float minDistance,
                                  const float maxDistance ) const {
    // FUN_0090d870
    outIt = AiRoadIt();

    const AiRoadMap::EntityArray *pGrid = gpAI->getRoadMap().getEntityArray();
    Eigen::Vector4f target;
    GridPos2D targetArea;
    computeZoneTarget( zone, &position, target, &targetArea );

    const int32_t startColumn =
        ClampCellIndex( target.x(), pGrid->ClusterWidth, pGrid->NumColumns );
    const int32_t startRow =
        ClampCellIndex( target.z(), pGrid->ClusterHeight, pGrid->NumRows );
    const AiRoadMap::EntityCluster *pCluster =
        pGrid->pClusters[pGrid->NumRows * startColumn + startRow];
    if ( pCluster == nullptr ) {
        outIt = AiRoadIt();
        return false;
    }

    int32_t ring = 0;
    int32_t ringColumn = startColumn;
    int32_t column = startColumn;
    int32_t row = startRow;
    for ( ;; ) {
        const bool bInsideRing = std::abs( row - startRow ) < ring &&
                                 std::abs( column - startColumn ) < ring;
        if ( !bInsideRing ) {
            pCluster = pGrid->pClusters[pGrid->NumRows * column + row];
        }
        if ( pCluster == nullptr ) {
            break;
        }

        if ( pCluster->NumEntities != 0 && !bInsideRing ) {
            for ( uint32_t i = 0; i < pCluster->NumEntities; i++ ) {
                const AiRoadMap::EntityRef &ref = pCluster->Refs[i];
                outIt.pRoad = ref.pRoad;
                outIt.IndEntity = static_cast<int32_t>( ref.EntityIndex );
                outIt.IndWay = 0;
                outIt.bWay = true;
                outIt.T = 0.0f;

                const AiRoadMap::Entity *pEntity = outIt.getEntity();
                float lateral = 0.0f;
                float height = 0.0f;
                pEntity->pProfile->computeWidthOffset( 0.0f, lateral, height );
                Eigen::Vector4f delta =
                    pEntity->computeOffsetPosition( 0, 0.0f, lateral, height ) -
                    position;
                delta.y() = 0.0f;
                const float distance = delta.norm();
                if ( distance <= minDistance || distance >= maxDistance ||
                     !IsRoadAvailable( outIt.pRoad->ID ) ) {
                    continue;
                }

                const AiRoadMap::Road *pRoad = outIt.pRoad;
                if ( ( outIt.bWay ? pRoad->pJunctionEnd
                                  : pRoad->pJunctionBegin ) == nullptr ) {
                    outIt.bWay = !outIt.bWay;
                }
                if ( ( outIt.bWay ? pRoad->pJunctionBegin
                                  : pRoad->pJunctionEnd ) == nullptr ) {
                    outIt.advanceByDistance( kRoadEndAdvance, true );
                }
                return true;
            }
        }

        if ( column - startColumn != ring && column + 1 < pGrid->NumColumns ) {
            column++;
            continue;
        }
        column = std::max( ringColumn, 0 );
        if ( row + 1 < pGrid->NumRows && ( row - startRow ) + 1 <= ring ) {
            row++;
            continue;
        }

        ring++;
        ringColumn--;
        if ( ring > 1 ) {
            const float ringDistance = static_cast<float>( ring - 1 );
            if ( ringDistance * pGrid->ClusterHeight > maxDistance &&
                 ringDistance * pGrid->ClusterWidth > maxDistance ) {
                break;
            }
        }
        row = std::max( startRow - startColumn + ringColumn, 0 );
        column = std::max( ringColumn, 0 );
    }

    outIt = AiRoadIt();
    return false;
}
