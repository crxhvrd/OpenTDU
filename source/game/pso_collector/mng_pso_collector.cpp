#include "shared.h"
#include "mng_pso_collector.h"

#if OTDU_DEVBUILD
#include "ai/ai_pool.h"
#include "ai/ai_veh_controller.h"
#include "ai/ai_vehicle.h"
#include "ai/gs_ai.h"
#include "core/arg_parser.h"
#include "filesystem/gs_file.h"
#include "game/gm_world.h"
#include "database/gs_database.h"
#include "game/scenaric/gs_scenaric.h"
#include "game/vehicle/car_desc.h"
#include "game/vehicle/vehicle_base.h"
#include "tdu_instance.h"
#include "game/vehicle/car_state.h"
#include "physics/vehicle/physics_vehicle.h"
#include "render/camera/gs_camera.h"
#include "render/ed_render.h"
#include "render/vulkan/render_device.h"
#include "world/gs_world.h"
#include "world/weather/gs_weather.h"
#include "world/sector_manager.h"
#include "world/streaming_manager.h"

#include <queue>

bool gbPSOCollectorEnabled = false;

static CmdLineArg CmdLineArgPSOCollector( "psocollect", []( const char *pArg ) {
    gbPSOCollectorEnabled = true;
} );

// Lets the world and the player car settle before the first teleport
static constexpr float kStartDelay = 5.0f;

static constexpr float kMinSettleTime = 1.0f;
static constexpr int32_t kStreamingStateLoading = 2;
static constexpr float kSettleIdleTime = 1.0f;
static constexpr float kMaxSettleTime = 20.0f;

static constexpr float kMinDriveDistance = 400.0f;
static constexpr float kMaxDriveDistance = 3000.0f;
static constexpr float kMaxDriveTime = 90.0f;

static constexpr float kTeleportLaneWidthRatio = 0.5f;
static constexpr float kTeleportHeight = 0.5f;

// Parked stops cycle through every vehicle camera instead
static constexpr uint32_t kDriveEveryNStops = 4;
static constexpr uint32_t kNumVehicleViews = 4;
static constexpr float kLookViewTime = 1.0f;

static constexpr uint32_t kSwapCarEveryNStops = 3;
static constexpr uint32_t kRandomSeed = 0x5eed;

static constexpr float kStuckTime = 15.0f;
static constexpr float kStuckDisplacement = 30.0f;

// Teleports (or physics resets) are not part of the driven distance
static constexpr float kMaxStepDistance = 20.0f;
static constexpr float kLogInterval = 30.0f;
static constexpr float kCameraCycleInterval = 20.0f;

// The library is otherwise only saved once no new PSO was built for a while
static constexpr float kSaveInterval = 120.0f;

// Game seconds per second (a full day every 8 minutes)
static constexpr float kTimeScale = 180.0f;

// Road polylines are subdivided so that long curves do not skip sectors
static constexpr float kRoadSampleStep = 100.0f;

static bool ComputeSectorKey( const Eigen::Vector4f &position,
                              uint64_t &outKey ) {
    GridPos2D sector;
    GridPos2D area;
    if ( !gpWorld->getStreamingManager()->sectorManager.computeCell(
             position, sector, area ) ) {
        return false;
    }

    outKey = ( uint64_t( uint16_t( area.posX ) ) << 48 ) |
             ( uint64_t( uint16_t( area.posY ) ) << 32 ) |
             ( uint64_t( uint16_t( sector.posX ) ) << 16 ) |
             uint64_t( uint16_t( sector.posY ) );
    return true;
}

static inline bool IsDrivableFrom( const AiRoadMap::Road *pRoad,
                                   const AiRoadMap::Junction *pJunction ) {
    return ( pRoad->pJunctionBegin == pJunction && pRoad->MinRightLane > 0 ) ||
           ( pRoad->pJunctionEnd == pJunction && pRoad->MinLeftLane > 0 );
}

static inline AiRoadMap::Junction *
GetOtherJunction( const AiRoadMap::Road *pRoad,
                  const AiRoadMap::Junction *pJunction ) {
    return ( pRoad->pJunctionBegin == pJunction ) ? pRoad->pJunctionEnd
                                                  : pRoad->pJunctionBegin;
}

MngPSOCollector::MngPSOCollector()
    : Manager(),
      elapsedTime( 0.0f ),
      nextLogTime( kLogInterval ),
      nextCameraCycleTime( kCameraCycleInterval ),
      nextSaveTime( kSaveInterval ),
      distanceTravelled( 0.0f ),
      bAutopilotEnabled( false ),
      lastCarPosition( Eigen::Vector4f::Zero() ),
      numPSOsAtStart( 0 ),
      numPSOsAtLastLog( 0 ),
      distanceAtLastLog( 0.0f ),
      totalRoadLength( 0.0f ),
      lastSteeredRoadID( 0 ),
      state( eState::CS_Start ),
      stateTime( 0.0f ),
      settleIdleTime( 0.0f ),
      driveStartDistance( 0.0f ),
      driveStartPosition( Eigen::Vector4f::Zero() ),
      numStops( 0 ),
      numViewsLeft( 0 ),
      nextCarIndex( 0 ),
      random( kRandomSeed ),
      settleDuration( 0.0f ),
      maxFrameTime( 0.0f ) {}

MngPSOCollector::~MngPSOCollector() {}

void MngPSOCollector::reset() {
    elapsedTime = 0.0f;
    nextLogTime = kLogInterval;
    nextCameraCycleTime = kCameraCycleInterval;
    nextSaveTime = kSaveInterval;
    distanceTravelled = 0.0f;
    distanceAtLastLog = 0.0f;
    bAutopilotEnabled = false;
    numPSOsAtStart = gEdRender.pRenderDevice->getNumCachedPSOs();
    numPSOsAtLastLog = numPSOsAtStart;
    lastSteeredRoadID = 0;
    plannedPath.clear();
    state = eState::CS_Start;
    stateTime = 0.0f;
    driveStartDistance = 0.0f;
    numStops = 0;

    visitedSectors.clear();
    loadVisitedSectors();

    gpWeather->setTimeScale( kTimeScale );
}

void MngPSOCollector::tick( float deltaTime, float totalTime ) {
    elapsedTime += deltaTime;
    stateTime += deltaTime;

    const auto now = std::chrono::steady_clock::now();
    maxFrameTime =
        std::max( maxFrameTime,
                  std::chrono::duration<float>( now - lastTickTime ).count() );
    lastTickTime = now;

    CarState *pCar = gpScenaric->getLocalPlayer().getCarState();
    if ( pCar == nullptr ) {
        return;
    }

    if ( roadInfos.empty() ) {
        buildRoadInfos();
    }

    const Eigen::Vector4f carPosition =
        pCar->getWorldMatrix().row( 3 ).transpose();
    if ( state == eState::CS_Drive ) {
        const float step = ( carPosition - lastCarPosition ).head<3>().norm();
        if ( step <= kMaxStepDistance ) {
            distanceTravelled += step;
        } else {
            OTDU_LOG_INFO( "Car jumped %.0f m (%.0f %.0f %.0f "
                           "-> %.0f %.0f %.0f)\n",
                           step, lastCarPosition.x(), lastCarPosition.y(),
                           lastCarPosition.z(), carPosition.x(),
                           carPosition.y(), carPosition.z() );
        }

        uint64_t sectorKey = 0;
        if ( ComputeSectorKey( carPosition, sectorKey ) ) {
            visitedSectors.insert( sectorKey );
        }
    }
    lastCarPosition = carPosition;

    switch ( state ) {
    case eState::CS_Start:
        if ( stateTime >= kStartDelay ) {
            setState( eState::CS_Teleport );
        }
        break;

    case eState::CS_Teleport:
        setAutopilotEnabled( pCar, false );
        if ( teleportToNextRoad( carPosition ) ) {
            stopStartTime = now;
            maxFrameTime = 0.0f;
            setState( eState::CS_Settle );
        } else {
            OTDU_LOG_INFO( "Sweep complete (%u stops)\n", numStops );
            saveVisitedSectors();
            gEdRender.pRenderDevice->flushPSOCaches();
            setState( eState::CS_Done );
        }
        break;

    case eState::CS_Settle:
        // Wait for the destination to be fully streamed (or give up after a
        // while, in case something keeps streaming)
        if ( !isStreamingSettled() ) {
            settleIdleTime = 0.0f;
        } else {
            settleIdleTime += deltaTime;
        }
        if ( ( stateTime >= kMinSettleTime &&
               settleIdleTime >= kSettleIdleTime ) ||
             stateTime >= kMaxSettleTime ) {
            settleDuration =
                std::chrono::duration<float>( now - stopStartTime ).count();

            // The car may have fallen through the ground before its collision
            // was streamed
            gpActiveGM->teleportLocalPlayerCarToPose( teleportPose );
            if ( numStops % kDriveEveryNStops == 0 ) {
                setAutopilotEnabled( pCar, true );
                driveStartDistance = distanceTravelled;
                driveStartPosition = carPosition;
                setState( eState::CS_Drive );
            } else {
                numViewsLeft = kNumVehicleViews;
                setState( eState::CS_Look );
            }
        }
        break;

    case eState::CS_Drive: {
        steerAutopilot( pCar );

        if ( elapsedTime >= nextCameraCycleTime ) {
            nextCameraCycleTime = elapsedTime + kCameraCycleInterval;
            cycleCamera( pCar );
        }

        // Keep driving as long as the road ahead leads to unvisited sectors
        const float driveDistance = distanceTravelled - driveStartDistance;
        const float displacement =
            ( carPosition - driveStartPosition ).head<3>().norm();
        const bool bStuck =
            stateTime >= kStuckTime && displacement < kStuckDisplacement;
        const bool bDone = ( driveDistance >= kMinDriveDistance &&
                             !isOnUnvisitedRoad( pCar ) ) ||
                           driveDistance >= kMaxDriveDistance ||
                           stateTime >= kMaxDriveTime || bStuck;
        if ( bDone ) {
            numStops++;
            OTDU_LOG_INFO( "Stop %u driven (%.0f m driven, %.0f m "
                           "displacement, autopilot %d%s)\n",
                           numStops, driveDistance, displacement,
                           pCar->getAIController() != nullptr,
                           bStuck ? ", STUCK" : "" );
            logStopTimings( now );
            setState( eState::CS_Teleport );
        }
        break;
    }

    case eState::CS_Look:
        if ( stateTime >= kLookViewTime ) {
            stateTime = 0.0f;
            cycleCamera( pCar );
            if ( --numViewsLeft == 0 ) {
                numStops++;
                logStopTimings( now );
                setState( eState::CS_Teleport );
            }
        }
        break;

    case eState::CS_Done:
    default:
        break;
    }

    if ( elapsedTime >= nextLogTime ) {
        nextLogTime += kLogInterval;
        logProgress();
    }

    if ( elapsedTime >= nextSaveTime ) {
        nextSaveTime += kSaveInterval;
        saveVisitedSectors();
        gEdRender.pRenderDevice->flushPSOCaches();
    }
}

bool MngPSOCollector::isStreamingSettled() const {
    if ( gpFile->hasPendingAsyncRequests() ||
         gpTestDriveInstance->hasPendingInstanciation() ) {
        return false;
    }

    const StreamingManager *pStreaming = gpWorld->getStreamingManager();
    for ( const Sector &sector : pStreaming->sectorManager.sectors ) {
        if ( sector.state == kStreamingStateLoading ) {
            return false;
        }
    }
    for ( const Area &area : pStreaming->areaGrid.areas ) {
        if ( area.state == kStreamingStateLoading ) {
            return false;
        }
    }
    return true;
}

void MngPSOCollector::logStopTimings(
    const std::chrono::steady_clock::time_point &now ) {
    OTDU_LOG_INFO( "Stop %u took %.1f s (settle %.1f s, longest frame "
                   "%.0f ms)\n",
                   numStops,
                   std::chrono::duration<float>( now - stopStartTime ).count(),
                   settleDuration, maxFrameTime * 1000.0f );
}

void MngPSOCollector::setState( const eState newState ) {
    state = newState;
    stateTime = 0.0f;
    settleIdleTime = 0.0f;
}

void MngPSOCollector::setAutopilotEnabled( CarState *pCar,
                                           const bool bEnabled ) {
    if ( ( pCar->getAIController() != nullptr ) == bEnabled ) {
        return;
    }

    gpAI->getAIPool()->togglePlayerAutopilot( false );
    bAutopilotEnabled = ( pCar->getAIController() != nullptr );
    lastSteeredRoadID = 0;
    plannedPath.clear();
    if ( bAutopilotEnabled != bEnabled ) {
        OTDU_LOG_WARN( "Failed to %s the autopilot\n",
                       bEnabled ? "enable" : "disable" );
    }
}

bool MngPSOCollector::isOnUnvisitedRoad( CarState *pCar ) const {
    AiVehicle *pAutopilot = static_cast<AiVehicle *>( pCar->getAIController() );
    if ( pAutopilot == nullptr || pAutopilot->getController() == nullptr ) {
        return false;
    }

    AiRoadIt from;
    AiRoadIt to;
    if ( !pAutopilot->getController()->getFront( from, to ) ||
         to.pRoad == nullptr ) {
        return false;
    }
    return getNumUnvisitedSectors( to.pRoad ) != 0 || !plannedPath.empty();
}

bool MngPSOCollector::teleportToNextRoad( const Eigen::Vector4f &carPosition ) {
    const SectorSample *pBestSample = nullptr;
    uint64_t bestSectorKey = 0;
    float bestDistanceSq = std::numeric_limits<float>::max();
    for ( const auto &entry : roadSectors ) {
        if ( visitedSectors.find( entry.first ) != visitedSectors.end() ) {
            continue;
        }

        const float distanceSq =
            ( entry.second.Position - carPosition ).head<3>().squaredNorm();
        if ( distanceSq < bestDistanceSq ) {
            bestDistanceSq = distanceSq;
            pBestSample = &entry.second;
            bestSectorKey = entry.first;
        }
    }

    if ( pBestSample == nullptr ) {
        return false;
    }

    // Whatever happens next, never pick this sector again
    visitedSectors.insert( bestSectorKey );

    AiRoadMap::Road *pRoad = gpAI->getRoadMap().getRoad( pBestSample->RoadID );
    const bool bWay = pRoad->MinRightLane > 0;
    const AiRoadIt it{ pRoad, pBestSample->EntityIndex, pBestSample->CurveIndex,
                       bWay, 0.0f };
    const int32_t lane = it.getEntity()->pProfile->computeLaneFromWidthRatio(
        it.bWay, kTeleportLaneWidthRatio );

    Matrix4x4f_t pose;
    if ( !it.computeFrame( pose, lane ) ) {
        return true;
    }
    pose( 3, 1 ) += kTeleportHeight;

    teleportPose = pose;
    if ( numStops != 0 && numStops % kSwapCarEveryNStops == 0 ) {
        swapCar( pose );
    } else {
        gpActiveGM->teleportLocalPlayerCarToPose( pose );
    }
    return true;
}

void MngPSOCollector::swapCar( const Matrix4x4f_t &pose ) {
    if ( carIDs.empty() ) {
        const GameDatabase &gameDB = gpDatabase->getGameDB();
        for ( uint32_t i = 0; i < gameDB.pNumItems[1]; i++ ) {
            carIDs.push_back( int32_t( gameDB.DBCarData[i].CarID ) );
        }
    }

    gpActiveGM->unloadLocalPlayerCar();

    // Cars without resources (e.g. unreleased DB entries) are skipped
    for ( size_t attempt = 0; attempt < carIDs.size(); attempt++ ) {
        const int32_t carID = carIDs[nextCarIndex % carIDs.size()];
        nextCarIndex = ( nextCarIndex + 1 ) % carIDs.size();

        // Rims are separate geometry (picked at load time)
        const int32_t numRims = gpDatabase->getNumRims( carID );
        const int32_t rimID =
            ( numRims > 0 ) ? randomInt( -1, numRims - 1 ) : -1;
        if ( gpActiveGM->respawnLocalPlayerCarWithID( pose, carID, rimID ) ) {
            randomizePaint( carID, rimID );
            const char *pName = "?";
            for ( const CarConfig &car : gpDatabase->getCarList() ) {
                if ( car.Hash == carID ) {
                    pName = car.FullName.c_str();
                    break;
                }
            }
            OTDU_LOG_INFO( "Car %zu/%zu: %d (%s), rim %d/%d, %s\n",
                           nextCarIndex, carIDs.size(), carID, pName, rimID,
                           numRims, lastPaintDescription.c_str() );
            return;
        }
        OTDU_LOG_WARN( "Failed to spawn car %d\n", carID );
    }

    gpActiveGM->respawnLocalPlayerCar( pose );
}

// 'OTPCOL' + version 1
static constexpr uint64_t kResumeFileMagic = 0x01004c4f4350544full;

int32_t MngPSOCollector::randomInt( const int32_t min, const int32_t max ) {
    return std::uniform_int_distribution<int32_t>( min, max )( random );
}

void MngPSOCollector::randomizePaint( const int32_t carID,
                                      const int32_t rimID ) {
    CarState *pCar = gpScenaric->getLocalPlayer().getCarState();
    VehicleBase *pVehicle =
        ( pCar != nullptr ) ? pCar->getVehicleInstance() : nullptr;
    if ( pVehicle == nullptr ) {
        return;
    }

    // Body/interior colors are colors.bnk materials (which may use different
    // shaders); custom (deluxe) paints are applied on top
    const int32_t numColors = gpDatabase->getNumColors( carID );
    const int32_t paintIndex =
        ( numColors > 0 ) ? randomInt( 0, numColors - 1 ) : 0;
    DBEntryCarColor *pColor = gpDatabase->getColorByIndex( carID, paintIndex );
    const int32_t numInteriors =
        ( pColor != nullptr ) ? gpDatabase->getNumInteriors( pColor ) : 0;
    const int32_t interiorIndex =
        ( numInteriors > 0 ) ? randomInt( 0, numInteriors - 1 ) : 0;

    const ePaintType paintType = ( randomInt( 0, 1 ) == 0 )
                                     ? ePaintType::PT_Default
                                     : ePaintType( randomInt( 1, 4 ) );
    auto randomColor = [&]() {
        return ColorRGBA( uint8_t( randomInt( 0, 255 ) ),
                          uint8_t( randomInt( 0, 255 ) ),
                          uint8_t( randomInt( 0, 255 ) ) );
    };
    const ColorRGBA mainColor = randomColor();
    const ColorRGBA secondColor = randomColor();
    const ColorRGBA logoColor = randomColor();
    const ColorRGBA secondColor2 = randomColor();

    pVehicle->applyPaint( uint32_t( carID ), paintType, paintIndex, rimID,
                          interiorIndex, &mainColor, &secondColor, &logoColor,
                          &secondColor2 );

    char description[96];
    snprintf( description, sizeof( description ),
              "paint %d/%d type %d, interior %d/%d", paintIndex, numColors,
              int32_t( paintType ), interiorIndex, numInteriors );
    lastPaintDescription = description;
}

static std::filesystem::path GetVisitedSectorsPath() {
    return std::filesystem::path( gpFile->getSaveFolder() ) / "cache" /
           "pso_collector.bin";
}

void MngPSOCollector::loadVisitedSectors() {
    std::ifstream file( GetVisitedSectorsPath(),
                        std::ios::binary | std::ios::ate );
    if ( !file.is_open() ) {
        return;
    }

    std::vector<uint64_t> words( static_cast<size_t>( file.tellg() ) /
                                 sizeof( uint64_t ) );
    file.seekg( 0 );
    file.read( reinterpret_cast<char *>( words.data() ),
               words.size() * sizeof( uint64_t ) );
    if ( file && words.size() >= 2 && words[0] == kResumeFileMagic ) {
        nextCarIndex = size_t( words[1] );
        visitedSectors.insert( words.begin() + 2, words.end() );
        OTDU_LOG_INFO( "Resuming (%zu sectors visited)\n",
                       visitedSectors.size() );
    }
}

void MngPSOCollector::saveVisitedSectors() {
    std::vector<uint64_t> words = { kResumeFileMagic,
                                    uint64_t( nextCarIndex ) };
    words.insert( words.end(), visitedSectors.begin(), visitedSectors.end() );
    std::ofstream file( GetVisitedSectorsPath(),
                        std::ios::binary | std::ios::trunc );
    file.write( reinterpret_cast<const char *>( words.data() ),
                words.size() * sizeof( uint64_t ) );
}

void MngPSOCollector::buildRoadInfos() {
    AiRoadMap &roadMap = gpAI->getRoadMap();
    totalRoadLength = 0.0f;

    for ( int32_t roadID = 1; roadID <= roadMap.getNumRoads(); roadID++ ) {
        const AiRoadMap::Road *pRoad = roadMap.getRoad( uint32_t( roadID ) );
        if ( pRoad == nullptr || pRoad->pDetail == nullptr ) {
            continue;
        }

        RoadInfos &infos = roadInfos[pRoad->ID];
        std::unordered_set<uint64_t> sectors;
        const bool bDrivable =
            pRoad->MinRightLane > 0 || pRoad->MinLeftLane > 0;
        int32_t entityIndex = 0;
        int32_t curveIndex = 0;
        auto addPoint = [&]( const Eigen::Vector4f &position ) {
            uint64_t key = 0;
            if ( !ComputeSectorKey( position, key ) ) {
                return;
            }
            sectors.insert( key );
            if ( bDrivable && roadSectors.find( key ) == roadSectors.end() ) {
                roadSectors[key] = { pRoad->ID, entityIndex, curveIndex,
                                     position };
            }
        };

        const AiRoadMap::RoadDetail *pDetail = pRoad->pDetail;
        for ( uint32_t e = 0; e < pDetail->NumEntities; e++ ) {
            const AiRoadMap::Entity *pEntity = pDetail->pEntities[e];
            Eigen::Vector4f previous = pEntity->FirstCV1.toVector4();
            entityIndex = int32_t( e );
            curveIndex = 0;
            addPoint( previous );

            const int32_t numCurves = pEntity->getNumExtraCurves() + 1;
            for ( int32_t c = 0; c < numCurves; c++ ) {
                curveIndex = c;
                const Eigen::Vector4f next = pEntity->Curves[c].CV4.toVector4();
                const float length = ( next - previous ).head<3>().norm();
                const int32_t numSteps =
                    std::max( 1, int32_t( length / kRoadSampleStep ) );
                for ( int32_t s = 1; s <= numSteps; s++ ) {
                    addPoint( previous +
                              ( next - previous ) * ( float( s ) / numSteps ) );
                }
                infos.Length += length;
                previous = next;
            }
        }

        infos.Sectors.assign( sectors.begin(), sectors.end() );
        totalRoadLength += infos.Length;
    }

    OTDU_LOG_INFO( "%zu roads, %.0f km, %zu road sectors\n", roadInfos.size(),
                   totalRoadLength / 1000.0f, roadSectors.size() );
}

uint32_t
MngPSOCollector::getNumUnvisitedSectors( const AiRoadMap::Road *pRoad ) const {
    auto it = roadInfos.find( pRoad->ID );
    if ( it == roadInfos.end() ) {
        return 0;
    }

    uint32_t numUnvisited = 0;
    for ( const uint64_t key : it->second.Sectors ) {
        if ( visitedSectors.find( key ) == visitedSectors.end() ) {
            numUnvisited++;
        }
    }
    return numUnvisited;
}

void MngPSOCollector::steerAutopilot( CarState *pCar ) {
    AiVehicle *pAutopilot = static_cast<AiVehicle *>( pCar->getAIController() );
    if ( pAutopilot == nullptr || pAutopilot->getController() == nullptr ) {
        return;
    }

    AiVehController *pController = pAutopilot->getController();
    AiRoadIt from;
    AiRoadIt to;
    if ( !pController->getFront( from, to ) || to.pRoad == nullptr ||
         to.pRoad->ID == lastSteeredRoadID ) {
        return;
    }
    lastSteeredRoadID = to.pRoad->ID;

    AiRoadMap::Junction *pJunction =
        to.bWay ? to.pRoad->pJunctionEnd : to.pRoad->pJunctionBegin;
    if ( pJunction == nullptr ) {
        return;
    }

    const uint32_t nextRoadID = pickNextRoad( pJunction, to.pRoad );
    if ( nextRoadID != 0 ) {
        pController->forceNextRoad( nextRoadID );
    }
}

uint32_t MngPSOCollector::pickNextRoad( AiRoadMap::Junction *pJunction,
                                        const AiRoadMap::Road *pCurrentRoad ) {
    uint32_t bestRoadID = 0;
    uint32_t bestNumUnvisited = 0;
    for ( int32_t i = 0; i < pJunction->NumRoads; i++ ) {
        const AiRoadMap::Road *pRoad = pJunction->getRoad( i );
        if ( pRoad->ID == pCurrentRoad->ID ||
             !IsDrivableFrom( pRoad, pJunction ) ||
             GetOtherJunction( pRoad, pJunction ) == nullptr ) {
            continue;
        }

        const uint32_t numUnvisited = getNumUnvisitedSectors( pRoad );
        if ( numUnvisited > bestNumUnvisited ) {
            bestNumUnvisited = numUnvisited;
            bestRoadID = pRoad->ID;
        }
    }

    if ( bestRoadID != 0 ) {
        plannedPath.clear();
        return bestRoadID;
    }

    auto isPlannedRoadAtJunction = [&]() {
        for ( int32_t i = 0; i < pJunction->NumRoads; i++ ) {
            if ( pJunction->getRoad( i )->ID == plannedPath.front() ) {
                return true;
            }
        }
        return false;
    };
    if ( plannedPath.empty() || !isPlannedRoadAtJunction() ) {
        if ( !planPathToUnvisitedRoad( pJunction ) ) {
            return 0;
        }
    }

    const uint32_t nextRoadID = plannedPath.front();
    plannedPath.erase( plannedPath.begin() );
    return nextRoadID;
}

bool MngPSOCollector::planPathToUnvisitedRoad( AiRoadMap::Junction *pStart ) {
    struct Node {
        float Distance;
        AiRoadMap::Junction *pJunction;
        bool operator>( const Node &other ) const {
            return Distance > other.Distance;
        }
    };
    struct Visit {
        float Distance;
        AiRoadMap::Junction *pPrevious;
        uint32_t RoadID;
    };

    std::priority_queue<Node, std::vector<Node>, std::greater<Node>> queue;
    std::unordered_map<AiRoadMap::Junction *, Visit> visits;
    visits[pStart] = { 0.0f, nullptr, 0 };
    queue.push( { 0.0f, pStart } );

    while ( !queue.empty() ) {
        const Node node = queue.top();
        queue.pop();
        if ( node.Distance > visits[node.pJunction].Distance ) {
            continue;
        }

        const Visit &visit = visits[node.pJunction];
        const AiRoadMap::Road *pIncomingRoad =
            ( visit.RoadID != 0 ) ? gpAI->getRoadMap().getRoad( visit.RoadID )
                                  : nullptr;
        if ( pIncomingRoad != nullptr &&
             getNumUnvisitedSectors( pIncomingRoad ) != 0 ) {
            plannedPath.clear();
            for ( AiRoadMap::Junction *pJunction = node.pJunction;
                  visits[pJunction].pPrevious != nullptr;
                  pJunction = visits[pJunction].pPrevious ) {
                plannedPath.insert( plannedPath.begin(),
                                    visits[pJunction].RoadID );
            }
            return true;
        }

        for ( int32_t i = 0; i < node.pJunction->NumRoads; i++ ) {
            const AiRoadMap::Road *pRoad = node.pJunction->getRoad( i );
            AiRoadMap::Junction *pNext =
                GetOtherJunction( pRoad, node.pJunction );
            if ( pNext == nullptr ||
                 !IsDrivableFrom( pRoad, node.pJunction ) ) {
                continue;
            }

            auto infosIt = roadInfos.find( pRoad->ID );
            const float length =
                ( infosIt != roadInfos.end() ) ? infosIt->second.Length : 0.0f;
            const float distance = node.Distance + length;
            auto visitIt = visits.find( pNext );
            if ( visitIt == visits.end() ||
                 distance < visitIt->second.Distance ) {
                visits[pNext] = { distance, node.pJunction, pRoad->ID };
                queue.push( { distance, pNext } );
            }
        }
    }

    OTDU_LOG_INFO( "No reachable unvisited road left\n" );
    return false;
}

void MngPSOCollector::cycleCamera( CarState *pCar ) {
    PhysicsVehicle *pPhysics = pCar->getPhysicsVehicle();
    if ( pPhysics != nullptr ) {
        pPhysics->setChangeView( true );
    }
}

void MngPSOCollector::logProgress() {
    uint32_t numVisitedRoadSectors = 0;
    for ( const uint64_t key : visitedSectors ) {
        numVisitedRoadSectors +=
            ( roadSectors.find( key ) != roadSectors.end() );
    }

    CameraVehicle *pCamera = gpCamera->getActiveVehicleCamera();
    const size_t numPSOs = gEdRender.pRenderDevice->getNumCachedPSOs();
    OTDU_LOG_INFO(
        "%.0f s, %u stops, %.1f km (+%.1f), road sectors "
        "%u/%zu, %zu PSOs "
        "(+%zu since last log, +%zu total), camera type %d, time %02d:%02d\n",
        elapsedTime, numStops, distanceTravelled / 1000.0f,
        ( distanceTravelled - distanceAtLastLog ) / 1000.0f,
        numVisitedRoadSectors, roadSectors.size(), numPSOs,
        numPSOs - numPSOsAtLastLog, numPSOs - numPSOsAtStart,
        ( pCamera != nullptr ) ? pCamera->getType() : -1,
        int32_t( gpWeather->getCurrentTime().Hours ),
        int32_t( gpWeather->getCurrentTime().Minutes ) );
    numPSOsAtLastLog = numPSOs;
    distanceAtLastLog = distanceTravelled;
}
#endif
