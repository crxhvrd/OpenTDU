#include "shared.h"
#include "ai_pool.h"

#include "ai/ai_bot_manager.h"
#include "ai/ai_config.h"
#include "ai/ai_road_buckets.h"
#include "ai/ai_sim_bubble.h"
#include "ai/gs_ai.h"
#include "ai/ai_road_map.h"
#include "player_data/gs_playerdata.h"
#include "ai/ai_veh_nav_opponent.h"
#include "ai/ai_road_query.h"
#include "core/arg_parser.h"
#include "database/gs_database.h"
#include "game/game_object.h"
#include "game/scenaric/gs_scenaric.h"
#include "game/vehicle/bank_vehicle.h"
#include "game/vehicle/car_state.h"
#include "game/vehicle/gs_car_display.h"
#include "game/vehicle/gs_car_state.h"
#include "audio/vehicle/gs_vehicle_sound.h"
#include "physics/gs_physics.h"
#include "physics/vehicle/physics_vehicle.h"
#include "render/camera/camera_game.h"
#include "render/camera/gs_camera.h"
#include "world/gs_world.h"
#include "gs_timer.h"

static constexpr float kMinSpawnDistanceSq = 2500.0f; // DAT_00faa9d8
static constexpr float kMaxPhysicsOriginDistanceSq =
    63999900.0f;                                           // DAT_00f41cac
static constexpr float kCopPhysicsDistanceSq = 160000.0f;  // DAT_00f41ca8
static constexpr float kCopKinematicDistanceSq = 90000.0f; // DAT_00f41ca4
static constexpr int32_t kFallbackCarID = 999;
static constexpr int32_t kFallbackBikeID = 666;
static constexpr int32_t kFallbackCarPhysicsID = 223;
static constexpr int32_t kFallbackBikePhysicsID = 1502;
static constexpr uint32_t kBikeCategoryHashcode = 0x35da5b5;

// Disable AI traffic
static bool gbDisableAITraffic = false;
static CmdLineArg CmdLineArgNoTraffic( "notraffic", []( const char *pArg ) {
    gbDisableAITraffic = true;
} );

bool AiVehicleList::contains( const AiVehicle *pVehicle ) const {
    for ( int32_t i = 0; i < Count; i++ ) {
        if ( Items[i] == pVehicle ) {
            return true;
        }
    }
    return false;
}

void AiVehicleList::add( AiVehicle *pVehicle ) {
    // FUN_0040bd80
    if ( Count < kCapacity && !contains( pVehicle ) ) {
        Items[Count++] = pVehicle;
    }
}

void AiVehicleList::remove( AiVehicle *pVehicle ) {
    // FUN_0040bdc0
    for ( int32_t i = 0; i < Count; i++ ) {
        if ( Items[i] != pVehicle ) {
            continue;
        }
        for ( int32_t j = i + 1; j < Count; j++ ) {
            Items[j - 1] = Items[j];
        }
        Count--;
        Items[Count] = nullptr;
        return;
    }
}

void AiVehicleList::clear() {
    Items.fill( nullptr );
    Count = 0;
}

void AiLODSorter::clear() {
    for ( Node &node : Nodes ) {
        node.pNext = nullptr;
    }
    pHead = nullptr;
    Count = 0;
}

void AiLODSorter::insert( AiVehicle *pVehicle ) {
    // FUN_009200b0
    if ( Count >= kCapacity ) {
        return;
    }

    Node &node = Nodes[Count];
    node.pVehicle = pVehicle;
    Eigen::Vector4f delta =
        pVehicle->getInfos().GroundMatrix.row( 3 ) - Reference.row( 3 );
    delta.y() = 0.0f;
    node.DistanceSq = delta.squaredNorm();
    node.pNext = nullptr;

    Node *pPrevious = nullptr;
    for ( Node *pIt = pHead;
          pIt != nullptr && node.DistanceSq > pIt->DistanceSq;
          pIt = pIt->pNext ) {
        pPrevious = pIt;
    }
    if ( pPrevious != nullptr ) {
        node.pNext = pPrevious->pNext;
        pPrevious->pNext = &node;
    } else {
        node.pNext = pHead;
        pHead = &node;
    }
    Count++;
}

AiPool::AiPool()
    : traffic( &random ),
      trafficScale( 1.0f ),
      field_0x7c( true ),
      bIgnoreZoneDensity( false ),
      policeScale( 0.0f ),
      bForceDensity( false ),
      field_0x85( true ),
      random(),
      removalQueue(),
      numRemovals( 0 ),
      removalReasons{},
      pUsedVehicles(),
      pFreeVehicles(),
      numUsedVehicles( 0 ),
      bVehicleUsed(),
      vehicles(),
      lists(),
      roadShapes(),
      spawnRequest(),
      opponentPickFrame( 0 ),
      nextOpponentIndex( -1 ),
      nextController1Index( -1 ) {
    // FUN_0091ead0
    random.setSeed( 0x3039 );
    removalQueue.fill( nullptr );
    pUsedVehicles.fill( nullptr );
    bVehicleUsed.fill( false );
    for ( int32_t i = 0; i < kMaxNumVehicles; i++ ) {
        pFreeVehicles[i] = &vehicles[i];
    }
}

void AiPool::initialize() {
    // FUN_0091db80
    traffic.initialize();
    for ( int32_t i = 0; i < kMaxNumVehicles; i++ ) {
        vehicles[i].setCarStateIndex(
            static_cast<int32_t>( kFirstCarStateSlot ) + i );
        vehicles[i].setRandomSeed( random.getRandomUint32() );
    }
}

void AiPool::terminate() {
    // FUN_0091fbc0
    traffic.terminate();
    removeAllVehicles( true );
}

AiRoadShape *AiPool::getUnusedShape() {
    // FUN_0091e170
    int32_t leastRecentIndex = 0;
    int32_t leastRecentUse = static_cast<int32_t>( roadShapes[0].LastUse );
    for ( int32_t i = 1; i < kNumRoadShapes; i++ ) {
        const int32_t lastUse = static_cast<int32_t>( roadShapes[i].LastUse );
        if ( lastUse < leastRecentUse ) {
            leastRecentUse = lastUse;
            leastRecentIndex = i;
        }
    }

    AiRoadShape *pShape = &roadShapes[leastRecentIndex];
    if ( leastRecentUse != 0 ) {
        pShape->clear();
    }
    return pShape;
}

AiVehicle *AiPool::allocVehicleSlot() {
    // FUN_0040ba60
    if ( kMaxNumVehicles - numUsedVehicles <= 0 ) {
        return nullptr;
    }
    const int32_t freeIndex = kMaxNumVehicles - 1 - numUsedVehicles;
    AiVehicle *pVehicle = pFreeVehicles[freeIndex];
    pFreeVehicles[freeIndex] = nullptr;
    pUsedVehicles[numUsedVehicles] = pVehicle;
    bVehicleUsed[pVehicle - vehicles.data()] = true;
    numUsedVehicles++;
    return pVehicle;
}

void AiPool::releaseVehicleSlot( AiVehicle *pVehicle ) {
    // FUN_0040b810
    const ptrdiff_t index = pVehicle - vehicles.data();
    if ( index < 0 || index >= kMaxNumVehicles ) {
        return;
    }
    for ( int32_t i = 0; i < numUsedVehicles; i++ ) {
        if ( pUsedVehicles[i] != pVehicle ) {
            continue;
        }
        pFreeVehicles[kMaxNumVehicles - numUsedVehicles] = pVehicle;
        for ( int32_t j = i + 1; j < numUsedVehicles; j++ ) {
            pUsedVehicles[j - 1] = pUsedVehicles[j];
        }
        pUsedVehicles[numUsedVehicles - 1] = nullptr;
        bVehicleUsed[index] = false;
        numUsedVehicles--;
        return;
    }
}

void AiPool::addToSubLists( AiVehicle *pVehicle ) {
    // FUN_00915d80
    const int32_t category = static_cast<int32_t>( pVehicle->getCategory() );
    if ( category >= 0 && category < 9 ) {
        lists[AVL_FirstCategory + category].add( pVehicle );
    }
    const int32_t behaviour = pVehicle->getBehaviourType();
    if ( behaviour >= 0 && behaviour < 5 ) {
        lists[AVL_FirstBehaviour + behaviour].add( pVehicle );
    }
    const int32_t controller = pVehicle->getControllerType();
    if ( controller >= 0 && controller < 9 ) {
        lists[AVL_FirstController + controller].add( pVehicle );
    }
    const int32_t navigation = pVehicle->getNavigationType();
    if ( navigation >= 0 && navigation < 4 ) {
        lists[AVL_FirstNavigation + navigation].add( pVehicle );
    }
    const int32_t locomotion = pVehicle->getLocomotionType();
    if ( locomotion >= 0 && locomotion < 3 ) {
        lists[AVL_FirstLocomotion + locomotion].add( pVehicle );
    }
    lists[( pVehicle->getTemplate() != nullptr ) ? AVL_WithTemplate
                                                 : AVL_WithoutTemplate]
        .add( pVehicle );
    if ( pVehicle->getCategory() == eAiVehicleCategory::AVC_Traffic &&
         pVehicle->getCarState() != nullptr ) {
        lists[IsLocallyOwned( pVehicle->getCarState() ) ? AVL_LocalTraffic
                                                        : AVL_DistantTraffic]
            .add( pVehicle );
    }
}

void AiPool::addVehicle( AiVehicle *pVehicle ) {
    // FUN_00915e60
    lists[AVL_All].add( pVehicle );
    addToSubLists( pVehicle );
}

void AiPool::removeFromLists( AiVehicle *pVehicle ) {
    // FUN_009159b0
    for ( AiVehicleList &list : lists ) {
        list.remove( pVehicle );
    }
}

CarState *AiPool::installVehicle( AiPoolTraffic::Template *pTemplate,
                                  AiVehicle *pVehicle,
                                  const bool bLinkAiToCarState,
                                  const Matrix4x4f_t &physicsSpawnMatrix ) {
    // FUN_0091d0f0
    VehicleBaseResources *pResource = pTemplate->pResource;
    if ( pResource == nullptr ) {
        return nullptr;
    }

    CarState *pCarState = nullptr;
    const int32_t slot = pVehicle->getCarStateIndex();
    if ( slot == -1 ) {
        for ( uint32_t i = 0; i < kMaxNumVehicles; i++ ) {
            if ( !gpCarState->isCarStateUsed( static_cast<int32_t>( i ) ) ) {
                pCarState = &gpCarState->grabCarState( i );
                break;
            }
        }
        if ( pCarState == nullptr ) {
            return nullptr;
        }
    } else {
        pCarState = &gpCarState->grabCarState( static_cast<uint32_t>( slot ) );
    }

    CarState &carState = *pCarState;
    carState.reset();
    carState.owner = CarState::eOwner::CSO_Traffic;
    carState.simulationType = CarState::eSimulationType::CSST_Local;
    carState.bUseShowcaseControl = false;
    carState.carID = pResource->CarID;
    carState.tireID = pResource->TireID;
    carState.rimID = pResource->RimID;
    carState.body.BackOnTrackState = 0;
    carState.body.resetBackOnTrack();

    DBEntryCarPhysicsData *pCarData =
        gpDatabase->getCarDBEntry( pResource->CarID );
    if ( pCarData == nullptr ) {
        pCarData = gpDatabase->getCarDBEntry( kFallbackCarID );
    } else if ( ( pCarData->BitField & 1 ) == 0 || pCarData->Brand == nullptr ||
                ( pCarData->Brand->Flags & 1 ) == 0 ) {
        const char *pBikeCategory =
            gpDatabase->getStringByHashcode( 1, kBikeCategoryHashcode );
        const bool bBike = ( pBikeCategory == pCarData->Category.pAsString );
        pCarData = gpDatabase->getCarDBEntry( bBike ? kFallbackBikeID
                                                    : kFallbackCarID );
    }
    const DBEntryAftermarketPack *pPack =
        gpDatabase->getAftermarketPack( pResource->PackHashcode );

    VehicleTraffic *pDisplay =
        gpCarDisplay->instantiateTraffic( &carState, &pResource->Exterior );

    PhysicsVehicle *pPhysics = nullptr;
    if ( pResource->bUseGamePhysics != 0 && gpPhysics != nullptr ) {
        DBEntryCarPhysicsData *pPhysicsData = pCarData;
        if ( carState.carID == kFallbackCarID ) {
            pPhysicsData = gpDatabase->getCarDBEntry( kFallbackCarPhysicsID );
            pPack = nullptr;
        } else if ( carState.carID == kFallbackBikeID ) {
            pPhysicsData = gpDatabase->getCarDBEntry( kFallbackBikePhysicsID );
            pPack = nullptr;
        }

        BankVehicle *pBank = pResource->Exterior.pBankInstance;
        PhysFactory *pFactory =
            ( pBank != nullptr ) ? pBank->getPhysFactory() : nullptr;
        const void *pTorqueBinary =
            ( pBank != nullptr ) ? pBank->getTorqueBinary() : nullptr;
        if ( pPhysicsData != nullptr ) {
            pPhysics = gpPhysics->createVehicle( pFactory, physicsSpawnMatrix,
                                                 *pPhysicsData, pTorqueBinary,
                                                 pPack, true, false );
        }
        if ( pPhysics != nullptr ) {
            pPhysics->setPhysicsPaused( true );
        }
    }

    VehicleSoundInstance *pSound = nullptr;
    if ( pResource->bSoundResource && gpVehicleSound != nullptr ) {
        pSound = gpVehicleSound->createInstance( &carState );
    }

    carState.pVehicleBase = pDisplay;
    carState.bIsIndoor = false;
    carState.pVehicleSound = pSound;
    carState.setPhysicsVehicle( pPhysics );
    carState.pResource = pResource;
    pResource->RefCount++;
    carState.field_0x39cc = bLinkAiToCarState ? pVehicle : nullptr;

    pVehicle->setTemplate( pTemplate );
    pVehicle->setCarState( &carState );
    pVehicle->getSignals().pCarState = &carState;
    pVehicle->getSignals().reset();
    pTemplate->NumTotalUsed++;
    pTemplate->NumUsed++;
    return &carState;
}

void AiPool::releaseTemplateCar( AiVehicle *pVehicle ) {
    // FUN_0091ce10
    if ( pVehicle->getCarState() != nullptr ) {
        gpCarState->releaseCarState( pVehicle->getCarState(), true );
        pVehicle->detachCarState();
    }
    pVehicle->setTemplate( nullptr );
}

int32_t AiPool::getMaxTraffic() const {
    // FUN_0091db20
    if ( gbDisableAITraffic ) {
        return 0;
    }
    if ( !field_0x7c ) {
        return 0;
    }
    const int32_t count = static_cast<int32_t>(
        static_cast<float>( gAIConfig.MaxNumTrafficVehicle ) * trafficScale +
        0.5f );
    if ( count < 0 ) {
        return 0;
    }
    return std::min( count, gAIConfig.MaxNumTrafficVehicle );
}

int32_t AiPool::getMaxPolice() const {
    // FUN_0091dad0
    // TODO: police manager flag (DAT_013d1a74)
    if ( !field_0x85 ) {
        return 0;
    }
    const int32_t count = static_cast<int32_t>(
        static_cast<float>( gAIConfig.MaxNumPoliceVehicle ) * policeScale +
        0.5f );
    if ( count < 0 ) {
        return 0;
    }
    return std::min( count, gAIConfig.MaxNumPoliceVehicle );
}

bool AiPool::spawnTrafficOnRoad( const AiRoadIt &candidate,
                                 const bool bPolice ) {
    // FUN_0091e880
    AiRoadIt roadIt = candidate;
    AiRoadMap::Road *pRoad = roadIt.pRoad;
    if ( pRoad->MinLeftLane == 0 ) {
        roadIt.setWay( true );
    } else if ( pRoad->MinRightLane == 0 || pRoad->pJunctionEnd == nullptr ||
                pRoad->pJunctionEnd->NumNeighbors < 2 ) {
        roadIt.setWay( false );
    } else if ( pRoad->pJunctionBegin == nullptr ||
                pRoad->pJunctionBegin->NumNeighbors < 2 ) {
        roadIt.setWay( true );
    }

    const AiRoadMap::Entity *pEntity = roadIt.getEntity();
    const int32_t numLanes =
        roadIt.bWay ? pEntity->NumRightLanes : pEntity->NumLeftLanes;
    int32_t lane = static_cast<int32_t>(
        RandomBelow( random, static_cast<uint32_t>( numLanes ) ) );
    lane = std::max( lane, 0 );
    lane = std::min( lane, numLanes - 1 );

    Matrix4x4f_t frame;
    if ( !roadIt.computeFrame( frame, lane ) ) {
        return false;
    }
    pRoad->getShape();

    AiRoadBuckets::Bucket *pBucket =
        gpAI->getRoadBuckets().getRoadBucket( pRoad->ID );
    if ( pBucket != nullptr ) {
        for ( GameObject *pObject = pBucket->pFirstObject; pObject != nullptr;
              pObject = pObject->getNextInBucket() ) {
            const Eigen::Vector4f delta =
                frame.row( 3 ).transpose() - pObject->getWorldPosition();
            if ( delta.squaredNorm() < kMinSpawnDistanceSq ) {
                return false;
            }
        }
    }
    return requestTraffic( frame, roadIt, lane, bPolice );
}

bool AiPool::requestTraffic( const Matrix4x4f_t &frame,
                             const AiRoadIt &roadIt,
                             const int32_t lane,
                             const bool bPolice ) {
    // FUN_0091ddc0
    if ( !spawnRequest.isIdle() ) {
        return false;
    }
    const int32_t numParked =
        lists[AVL_FirstCategory +
              static_cast<int32_t>( eAiVehicleCategory::AVC_Parked )]
            .Count;
    if ( lists[AVL_WithTemplate].Count - numParked >=
         gAIConfig.MaxNumTrafficVehicle ) {
        return false;
    }

    AiVehicle *pVehicle = allocVehicleSlot();
    if ( pVehicle == nullptr ) {
        return false;
    }
    spawnRequest.fillTraffic( pVehicle, frame, roadIt, lane, bPolice );
    return true;
}

void AiPool::queueRemoval( AiVehicle *pVehicle, const char *pReason ) {
    // FUN_0091d980
    for ( int32_t i = 0; i < numRemovals; i++ ) {
        if ( removalQueue[i < kMaxNumRemovals ? i : 0] == pVehicle ) {
            return;
        }
    }
    const int32_t index = ( numRemovals < kMaxNumRemovals ) ? numRemovals : 0;
    strncpy( removalReasons[index], pReason, AiVehicle::kRemovalReasonLength );
    removalReasons[index][AiVehicle::kRemovalReasonLength - 1] = '\0';
    removalQueue[index] = pVehicle;
    numRemovals++;
}

bool AiPool::togglePlayerAutopilot( const bool bOpponent ) {
    // FUN_0091f0b0
    CarState *pCar = gpScenaric->getLocalPlayer().getCarState();
    if ( pCar == nullptr ||
         pCar->getSimulationType() != CarState::eSimulationType::CSST_Local ||
         pCar->getOwner() != CarState::eOwner::CSO_LocalPlayer ) {
        return false;
    }

    AiVehicle *pVehicle = static_cast<AiVehicle *>( pCar->getAIController() );
    if ( pVehicle != nullptr ) {
        pVehicle->setPlayerCar( false );
        removeFromLists( pVehicle );
        pVehicle->release();
        pVehicle->getSignals().reset();
        pVehicle->getSignals().pCarState = nullptr;
        pVehicle->getSignals().reset();
        releaseVehicleSlot( pVehicle );
        pCar->setAIController( nullptr );

        gpPlayerData->applyDrivingOptions();
        OTDU_UNIMPLEMENTED; // TODO: GSCamera (shake / radial blur intensity
                            // from the local player data)
        PhysicsVehicle *pPhysics = pCar->getPhysicsVehicle();
        pPhysics->setDrivingControl( gpPhysics ); // +0x4388
        OTDU_UNIMPLEMENTED; // TODO: PHYSICS (+0xe40 = +0xa3c, GSPhysics +0x20 =
                            // 1, +0xd34 = 0, +0xdd4 = 0)
        pPhysics->setAutoReverseAllowed( true ); // +0x335
        {
            PhysicsLock lock;
            pPhysics->setPostponedPause( 2 );
        }
        gpCarState->setPoliceStateJob(
            pCar, &gpScenaric->getLocalPlayer().getInfringementCheck() );
        return false;
    }

    pVehicle = allocVehicleSlot();
    if ( pVehicle == nullptr ) {
        return false;
    }
    addVehicle( pVehicle );
    OTDU_UNIMPLEMENTED; // TODO: PHYSICS (FUN_00968d10 AI driving setup of the
                        // physics vehicle: +0x4388 control = null, +0xe54 = 4,
                        // +0x41b4 / +0x4258 = 1, +0x431c / +0x558 = 1.0,
                        // +0xd34 = 1, +0xe94 = 2, ...)
    pCar->setAIController( pVehicle );

    CarBodyState *pBody = pCar->getBody();
    AiRoadMap &roadMap = gpAI->getRoadMap();
    AiRoadMap::Road *pRoad =
        ( pBody->ActiveRoadID != 0 )
            ? roadMap.getRoad( uint32_t( pBody->ActiveRoadID ) )
            : roadMap.findNearestRoad( pBody->WorldMatrix.row( 3 ).transpose(),
                                       true );
    AiRoadIt location;
    ProjectOnRoadShape( pBody->WorldMatrix, location, nullptr, false,
                        pRoad->getShape() );

    Eigen::Vector4f side = location.computeSideDirection();
    Eigen::Vector4f offset = location.computeOffsetPosition( 0.0f, 0.0f ) -
                             pBody->WorldMatrix.row( 3 ).transpose();
    side.w() = 0.0f;
    offset.w() = 0.0f;
    side.normalize();
    const AiRoadProfile *pProfile = location.getEntity()->pProfile;
    const float widthRatio = std::clamp(
        side.dot( offset ) / ( pProfile->InnerWidth * 0.5f ), -1.0f, 1.0f );
    const int32_t lane =
        pProfile->computeLaneFromWidthRatio( location.bWay, widthRatio );

    if ( bOpponent ) {
        // FUN_009288f0
        pVehicle->initialize( eAiVehicleCategory::AVC_Opponent, pCar, location,
                              lane, AiVehicle::kBehaviourNone, 7, 1, 1, true );
    } else {
        // FUN_00928930
        pVehicle->initialize( eAiVehicleCategory::AVC_Autopilot, pCar, location,
                              lane, AiVehicle::kBehaviourNone, 8, 1, 1, true );
    }
    OTDU_UNIMPLEMENTED; // TODO: GSAI (AiVehicle +0xc2 = GSAI config +0xe2,
                        // +0x200 = 0)
    pVehicle->setRemovable( false ); // +0x22e4
    pVehicle->setPlayerCar( true );  // +0x22f2
    if ( pVehicle->getNavigationType() == 1 ) {
        pVehicle->getOpponentNavigation().requestState(
            AiVehNavOpponent::NS_LineEnded );
    }
    return true;
}

void AiPool::removeVehicle( AiVehicle *pVehicle,
                            const char *pReason,
                            const bool bReleaseCar ) {
    // FUN_0091ee60
    OTDU_LOG_DEBUG( "AI vehicle removed: %s\n", pReason );
    CarState *pCarState = pVehicle->getCarState();
    if ( pCarState == nullptr ) {
        removeFromLists( pVehicle );
        pVehicle->release();
        if ( bReleaseCar && pVehicle->getTemplate() != nullptr ) {
            releaseTemplateCar( pVehicle );
        }
        return;
    }

    OTDU_UNIMPLEMENTED_ONCE; // TODO: ONLINE

    removeFromLists( pVehicle );
    pVehicle->release();
    pCarState->setAIController( nullptr );
    if ( bReleaseCar ) {
        if ( pVehicle->getTemplate() != nullptr ) {
            releaseTemplateCar( pVehicle );
        } else if ( pVehicle->getCategory() == eAiVehicleCategory::AVC_Bot ) {
            VehicleBaseResources *pResource = pCarState->getResource();
            gpCarState->releaseCarState( pCarState, true );
            if ( pResource != nullptr ) {
                pResource->unload();
                gpCarState->releaseResourceEntry( pResource );
            }
        }
    }
    pVehicle->detachCarState();
    releaseVehicleSlot( pVehicle );
}

void AiPool::removeVehiclesWithCar( const int32_t carID ) {
    // FUN_0091f940
    spawnRequest.flush();
    for ( int32_t i = lists[AVL_All].Count - 1; i >= 0; i-- ) {
        AiVehicle *pVehicle = lists[AVL_All].Items[i];
        if ( pVehicle != nullptr && pVehicle->getTemplate() != nullptr &&
             pVehicle->getTemplate()->IDCar == carID ) {
            removeVehicle( pVehicle, "model rsc removed", true );
        }
    }
}

void AiPool::removeAllVehicles( const bool bResetPool ) {
    // FUN_0091f9e0
    spawnRequest.flush();
    while ( lists[AVL_All].Count > 0 ) {
        removeVehicle( lists[AVL_All].Items[0], "all AI removed", true );
    }
    numRemovals = 0;
    if ( bResetPool ) {
        traffic.unloadAll();
    }
}

void AiPool::removeCategory( const eAiVehicleCategory category,
                             const char *pReason ) {
    AiVehicleList &list =
        lists[AVL_FirstCategory + static_cast<int32_t>( category )];
    while ( list.Count > 0 ) {
        AiVehicle *pVehicle = list.Items[0];
        if ( pVehicle == nullptr ) {
            break;
        }
        pVehicle->setRemovable( true );
        removeVehicle( pVehicle, pReason, true );
    }
}

void AiPool::removeBots() {
    // FUN_0091f5f0
    removeCategory( eAiVehicleCategory::AVC_Bot, "bots disabled" );
}

void AiPool::removeCops() {
    // FUN_0091f630
    removeCategory( eAiVehicleCategory::AVC_Cop, "cops disabled" );
    removeCategory( eAiVehicleCategory::AVC_CopDam, "cop dams disabled" );
}

void AiPool::removeParkedVehicles() {
    // FUN_0091f6b0
    removeCategory( eAiVehicleCategory::AVC_Parked, "parked disabled" );
}

void AiPool::removeOpponents() {
    // FUN_0091f6f0
    removeCategory( eAiVehicleCategory::AVC_Opponent, "opponent disabled" );
}

void AiPool::removeTraffic() {
    // FUN_0091f730
    std::array<AiVehicle *, kMaxNumRemovals> kept = {};
    int32_t numKept = 0;
    for ( int32_t i = 0; i < numRemovals; i++ ) {
        AiVehicle *pVehicle = removalQueue[i < kMaxNumRemovals ? i : 0];
        if ( pVehicle != nullptr &&
             pVehicle->getCategory() != eAiVehicleCategory::AVC_Traffic ) {
            kept[numKept++] = pVehicle;
        }
    }
    removalQueue = kept;
    numRemovals = numKept; // sic: the reasons are not compacted

    AiVehicleList &traffic =
        lists[AVL_FirstCategory +
              static_cast<int32_t>( eAiVehicleCategory::AVC_Traffic )];
    while ( traffic.Count > 0 ) {
        removeVehicle( traffic.Items[0], "traffic removed", true );
    }
}

bool AiPool::wantsPhysics( AiVehicle *pVehicle,
                           const AiLODSorter &sorter,
                           const int32_t maxPhysics,
                           const float distanceSq,
                           int32_t &numPhysics ) const {
    // FUN_0091fef0
    CarState *pCarState = pVehicle->getCarState();
    if ( pCarState == nullptr ) {
        return false;
    }

    const Eigen::Vector4f position = pCarState->getWorldPosition();
    const Eigen::Vector4f localPosition =
        position - GSPhysics::GetShiftOrigin();
    if ( localPosition.head<3>().squaredNorm() > kMaxPhysicsOriginDistanceSq ) {
        return false;
    }
    if ( gpWorld == nullptr ||
         !gpWorld->isStreamedAround( &position, false, false ) ) {
        return false;
    }

    auto isInPhysicsRange = [&]() {
        return pVehicle->isPhysicsDriven()
                   ? distanceSq < kCopPhysicsDistanceSq
                   : distanceSq <= kCopKinematicDistanceSq;
    };

    switch ( pVehicle->getCategory() ) {
    case eAiVehicleCategory::AVC_Cop:
        return isInPhysicsRange();
    case eAiVehicleCategory::AVC_Opponent:
        if ( sorter.bNoOpponentPhysics ) {
            return false;
        }
        if ( pVehicle->getOpponentNavigation().getState() ==
                 AiVehNavOpponent::NS_Idle ||
             pVehicle->getOpponentNavigation().getState() ==
                 AiVehNavOpponent::NS_Grid ) {
            return true;
        }
        return isInPhysicsRange();
    case eAiVehicleCategory::AVC_Cinematic:
        return true;
    case eAiVehicleCategory::AVC_Traffic:
        if ( pVehicle->getInfos().bCrashed &&
             pVehicle->getInfos().Speed > 1.0f ) {
            return true;
        }
        [[fallthrough]];
    default:
        if ( sorter.PhysicsRadiusSq > distanceSq && numPhysics < maxPhysics ) {
            numPhysics++;
            return true;
        }
        return false;
    }
}

void AiPool::updateLOD( AiLODSorter &sorter ) {
    // FUN_00920310
    const float now = gpTimer->GameTotalTime;
    if ( sorter.LastLODTime >= 0.0f && now >= sorter.LastLODTime &&
         now - sorter.LastLODTime < gAIConfig.LODPeriod ) {
        return;
    }
    sorter.LastLODTime = now;

    int32_t numPhysics = 0;
    for ( AiLODSorter::Node *pNode = sorter.pHead; pNode != nullptr;
          pNode = pNode->pNext ) {
        AiVehicle *pVehicle = pNode->pVehicle;
        const bool bPhysics =
            wantsPhysics( pVehicle, sorter, gAIConfig.MaxNumPhysicsVehicle,
                          pNode->DistanceSq, numPhysics );
        if ( pVehicle->getLocomotion() != nullptr ) {
            pVehicle->getLocomotion()->setPhysicsActive( bPhysics, false );
        }

        if ( pVehicle->isRemovalPending() ) {
            // sic: retail removes as soon as Visibility <= 1
            if ( 1.0f >= pVehicle->getInfos().Visibility ||
                 ( pVehicle->getRemovalTime() > 0.0f &&
                   now >= pVehicle->getRemovalTime() ) ) {
                queueRemoval( pVehicle, pVehicle->getRemovalReason() );
            }
        } else if ( pVehicle->getCategory() ==
                        eAiVehicleCategory::AVC_Traffic &&
                    IsLocallyOwned( pVehicle->getCarState() ) &&
                    pVehicle->isRemovable() &&
                    pNode->DistanceSq >= sorter.CullRadiusSq ) {
            queueRemoval( pVehicle, "culling: traffic outside bubble" );
        }
    }
}

void AiPool::tick( const float deltaTime, const float totalTime ) {
    // FUN_0091fbf0
    if ( deltaTime <= 0.0f ) {
        return;
    }

    // FUN_0091f860
    spawnRequest.tick( false );
    OTDU_UNIMPLEMENTED_ONCE; // TODO: ONLINE
    traffic.tick();

    CarState *pLocalCar = gpScenaric->getLocalPlayer().getCarState();
    Matrix4x4f_t reference = Matrix4x4f_t::Identity();
    if ( pLocalCar != nullptr && !gpAI->getSimBubble().isCullingFromCamera() ) {
        reference = pLocalCar->getWorldMatrix();
    } else if ( gpCamera != nullptr ) {
        reference = gpCamera->getRenderCamera()->getCamToWorld();
    }

    OTDU_UNIMPLEMENTED_ONCE; // TODO: ONLINE

    AiLODSorter &sorter = gpAI->getLODSorter();
    sorter.Reference = reference;
    sorter.clear();
    for ( AiVehicle *pVehicle : lists[AVL_All] ) {
        sorter.insert( pVehicle );
    }
    updateLOD( sorter );

    for ( int32_t i = 0; i < numRemovals; i++ ) {
        const int32_t index = ( i < kMaxNumRemovals ) ? i : 0;
        removeVehicle( removalQueue[index], removalReasons[index], true );
    }
    numRemovals = 0;

    for ( int32_t i = 1; i < kNumVehicleLists; i++ ) {
        lists[i].clear();
    }
    for ( AiVehicle *pVehicle : lists[AVL_All] ) {
        addToSubLists( pVehicle );
    }

    updateOpponentRoundRobin();
    updateController1RoundRobin();

    for ( int32_t i = 0; i < lists[AVL_All].Count; i++ ) {
        lists[AVL_All].Items[i]->tick( deltaTime, totalTime );
    }
}

// Next CarState slot after the current one (cyclic over the 32 slots)
static int32_t PickNextCarStateSlot( const AiVehicleList &list,
                                     const int32_t current ) {
    static constexpr int32_t kNumCarStateSlots = 32;

    if ( list.Count == 0 ) {
        return -1;
    }
    if ( list.Count == 1 ) {
        return list.Items[0]->getCarStateIndex();
    }

    const int32_t from = ( current == -1 ) ? kNumCarStateSlots : current;
    int32_t bestDistance = kNumCarStateSlots;
    int32_t next = list.Count;
    for ( int32_t i = 0; i < list.Count; i++ ) {
        const int32_t slot = list.Items[i]->getCarStateIndex();
        if ( slot == from ) {
            continue;
        }
        const int32_t distance =
            ( slot > from ) ? slot - from : slot - from + kNumCarStateSlots;
        if ( distance < bestDistance ) {
            bestDistance = distance;
            next = slot;
        }
    }
    return next;
}

void AiPool::updateOpponentRoundRobin() {
    // FUN_0091def0
    static constexpr int32_t kNumPickFrames = 8;

    opponentPickFrame++;
    if ( opponentPickFrame == kNumPickFrames ) {
        opponentPickFrame = 0;
        return;
    }
    nextOpponentIndex = PickNextCarStateSlot( lists[AVL_FirstNavigation + 1],
                                              nextOpponentIndex );
}

void AiPool::updateController1RoundRobin() {
    // FUN_0091de30
    nextController1Index = PickNextCarStateSlot( lists[AVL_FirstController + 1],
                                                 nextController1Index );
}

AiVehicle *AiPool::addOpponent( CarState *pCar,
                                const Matrix4x4f_t &startFrame,
                                const AiRoadIt &startHint,
                                const int32_t missionType ) {
    // FUN_0091f020
    static constexpr int32_t kMissionTypeRace = 5;

    if ( missionType != kMissionTypeRace ) {
        return nullptr;
    }
    AiVehicle *pVehicle = allocVehicleSlot();
    if ( pVehicle == nullptr ) {
        return nullptr;
    }
    pVehicle->initializeOpponent( pCar, startFrame, 2, startHint );
    addVehicle( pVehicle );
    return pVehicle;
}

bool AiPool::requestBot( const AiRoadIt &candidate ) {
    // FUN_0091fa40
    static constexpr float kSplitEntryWidthRatio = 0.5f;    // DAT_00f41674
    static constexpr float kSplitCrossedWidthRatio = -0.5f; // DAT_00f4186c
    static constexpr float kMinBotSpawnDistanceSq = 100.0f; // DAT_00f41708

    AiRoadIt roadIt = candidate;
    AiRoadMap::Road *pRoad = roadIt.pRoad;
    float widthRatio = 0.0f;
    pRoad->getShape();
    if ( pRoad->isOncomingOvertakeForbidden() ) {
        widthRatio =
            roadIt.bWay ? kSplitEntryWidthRatio : kSplitCrossedWidthRatio;
    }
    const AiRoadProfile *pProfile = roadIt.getEntity()->pProfile;
    const int32_t lane =
        pProfile->computeLaneFromWidthRatio( roadIt.bWay, widthRatio );

    Matrix4x4f_t frame;
    if ( !roadIt.computeFrame( frame, lane ) ) {
        return false;
    }

    const int32_t botIndex =
        PickRandomBotInZone( gpAI->getBotManager().computeRoadZone( roadIt ) );
    if ( botIndex == -1 ) {
        return false;
    }

    AiRoadBuckets::Bucket *pBucket =
        gpAI->getRoadBuckets().getRoadBucket( pRoad->ID );
    if ( pBucket != nullptr ) {
        for ( GameObject *pObject = pBucket->pFirstObject; pObject != nullptr;
              pObject = pObject->getNextInBucket() ) {
            const Eigen::Vector4f delta =
                frame.row( 3 ).transpose() - pObject->getWorldPosition();
            if ( delta.squaredNorm() < kMinBotSpawnDistanceSq ) {
                return false;
            }
        }
    }
    return spawnBot( botIndex, frame, roadIt, lane );
}

bool AiPool::spawnBot( const int32_t botIndex,
                       const Matrix4x4f_t &frame,
                       const AiRoadIt &roadIt,
                       const int32_t lane ) {
    // FUN_0091f8c0
    if ( !spawnRequest.isIdle() || getNumBots() >= gAIConfig.MaxNumBots ) {
        return false;
    }
    AiVehicle *pVehicle = allocVehicleSlot();
    if ( pVehicle == nullptr ) {
        return false;
    }
    if ( spawnRequest.fillBot( pVehicle, frame, roadIt, lane, botIndex ) ) {
        return true;
    }
    releaseVehicleSlot( pVehicle );
    return false;
}

AiVehicle *AiPool::requestParked( const Matrix4x4f_t &frame,
                                  const int32_t colorIndex,
                                  EntityCarPlayer *pOwner ) {
    // FUN_0091dd00
    const int32_t numParked =
        lists[AVL_FirstCategory +
              static_cast<int32_t>( eAiVehicleCategory::AVC_Parked )]
            .Count;
    if ( !spawnRequest.isIdle() || numParked >= gAIConfig.MaxNumParkedVehicle ||
         traffic.getNumLoadedTemplates() <= 0 ) {
        return nullptr;
    }

    AiVehicle *pVehicle = allocVehicleSlot();
    if ( pVehicle == nullptr ) {
        return nullptr;
    }
    spawnRequest.fillParked( pVehicle, frame, colorIndex, pOwner );
    return isVehicleSlotUsed( pVehicle ) ? pVehicle : nullptr;
}

bool AiPool::isVehicleSlotUsed( const AiVehicle *pVehicle ) const {
    // FUN_0040bac0
    const ptrdiff_t index = pVehicle - vehicles.data();
    if ( index < 0 || index >= kMaxNumVehicles ) {
        return false;
    }
    for ( int32_t i = 0; i < numUsedVehicles; i++ ) {
        if ( pUsedVehicles[i] == pVehicle ) {
            return true;
        }
    }
    return false;
}
