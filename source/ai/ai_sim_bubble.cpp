#include "shared.h"
#include "ai_sim_bubble.h"

#include "ai/ai_bot_manager.h"
#include "ai/ai_config.h"
#include "ai/ai_opt_path.h"
#include "ai/ai_pool.h"
#include "ai/gs_ai.h"
#include "core/cubic_bezier.h"
#include "gs_timer.h"
#include "game/scenaric/gs_scenaric.h"
#include "game/vehicle/car_state.h"
#include "game/vehicle/gs_car_state.h"
#include "physics/gs_physics.h"
#include "player_data/saved_road_discovery.h"
#include "render/camera/camera_game.h"
#include "render/camera/gs_camera.h"

AiSimBubble gAiSimBubble; // DAT_01476a30

static constexpr int32_t kNumCurveSamples = 10;
static constexpr int32_t kMaxCircleHits = 8;
static constexpr float kCandidateMinDistanceSq = 2500.0f; // DAT_00f41c5c
static constexpr float kPlayerRoadWeightScale = 10.0f;    // DAT_00faa9e0
static constexpr float kFrontSeenScale = 3.0f;            // DAT_00f416b4
static constexpr float kRearSeenScale = 2.0f;             // DAT_00f416b0
static constexpr float kCullVisibilityMax = 1.5f;         // DAT_00f416b8
static constexpr float kLeastVisibleScoreMax = 1000.0f;   // DAT_00f41718
static constexpr float kIgnoredZoneDensity = 4.0f;        // DAT_00f418d4
static constexpr float kPerLaneKm = 0.001f;               // DAT_00f4167c

static float
RandomFloat( RandomGen &random, const float min, const float max ) {
    // FUN_006d9b70
    return min + RandomUnit( random ) * ( max - min );
}

static Eigen::Vector4f EvaluateCurve( const PhysicsRoadCurve &curve,
                                      const float t ) {
    if ( curve.bCubic ) {
        CubicBezier bezier;
        for ( int32_t i = 0; i < 4; i++ ) {
            bezier.ControlPoints[i] = curve.ControlPoints[i];
        }
        return bezier.evaluate( t );
    }
    QuadraticBezier bezier;
    for ( int32_t i = 0; i < 3; i++ ) {
        bezier.ControlPoints[i] = curve.ControlPoints[i];
    }
    return bezier.evaluate( t );
}

static AiRoadIt RecordToIt( const PhysicsRoadCurve &curve, const float t ) {
    // FUN_009174b0
    AiRoadMap::Road *pRoad = gpAI->getRoadMap().getRoad( curve.RoadID );
    if ( pRoad == nullptr ) {
        return AiRoadIt();
    }
    const int32_t numEntities = pRoad->pDetail->NumEntities;
    const int32_t entity = ( curve.EntityIndex >= numEntities )
                               ? numEntities - 1
                               : curve.EntityIndex;
    const int32_t numCurves =
        ( pRoad->pDetail->pEntities[entity]->NumVertex + 2 ) / 3;
    const int32_t curveIndex = ( curve.CurveIndex >= numCurves - 1 )
                                   ? numCurves - 2
                                   : curve.CurveIndex;
    return AiRoadIt{ pRoad, entity, curveIndex, true, fabsf( t ) };
}

static float ComputePolylineLength( const Eigen::Vector2f *pSamples ) {
    // FUN_0091ad20
    float length = 0.0f;
    for ( int32_t i = 0; i < kNumCurveSamples - 1; i++ ) {
        length += ( pSamples[i + 1] - pSamples[i] ).norm();
    }
    return length;
}

static int32_t IntersectCurveCircle( float *pOutT,
                                     const int32_t maxHits,
                                     float &outInside,
                                     const Eigen::Vector2f &centre,
                                     const float radiusSq,
                                     const Eigen::Vector2f *pControlPoints ) {
    // FUN_0091b1c0
    bool bAnyInside = false;
    bool bAnyOutside = false;
    for ( int32_t i = 0; i < 4; i++ ) {
        if ( ( pControlPoints[i] - centre ).squaredNorm() < radiusSq ) {
            bAnyInside = true;
        } else {
            bAnyOutside = true;
        }
    }

    Eigen::Vector2f samples[kNumCurveSamples];
    float sampleT[kNumCurveSamples];
    for ( int32_t k = 0; k < kNumCurveSamples; k++ ) {
        const float t = static_cast<float>( k ) / 9.0f;
        const float u = 1.0f - t;
        sampleT[k] = t;
        samples[k] = pControlPoints[0] * ( u * u * u ) +
                     pControlPoints[1] * ( 3.0f * t * u * u ) +
                     pControlPoints[2] * ( 3.0f * t * t * u ) +
                     pControlPoints[3] * ( t * t * t );
    }
    samples[0] = pControlPoints[0];
    samples[kNumCurveSamples - 1] = pControlPoints[3];

    if ( !bAnyInside ) {
        outInside = 0.0f;
        return 0;
    }
    if ( !bAnyOutside ) {
        outInside = ComputePolylineLength( samples );
        return 0;
    }

    int32_t numHits = 0;
    float inside = 0.0f;
    for ( int32_t k = 0; k < kNumCurveSamples - 1; k++ ) {
        const float d0 = ( samples[k] - centre ).squaredNorm();
        const float d1 = ( samples[k + 1] - centre ).squaredNorm();
        const float length = ( samples[k + 1] - samples[k] ).norm();
        const float dt = sampleT[k + 1] - sampleT[k];
        if ( d0 <= radiusSq && d1 >= radiusSq ) {
            const float f = ( d1 - radiusSq ) / ( d1 - d0 );
            pOutT[numHits++] = dt * ( 1.0f - f ) + sampleT[k];
            // sic: retail adds the outside fraction when leaving
            inside += f * length;
            if ( numHits == maxHits ) {
                break;
            }
        } else if ( radiusSq >= d1 && d0 >= radiusSq ) {
            const float f = ( d0 - radiusSq ) / ( d0 - d1 );
            pOutT[numHits++] = dt * f + sampleT[k];
            inside += ( 1.0f - f ) * length;
            if ( numHits == maxHits ) {
                break;
            }
        } else if ( radiusSq >= d0 && radiusSq >= d1 ) {
            inside += length;
        }
    }
    outInside = inside;
    return numHits;
}

static void FixWayForLanes( AiRoadIt &it ) {
    const AiRoadMap::Entity *pEntity = it.getEntity();
    const uint8_t numLanes =
        it.bWay ? pEntity->NumRightLanes : pEntity->NumLeftLanes;
    if ( numLanes == 0 ) {
        it.flip();
    }
}

AiSimBubble::AiSimBubble()
    : random(),
      position( Eigen::Vector4f::Zero() ),
      radius( 0.0f ),
      velocity( Eigen::Vector4f::Zero() ),
      bInitialized( false ),
      bubbleIt(),
      pCamera( nullptr ),
      pRearCamera( nullptr ),
      candidates(),
      altWeightSum( 0.0f ),
      roadLengthInside( 0.0f ),
      laneLengthInside( 0.0f ),
      zoneWeights{},
      maxWeight( 0.0f ),
      minWeight( 1.0f ),
      numTraffic( 0 ),
      targetTraffic( 0 ),
      numVisibleHits( 0 ),
      targetVisible( 0 ),
      onlineSpawnCredits( 0 ),
      spawnConfig{ 0.0f, 0.1f, 1.0f },
      spawnInterval( 0.1f ),
      lastSpawnTick( -1.0f ),
      frontSeenMemory( 10.0f ),
      rearSeenMemory( 10.0f ),
      lastOnlineUpdateTime( 0.0f ),
      lastVisibleCullTime( 0.0f ),
      bCullFromCamera( false ),
      bOcclusionRaycasts( false ),
      currentZoneIndex( 0 ),
      pCurrentZone( &gAIConfig.Zones[0] ) {
    // FUN_00917020
    OTDU_UNIMPLEMENTED_ONCE; // TODO: stats history
    random.setSeed( 0x3039 );
}

void AiSimBubble::initialize() {
    // FUN_00916e00
    if ( bInitialized ) {
        return;
    }

    for ( float &weight : zoneWeights ) {
        weight = 0.0f;
    }
    currentZoneIndex = 0;
    pCurrentZone = &gAIConfig.Zones[0];
    spawnConfig[0] = gAIConfig.SpawnConfig[0];
    spawnConfig[1] = gAIConfig.SpawnConfig[1];
    spawnConfig[2] = gAIConfig.SpawnConfig[2];
    radius = gAIConfig.SpawnDistances[1];
    position = Eigen::Vector4f( 0.0f, 0.0f, 0.0f, 1.0f );

    candidates.clear();
    candidates.reserve( kMaxNumCandidates );
    altWeightSum = 0.0f;
    roadLengthInside = 0.0f;
    laneLengthInside = 0.0f;

    random.setSeed( 0x12345678 );
    lastOnlineUpdateTime = gpTimer->GameTotalTime;
    lastVisibleCullTime = gpTimer->GameTotalTime;
    bInitialized = true;
}

void AiSimBubble::tick( const float deltaTime, const float totalTime ) {
    // FUN_0091a760
    if ( deltaTime <= 0.0f || gpCarState == nullptr || gpAI == nullptr ) {
        return;
    }

    CarState *pLocalCar = gpScenaric->getLocalPlayer().getCarState();
    updateCameras();
    if ( pLocalCar != nullptr && !bCullFromCamera ) {
        velocity = pLocalCar->getLinearVelocity();
        velocity.y() = 0.0f;
        velocity.w() = 0.0f;
        position = pLocalCar->getWorldPosition();

        const ObjectWorldLocation &location = pLocalCar->getWorldLocation();
        AiRoadMap::Road *pRoad =
            ( location.LocationType == ObjectWorldLocation::OWL_Road )
                ? gpAI->getRoadMap().getRoad( location.LocationRoad.RoadID )
                : nullptr;
        if ( pRoad != nullptr ) {
            pRoad->getShape()->computeIt(
                ( location.LocationRoad.FractionMin +
                  location.LocationRoad.FractionMax ) *
                    0.5f,
                bubbleIt );
            bubbleIt.setWay( location.bWay != 0 );
        } else {
            bubbleIt = AiRoadIt();
        }
    } else if ( pCamera != nullptr ) {
        velocity = Eigen::Vector4f::Zero();
        position = pCamera->getCamToWorld().row( 3 ).transpose();
    }

    radius = velocity.norm() * gAIConfig.SpawnDistances[0];
    if ( radius < gAIConfig.SpawnDistances[1] ) {
        radius = gAIConfig.SpawnDistances[1];
    } else if ( radius > gAIConfig.SpawnDistances[2] ) {
        radius = gAIConfig.SpawnDistances[2];
    }

    selectZone();
    updateTrafficVisibility( deltaTime, totalTime );
    updateOtherVisibility( totalTime );
    AiPool *pPool = gpAI->getAIPool();
    if ( pPool->getOpponentPickFrame() == 0 ||
         pPool->getNextOpponentIndex() == -1 ) {
        buildCandidates();
    }
    OTDU_UNIMPLEMENTED_ONCE; // TODO: cop spawn (FUN_0091a290)
    spawnTraffic( totalTime );
    spawnBots( deltaTime );
}

void AiSimBubble::selectZone() {
    // FUN_00917cd0
    float bestWeight = std::max( zoneWeights[0], 0.0f );
    int32_t bestZone = 0;
    for ( int32_t i = 1; i < kNumZoneWeights; i++ ) {
        if ( bestWeight < zoneWeights[i] ) {
            bestWeight = zoneWeights[i];
            bestZone = i;
        }
    }
    if ( bestZone >= gAIConfig.NumZones ) {
        bestZone = 0;
    }

    if ( bestZone != currentZoneIndex ) {
        currentZoneIndex = bestZone;
        pCurrentZone = &gAIConfig.Zones[bestZone];
        gpAI->getAIPool()->getTraffic().refreshFrequencies();
    }
}

void AiSimBubble::updateCameras() {
    // FUN_00918690
    CameraGame *pActiveCamera =
        ( gpCamera != nullptr ) ? gpCamera->getActiveCamera() : nullptr;
    OTDU_UNIMPLEMENTED; // TODO: camera modes 1/2
    pCamera = pActiveCamera;

    pRearCamera = nullptr;
    CarState *pLocalCar = gpScenaric->getLocalPlayer().getCarState();
    if ( pActiveCamera != nullptr && pLocalCar != nullptr ) {
        const uint32_t flags = pActiveCamera->getFlags();
        if ( ( ( flags >> 18 ) & 1 ) != 0 || ( ( flags >> 1 ) & 1 ) != 0 ) {
            OTDU_UNIMPLEMENTED_ONCE; // TODO: rear / mirror culling camera
        }
    }
}

float AiSimBubble::computeVisibility( AiVehicle *pVehicle,
                                      const float totalTime ) {
    // FUN_009171f0
    AiVehicleInfo &infos = pVehicle->getInfos();
    CarState *pCarState = pVehicle->getCarState();
    if ( pCamera != nullptr ) {
        if ( pCarState->isInFrustum( pCamera, -1.0f, nullptr ) ) {
            infos.LastTimeSeenFront = totalTime;
            numVisibleHits++;
        }
        if ( pRearCamera != nullptr &&
             pCarState->isInFrustum( pRearCamera, -1.0f, nullptr ) ) {
            infos.LastTimeSeenRear = totalTime;
            numVisibleHits++;
        }
    }

    float frontTerm = 0.0f;
    if ( infos.LastTimeSeenFront >= 0.0f ) {
        frontTerm = ( 1.0f - std::clamp( totalTime - infos.LastTimeSeenFront,
                                         0.0f, frontSeenMemory ) /
                                 frontSeenMemory ) *
                    kFrontSeenScale;
    }
    // sic: no "ever seen" test on the rear term
    const float rearTerm =
        ( 1.0f - std::clamp( totalTime - infos.LastTimeSeenRear, 0.0f,
                             rearSeenMemory ) /
                     rearSeenMemory ) *
        kRearSeenScale;
    const float visibility = std::max( frontTerm + rearTerm, 1.0f );
    if ( pCamera == nullptr ) {
        return visibility;
    }

    const float distance = ( pCamera->getCamToWorld().row( 3 ).transpose() -
                             pCarState->getWorldPosition() )
                               .norm();
    return ( 1.0f - std::clamp( distance, 0.0f, gAIConfig.CullingFovDistance ) /
                        gAIConfig.CullingFovDistance ) *
           visibility;
}

void AiSimBubble::updateTrafficVisibility( const float deltaTime,
                                           const float totalTime ) {
    // FUN_009182f0
    if ( deltaTime <= 0.0f || gpAI == nullptr || gpCamera == nullptr ) {
        return;
    }

    AiPool *pPool = gpAI->getAIPool();
    numVisibleHits = 0;
    targetVisible = std::min( targetVisible, pPool->getMaxTraffic() );
    AiVehicleList &trafficList =
        pPool->getList( AiPool::AVL_FirstLocomotion + 0 );
    numTraffic = trafficList.Count;
    for ( AiVehicle *pVehicle : trafficList ) {
        pVehicle->getInfos().Visibility =
            computeVisibility( pVehicle, totalTime );
        if ( bOcclusionRaycasts ) {
            OTDU_UNIMPLEMENTED_ONCE; // TODO: occlusion raycasts (FUN_009180c0)
        }
        pVehicle->getInfos().bVisible = true;
    }
    OTDU_UNIMPLEMENTED_ONCE; // TODO: online zone update (FUN_00917d90)
}

void AiSimBubble::updateOtherVisibility( const float totalTime ) {
    // FUN_00918290
    for ( AiVehicle *pVehicle : gpAI->getAIPool()->getVehicles() ) {
        if ( pVehicle->getCategory() != eAiVehicleCategory::AVC_Traffic ) {
            pVehicle->getInfos().Visibility =
                computeVisibility( pVehicle, totalTime );
        }
    }
}

void AiSimBubble::buildCandidates() {
    // FUN_00918eb0
    const Eigen::Vector2f centre( position.x(), position.z() );
    const float radiusSq = radius * radius;

    candidates.clear();
    maxWeight = 0.0f;
    minWeight = 1.0f;
    roadLengthInside = 0.0f;
    laneLengthInside = 0.0f;
    for ( float &weight : zoneWeights ) {
        weight = 0.0f;
    }

    float weightSum = 0.0f;
    PhysicsLock lock;
    for ( const PhysicsRoadCurve &curve : gpPhysics->getRoadCurves() ) {
        Eigen::Vector2f controlPoints[4];
        for ( int32_t i = 0; i < 4; i++ ) {
            controlPoints[i] = Eigen::Vector2f( curve.ControlPoints[i].x(),
                                                curve.ControlPoints[i].z() );
        }

        float hitT[kMaxCircleHits];
        float inside = 0.0f;
        int32_t numHits = IntersectCurveCircle(
            hitT, kMaxCircleHits, inside, centre, radiusSq, controlPoints );
        if ( inside > 0.0f && numHits == 0 ) {
            const AiRoadIt it = RecordToIt( curve, 0.5f );
            if ( it.pRoad != nullptr ) {
                if ( it.isAtRoadStart() ) {
                    const AiRoadMap::Junction *pJunction =
                        it.bWay ? it.pRoad->pJunctionBegin
                                : it.pRoad->pJunctionEnd;
                    if ( pJunction != nullptr &&
                         pJunction->crossesCircle( centre, radiusSq ) ) {
                        hitT[0] = 0.0f;
                        numHits = 1;
                    }
                }
                if ( it.isAtRoadEnd() ) {
                    const AiRoadMap::Junction *pJunction =
                        it.bWay ? it.pRoad->pJunctionEnd
                                : it.pRoad->pJunctionBegin;
                    if ( pJunction != nullptr &&
                         pJunction->crossesCircle( centre, radiusSq ) ) {
                        hitT[numHits++] = 1.0f;
                    }
                }
            }
        }

        if ( numHits > 0 ) {
            if ( numHits > 1 ) {
                const float lastT = hitT[numHits - 1];
                const AiRoadIt it = RecordToIt( curve, lastT );
                weightSum += addCandidate( it, curve, lastT, true );
                weightSum += addCandidate( it, curve, lastT, false );
            }
            const AiRoadIt it = RecordToIt( curve, hitT[0] );
            weightSum += addCandidate( it, curve, hitT[0], true );
            weightSum += addCandidate( it, curve, hitT[0], false );
        }

        if ( inside > 0.0f ) {
            const AiRoadMap::Road *pRoad =
                gpAI->getRoadMap().getRoad( curve.RoadID );
            if ( pRoad == nullptr || pRoad->pDetail == nullptr ||
                 curve.EntityIndex >= pRoad->pDetail->NumEntities ) {
                continue;
            }
            const AiRoadMap::Entity *pEntity =
                pRoad->pDetail->pEntities[curve.EntityIndex];
            roadLengthInside += inside;
            laneLengthInside +=
                inside * static_cast<float>( pEntity->pProfile->NumLeftLanes +
                                             pEntity->pProfile->NumRightLanes );
            zoneWeights[pEntity->DensityZoneIndice] += inside;
        }
    }

    altWeightSum = 0.0f;
    if ( !candidates.empty() ) {
        const float invWeightSum = 1.0f / weightSum;
        for ( Candidate &candidate : candidates ) {
            candidate.Weight *= invWeightSum;
            candidate.AltWeight *= invWeightSum;
            altWeightSum += candidate.AltWeight;
        }
        minWeight *= invWeightSum;
        maxWeight *= invWeightSum;
    }
}

float AiSimBubble::addCandidate( const AiRoadIt &source,
                                 const PhysicsRoadCurve &curve,
                                 const float t,
                                 const bool bWay ) {
    // FUN_009189a0
    if ( source.pRoad == nullptr ) {
        return 0.0f;
    }
    const float absT = fabsf( t );
    AiRoadIt it = source;
    it.setWay( bWay );
    const AiRoadMap::Entity *pEntity = it.getEntity();
    if ( pEntity->pProfile == nullptr ) {
        return 0.0f;
    }

    Eigen::Vector4f worldPos = EvaluateCurve( curve, absT );
    worldPos.w() = 1.0f;
    OTDU_UNIMPLEMENTED_ONCE; // TODO: online
    for ( const Candidate &other : candidates ) {
        if ( other.It.pRoad->ID == it.pRoad->ID && other.It.bWay == bWay &&
             ( other.WorldPos - worldPos ).squaredNorm() <
                 kCandidateMinDistanceSq ) {
            return 0.0f;
        }
    }
    if ( candidates.size() >= static_cast<size_t>( kMaxNumCandidates ) ) {
        return 0.0f;
    }

    CubicBezier bezier;
    for ( int32_t i = 0; i < 4; i++ ) {
        bezier.ControlPoints[i] = curve.ControlPoints[i];
    }
    Eigen::Vector4f direction = bezier.evaluateTangent( absT );
    direction.y() = 0.0f;
    direction.w() = 0.0f;
    direction.normalize();

    Candidate candidate;
    candidate.It = it;
    candidate.bNegT = ( t < 0.0f );
    candidate.Direction = direction;
    candidate.WorldPos = worldPos;
    candidate.ZoneIndex = pEntity->DensityZoneIndice;
    candidate.Weight = static_cast<float>( bWay ? pEntity->NumRightLanes
                                                : pEntity->NumLeftLanes );
    if ( bubbleIt.pRoad != nullptr && it.pRoad->ID == bubbleIt.pRoad->ID ) {
        candidate.Weight *= kPlayerRoadWeightScale;
    }
    if ( pCamera != nullptr ) {
        candidate.AltWeight =
            ( pCamera->sphereInFrustum( &worldPos, 1.0f ) != 0 )
                ? candidate.Weight
                : 0.0f;
    } else {
        candidate.AltWeight = candidate.Weight;
    }
    maxWeight = std::max( maxWeight, candidate.Weight );
    minWeight = std::min( minWeight, candidate.Weight );
    candidates.push_back( candidate );
    return candidate.Weight;
}

bool AiSimBubble::computeEnabledZones( bool *pOutMask ) const {
    // FUN_009173e0
    int32_t carIDs[AiPoolTraffic::kMaxLoadedTemplates];
    int32_t numCarIDs = 0;
    for ( const AiPoolTraffic::Template *pTemplate =
              gpAI->getAIPool()->getTraffic().getFirstLoadedTemplate();
          pTemplate != nullptr &&
          numCarIDs < AiPoolTraffic::kMaxLoadedTemplates;
          pTemplate = pTemplate->pNext ) {
        carIDs[numCarIDs++] = pTemplate->IDCar;
    }

    const bool bIgnoreDensity = gpAI->getAIPool()->isZoneDensityIgnored();
    bool bAllEnabled = true;
    for ( int32_t zone = 0; zone < kNumZoneWeights; zone++ ) {
        pOutMask[zone] = false;
        const AIZone &zoneConfig = gAIConfig.Zones[zone];
        if ( zoneConfig.TrafficPerLaneKm > 0.0f || bIgnoreDensity ) {
            for ( int32_t i = 0; i < numCarIDs && !pOutMask[zone]; i++ ) {
                for ( const AIZoneVehicle &vehicle : zoneConfig.Vehicles ) {
                    if ( vehicle.DatabaseId == carIDs[i] ) {
                        pOutMask[zone] = ( vehicle.Frequency > 0.0f );
                        break;
                    }
                }
            }
        }
        if ( !pOutMask[zone] ) {
            bAllEnabled = false;
        }
    }
    return bAllEnabled;
}

bool AiSimBubble::pickCandidate( AiRoadIt &outIt, const bool bUseAltWeight ) {
    // FUN_009184e0
    if ( candidates.empty() ) {
        return false;
    }
    if ( bUseAltWeight && altWeightSum <= 0.0f ) {
        return false;
    }

    bool zoneMask[kNumZoneWeights];
    float weightSum = 1.0f;
    float altSum = altWeightSum;
    if ( !computeEnabledZones( zoneMask ) ) {
        weightSum = 0.0f;
        altSum = 0.0f;
        for ( const Candidate &candidate : candidates ) {
            if ( zoneMask[candidate.ZoneIndex] ) {
                weightSum += candidate.Weight;
                altSum += candidate.AltWeight;
            }
        }
    }
    const float r = RandomUnit( random );
    if ( bUseAltWeight && altSum <= 0.0f ) {
        return false;
    }
    if ( weightSum <= 0.0f ) {
        return false;
    }

    const float threshold = r * ( bUseAltWeight ? altSum : weightSum );
    float accumulated = 0.0f;
    for ( int32_t i = static_cast<int32_t>( candidates.size() ) - 1; i >= 0;
          i-- ) {
        const Candidate &candidate = candidates[i];
        if ( !zoneMask[candidate.ZoneIndex] ) {
            continue;
        }
        const float next = accumulated + ( bUseAltWeight ? candidate.AltWeight
                                                         : candidate.Weight );
        if ( i > 0 && next <= threshold ) {
            accumulated = next;
            continue;
        }
        outIt = candidate.It;
        if ( outIt.pRoad != nullptr ) {
            FixWayForLanes( outIt );
            return true;
        }
    }
    return false;
}

AiVehicle *AiSimBubble::pickLeastVisible() const {
    // FUN_00917e50
    AiVehicle *pBest = nullptr;
    float bestScore = kLeastVisibleScoreMax;
    for ( AiVehicle *pVehicle : gpAI->getAIPool()->getVehicles() ) {
        if ( pVehicle->getControllerType() != 0 ||
             !IsLocallyOwned( pVehicle->getCarState() ) ||
             !pVehicle->isRemovable() ) {
            continue;
        }
        OTDU_UNIMPLEMENTED_ONCE; // TODO: online
        const float score = pVehicle->getInfos().Visibility;
        if ( score < bestScore ) {
            bestScore = score;
            pBest = pVehicle;
        }
    }
    return pBest;
}

void AiSimBubble::cullLeastVisible( const float totalTime,
                                    const char *pReason ) {
    // FUN_00918060
    AiVehicle *pVehicle = pickLeastVisible();
    if ( pVehicle != nullptr &&
         pVehicle->getInfos().Visibility < kCullVisibilityMax &&
         IsLocallyOwned( pVehicle->getCarState() ) ) {
        gpAI->getAIPool()->queueRemoval( pVehicle, pReason );
        lastVisibleCullTime = totalTime;
    }
}

void AiSimBubble::spawnTraffic( const float totalTime ) {
    // FUN_00919d60
    AiPool *pPool = gpAI->getAIPool();
    const AiPoolTraffic &traffic = pPool->getTraffic();
    if ( traffic.getNumLoadedTemplates() <= 0 ||
         traffic.getNumLoadingTemplates() != 0 ) {
        return;
    }
    if ( pPool->getSpawnRequest().getState() != 0 ) {
        return;
    }
    if ( lastSpawnTick < 0.0f || lastSpawnTick > totalTime ) {
        lastSpawnTick = totalTime;
    }
    if ( lastSpawnTick + spawnInterval >= totalTime ) {
        return;
    }
    lastSpawnTick = totalTime;
    spawnInterval = RandomFloat( random, spawnConfig[1], spawnConfig[2] );

    const int32_t numCops =
        pPool->getList( AiPool::AVL_FirstCategory + 1 ).Count;
    if ( pPool->isForceDensity() ) {
        targetTraffic = std::clamp( pPool->getMaxPolice() - numCops +
                                        pPool->getMaxTraffic(),
                                    0, gAIConfig.MaxNumVehicle );
        targetVisible = static_cast<int32_t>(
            static_cast<float>( targetTraffic ) * spawnConfig[0] + 0.5f );
    } else {
        const float density = pPool->isZoneDensityIgnored()
                                  ? kIgnoredZoneDensity
                                  : pCurrentZone->TrafficPerLaneKm;
        const float target =
            std::min( laneLengthInside * density * kPerLaneKm,
                      static_cast<float>( pPool->getMaxTraffic() ) );
        targetTraffic = static_cast<int32_t>( target + 0.5f );
        targetVisible = static_cast<int32_t>( spawnConfig[0] * target + 0.5f );
    }

    if ( lastVisibleCullTime > totalTime ) {
        lastVisibleCullTime = totalTime - gAIConfig.Unknown0x160;
    }
    const int32_t numWithTemplate =
        pPool->getList( AiPool::AVL_WithTemplate ).Count;
    int32_t maxVehicles = gAIConfig.MaxNumVehicle;
    if ( numVisibleHits < targetVisible &&
         ( numWithTemplate >= maxVehicles || numTraffic >= targetTraffic ) &&
         numTraffic > 0 && altWeightSum > 0.0f &&
         fabsf( lastVisibleCullTime - totalTime ) > gAIConfig.Unknown0x160 ) {
        cullLeastVisible( totalTime, "least visible traffic, need visible" );
        return;
    }

    OTDU_UNIMPLEMENTED; // TODO: online spawn credits

    if ( numWithTemplate >= maxVehicles ) {
        return;
    }
    const int32_t numTrafficLoco =
        pPool->getList( AiPool::AVL_FirstLocomotion + 0 ).Count;
    const int32_t extraTraffic = std::max(
        pPool->getList( AiPool::AVL_FirstCategory + 0 ).Count - numTrafficLoco,
        0 );
    const int32_t freeCopSlots = pPool->getMaxPolice() - extraTraffic - numCops;
    const int32_t numMissing = targetTraffic - numTrafficLoco;
    if ( numTraffic >= targetTraffic ) {
        return;
    }

    bool bPolice = false;
    if ( freeCopSlots > 0 && numMissing > 0 ) {
        if ( pPool->isForceDensity() && freeCopSlots >= numMissing ) {
            bPolice = true;
        } else {
            bPolice =
                RandomUnit( pPool->getRandom() ) < gAIConfig.PoliceSpawnProba;
        }
    }

    AiRoadIt it;
    bool bPicked = false;
    if ( ( numVisibleHits < targetVisible || bPolice ) && !candidates.empty() &&
         altWeightSum > 0.0f ) {
        bPicked = pickCandidate( it, true );
    }
    if ( !bPicked && !pickCandidate( it, false ) ) {
        return;
    }
    pPool->spawnTrafficOnRoad( it, bPolice );
}

bool AiSimBubble::pickBotCandidate( AiRoadIt &outIt,
                                    const bool bBehind,
                                    const bool bAhead ) {
    // FUN_00918720
    if ( candidates.empty() || bubbleIt.pRoad == nullptr ) {
        return false;
    }

    AiPathFloodSolver &solver = gpAI->getFloodSolver();
    solver.reset();
    solver.setDirections( bBehind, bAhead );
    solver.setStart( bubbleIt );
    solver.setRadius( static_cast<int32_t>( radius ) );
    if ( solver.solve() != AiPathSolver::S_Success ||
         solver.getResult() == nullptr ) {
        return false;
    }
    const AiRoadMap::Junction *pFrontier = gpAI->getRoadMap().getJunction(
        static_cast<uint32_t>( solver.getResult()->Waypoint ) );

    for ( auto candidate = candidates.rbegin(); candidate != candidates.rend();
          candidate++ ) {
        outIt = candidate->It;
        const AiRoadMap::Road *pRoad = outIt.pRoad;
        if ( pRoad == nullptr ) {
            continue;
        }

        if ( pRoad != bubbleIt.pRoad ) {
            if ( bBehind ) {
                if ( pRoad->pJunctionEnd == pFrontier ) {
                    outIt.setWay( false );
                    return true;
                }
                if ( pRoad->pJunctionBegin == pFrontier ) {
                    outIt.setWay( true );
                    return true;
                }
            } else if ( bAhead ) {
                if ( pRoad->pJunctionEnd == pFrontier ) {
                    outIt.setWay( true );
                    return true;
                }
                if ( pRoad->pJunctionBegin == pFrontier ) {
                    outIt.setWay( false );
                    return true;
                }
            } else {
                return true;
            }
            continue;
        }

        const bool bNoEnd = pRoad->pJunctionEnd == nullptr;
        const bool bNoBegin = pRoad->pJunctionBegin == nullptr;
        if ( bNoEnd ) {
            outIt.setWay( false );
        } else if ( bNoBegin ) {
            outIt.setWay( true );
        } else {
            outIt.setWay( bubbleIt.bWay );
        }

        if ( bAhead ) {
            if ( bBehind ) {
                return true;
            }
            if ( !IsBeforeInWay( bubbleIt, outIt, bubbleIt.bWay ) ) {
                continue;
            }
            if ( !bNoEnd && !bNoBegin ) {
                outIt.setWay( gpAI->getAIPool()->getRandom().getRandomUint32() <
                              0x7fffffffu );
            }
            return true;
        }
        if ( bBehind && IsBeforeInWay( outIt, bubbleIt, bubbleIt.bWay ) ) {
            return true;
        }
    }
    return false;
}

void AiSimBubble::spawnBots( const float deltaTime ) {
    // FUN_0091a5b0
    static constexpr float kNearSpawnDistance = 1024.0f; // 0x44800000
    static constexpr float kFarSpawnDistance = 2048.0f;  // 0x45000000

    if ( deltaTime <= 0.0f || gpAI == nullptr ) {
        return;
    }
    AiPool *pPool = gpAI->getAIPool();
    AiBotManager &botManager = gpAI->getBotManager();
    if ( !pPool->getSpawnRequest().isIdle() || !botManager.canSpawn() ) {
        return;
    }
    CarState *pLocalCar = gpScenaric->getLocalPlayer().getCarState();
    if ( pLocalCar == nullptr ) {
        return;
    }

    const Eigen::Vector4f position = pLocalCar->getWorldPosition();
    const int32_t zone = botManager.computeZone( &position );
    const int32_t numBots = pPool->getNumBots();
    AiRoadIt candidate;
    if ( numBots == 0 || numBots < gAIConfig.MaxNumBots / 3 ) {
        if ( !pickBotCandidate( candidate, true, false ) &&
             !pickBotCandidate( candidate, false, true ) ) {
            return;
        }
    } else if ( numBots < ( gAIConfig.MaxNumBots * 2 ) / 3 ) {
        botManager.pickSpawnRoad( candidate, zone, position, 0.0f,
                                  kNearSpawnDistance );
    } else {
        botManager.pickSpawnRoad( candidate, zone, position, kNearSpawnDistance,
                                  kFarSpawnDistance );
    }

    const AiRoadMap::Road *pRoad = candidate.pRoad;
    if ( pRoad == nullptr || !IsRoadAvailable( pRoad->ID ) ) {
        return;
    }
    if ( ( candidate.bWay ? pRoad->pJunctionEnd : pRoad->pJunctionBegin ) ==
         nullptr ) {
        candidate.flip();
    }
    pPool->requestBot( candidate );
}
