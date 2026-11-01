#include "shared.h"
#include "saved_road_discovery.h"

#include "ai/ai_road_iterator.h"
#include "ai/ai_road_shape.h"
#include "ai/gs_ai.h"
#include "core/crypto/tea.h"
#include "core/ed_store.h"
#include "game/scenaric/spots.h"
#include "game/vehicle/car_state.h"
#include "player_data/achievement_checks.h"
#include "player_data/achievements_data.h"
#include "player_data/gs_playerdata.h"
#include "player_data/player_data_interface.h"

bool gUnlockAllRoads = false; // DAT_010e7539

SavedRoadDiscovery::SavedRoadDiscovery() : pBlocks() {
    // FUN_0088d560
    clear();
}

SavedRoadDiscovery::~SavedRoadDiscovery() {}

void SavedRoadDiscovery::clear() {
    // FUN_0088d560
    for ( int32_t i = 0; i < kNumBlocks; i++ ) {
        TestDriveTEAWriteQword( pBlocks[i], 0ull );
    }
}

void SavedRoadDiscovery::deserialize( edStore &stream ) {
    // FUN_0088d400
    for ( int32_t i = 0; i < kNumBlocks; i++ ) {
        pBlocks[i][0] = stream.readULong();
        pBlocks[i][1] = stream.readULong();
    }
}

void SavedRoadDiscovery::serialize( edStore &stream ) const {
    // FUN_0088d3d0
    for ( int32_t i = 0; i < kNumBlocks; i++ ) {
        stream.writeInteger( int32_t( pBlocks[i][0] ) );
        stream.writeInteger( int32_t( pBlocks[i][1] ) );
    }
}

void SavedRoadDiscovery::unlockAllRoads() {
    // FUN_0088d520
    for ( int32_t i = 0; i < kNumBlocks; i++ ) {
        TestDriveTEAWriteQword( pBlocks[i], 0xffffffffffffffffull );
    }
}

uint64_t SavedRoadDiscovery::getBlock( const int32_t index ) const {
    return TestDriveTEAReadQword( pBlocks[index] );
}

void SavedRoadDiscovery::setBlock( const int32_t index, const uint64_t value ) {
    TestDriveTEAWriteQword( pBlocks[index], value );
}

bool SavedRoadDiscovery::isRoadLocked( const int32_t roadID,
                                       const int32_t segment ) const {
    // FUN_0040ab10
    const int32_t blockIndex = roadID + segment / 64;
    if ( blockIndex < 0 || blockIndex >= kNumBlocks ) {
        return true;
    }

    const int32_t bitIndex = segment % 64;
    return ( getBlock( blockIndex ) & ( 1ull << bitIndex ) ) == 0ull;
}

void SavedRoadDiscovery::unlockRoad( const int32_t roadID,
                                     const int32_t segment ) {
    // FUN_0088d430
    const int32_t blockIndex = roadID + segment / 64;
    const int32_t bitIndex = segment % 64;
    setBlock( blockIndex, getBlock( blockIndex ) | ( 1ull << bitIndex ) );
}

void DiscoverCarRoad( const CarState *pCarState ) {
    // FUN_008058f0
    const ObjectWorldLocation &location = pCarState->getWorldLocation();
    if ( location.LocationType != ObjectWorldLocation::OWL_Road ) {
        return;
    }

    const ObjectLocationRoad &road = location.LocationRoad;
    const float fraction = ( road.FractionMin + road.FractionMax ) * 0.5f;
    AiRoadMap::Road *pRoad = gpAI->getRoadMap().getRoad( road.RoadID );
    AiRoadShape *pShape = ( pRoad != nullptr ) ? pRoad->getShape() : nullptr;
    if ( pShape == nullptr ) {
        return;
    }

    AiRoadIt it;
    it.IndEntity = 0;
    it.IndWay = -1;
    pShape->computeIt( fraction, it );

    gpPlayerData->getLoadedSave()->getRoadDiscovery()->unlockRoad(
        int32_t( road.RoadID ), it.IndEntity );
}

void DiscoverSpotRoad( const SpotMap *pSpot ) {
    // FUN_0088e320
    if ( pSpot == nullptr ) {
        return;
    }

    int32_t roadID = pSpot->RoadID;
    int32_t segment = pSpot->RoadSegment;
    if ( pSpot->Type == 8 && pSpot->SubType >= 5 && pSpot->SubType <= 8 ) {
        OTDU_UNIMPLEMENTED; // TODO: AI
        return;
    }
    if ( roadID == -1 || segment == -1 ) {
        return;
    }

    IPlayerData *pSave = gpPlayerData->getLoadedSave();
    SavedRoadDiscovery *pRoads = pSave->getRoadDiscovery();
    const bool bWasLocked = pRoads->isRoadLocked( roadID, segment );
    pRoads->unlockRoad( roadID, segment );
    if ( bWasLocked ) {
        pSave->getAchievements()->notify(
            ACF_Challenge | ACF_Road | ACF_Collection, false );
    }
}

int32_t GetRoadZone( const uint32_t roadID ) {
    // FUN_0092ded0
    switch ( roadID ) {
    case 0x42c:
    case 0x455:
        return 1; // Oahu Raceway
    case 0x4a7:
    case 0x4a8:
    case 0x4a9:
    case 0x4aa:
        return 2; // Ford Island
    default:
        return 0;
    }
}

bool IsRoadAvailable( const uint32_t roadID ) {
    // FUN_0088d4b0
    if ( gpAI == nullptr || gpAI->getRoadMap().getRoad( roadID ) == nullptr ) {
        return false;
    }

    const AchievementsData *pAchievements =
        gpPlayerData->getLoadedSave()->getAchievements();
    switch ( GetRoadZone( roadID ) ) {
    case 0:
        return true;
    case 1:
        return pAchievements->isSimulationModeUnlocked();
    case 2:
        return pAchievements->isReward0x5912a0Unlocked();
    default:
        return false;
    }
}

float GetRoadNetworkLength( const AiRoadMap *pRoadMap,
                            const bool bIncludeLockedRoads ) {
    // FUN_0088d660
    float length = 0.0f;
    if ( pRoadMap == nullptr ) {
        return length;
    }

    for ( int32_t i = 0; i < pRoadMap->getNumRoads(); i++ ) {
        const AiRoadMap::Road &road = pRoadMap->getRoads()[i];
        const AiRoadMap::RoadDetail *pDetail = road.pDetail;
        if ( pDetail == nullptr ) {
            continue;
        }

        for ( int32_t j = 0; j < pDetail->NumEntities; j++ ) {
            if ( !bIncludeLockedRoads ) {
                const SavedRoadDiscovery *pDiscovery =
                    gpPlayerData->getLoadedSave()->getRoadDiscovery();
                if ( pDiscovery->isRoadLocked( road.ID, j ) ) {
                    continue;
                }
            }

            const AiRoadMap::Entity *pEntity = pDetail->pEntities[j];
            if ( pEntity != nullptr ) {
                length += static_cast<float>( pEntity->Length );
            }
        }
    }

    return length;
}
