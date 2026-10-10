#include "shared.h"
#include "road_phantom_manager.h"

#include "physics/gs_physics.h"
#include "physics/physics_layers.h"

#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyInterface.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/PhysicsSystem.h>

static constexpr float kMinHalfExtent = 0.01f;

RoadPhantomManager::RoadPhantomManager()
    : numMaxRoadPhantoms( 0 ),
      numRoadPhantoms( 0 ),
      roadPhantoms(),
      numRoadPhantomEvents( 0 ),
      roadPhantomEvents(),
      numMaxCallbacks( 0 ),
      numCallbacks( 0 ),
      callbacks(),
      phantomSlots(),
      overlapMutex() {}

RoadPhantomManager::~RoadPhantomManager() {
    for ( RoadPhantom *&pPhantom : roadPhantoms ) {
        delete pPhantom;
        pPhantom = nullptr;
    }
}

void RoadPhantomManager::initialize( int32_t numMaxPhantoms,
                                     int32_t numCallbackSlots ) {
    // FUN_00819d80
    numMaxRoadPhantoms = numMaxPhantoms;
    numRoadPhantoms = 0;
    roadPhantoms.assign( static_cast<size_t>( numMaxPhantoms ), nullptr );

    numMaxCallbacks = numCallbackSlots;
    numCallbacks = 0;
    callbacks.assign( static_cast<size_t>( numCallbackSlots ), nullptr );

    numRoadPhantomEvents = 0;
    roadPhantomEvents.assign( static_cast<size_t>( kNumMaxRoadPhantomEvents ),
                              RoadPhantomEvt{} );
}

void RoadPhantomManager::createRoadPhantom( JPH::PhysicsSystem *pPhysicsSystem,
                                            const void *pSectorKey,
                                            const Eigen::Vector3f &boundsMin,
                                            const Eigen::Vector3f &boundsMax,
                                            const int32_t roadID,
                                            const int32_t entityIndex,
                                            const int32_t curveIndex ) {
    // FUN_00819ec0
    int32_t slot = -1;
    for ( int32_t i = 0; i < static_cast<int32_t>( roadPhantoms.size() );
          i++ ) {
        if ( roadPhantoms[i] == nullptr ) {
            slot = i;
            break;
        }
    }

    if ( slot == -1 || pPhysicsSystem == nullptr ) {
        return;
    }

    const Eigen::Vector3f center = ( boundsMin + boundsMax ) * 0.5f;
    const Eigen::Vector3f halfExtent =
        ( ( boundsMax - boundsMin ) * 0.5f )
            .cwiseMax( Eigen::Vector3f::Constant( kMinHalfExtent ) );
    JPH::BodyCreationSettings settings(
        new JPH::BoxShape(
            JPH::Vec3( halfExtent.x(), halfExtent.y(), halfExtent.z() ), 0.0f ),
        JPH::RVec3( center.x(), center.y(), center.z() ),
        JPH::Quat::sIdentity(), JPH::EMotionType::Kinematic,
        TDUPhysLayers::SENSOR );

    // NOTE: a static Jolt sensor loses its contacts as soon as the other body
    // sleeps; an active kinematic one keeps them (like the Havok phantom).
    settings.mIsSensor = true;
    settings.mAllowSleeping = false;
    settings.mUserData =
        ( static_cast<uint64_t>( slot ) << TDUSurface::kOwnerShift ) |
        TDUSurface::RoadPhantom;

    JPH::BodyInterface &bodyInterface = pPhysicsSystem->GetBodyInterface();
    JPH::Body *pBody = bodyInterface.CreateBody( settings );
    if ( pBody == nullptr ) {
        return;
    }

    RoadPhantom *pPhantom = new RoadPhantom();
    pPhantom->BodyID = pBody->GetID();
    pPhantom->pSectorKey = pSectorKey;
    pPhantom->RoadID = roadID;
    pPhantom->EntityIndex = entityIndex;
    pPhantom->CurveIndex = curveIndex;
    {
        ScopedMutexLock lock( &overlapMutex );
        roadPhantoms[slot] = pPhantom;
        phantomSlots[pPhantom->BodyID.GetIndexAndSequenceNumber()] = slot;
    }
    bodyInterface.AddBody( pPhantom->BodyID, JPH::EActivation::Activate );
}

void RoadPhantomManager::removeSectorRoadPhantoms(
    JPH::PhysicsSystem *pPhysicsSystem, const void *pSectorKey ) {
    // FUN_00819c10
    if ( pPhysicsSystem == nullptr ) {
        return;
    }
    JPH::BodyInterface &bodyInterface = pPhysicsSystem->GetBodyInterface();
    for ( RoadPhantom *&pPhantom : roadPhantoms ) {
        if ( pPhantom == nullptr || pPhantom->pSectorKey != pSectorKey ) {
            continue;
        }

        {
            ScopedMutexLock lock( &overlapMutex );
            for ( const RoadPhantom::Overlap &overlap : pPhantom->Overlaps ) {
                queueEvent( { false, pPhantom->RoadID, pPhantom->EntityIndex,
                              pPhantom->CurveIndex, overlap.pObject } );
            }
            pPhantom->Overlaps.clear();
            phantomSlots.erase( pPhantom->BodyID.GetIndexAndSequenceNumber() );
        }

        bodyInterface.RemoveBody( pPhantom->BodyID );
        bodyInterface.DestroyBody( pPhantom->BodyID );
        delete pPhantom;
        pPhantom = nullptr;
    }
}

void RoadPhantomManager::onOverlapAdded( const JPH::BodyID &phantomID,
                                         const JPH::BodyID &bodyID,
                                         void *pObject ) {
    // FUN_0081a0f0
    ScopedMutexLock lock( &overlapMutex );
    auto it = phantomSlots.find( phantomID.GetIndexAndSequenceNumber() );
    if ( it == phantomSlots.end() ) {
        return;
    }
    RoadPhantom *pPhantom = roadPhantoms[it->second];
    for ( RoadPhantom::Overlap &overlap : pPhantom->Overlaps ) {
        if ( overlap.BodyID == bodyID ) {
            overlap.NumContacts++;
            return;
        }
    }
    pPhantom->Overlaps.push_back( { bodyID, pObject, 1 } );
    queueEvent( { true, pPhantom->RoadID, pPhantom->EntityIndex,
                  pPhantom->CurveIndex, pObject } );
}

void RoadPhantomManager::onOverlapRemoved( const JPH::BodyID &phantomID,
                                           const JPH::BodyID &bodyID ) {
    // FUN_0081a010
    ScopedMutexLock lock( &overlapMutex );
    auto it = phantomSlots.find( phantomID.GetIndexAndSequenceNumber() );
    if ( it == phantomSlots.end() ) {
        return;
    }
    RoadPhantom *pPhantom = roadPhantoms[it->second];
    for ( auto overlap = pPhantom->Overlaps.begin();
          overlap != pPhantom->Overlaps.end(); ++overlap ) {
        if ( overlap->BodyID == bodyID ) {
            if ( --overlap->NumContacts > 0 ) {
                return;
            }
            queueEvent( { false, pPhantom->RoadID, pPhantom->EntityIndex,
                          pPhantom->CurveIndex, overlap->pObject } );
            pPhantom->Overlaps.erase( overlap );
            return;
        }
    }
}

void RoadPhantomManager::queueEvent( const RoadPhantomEvt &evt ) {
    // FUN_00819950
    if ( numRoadPhantomEvents <
         static_cast<int32_t>( roadPhantomEvents.size() ) ) {
        roadPhantomEvents[numRoadPhantomEvents++] = evt;
    }
}

bool RoadPhantomManager::registerCallback( RoadPhantomCallback *pCallback ) {
    // FUN_00819be0
    for ( RoadPhantomCallback *&pSlot : callbacks ) {
        if ( pSlot == nullptr ) {
            pSlot = pCallback;
            return true;
        }
    }
    return false;
}

void RoadPhantomManager::unregisterCallback( RoadPhantomCallback *pCallback ) {
    // FUN_00819bb0
    for ( RoadPhantomCallback *&pSlot : callbacks ) {
        if ( pSlot == pCallback ) {
            pSlot = nullptr;
            return;
        }
    }
}

void RoadPhantomManager::dispatchEvents() {
    // FUN_00819b40
    const int32_t numEvents =
        Min( numRoadPhantomEvents,
             static_cast<int32_t>( roadPhantomEvents.size() ) );

    for ( int32_t i = 0; i < numEvents; i++ ) {
        const RoadPhantomEvt &evt = roadPhantomEvents[i];
        for ( RoadPhantomCallback *pCallback : callbacks ) {
            if ( pCallback == nullptr ) {
                continue;
            }
            if ( evt.bAdding ) {
                pCallback->onRoadPhantomAdd( evt );
            } else {
                pCallback->onRoadPhantomRemove( evt );
            }
        }
    }
}
