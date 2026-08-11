#include "shared.h"
#include "gs_physics.h"

#include "config/gs_config.h"
#include "gs_physicsfx.h"
#include "vehicle/physics_vehicle.h"
#include "world_collisions.h"
#include "physics_layers.h"
#include "physics_material.h"
#include "game/material_types.h"
#include "havok/havok_container.h"
#include "havok/havok_packfile.h"
#include "havok/havok_to_jolt.h"
#include "database/gs_database.h"
#include "input/ed_dev.h"
#include "input/input_device.h"

#include "render/draw_list.h"
#include "render/gs_render.h"
#include "world/gs_world_streaming.h"
#include "world/streaming_manager.h"

#include "imgui.h"

#include <cmath>
#include <vector>

#include <Jolt/Physics/Collision/Shape/Shape.h>

#include <Jolt/Physics/Collision/Shape/StaticCompoundShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>

#include <Jolt/Jolt.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/RegisterTypes.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/PhysicsSettings.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyInterface.h>
#include <Jolt/Physics/Body/BodyID.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/OffsetCenterOfMassShape.h>
#include <Jolt/Physics/Collision/Shape/HeightFieldShape.h>
#include <Jolt/Physics/Collision/CollideShape.h>
#include <Jolt/Physics/Body/BodyFilter.h>
#include <Jolt/Physics/Collision/Shape/MeshShape.h>
#include <Jolt/Physics/Collision/CollideShape.h>
#include <Jolt/Physics/Collision/CollisionCollectorImpl.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/GroupFilterTable.h>
#include <Jolt/Physics/Constraints/FixedConstraint.h>
#include <Jolt/Physics/Collision/ContactListener.h>
#include <Jolt/Physics/EActivation.h>

namespace {
    // Terrain replaces terrain, so another sector's heightfield never counts.
    class NonTerrainBodyFilter : public JPH::BodyFilter {
    public:
        bool ShouldCollideLocked( const JPH::Body &inBody ) const override {
            const JPH::Shape *pShape = inBody.GetShape();
            return pShape != nullptr &&
                   pShape->GetSubType() != JPH::EShapeSubType::HeightField;
        }
    };

    // Static world geometry plus the wheel-only road: the drivable surface
    // inside a tunnel is a ROAD body, and keeping terrain over it would wall
    // the tunnel off exactly like the bug this replaces.
    class StaticOrRoadLayerFilter : public JPH::ObjectLayerFilter {
    public:
        bool ShouldCollide( JPH::ObjectLayer inLayer ) const override {
            return inLayer == TDUPhysLayers::NON_MOVING ||
                   inLayer == TDUPhysLayers::ROAD;
        }
    };

    // Equivalent of hkCollisionListener used by the original game.
    class PhysicsFXContactListener : public JPH::ContactListener {
    public:
        void OnContactAdded( const JPH::Body &inBody1,
                             const JPH::Body &inBody2,
                             const JPH::ContactManifold &inManifold,
                             JPH::ContactSettings & ) override {
            emit( inBody1, inBody2, inManifold, PFXEK_Impact );
        }

        void OnContactPersisted( const JPH::Body &inBody1,
                                 const JPH::Body &inBody2,
                                 const JPH::ContactManifold &inManifold,
                                 JPH::ContactSettings & ) override {
            // A contact that survives across steps is a scrape/slide, which is
            // what the original's looping kind describes.
            emit( inBody1, inBody2, inManifold, PFXEK_Slide );
        }

    private:
        static int32_t ResolveMaterial( const JPH::Body &body,
                                        const JPH::SubShapeID &subShapeID ) {
            uint32_t code = TDUMaterialValue::kDefault;

            const JPH::Shape *pShape = body.GetShape();
            if ( pShape != nullptr ) {
                code = TDUPhysicsMaterials::codeOf(
                    pShape->GetMaterial( subShapeID ) );
            }

            if ( code == TDUMaterialValue::kDefault &&
                 ( body.GetUserData() & TDUSurface::kClassMask ) ==
                     TDUSurface::Vehicle ) {
                code = static_cast<uint32_t>( eMaterialType::MT_Body );
            }

            return static_cast<int32_t>( code );
        }

        static float GetMass( const JPH::Body &body ) {
            // NOTE: Static and kinematic bodies should report 0
            if ( !body.IsDynamic() ) {
                return 0.0f;
            }
            const JPH::MotionProperties *pMotion = body.GetMotionProperties();
            if ( pMotion == nullptr ) {
                return 0.0f;
            }
            const float inverseMass = pMotion->GetInverseMass();
            return ( inverseMass > 0.0f ) ? ( 1.0f / inverseMass ) : 0.0f;
        }

        static Eigen::Vector4f ToVector4( const JPH::Vec3 &v, float w = 0.0f ) {
            return Eigen::Vector4f( v.GetX(), v.GetY(), v.GetZ(), w );
        }

        static void emit( const JPH::Body &inBody1,
                          const JPH::Body &inBody2,
                          const JPH::ContactManifold &inManifold,
                          ePhysicsFXEventKind kind ) {
            if ( gpPhysicsFX == nullptr || !gpPhysicsFX->isReady() ) {
                return;
            }
            if ( inManifold.mRelativeContactPointsOn1.empty() ) {
                return;
            }

            const int32_t materialA =
                ResolveMaterial( inBody1, inManifold.mSubShapeID1 );
            const int32_t materialB =
                ResolveMaterial( inBody2, inManifold.mSubShapeID2 );
            const int32_t fxMaterialA = GSPhysicsFX::RemapMaterial( materialA );
            const int32_t fxMaterialB = GSPhysicsFX::RemapMaterial( materialB );

            if ( !gpPhysicsFX->hasEffectFor( fxMaterialA, fxMaterialB,
                                             kind ) ) {
                OTDU_LOG_ERROR( "No entry: mat %d/%d -> fx %d/%d kind %d "
                                "(userData %llu/%llu)!!\n",
                                materialA, materialB, fxMaterialA, fxMaterialB,
                                int32_t( kind ),
                                (unsigned long long)inBody1.GetUserData(),
                                (unsigned long long)inBody2.GetUserData() );
                return;
            }

            const JPH::RVec3 contactPoint =
                inManifold.GetWorldSpaceContactPointOn1( 0 );

            const JPH::Vec3 velocity1 =
                inBody1.GetPointVelocity( contactPoint );
            const JPH::Vec3 velocity2 =
                inBody2.GetPointVelocity( contactPoint );

            const float energy = std::fabs(
                ( velocity1 - velocity2 ).Dot( inManifold.mWorldSpaceNormal ) );

            PhysicsFXEvent event;
            event.FXMaterialA = fxMaterialA;
            event.FXMaterialB = fxMaterialB;
            event.pBodyA = &inBody1;
            event.pBodyB = &inBody2;
            event.Kind = kind;
            event.Energy = energy;

            event.MassA = GetMass( inBody1 );
            event.MassB = GetMass( inBody2 );
            const float heaviest =
                ( event.MassA > event.MassB ) ? event.MassA : event.MassB;
            if ( event.MassA == 0.0f ) {
                event.MassA = heaviest;
            }
            if ( event.MassB == 0.0f ) {
                event.MassB = heaviest;
            }

            event.VelocityA = ToVector4( velocity1 );
            event.VelocityB = ToVector4( velocity2 );

            const Eigen::Vector4f shiftOrigin = GSPhysics::GetShiftOrigin();
            event.ContactPosition =
                ToVector4( JPH::Vec3( contactPoint ), 1.0f ) + shiftOrigin;
            event.ContactPosition.w() = 1.0f;
            event.Normal = ToVector4( inManifold.mWorldSpaceNormal );

            gpPhysicsFX->getEventQueue().push( event );
        }
    };
} // namespace

GSPhysics gPhysics = {}; // DAT_0129c0ac
GSPhysics *gpPhysics = nullptr;

bool gDrawCollisionVis = false;
bool gDrawCollisionVisTerrain = false;

ePhysicsThreadMode gPhysicsThreadPriority = PTM_Off; // DAT_010e765c
int32_t gPhysicsThreadFrequency = 0x64;              // DAT_00f87ffc

int32_t DAT_010e7640 = 0;        // DAT_010e7640
int32_t INT_00f87fec = 0x200;    // DAT_00f87fec
int32_t INT_00f87ff4 = 0x500000; // DAT_00f87ff4
int32_t INT_00f87ff8 = 0x1;      // INT_00f87ff8
bool BOOL_010e7644 = false;      // DAT_010e7644

static float gPhysicsRefreshRate = 0.0f;                   // DAT_0168d6b4
static float gPhysicsStepPeriod = 0.0f;                    // DAT_0168d6b8
static bool gbPhysicsUseMeasuredDelta = true;              // DAT_00f8a2e8
static std::atomic<bool> gbPhysicsTickInProgress{ false }; // DAT_010e7a58+0x1b0

static int32_t DAT_010e7668 = 0; // DAT_010e7668

static int32_t gGMeterAvailableBits = 0; // DAT_016a9878

bool gbShowGMeter = false;                            // DAT_010e7620
static Device *gpGMeterKeyboard = nullptr;            // DAT_016a97d0
static InputChannel *gpChannelRightControl = nullptr; // DAT_016a97cc
static InputChannel *gpChannelLeftControl = nullptr;  // DAT_016a97c8
static InputChannel *gpChannelG = nullptr;            // DAT_016a97c4

// Global time multiplier (1.0 = real-time; < 1.0 = slow-mo).
static float gPhysicsTimeScale = 1.0f; // DAT_00f8a2ec

// TEMP bring-up diagnostic counters for sector collision orientation.
static int32_t gTallBodyCount = 0;
static int32_t gFlatBodyCount = 0;
static float gMaxBodyY = 0.0f;

// If incoming dt exceeds this, clamp to kPhysicsMaxDt.
static constexpr float kPhysicsDtClampThreshold =
    1.0f / 45.0f; // DAT_00f41c08 (0x3cb60b61)

// Clamped dt value used when the step would be too large.
static constexpr float kPhysicsMaxDt =
    1.0f / 90.0f; // DAT_00f41c04 (0x3c360b61)

// Fixed-step cap in GSPhysics::Tick's no-physics-thread path.
static constexpr int32_t kMaxPhysicsSubSteps = 5;

constexpr uint32_t kSR20Magic = 0x30325253; // "SR20"
constexpr uint32_t kER20Magic = 0x30325245; // "ER20"
constexpr int32_t kLongSteps = 10; // Bezier longitudinal divisions/record

static constexpr float kHeightmapSeaLevel = -1.0f;     // FLOAT_00f41664
static constexpr float kHeightmapFlatThreshold = 0.1f; // FLOAT_00f41668
static constexpr float kTileProbeHalfHeight = 25.0f;
static constexpr float kSupportProbeRise = 3.0f;
static constexpr float kSupportProbeDepth = 40.0f;

static void FUN_00607370() {
    // FUN_00607370
    // Some kind of global linked list to keep track of mutexes?
    // Does not seem to be useful at runtime (so it could be a debug leftover)
    OTDU_UNIMPLEMENTED;
}

static void FUN_006072c0() {
    // FUN_006072c0
    // Same as FUN_00607370 (registers to the same global list) but registers a
    // thread
    OTDU_UNIMPLEMENTED;
}

using PhysicsClock_t = std::chrono::steady_clock;

// Replaces raw QueryPerformanceCounter calls from the original pcode
static PhysicsClock_t::time_point gPhysicsLastStepTime = {};
static bool gbPhysicsLastStepTimeValid = false;

void FUN_0081ee50( GSPhysics *pPhysics ) {
    // FUN_0081ee50
    if ( gbPhysicsTickInProgress.exchange( true ) ) {
        return;
    }

    const PhysicsClock_t::time_point now = PhysicsClock_t::now();
    if ( !gbPhysicsLastStepTimeValid ) {
        gPhysicsLastStepTime = now;
        gbPhysicsLastStepTimeValid = true;
    }

    const float elapsed =
        std::chrono::duration<float>( now - gPhysicsLastStepTime ).count();

    if ( gPhysicsStepPeriod * 0.5f <= elapsed ) {
        gPhysicsLastStepTime = now;

        if ( !pPhysics->bFreezeSimulation ) {
            float stepDelta = 0.0f;
            if ( !pPhysics->bPausePhysicsThread && !pPhysics->bPauseJoltTick ) {
                stepDelta =
                    gbPhysicsUseMeasuredDelta ? elapsed : gPhysicsStepPeriod;
            }

            pPhysics->physicsStep( stepDelta, 0.0f );
        }
    }

    gbPhysicsTickInProgress = false;
}

void *FUN_0081f0b0( void *pArg ) {
    // FUN_0081f0b0
    GSPhysics *pPhysics = reinterpret_cast<GSPhysics *>( pArg );

    gPhysicsRefreshRate = 1.0f / static_cast<float>( gPhysicsThreadFrequency );
    gPhysicsStepPeriod = 1.0f / static_cast<float>( gPhysicsThreadFrequency );

    gbPhysicsTickInProgress = false;
    gbPhysicsLastStepTimeValid = false;

    const auto period = std::chrono::duration_cast<PhysicsClock_t::duration>(
        std::chrono::duration<float>( gPhysicsStepPeriod ) );

    PhysicsClock_t::time_point nextWake = PhysicsClock_t::now() + period;
    while ( true ) {
        std::this_thread::sleep_until( nextWake );
        nextWake += period;

        const PhysicsClock_t::time_point now = PhysicsClock_t::now();
        if ( nextWake < now ) {
            nextWake = now + period;
        }

        if ( pPhysics->field27335_0x8670 == (void *)1 ||
             pPhysics->simulationThread.isStopRequested() ) {
            pPhysics->field27335_0x8670 = (void *)2;
            break;
        }

        FUN_0081ee50( pPhysics );
    }

    return nullptr;
}

static void *
JoltReallocate( void *inBlock, size_t inOldSize, size_t inNewSize ) {
    return TestDrive::Realloc( inBlock, inNewSize );
}

struct PropConstraintRecord {
    JPH::Ref<JPH::FixedConstraint> pConstraint;
    JPH::BodyID BodyA;
    JPH::BodyID BodyB;
    float Threshold = 0.0f;
    bool bBreakable = false;
};
std::vector<PropConstraintRecord> gPropConstraints;

// Shared collision filter: members of the same placed instance (same group
// id) never collide with each other while joined; different instances (and
// the car/world, which carry no filter) collide normally.
JPH::Ref<JPH::GroupFilterTable> gPropGroupFilter;
uint32_t gNextPropGroupID = 1;

// Max members of one prop instance that get mutually-disabled self-collision
// (273 member bodies / 93 mechas ~= 3 each; 32 is a safe ceiling).
constexpr uint32_t kMaxPropMembers = 32;
constexpr float kBreakThresholdScale = 1.0f;

JPH::GroupFilterTable *GetPropGroupFilter() {
    if ( gPropGroupFilter == nullptr ) {
        gPropGroupFilter = new JPH::GroupFilterTable( kMaxPropMembers );
        for ( uint32_t i = 0; i < kMaxPropMembers; i++ ) {
            for ( uint32_t j = i + 1; j < kMaxPropMembers; j++ ) {
                gPropGroupFilter->DisableCollision( i, j );
            }
        }
    }
    return gPropGroupFilter.GetPtr();
}

void BuildMechaMembers( const HavokContainer &container,
                        int32_t entryCount,
                        HavokFactoryData *pData ) {
    const uint8_t *pBuf = container.getVehicleBuffer();
    const uint32_t bufSize = container.getVehicleBufferSize();
    if ( pBuf == nullptr || bufSize < 0x40 ) {
        return; // no descriptors (e.g. a plain vehicle factory)
    }

    auto readU32 = [&]( uint32_t off ) -> uint32_t {
        return *reinterpret_cast<const uint32_t *>( pBuf + off );
    };

    uint32_t o = 0;
    bool bLayoutOk = true;

    while ( o + 0x40 <= bufSize ) {
        const uint32_t regIdx = readU32( o + 0x00 );
        const uint32_t flags = readU32( o + 0x04 );
        const uint32_t marker = readU32( o + 0x10 );

        if ( regIdx >= static_cast<uint32_t>( entryCount ) ||
             container.getEntry( static_cast<int32_t>( regIdx ) ).Type !=
                 ePhysEntityType::PET_Vehicle ||
             marker != 0xFFFFFFFFu ) {
            bLayoutOk = false;
            break;
        }

        const uint32_t rbCount = readU32( o + 0x30 );
        const uint32_t rbCount2 = readU32( o + 0x34 );
        const uint32_t refsOff = o + 0x40;
        if ( rbCount > 4096 || rbCount2 > 4096 ||
             refsOff + ( static_cast<uint64_t>( rbCount ) + rbCount2 ) * 0x10 >
                 bufSize ) {
            bLayoutOk = false;
            break;
        }

        std::vector<HavokToJolt::MechaMember> members;
        members.reserve( rbCount );
        for ( uint32_t r = 0; r < rbCount; r++ ) {
            HavokToJolt::MechaMember mm;
            mm.BodyHashcode = *reinterpret_cast<const uint64_t *>(
                pBuf + refsOff + r * 0x10 );
            members.push_back( std::move( mm ) );
        }

        const uint32_t mapOff = refsOff + rbCount * 0x10;
        for ( uint32_t m = 0; m < rbCount2; m++ ) {
            const uint32_t entryOff = mapOff + m * 0x10;
            const uint64_t logicID =
                *reinterpret_cast<const uint64_t *>( pBuf + entryOff + 0x00 );
            const uint32_t k = readU32( entryOff + 0x08 );
            if ( k < rbCount ) {
                members[k].LogicIDs.push_back( logicID );
            }
        }

        uint32_t next = refsOff + ( rbCount + rbCount2 ) * 0x10;
        if ( ( flags & 1u ) != 0 ) {
            if ( next + 0x10 > bufSize ) {
                bLayoutOk = false;
                break;
            }
            const uint32_t cCount = readU32( next + 0x00 );
            const uint32_t cCount2 = readU32( next + 0x04 );
            next += 0x10 + ( cCount + cCount2 ) * 0x10;
        }
        if ( next > bufSize ) {
            bLayoutOk = false;
            break;
        }

        const PhysFactory::Entry e =
            container.getEntry( static_cast<int32_t>( regIdx ) );
        if ( !members.empty() ) {
            pData->MechaMembers.emplace_back( e.Hashcode,
                                              std::move( members ) );
        }
        o = next;
    }

    if ( !bLayoutOk || o != bufSize ) {
        OTDU_LOG_WARN( "Prop factory: Mecha buffer parse drifted (consumed "
                       "%u/%u bytes)\n",
                       o, bufSize );
    }
}

GSPhysics::GSPhysics()
    : GameSystem(),
      Bank(),
      isLocked( 0 ),
      numTires( 0 ),
      numPhysicsVehicle( 0 ),
      pPhysicsVehicles( nullptr ),
      pPhysicsVehiclesBegin( nullptr ),
      pPhysicsVehiclesEnd( nullptr ),
      field_0x2cc( nullptr ),
      numFreeSectors( 0 ),
      maxNumSectors( 0 ),
      pSectors( nullptr ),
      pSectorsBegin( nullptr ),
      pSectorsEnd( nullptr ),
      numFreeRoadSectors( 0 ),
      maxNumRoadSectors( 0 ),
      pRoads( nullptr ),
      pRoadsBegin( nullptr ),
      pRoadsEnd( nullptr ),
      field27335_0x8670( nullptr ),
      field_0x31a0{ 0, 0 },
      field_0x4dc0{ 0, 0 },
      field_0x5bd0{ 0, 0 },
      field_0x69e0{ 0, 0 },
      field_0x8600{ 0, 0 },
      bJoltInit( false ),
      bFreezeSimulation( false ),
      bPauseJoltTick( false ),
      bHeightfieldInit( false ),
      bSectorsInit( false ),
      bHavokDebugEnabled( false ),
      bDrawDebugInfos( false ) {
    // FUN_008471e0
    // Original: initializes factories, Havok memory masters,
    // thread, mutex, and various flags. Havok memory subsystem init is not
    // needed for Jolt.
    pTireProfiles.fill( nullptr );

    for ( auto &factory : pFactories ) {
        factory.reset();
    }

    gpPhysics = this;
}

GSPhysics::~GSPhysics() {
    // FUN_00419310
    if ( gpWorldStreaming != nullptr ) {
        gpWorldStreaming->setPhysicsCallback( nullptr );
    }
    gpPhysics = nullptr;
}

bool GSPhysics::initialize( TestDriveGameInstance *param_2 ) {
    // FUN_0084d570
    pGameInstance = param_2;
    bJoltInit = false;
    bPauseJoltTick = true;
    bFreezeSimulation = true;
    field27335_0x8670 = nullptr;
    bPaused = false;

    if ( gpWorldStreaming != nullptr ) {
        gpWorldStreaming->setPhysicsCallback( this );
    }

    OTDU_LOG_INFO( "Intall BEGIN\n" ); // DAT_00f2794c

    // NOTE: Original P-Code initializes Havok-specific allocator
    // (hkMemoryRouter) to use the game allocator. This is the equivalent with
    // Jolt.
    JPH::Allocate = TestDrive::Alloc;
    JPH::Reallocate = JoltReallocate;
    JPH::Free = TestDrive::Free;
    JPH::AlignedAllocate = TestDrive::AllocAligned;
    JPH::AlignedFree = TestDrive::FreeAligned;

    OTDU_LOG_INFO( "Install Pacejaka Tire models\n" ); // DAT_00f2792c

    std::string acStack_200 = gpConfig->getResRootPath();
    acStack_200 += "Physics/tires.bnk";

    loadBootBank( acStack_200.c_str(), nullptr );
    numTires = 0;

    if ( bLoaded ) {
        void *peVar4 = getFirstEntry( AG_Physics, AT_TireProfile, nullptr );
        while ( peVar4 != nullptr ) {
            pTireProfiles[numTires] = (TirePacejka *)peVar4;
            numTires++;

            peVar4 = getNextEntry( AG_Physics, AT_TireProfile, nullptr );
        }
    }

    OTDU_LOG_INFO( "Initialise Physics Cars\n" ); // DAT_00f27900

    numPhysicsVehicle = kMaxNumPhysicsVehicles;
    pPhysicsVehicles =
        new ( TestDrive::Alloc( sizeof( PhysicsVehicle ) * numPhysicsVehicle ) )
            PhysicsVehicle[numPhysicsVehicle];
    pPhysicsVehiclesBegin = pPhysicsVehicles;
    pPhysicsVehiclesEnd = pPhysicsVehicles + numPhysicsVehicle;
    field_0x2cc = nullptr;
    for ( auto it = pPhysicsVehiclesBegin; it != pPhysicsVehiclesEnd; it++ ) {
        it->initialize();
    }

    OTDU_LOG_INFO( "HeightFields Creation\n" ); // DAT_00f278e8
    bHeightfieldInit = true;
    createHeightfields();

    OTDU_LOG_INFO( "Sectors Creation\n" ); // DAT_00f278d4

    for ( auto &factory : pFactories ) {
        factory.reset();
    }

    numFreeSectors = 0x40;
    maxNumSectors = 0x40;
    DAT_010e7640 = 0;

    pSectors = new ( TestDrive::Alloc( sizeof( PhysicsSHK ) * maxNumSectors ) )
        PhysicsSHK[maxNumSectors];
    pSectorsBegin = pSectors;
    pSectorsEnd = pSectors + maxNumSectors;

    for ( auto it = pSectorsBegin; it != pSectorsEnd; it++ ) {
        it->MaxNumObjects = kMaxNumObjectsPerSector;
        it->NumObjects = 0;
        it->pObjects = new ( TestDrive::Alloc( sizeof( PhysicsSectorObj ) *
                                               kMaxNumObjectsPerSector ) )
            PhysicsSectorObj[kMaxNumObjectsPerSector];
        OTDU_ASSERT( it->pObjects );

        it->pObjectsBegin = it->pObjects;
        it->pObjectsEnd = it->pObjects + it->MaxNumObjects;
        it->pFilestream = nullptr;
    }

    OTDU_LOG_INFO( "Road Sectors Creation\n" ); // DAT_00f278bc
    maxNumRoadSectors = 7;
    numFreeRoadSectors = 7;

    pRoads =
        new ( TestDrive::Alloc( sizeof( PhysicsSHKRoad ) * maxNumRoadSectors ) )
            PhysicsSHKRoad[maxNumRoadSectors];
    pRoadsBegin = pRoads;
    pRoadsEnd = pRoads + maxNumRoadSectors;

    bSectorsInit = true;

    for ( auto it = pRoadsBegin; it != pRoadsEnd; it++ ) {
        it->MaxNumObjects = kMaxNumObjectsPerSector;
        it->NumObjects = 0;
        it->pObjects = new ( TestDrive::Alloc( sizeof( PhysicsRoadObj ) *
                                               kMaxNumObjectsPerSector ) )
            PhysicsRoadObj[kMaxNumObjectsPerSector];
        OTDU_ASSERT( it->pObjects );

        it->pObjectsBegin = it->pObjects;
        it->pObjectsEnd = it->pObjects + it->MaxNumObjects;
        it->pFilestream = nullptr;
    }

    initializeJolt();

    isLocked = 0;
    FUN_00607370();
    if ( !simulationThread.isValid() && gPhysicsThreadPriority != PTM_Off ) {
        simulationThread.setName( "PhysThread" );
        simulationThread.initialize(
            (TestDriveThread::StartRoutine_t)&FUN_0081f0b0, this, 0x40000, 0 );

        simulationThread.setPriority( TP_Critical );
        FUN_006072c0();
    }

    if ( bHavokDebugEnabled ) {
        // TODO: I think this is the logic to init/enable Havok Remote debugger
        // We could swap this with whatever debug features Jolt offers
        OTDU_UNIMPLEMENTED;
    }

    field_0x31a0[0] = 0;
    field_0x31a0[1] = 0;

    field_0x4dc0[0] = 0;
    field_0x4dc0[1] = 0;

    field_0x5bd0[0] = 0;
    field_0x5bd0[1] = 0;

    field_0x69e0[0] = 0;
    field_0x69e0[1] = 0;

    field_0x8600[0] = 0;
    field_0x8600[1] = 0;

    OTDU_LOG_INFO( "Road Phantom Manager Creation\n" ); // DAT_00f27884

    roadPhantomManager.initialize( 0x41a, 4 );

    if ( bDrawDebugInfos ) {
        OTDU_UNIMPLEMENTED;
    }

    bFreezeSimulation = false;
    BOOL_010e7644 = true;

    return true;
}

void GSPhysics::terminate() {
    // FUN_00846cf0
    bFreezeSimulation = true;
    bPauseJoltTick = true;

    if ( gpWorldStreaming != nullptr ) {
        gpWorldStreaming->setPhysicsCallback( nullptr );
    }

    if ( gPhysicsThreadPriority != PTM_Off && simulationThread.isValid() ) {
        field27335_0x8670 = (void *)1;
        while ( field27335_0x8670 != (void *)2 ) {
            std::this_thread::yield();
        }
        simulationThread.terminate();
    }

    if ( pCollisionDebugList != nullptr ) {
        pCollisionDebugList->destroy();
        delete pCollisionDebugList;
        pCollisionDebugList = nullptr;
    }

    // Reset all factories (destroy any Havok/physics objects they hold)
    for ( auto &factory : pFactories ) {
        factory.reset();
    }

    // Free sector collision object arrays
    if ( pSectors != nullptr ) {
        for ( auto it = pSectorsBegin; it != pSectorsEnd; it++ ) {
            if ( it->pObjects != nullptr ) {
                it->NumObjects = 0;
                it->MaxNumObjects = 0;
                TestDrive::Free( it->pObjects );
                it->pObjects = nullptr;
                it->pObjectsBegin = nullptr;
                it->pObjectsEnd = nullptr;
            }
        }
        TestDrive::Free( pSectors );
        pSectors = nullptr;
        pSectorsBegin = nullptr;
        pSectorsEnd = nullptr;
    }
    maxNumSectors = 0;
    numFreeSectors = 0;
    DAT_010e7640 = 0;

    // Free road sector collision object arrays
    if ( pRoads != nullptr ) {
        for ( auto it = pRoadsBegin; it != pRoadsEnd; it++ ) {
            if ( it->pObjects != nullptr ) {
                it->NumObjects = 0;
                it->MaxNumObjects = 0;
                TestDrive::Free( it->pObjects );
                it->pObjects = nullptr;
                it->pObjectsBegin = nullptr;
                it->pObjectsEnd = nullptr;
            }
        }
        TestDrive::Free( pRoads );
        pRoads = nullptr;
        pRoadsBegin = nullptr;
        pRoadsEnd = nullptr;
    }
    maxNumRoadSectors = 0;
    numFreeRoadSectors = 0;

    // Free physics car array
    if ( pPhysicsVehicles != nullptr ) {
        TestDrive::Free( pPhysicsVehicles );
        pPhysicsVehicles = nullptr;
        pPhysicsVehiclesBegin = nullptr;
        pPhysicsVehiclesEnd = nullptr;
    }
    numPhysicsVehicle = 0;

    // Remove and destroy Jolt bodies before tearing down the physics system
    if ( pPhysicsSystem != nullptr ) {
        JPH::BodyInterface &bodyInterface = pPhysicsSystem->GetBodyInterface();

        if ( worldRefBody != nullptr ) {
            JPH::Body *pBody = static_cast<JPH::Body *>( worldRefBody );
            bodyInterface.RemoveBody( pBody->GetID() );
            bodyInterface.DestroyBody( pBody->GetID() );
            worldRefBody = nullptr;
        }

        if ( infinitePlaneBodyID != JPH::BodyID::cInvalidBodyID ) {
            JPH::BodyID planeID( infinitePlaneBodyID );
            bodyInterface.RemoveBody( planeID );
            bodyInterface.DestroyBody( planeID );
            infinitePlaneBodyID = JPH::BodyID::cInvalidBodyID;
        }

        for ( auto &hf : pHeightfields ) {
            if ( hf.pBody != nullptr ) {
                JPH::Body *pBody = static_cast<JPH::Body *>( hf.pBody );
                bodyInterface.RemoveBody( pBody->GetID() );
                bodyInterface.DestroyBody( pBody->GetID() );
                hf.pBody = nullptr;
                hf.pKey = nullptr;
            }
        }

        pPhysicsSystem->SetContactListener( nullptr );

        delete pPhysicsSystem;
        pPhysicsSystem = nullptr;
    }

    delete pContactListener;
    pContactListener = nullptr;

    delete pJobSystem;
    pJobSystem = nullptr;

    delete pTempAllocator;
    pTempAllocator = nullptr;

    delete pBpLayerInterface;
    pBpLayerInterface = nullptr;

    delete pObjectVsBPLayerFilter;
    pObjectVsBPLayerFilter = nullptr;

    delete pObjectLayerPairFilter;
    pObjectLayerPairFilter = nullptr;

    JPH::UnregisterTypes();
    if ( JPH::Factory::sInstance != nullptr ) {
        delete JPH::Factory::sInstance;
        JPH::Factory::sInstance = nullptr;
    }

    bJoltInit = false;
}

void GSPhysics::tick( float totalTime, float deltaTime ) {
    // FUN_0084de60

    // Pause toggle: a deltaTime of 0 is the engine's sentinel for "simulation
    // frozen" (paused frame / hitch with no elapsed time). The original tests
    // the delta argument, not the accumulated total.
    bPauseJoltTick = ( deltaTime == 0.0f );

    // Sub-stepped physics — only when no dedicated physics thread is running.
    // With PTM_Normal / PTM_Realtime the dedicated thread drives physicsStep.
    if ( gPhysicsThreadPriority == PTM_Off && gPhysicsThreadFrequency > 0 ) {
        const float stepDt =
            1.0f / static_cast<float>( gPhysicsThreadFrequency );

        int32_t numSteps = static_cast<int32_t>( deltaTime / stepDt );
        if ( numSteps >= kMaxPhysicsSubSteps + 1 ) {
            numSteps = kMaxPhysicsSubSteps;
        }

        if ( numSteps == 0 ) {
            physicsStep( deltaTime, totalTime );
        } else if ( numSteps > 0 ) {
            float accTime = totalTime;
            for ( int32_t i = 0; i < numSteps; i++ ) {
                physicsStep( stepDt, accTime );
                accTime += stepDt;
            }
        }
    }

    // Post-step debug hook (FUN_00846590 — GMAITrainer update)
    if ( DAT_010e7668 != 0 ) {
        OTDU_UNIMPLEMENTED;
    }

    // G-meter overlay init (see FUN_009078f0)
    if ( ( gGMeterAvailableBits & 1 ) == 0 ) {
        gGMeterAvailableBits |= 1;
    }

    OTDU_UNIMPLEMENTED; // TODO: AI trainer + back-on-track

    updateGMeterToggle();

    if ( bHavokDebugEnabled ) {
        OTDU_UNIMPLEMENTED;
    }
}

void GSPhysics::updateGMeterToggle() {
    // FUN_0084de60
    if ( ( gGMeterAvailableBits & 2 ) == 0 ) {
        gGMeterAvailableBits |= 2;
        gpGMeterKeyboard = gEdDev.getDevice( eDeviceType::DT_Keyboard, 0 );
    }

    if ( gpGMeterKeyboard == nullptr ) {
        return;
    }

    if ( ( gGMeterAvailableBits & 4 ) == 0 ) {
        gGMeterAvailableBits |= 4;
        gpChannelRightControl = gpGMeterKeyboard->getChannel( "RCTRL" );
    }
    if ( ( gGMeterAvailableBits & 8 ) == 0 ) {
        gGMeterAvailableBits |= 8;
        gpChannelLeftControl = gpGMeterKeyboard->getChannel( "LCTRL" );
    }
    if ( ( gGMeterAvailableBits & 0x10 ) == 0 ) {
        gGMeterAvailableBits |= 0x10;
        gpChannelG = gpGMeterKeyboard->getChannel( "G" );
    }

    if ( gpChannelRightControl == nullptr || gpChannelLeftControl == nullptr ||
         gpChannelG == nullptr ) {
        return;
    }

    const bool bControlDown = ( gpChannelLeftControl->Flags & 0x10 ) != 0 ||
                              ( gpChannelRightControl->Flags & 0x10 ) != 0;
    if ( bControlDown && static_cast<int8_t>( gpChannelG->Flags ) < 0 ) {
        gbShowGMeter = !gbShowGMeter;
    }
}

#if OTDU_DEVBUILD
inline ColorRGBA PackDebugColor( uint8_t r, uint8_t g, uint8_t b ) {
    return ColorRGBA( b, g, r, 0xff );
}

// Per-collision-body color
static uint32_t CollisionBodyColor( JPH::EShapeSubType subType,
                                    JPH::ObjectLayer layer,
                                    uint64_t userData ) {
    if ( subType == JPH::EShapeSubType::HeightField ) {
        return PackDebugColor( 90, 90, 90 ).Color; // terrain: grey
    }
    if ( layer == TDUPhysLayers::MOVING || layer == TDUPhysLayers::VEHICLE ) {
        return PackDebugColor( 255, 140, 0 ).Color; // props / car: orange
    }
    switch ( userData ) {
    case TDUSurface::RoadMesh:
        return PackDebugColor( 60, 120, 255 ).Color; // road mesh: blue
    case TDUSurface::Paved:
    case TDUSurface::Generic:
        return PackDebugColor( 40, 230, 40 ).Color; // world .shk: green
    default:
        return PackDebugColor( 200, 200, 40 ).Color; // other static: yellow
    }
}

constexpr uint32_t kMaxDebugLineVerts = 300000u; // 150k segments

void GSPhysics::drawCollisionShapes() {
    if ( pPhysicsSystem == nullptr || gpRender == nullptr ) {
        return;
    }

    struct BodySnapshot {
        JPH::RefConst<JPH::Shape> shape;
        JPH::Mat44 com;
        uint32_t color;
        bool bMesh;
    };
    std::vector<BodySnapshot> snapshots;

    lockMutex();
    JPH::BodyIDVector bodyIDs;
    pPhysicsSystem->GetBodies( bodyIDs );
    const JPH::BodyInterface &bi = pPhysicsSystem->GetBodyInterfaceNoLock();
    snapshots.reserve( bodyIDs.size() );
    for ( const JPH::BodyID id : bodyIDs ) {
        JPH::RefConst<JPH::Shape> shape = bi.GetShape( id );
        if ( shape.GetPtr() == nullptr ) {
            continue;
        }
        const JPH::EShapeSubType st = shape->GetSubType();
        if ( st == JPH::EShapeSubType::HeightField &&
             !gDrawCollisionVisTerrain ) {
            continue;
        }
        BodySnapshot s;
        s.shape = shape;
        s.com = bi.GetCenterOfMassTransform( id );
        s.color = CollisionBodyColor( st, bi.GetObjectLayer( id ),
                                      bi.GetUserData( id ) );
        s.bMesh = ( st == JPH::EShapeSubType::Mesh );
        snapshots.push_back( std::move( s ) );
    }
    unlockMutex();

    // Lazily create the dynamic line list.
    if ( pCollisionDebugList == nullptr ) {
        pCollisionDebugList = new DrawList();
        if ( !pCollisionDebugList->initialize( 1, kMaxDebugLineVerts, 0, true,
                                               nullptr, "CollisionDebug",
                                               false ) ) {
            pCollisionDebugList->destroy();
            delete pCollisionDebugList;
            pCollisionDebugList = nullptr;
            return;
        }
    }

    // Gather line-segment endpoints (render space) into a reusable buffer.
    struct LineVert {
        float x, y, z;
        uint32_t color;
    };
    static std::vector<LineVert> verts;
    verts.clear();

    auto pushSeg = [&]( const JPH::Vec3 &a, const JPH::Vec3 &b,
                        uint32_t color ) {
        if ( verts.size() + 2 > kMaxDebugLineVerts ) {
            return;
        }
        verts.push_back( { a.GetX(), a.GetY(), a.GetZ(), color } );
        verts.push_back( { b.GetX(), b.GetY(), b.GetZ(), color } );
    };

    for ( const BodySnapshot &s : snapshots ) {
        if ( verts.size() + 2 > kMaxDebugLineVerts ) {
            break;
        }
        if ( s.bMesh ) {
            // Actual triangle wireframe (buildings/walls/road surfaces).
            JPH::Shape::GetTrianglesContext ctx;
            s.shape->GetTrianglesStart(
                ctx, JPH::AABox::sBiggest(), s.com.GetTranslation(),
                s.com.GetQuaternion(), JPH::Vec3::sReplicate( 1.0f ) );
            constexpr int kBatch = 64;
            JPH::Float3 tri[3 * kBatch];
            for ( ;; ) {
                const int n = s.shape->GetTrianglesNext( ctx, kBatch, tri );
                if ( n == 0 ) {
                    break;
                }
                for ( int t = 0; t < n; t++ ) {
                    const JPH::Vec3 a( tri[3 * t + 0].x, tri[3 * t + 0].y,
                                       tri[3 * t + 0].z );
                    const JPH::Vec3 b( tri[3 * t + 1].x, tri[3 * t + 1].y,
                                       tri[3 * t + 1].z );
                    const JPH::Vec3 c( tri[3 * t + 2].x, tri[3 * t + 2].y,
                                       tri[3 * t + 2].z );
                    pushSeg( a, b, s.color );
                    pushSeg( b, c, s.color );
                    pushSeg( c, a, s.color );
                }
                if ( verts.size() + 2 > kMaxDebugLineVerts ) {
                    break;
                }
            }
        } else {
            // OBB from the shape's local bounds (posts, boxes, capsules,
            // props, car)
            const JPH::AABox lb = s.shape->GetLocalBounds();
            JPH::Vec3 corner[8];
            for ( int i = 0; i < 8; i++ ) {
                const JPH::Vec3 local(
                    ( i & 1 ) ? lb.mMax.GetX() : lb.mMin.GetX(),
                    ( i & 2 ) ? lb.mMax.GetY() : lb.mMin.GetY(),
                    ( i & 4 ) ? lb.mMax.GetZ() : lb.mMin.GetZ() );
                corner[i] = s.com * local;
            }
            static const int kEdges[12][2] = {
                {0, 1},
                {1, 3},
                {3, 2},
                {2, 0}, // bottom
                {4, 5},
                {5, 7},
                {7, 6},
                {6, 4}, // top
                {0, 4},
                {1, 5},
                {2, 6},
                {3, 7}
            }; // verticals
            for ( const auto &e : kEdges ) {
                pushSeg( corner[e[0]], corner[e[1]], s.color );
            }
        }
    }

    pCollisionDebugList->reset();
    if ( verts.empty() ) {
        return;
    }
    pCollisionDebugList->setIdentityTransform();
    if ( !pCollisionDebugList->beginPrimitive(
             ePrimitiveType::PT_Line, static_cast<int32_t>( verts.size() ) ) ) {
        return;
    }
    for ( const LineVert &v : verts ) {
        pCollisionDebugList->setDiffuse( 0, ColorRGBA( v.color ) );
        pCollisionDebugList->pushVertex( v.x, v.y, v.z );
    }
    pCollisionDebugList->commitPrimitive();
    gpRender->getRenderPass<eRenderPass::RP_Near>()
        .pScene->enqueueDynamicDrawList( pCollisionDebugList );
}
#endif

void GSPhysics::draw( float deltaTime ) {
    // FUN_0084b650
    if ( gDrawCollisionVis ) {
#if OTDU_DEVBUILD
        drawCollisionShapes();
#endif
    }
    // Both debug flags must be set for any rendering to occur.
    // bHavokDebugEnabled is set by AI trainer (GMAITrainer::tick);
    // bDrawDebugInfos is a secondary opt-in. Both start false so this function
    // is a no-op until the game mode is implemented.
    if ( !bHavokDebugEnabled || !bDrawDebugInfos )
        return;

    OTDU_UNIMPLEMENTED;
}

void GSPhysics::FUN_00843b40() {
    // FUN_00843b40
    if ( pPhysicsSystem == nullptr )
        return;

    lockMutex();
    JPH::BodyInterface &bodyInterface = pPhysicsSystem->GetBodyInterface();
    for ( auto &hf : pHeightfields ) {
        if ( hf.pBody != nullptr ) {
            JPH::Body *pBody = static_cast<JPH::Body *>( hf.pBody );
            bodyInterface.RemoveBody( pBody->GetID() );
            bodyInterface.DestroyBody( pBody->GetID() );
            hf.pBody = nullptr;
            hf.pKey = nullptr;
        }
    }
    unlockMutex();
}

PhysFactory *GSPhysics::createFromBinary( void *param_3, int32_t param_2 ) {
    // FUN_008450b0 / FUN_00845240
    PhysicsLock lock;

    HavokContainer container;
    if ( !container.parse( param_3, static_cast<uint32_t>( param_2 ) ) ) {
        OTDU_LOG_WARN( "Failed to parse physics container blob\n" );
        return nullptr;
    }

    HavokPackfile packfile;
    if ( !packfile.parse( container.getHavokScene(),
                          container.getHavokSceneSize() ) ) {
        OTDU_LOG_WARN( "Failed to parse the embedded Havok scene\n" );
        return nullptr;
    }

    HavokFactoryData *pData = new HavokFactoryData();
    HavokToJolt::buildBodies( packfile, pData->Bodies );
    if ( pData->Bodies.empty() ) {
        OTDU_LOG_WARN( "Havok scene produced no collision bodies\n" );
        delete pData;
        return nullptr;
    }

    const int32_t entryCount = container.getEntryCount();
    for ( HavokToJolt::RigidBody &b : pData->Bodies ) {
        if ( b.RegistryIndex >= static_cast<uint32_t>( entryCount ) ) {
            continue;
        }
        const PhysFactory::Entry e =
            container.getEntry( static_cast<int32_t>( b.RegistryIndex ) );
        if ( e.Type == ePhysEntityType::PET_RigidBody ) {
            b.Hashcode = e.Hashcode;
        }
    }

    pData->Registry.reserve( entryCount );
    for ( int32_t i = 0; i < entryCount; i++ ) {
        const PhysFactory::Entry e = container.getEntry( i );
        pData->Registry.emplace_back( e.Hashcode,
                                      static_cast<uint32_t>( e.Type ) );
    }

    BuildMechaMembers( container, entryCount, pData );

    // Parse the scene's joints (hkConstraintInstance list)
    HavokToJolt::buildConstraints( packfile, pData->Constraints );
    for ( HavokToJolt::Constraint &c : pData->Constraints ) {
        if ( c.BodyARegistryIndex < static_cast<uint32_t>( entryCount ) ) {
            const PhysFactory::Entry ea = container.getEntry(
                static_cast<int32_t>( c.BodyARegistryIndex ) );
            if ( ea.Type == ePhysEntityType::PET_RigidBody ) {
                c.BodyAHashcode = ea.Hashcode;
            }
        }
        if ( c.BodyBRegistryIndex < static_cast<uint32_t>( entryCount ) ) {
            const PhysFactory::Entry eb = container.getEntry(
                static_cast<int32_t>( c.BodyBRegistryIndex ) );
            if ( eb.Type == ePhysEntityType::PET_RigidBody ) {
                c.BodyBHashcode = eb.Hashcode;
            }
        }
    }

    PhysFactory *pFactory = nullptr;
    for ( PhysFactory &factory : pFactories ) {
        if ( !factory.isInitialized() ) {
            pFactory = &factory;
            break;
        }
    }
    if ( pFactory == nullptr ) {
        OTDU_LOG_WARN( "No free physics factory slot for the vehicle blob\n" );
        delete pData;
        return nullptr;
    }

    pFactory->initialize( pData );
    OTDU_LOG_INFO( "Registered physics factory: %d collision bodies\n",
                   static_cast<int>( pData->Bodies.size() ) );
    return pFactory;
}

PhysicsVehicle *GSPhysics::createVehicle( PhysFactory *pFactory,
                                          const Matrix4x4f_t &worldTransform,
                                          const DBEntryCarPhysicsData &db,
                                          const void *pTorqueBinary ) {
    // FUN_0082fc80
    if ( pPhysicsSystem == nullptr ) {
        return nullptr;
    }

    HavokFactoryData *pData =
        ( pFactory != nullptr ) ? pFactory->getHavokData() : nullptr;
    const bool bHasFactoryShape =
        ( pData != nullptr && !pData->Bodies.empty() );

    JPH::ShapeSettings::ShapeResult shapeResult;
    if ( bHasFactoryShape ) {
        // Combine the reconstructed collision bodies into a single chassis
        // shape.
        JPH::StaticCompoundShapeSettings compound;
        for ( const HavokToJolt::RigidBody &body : pData->Bodies ) {
            compound.AddShape( body.Position, body.Rotation, body.Shape );
        }
        shapeResult = compound.Create();
    } else {
        // DB body dimensions are in millimetres (X=Width, Y=Height, Z=Length).
        const float kMmToM = 0.001f;
        const float halfW = ( db.Width > 0 ? db.Width : 1800 ) * 0.5f * kMmToM;
        const float halfH =
            ( db.Height > 0 ? db.Height : 1300 ) * 0.5f * kMmToM;
        const float halfL =
            ( db.Length > 0 ? db.Length : 4000 ) * 0.5f * kMmToM;

        const float kConvexRadius = 0.05f; // FLOAT_00f41778
        const JPH::Vec3 halfExtents( Max( 0.10f, halfW - 0.05f ),
                                     Max( 0.10f, halfH - 0.15f ),
                                     Max( 0.10f, halfL - 0.55f ) );
        JPH::BoxShapeSettings box( halfExtents, kConvexRadius );
        shapeResult = box.Create();
    }
    if ( !shapeResult.IsValid() ) {
        return nullptr;
    }
    const float bodyHeightM = ( db.Height > 0 ? db.Height : 1300 ) * 0.001f;
    const float bodyHalfHeight = bodyHeightM * 0.5f;
    const float gravHeightFrac =
        db.GravityCenterHeight > 0 ? db.GravityCenterHeight * 0.01f : 0.40f;
    const float gcHeight = JPH::Clamp(
        ( 1.0f - gravHeightFrac ) * 0.5f * bodyHeightM, 0.15f, 0.60f );
    const PhysicsVehicle::SuspensionSetup comFrontSusp =
        PhysicsVehicle::ComputeAxleSuspension( db, true );
    const PhysicsVehicle::SuspensionSetup comRearSusp =
        PhysicsVehicle::ComputeAxleSuspension( db, false );
    const float restLen =
        0.5f * ( comFrontSusp.restLength + comRearSusp.restLength );
    const float contactPlaneY = -( bodyHalfHeight + restLen );
    const float comOffsetY =
        JPH::Clamp( contactPlaneY + gcHeight, contactPlaneY + 0.10f, -0.02f );

    const float wheelBaseM =
        ( db.WheelBase > 0 ? db.WheelBase : 2600 ) * 0.001f;
    const float gcLengthFrac =
        db.GravityCenterLength > 0 ? db.GravityCenterLength * 0.01f : 0.5f;
    const float comOffsetZ =
        JPH::Clamp( ( gcLengthFrac - 0.5f ) * wheelBaseM, -0.175f * wheelBaseM,
                    0.175f * wheelBaseM );

    JPH::OffsetCenterOfMassShapeSettings comShapeSettings(
        JPH::Vec3( 0.0f, comOffsetY, comOffsetZ ), shapeResult.Get() );
    JPH::ShapeSettings::ShapeResult comResult = comShapeSettings.Create();
    if ( !comResult.IsValid() ) {
        return nullptr;
    }

    // World placement (row-major Matrix4x4f_t -> column-major Jolt), rebased
    // into shifted space.
    JPH::Mat44 world = JPH::Mat44::sIdentity();
    for ( int32_t c = 0; c < 4; c++ ) {
        world.SetColumn3( c, JPH::Vec3( worldTransform( c, 0 ),
                                        worldTransform( c, 1 ),
                                        worldTransform( c, 2 ) ) );
    }
    {
        const Eigen::Vector4f shiftOrigin = GetShiftOrigin();
        world.SetTranslation(
            world.GetTranslation() -
            JPH::Vec3( shiftOrigin.x(), shiftOrigin.y(), shiftOrigin.z() ) );
    }

    JPH::BodyCreationSettings settings(
        comResult.Get(), JPH::RVec3( world.GetTranslation() ),
        world.GetQuaternion().Normalized(), JPH::EMotionType::Dynamic,
        TDUPhysLayers::VEHICLE );
    const float chassisMass =
        db.Weight > 0 ? static_cast<float>( db.Weight ) : 1200.0f;
    settings.mMassPropertiesOverride.mMass = chassisMass;

    const JPH::Vec3 h = shapeResult.Get()->GetLocalBounds().GetExtent() +
                        JPH::Vec3::sReplicate( 0.05f );
    const float k = chassisMass / 3.0f;
    JPH::MassProperties mp;
    mp.mMass = chassisMass;
    mp.mInertia = JPH::Mat44::sZero();
    mp.mInertia( 0, 0 ) = k * ( h.GetY() * h.GetY() + h.GetZ() * h.GetZ() );
    mp.mInertia( 1, 1 ) = k * ( h.GetX() * h.GetX() + h.GetZ() * h.GetZ() );
    mp.mInertia( 2, 2 ) = k * ( h.GetX() * h.GetX() + h.GetY() * h.GetY() );
    mp.mInertia( 3, 3 ) = 1.0f;
    settings.mMassPropertiesOverride = mp;

    settings.mOverrideMassProperties =
        JPH::EOverrideMassProperties::MassAndInertiaProvided;

    settings.mMotionQuality = JPH::EMotionQuality::LinearCast;
    settings.mLinearDamping = 0.0f;
    settings.mAngularDamping = 0.05f;
    settings.mUserData = TDUSurface::Vehicle;

    JPH::BodyInterface &bodyInterface = pPhysicsSystem->GetBodyInterface();
    lockMutex();
    JPH::Body *pChassis = bodyInterface.CreateBody( settings );
    PhysicsVehicle *pCar = nullptr;
    if ( pChassis != nullptr ) {
        bodyInterface.AddBody( pChassis->GetID(), JPH::EActivation::Activate );
        for ( PhysicsVehicle *it = pPhysicsVehiclesBegin;
              it != pPhysicsVehiclesEnd; ++it ) {
            if ( !it->isActive() ) {
                pCar = it;
                break;
            }
        }
        if ( pCar == nullptr ||
             !pCar->create( pPhysicsSystem, pChassis, db, pTorqueBinary,
                            bHasFactoryShape ) ) {
            bodyInterface.RemoveBody( pChassis->GetID() );
            bodyInterface.DestroyBody( pChassis->GetID() );
            pCar = nullptr;
        }
    }
    unlockMutex();

    if ( pCar == nullptr ) {
        OTDU_LOG_WARN( "Failed to create vehicle (no chassis or free slot)\n" );
        return nullptr;
    }
    OTDU_LOG_INFO( "Created vehicle: %s chassis, mass=%.0f kg\n",
                   bHasFactoryShape ? "havok" : "db-box",
                   settings.mMassPropertiesOverride.mMass );
    return pCar;
}

void GSPhysics::destroyVehicle( PhysicsVehicle *pVehicle ) {
    if ( pVehicle == nullptr || !pVehicle->isActive() ) {
        return;
    }
    lockMutex();
    pVehicle->destroy();
    unlockMutex();
}

void GSPhysics::drawDebug( float ) {
    ImGui::Checkbox( "Collision wireframe (green=world blue=road orange=props)",
                     &gDrawCollisionVis );
    if ( gDrawCollisionVis ) {
        ImGui::SameLine();
        ImGui::Checkbox( "+terrain", &gDrawCollisionVisTerrain );
    }

    extern int32_t gVehicleDrivingHelpOverride;
    extern bool gVehicleABS;
    extern bool gVehicleAntiRoll;
    extern bool gVehicleEngineStateMachine;
    extern int gVehicleDiffTypeOverride; // 0=per-car, 1 open, 2 LSD, 4 spool
    extern float gVehicleDiffLockCoef;   // LSD/spool lock coef (override)
    extern float gVehicleAeroDragScale;  // debug: aero drag multiplier
    extern float gVehicleCouplingCoefE;  //
    extern float gVehicleCouplingCoefW;  //
    extern float gVehicleMaxCoupling;    // FIXME
    extern bool gVehicleStartChronopack; //
    extern bool gVehicleESPEnabled;      //
    extern int
        gVehicleDrivelineInertiaMode; // -1=per-car, 0 /|k|, 1 /k^2, 2 /sqrt

    ImGui::SeparatorText( "Vehicle" );
    static const char *kDrivingHelpLevels[] = {
        "player setting", "0 SECURE", "1 SPORT", "2 HYPERSPORT", "3 off" };
    int32_t helpCombo = gVehicleDrivingHelpOverride + 1;
    if ( ImGui::Combo( "Driving help", &helpCombo, kDrivingHelpLevels,
                       IM_ARRAYSIZE( kDrivingHelpLevels ) ) ) {
        gVehicleDrivingHelpOverride = helpCombo - 1;
    }
    ImGui::Checkbox( "ABS", &gVehicleABS );
    extern int32_t gVehicleSteerControlOverride;
    static const char *kSteerControlModes[] = {
        "default (Physics.ini UseSteerControl)", "pad/analog",
        "wheel/bar (speed curves + rate limit)" };
    int32_t steerCombo = gVehicleSteerControlOverride + 1;
    if ( ImGui::Combo( "Steer control mode", &steerCombo, kSteerControlModes,
                       IM_ARRAYSIZE( kSteerControlModes ) ) ) {
        gVehicleSteerControlOverride = steerCombo - 1;
    }
    ImGui::SliderFloat( "Aero drag scale", &gVehicleAeroDragScale, 0.0f, 4.0f,
                        "%.2f" );
    ImGui::Checkbox( "Enable ESP", &gVehicleESPEnabled );
    if ( ImGui::Button( "Start accel chrono" ) ) {
        gVehicleStartChronopack =
            true; // stop, then move off: splits go to the log
    }
    ImGui::SliderFloat( "Coupling coef E", &gVehicleCouplingCoefE, -1.0f, 20.0f,
                        "%.2f" );
    ImGui::SliderFloat( "Coupling coef W", &gVehicleCouplingCoefW, -1.0f, 20.0f,
                        "%.2f" );
    ImGui::SliderFloat( "Max coupling (0 = unclamped)", &gVehicleMaxCoupling,
                        0.0f, 20000.0f, "%.0f" );

    ImGui::Checkbox( "Enable anti-roll bars sim", &gVehicleAntiRoll );
    ImGui::Checkbox( "Enable ingition sim", &gVehicleEngineStateMachine );
    ImGui::TextUnformatted( "Differential override" );
    ImGui::SameLine();
    ImGui::RadioButton( "none##diff", &gVehicleDiffTypeOverride, 0 );
    ImGui::SameLine();
    ImGui::RadioButton( "open##diff", &gVehicleDiffTypeOverride, 1 );
    ImGui::SameLine();
    ImGui::RadioButton( "LSD##diff", &gVehicleDiffTypeOverride, 2 );
    ImGui::SameLine();
    ImGui::RadioButton( "spool##diff", &gVehicleDiffTypeOverride, 4 );
    if ( gVehicleDiffTypeOverride == 2 || gVehicleDiffTypeOverride == 4 ) {
        ImGui::SliderFloat( "Lock coef", &gVehicleDiffLockCoef, 0.0f, 200.0f );
    }
    ImGui::TextUnformatted( "Driveline inertia divisor" );
    ImGui::SameLine();
    ImGui::RadioButton( "car##dli", &gVehicleDrivelineInertiaMode, -1 );
    ImGui::SameLine();
    ImGui::RadioButton( "/|k|##dli", &gVehicleDrivelineInertiaMode, 0 );
    ImGui::SameLine();
    ImGui::RadioButton( "/k^2##dli", &gVehicleDrivelineInertiaMode, 1 );
    ImGui::SameLine();
    ImGui::RadioButton( "/sqrt##dli", &gVehicleDrivelineInertiaMode, 2 );
    ImGui::Checkbox( "Hardcore mode", &gbSimuModeEnabled );

    ImGui::Separator();

    int32_t carIndex = 0;
    if ( pPhysicsVehiclesBegin != nullptr ) {
        for ( PhysicsVehicle *it = pPhysicsVehiclesBegin;
              it != pPhysicsVehiclesEnd; ++it ) {
            if ( !it->isActive() ) {
                continue;
            }

            const float speedKmh = it->getChassisSpeed() * 3.6f;
            const float rpm = it->getEngineAngularVelocity() * 9.549296f;
            ImGui::Text( "Vehicle %d", carIndex );
            ImGui::Text( "Speed %6.1f km/h   Gear %d   RPM %5.0f", speedKmh,
                         it->getCurrentGear(), rpm );
            ImGui::Text( "Lateral G %+5.2f   Longitudinal G %+5.2f",
                         it->getLateralG(), it->getLongitudinalG() );

            const Matrix4x4f_t chassis = it->getChassisWorldMatrix();
            const float rollDeg =
                atan2f( chassis( 1, 0 ), chassis( 1, 1 ) ) * 57.29578f;
            const float pitchDeg =
                atan2f( chassis( 1, 2 ), chassis( 1, 1 ) ) * 57.29578f;
            ImGui::Text( "Roll %+5.1f deg   Pitch %+5.1f deg", rollDeg,
                         pitchDeg );
            ImGui::Text( "Pos  %.1f, %.1f, %.1f", chassis( 3, 0 ),
                         chassis( 3, 1 ), chassis( 3, 2 ) );
            ImGui::Text(
                "ESP %-4s  gripDiff %+6.2f / thr %5.2f   mag F %4.2f R %4.2f",
                it->areElectronicsEnabled()
                    ? ( it->isEspIntervening() ? "ON" : "idle" )
                    : "off",
                it->getEspGripDiff(), it->getEspThreshold(),
                it->getEspMagFront(), it->getEspMagRear() );

            const char *pWheelNames[4] = { "FL", "FR", "RL", "RR" };
            if ( ImGui::BeginTable( "wheels", 9,
                                    ImGuiTableFlags_Borders |
                                        ImGuiTableFlags_RowBg ) ) {
                ImGui::TableSetupColumn( "Wheel" );
                ImGui::TableSetupColumn( "Ground" );
                ImGui::TableSetupColumn( "Load" );
                ImGui::TableSetupColumn( "Susp" );
                ImGui::TableSetupColumn( "SlipLong" );
                ImGui::TableSetupColumn( "SlipLat" );
                ImGui::TableSetupColumn( "ESP" );
                ImGui::TableSetupColumn( "Tesp" );
                ImGui::TableSetupColumn( "Tbrk" );
                ImGui::TableHeadersRow();

                static float smoothedLoad[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
                const uint32_t numWheels = it->getNumWheels();
                for ( uint32_t w = 0; w < numWheels; w++ ) {
                    const PhysicsVehicle::WheelReadout wheel =
                        it->getWheelReadout( w );
                    if ( w < 4 ) {
                        smoothedLoad[w] +=
                            ( wheel.SuspensionLoad - smoothedLoad[w] ) * 0.1f;
                    }
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::Text( "%s", w < 4 ? pWheelNames[w] : "?" );
                    ImGui::TableNextColumn();
                    ImGui::Text( "%s", wheel.bGrounded ? "yes" : "AIR" );
                    ImGui::TableNextColumn();
                    ImGui::Text( "%.0f", w < 4 ? smoothedLoad[w]
                                               : wheel.SuspensionLoad );
                    ImGui::TableNextColumn();
                    ImGui::Text( "%.3f", wheel.SuspensionLength );
                    ImGui::TableNextColumn();
                    ImGui::Text( "%+.2f", wheel.LongitudinalSlip );
                    ImGui::TableNextColumn();
                    ImGui::Text( "%+.1f", wheel.LateralSlip * 57.29578f );
                    ImGui::TableNextColumn();
                    ImGui::Text( "%d", wheel.EspState );
                    ImGui::TableNextColumn();
                    ImGui::Text( "%+.0f", wheel.EspTorqueOut );
                    ImGui::TableNextColumn();
                    ImGui::Text( "%+.0f", wheel.BrakeTorqueOut );
                }
                ImGui::EndTable();
            }
            ImGui::Separator();
            carIndex++;
        }
    }

    if ( carIndex == 0 ) {
        ImGui::Text( "No active vehicle" );
    }
}

void GSPhysics::addInfinitePlane( float planeHeight ) {
    // FUN_00844c60
    // TODO: Use a box shape for now (as Jolt plane shape does not seem to match
    // 1:1 with Havok); but might want to revisit this in the future.
    if ( pPhysicsSystem == nullptr )
        return;

    const Eigen::Vector4f shiftOrigin = GetShiftOrigin();
    JPH::BodyCreationSettings settings(
        new JPH::BoxShapeSettings(
            JPH::Vec3( 10000.0f, 1.0f, 10000.0f ), JPH::cDefaultConvexRadius,
            TDUPhysicsMaterials::get(
                static_cast<uint32_t>( eMaterialType::MT_Asphalt ) ) ),
        JPH::RVec3( 0.0f, planeHeight - 1.0f - shiftOrigin.y(), 0.0f ),
        JPH::Quat::sIdentity(), JPH::EMotionType::Static,
        TDUPhysLayers::NON_MOVING );
    settings.mFriction = 1.0f; // FLOAT_00f4165c
    settings.mUserData = TDUSurface::Paved;

    lockMutex();
    JPH::BodyID id = pPhysicsSystem->GetBodyInterface().CreateAndAddBody(
        settings, JPH::EActivation::DontActivate );
    infinitePlaneBodyID = id.GetIndexAndSequenceNumber();
    unlockMutex();
}

void GSPhysics::removeAndDestroyFactory( PhysFactory *param_2 ) {
    // FUN_008427b0
    if ( param_2 == nullptr ) {
        return;
    }
    lockMutex();
    HavokFactoryData *pData = param_2->getHavokData();
    if ( pData != nullptr && pPhysicsSystem != nullptr ) {
        JPH::BodyInterface &bodyInterface = pPhysicsSystem->GetBodyInterface();
        for ( uint32_t idValue : pData->LiveBodyIDs ) {
            const JPH::BodyID id( idValue );
            bodyInterface.RemoveBody( id );
            bodyInterface.DestroyBody( id );
        }
    }
    param_2->reset();
    unlockMutex();
}

RigidBody_t GSPhysics::createSphere( uint32_t collisionGroup,
                                     const Eigen::Vector4f &worldPosition,
                                     float radius,
                                     float mass,
                                     float massScale,
                                     bool registerNow,
                                     bool bPhantom ) {
    // FUN_0081d660
    (void)collisionGroup;

    if ( pPhysicsSystem == nullptr )
        return nullptr;

    const Eigen::Vector4f origin = GetShiftOrigin();
    JPH::RVec3 position( worldPosition.x() - origin.x(),
                         worldPosition.y() - origin.y(),
                         worldPosition.z() - origin.z() );

    JPH::BodyCreationSettings settings( new JPH::SphereShapeSettings( radius ),
                                        position, JPH::Quat::sIdentity(),
                                        JPH::EMotionType::Kinematic,
                                        TDUPhysLayers::MOVING );
    settings.mIsSensor = bPhantom;
    settings.mFriction = 1.0f; // FLOAT_00f4165c
    settings.mOverrideMassProperties =
        JPH::EOverrideMassProperties::CalculateInertia;
    settings.mMassPropertiesOverride.mMass = mass * massScale;

    JPH::EActivation activation = registerNow ? JPH::EActivation::Activate
                                              : JPH::EActivation::DontActivate;

    lockMutex();
    JPH::Body *pBody =
        pPhysicsSystem->GetBodyInterface().CreateBody( settings );
    if ( pBody != nullptr ) {
        pPhysicsSystem->GetBodyInterface().AddBody( pBody->GetID(),
                                                    activation );
    }
    unlockMutex();

    return pBody;
}

void GSPhysics::destroyRigidBody( RigidBody_t pRigidBody ) {
    // NOTE: In the original code, this is done on the Havok rigid body
    // instance directly. The logic was moved to GSPhysics to give flexibility
    // to adopt any physics engine in the future.
    if ( pRigidBody == nullptr || pPhysicsSystem == nullptr )
        return;

    PhysicsLock lock;

    // A prop member may be jointed to its siblings; drop those joints first so
    // Jolt never references a destroyed body.
    removePropConstraintsFor( pRigidBody );

    JPH::BodyInterface &bodyInterface = pPhysicsSystem->GetBodyInterface();
    JPH::Body *pBody = static_cast<JPH::Body *>( pRigidBody );
    bodyInterface.RemoveBody( pBody->GetID() );
    bodyInterface.DestroyBody( pBody->GetID() );
}

void GSPhysics::moveSphereBody( RigidBody_t body,
                                const Eigen::Vector4f &position ) {
    if ( body == nullptr || pPhysicsSystem == nullptr )
        return;

    const Eigen::Vector4f origin = GetShiftOrigin();
    JPH::RVec3 joltPos( position.x() - origin.x(), position.y() - origin.y(),
                        position.z() - origin.z() );

    lockMutex();
    pPhysicsSystem->GetBodyInterface().SetPosition(
        body->GetID(), joltPos, JPH::EActivation::Activate );
    unlockMutex();
}

int32_t
GSPhysics::queryCameraCollisions( const Eigen::Vector4f &center,
                                  float radius,
                                  std::vector<CameraCollisionHit> &hits ) {
    if ( pPhysicsSystem == nullptr )
        return 0;

    JPH::Ref<JPH::SphereShape> sphere = new JPH::SphereShape( radius );
    JPH::CollideShapeSettings settings;
    JPH::AllHitCollisionCollector<JPH::CollideShapeCollector> collector;

    const Eigen::Vector4f origin = GetShiftOrigin();
    JPH::RVec3 joltCenter( center.x() - origin.x(), center.y() - origin.y(),
                           center.z() - origin.z() );

    lockMutex();
    pPhysicsSystem->GetNarrowPhaseQuery().CollideShape(
        sphere, JPH::Vec3::sReplicate( 1.0f ),
        JPH::RMat44::sTranslation( joltCenter ), settings, JPH::RVec3::sZero(),
        collector );
    unlockMutex();

    hits.reserve( collector.mHits.size() );
    for ( const JPH::CollideShapeResult &h : collector.mHits ) {
        CameraCollisionHit result;
        JPH::Vec3 axis = ( -h.mPenetrationAxis ).Normalized();
        result.exitNormal =
            Eigen::Vector4f( axis.GetX(), axis.GetY(), axis.GetZ(), 0.0f );
        result.contactPoint =
            Eigen::Vector4f( h.mContactPointOn2.GetX() + origin.x(),
                             h.mContactPointOn2.GetY() + origin.y(),
                             h.mContactPointOn2.GetZ() + origin.z(), 1.0f );
        result.penetrationDepth = h.mPenetrationDepth;
        hits.push_back( result );
    }

    return static_cast<int32_t>( hits.size() );
}

float GSPhysics::getGroundHeight( float x, float z ) {
    if ( pPhysicsSystem == nullptr ) {
        return -3.0e38f;
    }

    const float kTop = 10000.0f;
    const float kLength = 20000.0f;
    const Eigen::Vector4f origin = GetShiftOrigin();
    JPH::RRayCast ray(
        JPH::RVec3( x - origin.x(), kTop - origin.y(), z - origin.z() ),
        JPH::Vec3( 0.0f, -kLength, 0.0f ) );
    JPH::RayCastResult hit;

    lockMutex();
    const bool bHit = pPhysicsSystem->GetNarrowPhaseQuery().CastRay( ray, hit );
    unlockMutex();

    if ( !bHit ) {
        return -3.0e38f;
    }
    return kTop - kLength * hit.mFraction;
}

int32_t GSPhysics::gatherRoadShadowTriangles( const Eigen::Vector4f &bbMin,
                                              const Eigen::Vector4f &bbMax,
                                              Eigen::Vector4f *pOutVerts,
                                              int32_t maxVerts ) {
    // FUN_00845c20
    if ( pPhysicsSystem == nullptr || pOutVerts == nullptr || maxVerts <= 0 ) {
        return 0;
    }

    const Eigen::Vector4f shiftOrigin = GetShiftOrigin();
    const JPH::Vec3 joltOrigin( shiftOrigin.x(), shiftOrigin.y(),
                                shiftOrigin.z() );
    const JPH::Vec3 p0 =
        JPH::Vec3( bbMin.x(), bbMin.y(), bbMin.z() ) - joltOrigin;
    const JPH::Vec3 p1 =
        JPH::Vec3( bbMax.x(), bbMax.y(), bbMax.z() ) - joltOrigin;
    const JPH::AABox queryBox( JPH::Vec3::sMin( p0, p1 ),
                               JPH::Vec3::sMax( p0, p1 ) );

    struct RoadBody {
        JPH::RefConst<JPH::Shape> shape;
        JPH::Mat44 com;
    };
    std::vector<RoadBody> roadBodies;

    lockMutex();
    JPH::BodyIDVector bodyIDs;
    pPhysicsSystem->GetBodies( bodyIDs );
    const JPH::BodyInterface &bi = pPhysicsSystem->GetBodyInterfaceNoLock();
    roadBodies.reserve( bodyIDs.size() );
    for ( const JPH::BodyID id : bodyIDs ) {
        const uint64_t surf = bi.GetUserData( id );
        if ( !TDUSurface::isOnRoad( static_cast<uint32_t>( surf ) ) ) {
            continue;
        }
        JPH::RefConst<JPH::Shape> shape = bi.GetShape( id );
        if ( shape.GetPtr() == nullptr ||
             shape->GetSubType() != JPH::EShapeSubType::Mesh ) {
            continue;
        }
        roadBodies.push_back( { shape, bi.GetCenterOfMassTransform( id ) } );
    }
    unlockMutex();

    int32_t numVerts = 0;
    constexpr int32_t kTriBatch = 64;
    JPH::Float3 tri[3 * kTriBatch];
    for ( const RoadBody &rb : roadBodies ) {
        JPH::Shape::GetTrianglesContext ctx;
        rb.shape->GetTrianglesStart( ctx, queryBox, rb.com.GetTranslation(),
                                     rb.com.GetQuaternion(),
                                     JPH::Vec3::sReplicate( 1.0f ) );
        for ( ;; ) {
            const int32_t n = rb.shape->GetTrianglesNext( ctx, kTriBatch, tri );
            if ( n == 0 ) {
                break;
            }
            for ( int32_t t = 0; t < n; t++ ) {
                for ( int32_t v = 0; v < 3; v++ ) {
                    if ( numVerts >= maxVerts ) {
                        return numVerts;
                    }
                    const JPH::Float3 &f = tri[3 * t + v];
                    pOutVerts[numVerts++] = Eigen::Vector4f(
                        f.x + shiftOrigin.x(), f.y + shiftOrigin.y(),
                        f.z + shiftOrigin.z(), 1.0f );
                }
            }
        }
    }
    return numVerts;
}

void GSPhysics::createHeightfields() {
    // FUN_00842b10
    for ( auto &hf : pHeightfields ) {
        hf.pBody = nullptr;
        hf.pKey = nullptr;
    }
}

bool GSPhysics::isRegionCovered( float minX,
                                 float minZ,
                                 float maxX,
                                 float maxZ,
                                 float centreY,
                                 float halfY ) {
    if ( pPhysicsSystem == nullptr ) {
        return false;
    }

    const Eigen::Vector4f shiftOrigin = GetShiftOrigin();
    const JPH::Vec3 halfExtent( 0.5f * ( maxX - minX ), halfY,
                                0.5f * ( maxZ - minZ ) );
    const JPH::Vec3 centre( 0.5f * ( minX + maxX ) - shiftOrigin.x(),
                            centreY - shiftOrigin.y(),
                            0.5f * ( minZ + maxZ ) - shiftOrigin.z() );

    JPH::BoxShape probe( halfExtent, 0.0f );
    probe.SetEmbedded();

    JPH::AnyHitCollisionCollector<JPH::CollideShapeCollector> hit;
    pPhysicsSystem->GetNarrowPhaseQuery().CollideShape(
        &probe, JPH::Vec3::sReplicate( 1.0f ),
        JPH::Mat44::sTranslation( centre ), JPH::CollideShapeSettings(),
        JPH::RVec3::sZero(), hit, JPH::BroadPhaseLayerFilter(),
        StaticOrRoadLayerFilter(), NonTerrainBodyFilter() );
    return hit.HadHit();
}

bool GSPhysics::hasCollisionBelow( float worldX,
                                   float worldZ,
                                   float startY,
                                   float depth ) {
    if ( pPhysicsSystem == nullptr ) {
        return false;
    }

    const Eigen::Vector4f shiftOrigin = GetShiftOrigin();
    const JPH::RRayCast ray{ JPH::RVec3( worldX - shiftOrigin.x(),
                                         startY - shiftOrigin.y(),
                                         worldZ - shiftOrigin.z() ),
                             JPH::Vec3( 0.0f, -depth, 0.0f ) };

    JPH::RayCastResult result;
    if ( !pPhysicsSystem->GetNarrowPhaseQuery().CastRay(
             ray, result, JPH::BroadPhaseLayerFilter(),
             StaticOrRoadLayerFilter(), NonTerrainBodyFilter() ) ) {
        return false;
    }
    return true;
}

RigidBody_t GSPhysics::addHeightfield( const void *pKey,
                                       const float *pHeights,
                                       int32_t stride,
                                       float cellSize,
                                       float originX,
                                       float originZ,
                                       const float *pTileErrors,
                                       int32_t numTileX,
                                       int32_t numTileZ ) {
    // FUN_0084a390 -> FUN_00848b50
    if ( pPhysicsSystem == nullptr || pHeights == nullptr || stride <= 1 ||
         cellSize <= 0.0f )
        return nullptr;

    // Guard against a malformed heightmap descriptor before sizing buffers off
    // it (a garbage stride would allocate gigabytes and crash inside Jolt).
    if ( stride > 1025 ) {
        OTDU_LOG_WARN( "Skipping terrain heightfield: implausible stride %d\n",
                       stride );
        return nullptr;
    }

    constexpr uint32_t kBlockSize = 2;
    uint32_t sampleCount = kBlockSize;
    while ( sampleCount < static_cast<uint32_t>( stride ) )
        sampleCount <<= 1;

    std::vector<float> samples(
        static_cast<size_t>( sampleCount ) * sampleCount,
        JPH::HeightFieldShapeConstants::cNoCollisionValue );
    for ( int32_t row = 0; row < stride; row++ ) {
        for ( int32_t col = 0; col < stride; col++ ) {
            float h = pHeights[row * stride + col];
            // Drop non-finite samples and terrain holes (orig block flag 0xFE
            // -> -100000) to no-collision cells.
            if ( !std::isfinite( h ) || h < -50000.0f || h > 50000.0f )
                h = JPH::HeightFieldShapeConstants::cNoCollisionValue;
            samples[static_cast<size_t>( row ) * sampleCount + col] = h;
        }
    }

    const int32_t quadsPerTile =
        ( numTileX > 0 ) ? ( stride - 1 ) / numTileX : 0;
    if ( pTileErrors != nullptr && numTileX > 0 && numTileZ > 0 &&
         quadsPerTile > 0 ) {
        int32_t numBakedHoles = 0;
        int32_t numSubmergedHoles = 0;
        int32_t numKeptTiles = 0;
        const int32_t numCells = stride - 1;
        std::vector<uint8_t> covered(
            static_cast<size_t>( numCells ) * numCells, 0u );
        for ( int32_t tileZ = 0; tileZ < numTileZ; tileZ++ ) {
            for ( int32_t tileX = 0; tileX < numTileX; tileX++ ) {
                const float *pErrors =
                    pTileErrors + ( tileZ * numTileX + tileX ) * 5;
                float error0 = pErrors[0];
                const bool bBaked = error0 < 0.0f;
                if ( pErrors[4] < kHeightmapSeaLevel &&
                     error0 < kHeightmapFlatThreshold ) {
                    error0 = kHeightmapSeaLevel;
                }
                if ( error0 >= 0.0f ) {
                    continue;
                }
                if ( bBaked ) {
                    numBakedHoles++;
                } else {
                    numSubmergedHoles++;
                }

                const int32_t firstCell = tileX * quadsPerTile;
                const int32_t firstCellZ = tileZ * quadsPerTile;
                const float tileX0 = originX + firstCell * cellSize;
                const float tileZ0 = originZ + firstCellZ * cellSize;
                const float tileSize = quadsPerTile * cellSize;

                // Cheap whole-tile reject first
                if ( !isRegionCovered( tileX0, tileZ0, tileX0 + tileSize,
                                       tileZ0 + tileSize, pErrors[4],
                                       kTileProbeHalfHeight ) ) {
                    numKeptTiles++;
                    continue;
                }

                for ( int32_t r = 0; r < quadsPerTile; r++ ) {
                    for ( int32_t c = 0; c < quadsPerTile; c++ ) {
                        const int32_t cellX = firstCell + c;
                        const int32_t cellZ = firstCellZ + r;
                        // Start just above this cell's own terrain surface
                        float surfaceY = -1.0e30f;
                        for ( int32_t dz = 0; dz <= 1; dz++ ) {
                            for ( int32_t dx = 0; dx <= 1; dx++ ) {
                                const float h =
                                    pHeights[( cellZ + dz ) * stride + cellX +
                                             dx];
                                if ( std::isfinite( h ) && h > surfaceY ) {
                                    surfaceY = h;
                                }
                            }
                        }
                        if ( surfaceY <= -1.0e29f ) {
                            continue;
                        }
                        const float cx = originX + ( cellX + 0.5f ) * cellSize;
                        const float cz = originZ + ( cellZ + 0.5f ) * cellSize;
                        if ( hasCollisionBelow( cx, cz,
                                                surfaceY + kSupportProbeRise,
                                                kSupportProbeDepth ) ) {
                            covered[static_cast<size_t>( cellZ ) * numCells +
                                    cellX] = 1u;
                        }
                    }
                }
            }
        }

        for ( int32_t row = 0; row < stride; row++ ) {
            for ( int32_t col = 0; col < stride; col++ ) {
                bool bDrop = true;
                for ( int32_t dr = -1; dr <= 0 && bDrop; dr++ ) {
                    for ( int32_t dc = -1; dc <= 0; dc++ ) {
                        const int32_t cr = row + dr;
                        const int32_t cc = col + dc;
                        const bool bInside = cr >= 0 && cr < numCells &&
                                             cc >= 0 && cc < numCells;
                        if ( !bInside ||
                             !covered[static_cast<size_t>( cr ) * numCells +
                                      cc] ) {
                            bDrop = false;
                            break;
                        }
                    }
                }
                if ( bDrop ) {
                    samples[static_cast<size_t>( row ) * sampleCount + col] =
                        JPH::HeightFieldShapeConstants::cNoCollisionValue;
                }
            }
        }

        if ( numBakedHoles != 0 || numSubmergedHoles != 0 ) {
            OTDU_LOG_DEBUG(
                "Terrain heightfield: %d/%d tile(s) marked (%d authored, %d "
                "submerged); %d kept whole for lack of authored cover\n",
                numBakedHoles + numSubmergedHoles, numTileX * numTileZ,
                numBakedHoles, numSubmergedHoles, numKeptTiles );
        }
    }

    JPH::HeightFieldShapeSettings shapeSettings(
        samples.data(), JPH::Vec3::sZero(),
        JPH::Vec3( cellSize, 1.0f, cellSize ), sampleCount );
    shapeSettings.mBlockSize = kBlockSize;

    shapeSettings.mMaterials.push_back( TDUPhysicsMaterials::get(
        static_cast<uint32_t>( eMaterialType::MT_Asphalt ) ) );

    JPH::ShapeSettings::ShapeResult shapeResult = shapeSettings.Create();
    if ( !shapeResult.IsValid() ) {
        OTDU_LOG_WARN( "Failed to build terrain heightfield shape\n" );
        return nullptr;
    }

    PhysicsHeightfield *pSlot = nullptr;
    for ( auto &hf : pHeightfields ) {
        if ( hf.pBody == nullptr ) {
            pSlot = &hf;
            break;
        }
    }
    if ( pSlot == nullptr ) {
        OTDU_LOG_WARN( "No free physics heightfield slot\n" );
        return nullptr;
    }

    // Static ground body: friction 1.0 (FLOAT_00f4165c), non-moving layer, as
    // in the original heightfield/infinite-plane material.
    const Eigen::Vector4f shiftOrigin = GetShiftOrigin();
    JPH::BodyCreationSettings bodySettings(
        shapeResult.Get(),
        JPH::RVec3( originX - shiftOrigin.x(), -shiftOrigin.y(),
                    originZ - shiftOrigin.z() ),
        JPH::Quat::sIdentity(), JPH::EMotionType::Static,
        TDUPhysLayers::NON_MOVING );
    bodySettings.mUserData = TDUSurface::Terrain;
    bodySettings.mFriction = TDUSurface::friction( TDUSurface::Terrain );

    lockMutex();
    JPH::Body *pBody =
        pPhysicsSystem->GetBodyInterface().CreateBody( bodySettings );
    if ( pBody != nullptr ) {
        pPhysicsSystem->GetBodyInterface().AddBody(
            pBody->GetID(), JPH::EActivation::DontActivate );
        pSlot->pBody = pBody;
        pSlot->pKey = pKey;
    }
    unlockMutex();

    return pBody;
}

void GSPhysics::removeHeightfield( const void *pKey ) {
    if ( pPhysicsSystem == nullptr || pKey == nullptr )
        return;

    for ( auto &hf : pHeightfields ) {
        if ( hf.pKey == pKey && hf.pBody != nullptr ) {
            JPH::Body *pBody = static_cast<JPH::Body *>( hf.pBody );
            lockMutex();
            JPH::BodyInterface &bodyInterface =
                pPhysicsSystem->GetBodyInterface();
            bodyInterface.RemoveBody( pBody->GetID() );
            bodyInterface.DestroyBody( pBody->GetID() );
            unlockMutex();
            hf.pBody = nullptr;
            hf.pKey = nullptr;
            return;
        }
    }
}

static uint32_t PhysTypeForShape( const JPH::Shape *pShape, bool bRoad ) {
    if ( pShape == nullptr ||
         pShape->GetSubType() != JPH::EShapeSubType::Mesh ) {
        return TDUSurface::Generic;
    }
    return bRoad ? TDUSurface::RoadMesh : TDUSurface::Paved;
}

void GSPhysics::addSectorCollision( const void *pKey,
                                    void *pBlob,
                                    uint32_t blobSize,
                                    float baseX,
                                    float baseZ,
                                    bool bRoadBlob ) {
    // Sector static world collision
    if ( pPhysicsSystem == nullptr || pBlob == nullptr || pKey == nullptr )
        return;

    HavokPackfile packfile;
    bool bParsed = false;
    HavokContainer container;
    if ( container.parse( pBlob, blobSize ) ) {
        bParsed = packfile.parse( container.getHavokScene(),
                                  container.getHavokSceneSize() );
    }
    if ( !bParsed ) {
        bParsed =
            packfile.parse( static_cast<const uint8_t *>( pBlob ), blobSize );
    }
    if ( !bParsed ) {
        OTDU_LOG_WARN( "Sector collision: blob is neither a physics container "
                       "nor a Havok packfile (size=%u)\n",
                       blobSize );
        return;
    }

    std::vector<HavokToJolt::RigidBody> bodies;
    HavokToJolt::buildBodies( packfile, bodies );
    if ( bodies.empty() ) {
        // Expected until the MOPP/triangle-mesh converter lands: world/road
        // shells are hkMoppBvTreeShape-wrapped meshes (type 0x18) which
        // HavokToJolt::buildShape does not yet handle.
        OTDU_LOG_WARN( "Sector collision: Havok scene produced no collidable "
                       "bodies (mesh converter pending?)\n" );
        return;
    }

    // Claim a free sector slot.
    PhysicsSHK *pSlot = nullptr;
    for ( PhysicsSHK *it = pSectorsBegin; it != pSectorsEnd; ++it ) {
        if ( it->pKey == nullptr && it->NumObjects == 0 ) {
            pSlot = it;
            break;
        }
    }
    if ( pSlot == nullptr ) {
        OTDU_LOG_WARN( "Sector collision: no free sector slot\n" );
        return;
    }

    const Eigen::Vector4f shiftOrigin = GetShiftOrigin();

    JPH::BodyInterface &bodyInterface = pPhysicsSystem->GetBodyInterface();
    lockMutex();
    for ( const HavokToJolt::RigidBody &body : bodies ) {
        if ( pSlot->NumObjects >= pSlot->MaxNumObjects )
            break;

        const JPH::RVec3 position(
            baseX + body.Position.GetX() - shiftOrigin.x(),
            body.Position.GetY() - shiftOrigin.y(),
            baseZ + body.Position.GetZ() - shiftOrigin.z() );

        const bool bRoadSurface = bRoadBlob && body.bBvTreeShape;
        const uint32_t physType = PhysTypeForShape( body.Shape, bRoadSurface );

        JPH::BodyCreationSettings settings( body.Shape, position, body.Rotation,
                                            JPH::EMotionType::Static,
                                            TDUPhysLayers::NON_MOVING );
        settings.mUserData = physType;
        settings.mFriction = TDUSurface::friction( physType );

        JPH::Body *pBody = bodyInterface.CreateBody( settings );
        if ( pBody != nullptr ) {
            bodyInterface.AddBody( pBody->GetID(),
                                   JPH::EActivation::DontActivate );
            PhysicsSectorObj &obj = pSlot->pObjects[pSlot->NumObjects++];
            obj.pRigidBody = pBody;
            obj.pShape = body.Shape;

            // TEMP: REMOVEME:
            if ( !body.Rotation.IsClose( JPH::Quat::sIdentity(), 1.0e-4f ) ) {
                const JPH::Vec3 sz = pBody->GetWorldSpaceBounds().GetSize();
                // Bucket by vertical (Y) size: >8m = an upright wall/building.
                if ( sz.GetY() > 8.0f )
                    gTallBodyCount++;
                else
                    gFlatBodyCount++;
                if ( sz.GetY() > gMaxBodyY )
                    gMaxBodyY = sz.GetY();
            }
        }
    }
    pSlot->pKey = pKey;
    unlockMutex();

    // TEMP: REMOVEME: absolute AABB of body positions + how many carry a
    // non-identity rotation (road-surface meshes are axis-aligned; .shk world
    // objects rotate — a placement/rotation regression shows up here).
    JPH::Vec3 lo( 1e18f, 1e18f, 1e18f ), hi( -1e18f, -1e18f, -1e18f );
    int32_t nonIdentityRot = 0;
    for ( const HavokToJolt::RigidBody &body : bodies ) {
        const JPH::Vec3 p( baseX + body.Position.GetX(), body.Position.GetY(),
                           baseZ + body.Position.GetZ() );
        lo = JPH::Vec3::sMin( lo, p );
        hi = JPH::Vec3::sMax( hi, p );
        if ( !body.Rotation.IsClose( JPH::Quat::sIdentity(), 1.0e-4f ) )
            nonIdentityRot++;
    }
    OTDU_LOG_DEBUG(
        "Sector collision: %d bodies; areaBase (%.0f,%.0f); body0 "
        "abs (%.0f,%.0f,%.0f); AABB (%.0f,%.0f,%.0f)..(%.0f,%.0f,"
        "%.0f); %d/%d rotated\n",
        pSlot->NumObjects, baseX, baseZ, baseX + bodies[0].Position.GetX(),
        bodies[0].Position.GetY(), baseZ + bodies[0].Position.GetZ(), lo.GetX(),
        lo.GetY(), lo.GetZ(), hi.GetX(), hi.GetY(), hi.GetZ(), nonIdentityRot,
        (int)bodies.size() );
    OTDU_LOG_DEBUG( "  orientation tally so far: %d tall(>8m Y) / %d flat; max "
                    "Y-size %.1f m\n",
                    gTallBodyCount, gFlatBodyCount, gMaxBodyY );
}

void GSPhysics::removeSectorCollision( const void *pKey ) {
    if ( pPhysicsSystem == nullptr || pKey == nullptr )
        return;

    // A sector may own more than one collision slot (its .shk world blob and
    // its -R.shk road blob are both keyed by the Sector), so tear down every
    // match.
    for ( PhysicsSHK *it = pSectorsBegin; it != pSectorsEnd; ++it ) {
        if ( it->pKey != pKey )
            continue;

        lockMutex();
        JPH::BodyInterface &bodyInterface = pPhysicsSystem->GetBodyInterface();
        for ( int32_t i = 0; i < it->NumObjects; i++ ) {
            PhysicsSectorObj &obj = it->pObjects[i];
            if ( obj.pRigidBody != nullptr ) {
                JPH::Body *pBody = static_cast<JPH::Body *>( obj.pRigidBody );
                bodyInterface.RemoveBody( pBody->GetID() );
                bodyInterface.DestroyBody( pBody->GetID() );
                obj.pRigidBody = nullptr;
            }
            obj.pShape = nullptr; // release the shape ref
        }
        it->NumObjects = 0;
        it->pKey = nullptr;
        unlockMutex();
    }
}

void GSPhysics::addSpotCollision( const void *pKey,
                                  void *pBlob,
                                  uint32_t blobSize,
                                  float angleY,
                                  const Eigen::Vector3f &translation ) {
    if ( pPhysicsSystem == nullptr || pBlob == nullptr || pKey == nullptr )
        return;

    HavokPackfile packfile;
    bool bParsed = false;
    HavokContainer container;
    if ( container.parse( pBlob, blobSize ) ) {
        bParsed = packfile.parse( container.getHavokScene(),
                                  container.getHavokSceneSize() );
    }
    if ( !bParsed ) {
        bParsed =
            packfile.parse( static_cast<const uint8_t *>( pBlob ), blobSize );
    }
    if ( !bParsed ) {
        return;
    }

    std::vector<HavokToJolt::RigidBody> bodies;
    HavokToJolt::buildBodies( packfile, bodies );
    if ( bodies.empty() ) {
        return;
    }

    // Claim a free slot.
    PhysicsSHK *pSlot = nullptr;
    for ( PhysicsSHK *it = pSectorsBegin; it != pSectorsEnd; ++it ) {
        if ( it->pKey == nullptr && it->NumObjects == 0 ) {
            pSlot = it;
            break;
        }
    }
    if ( pSlot == nullptr ) {
        OTDU_LOG_WARN( "Spot collision: no free sector slot\n" );
        return;
    }

    const Eigen::Vector4f shiftOrigin = GetShiftOrigin();
    const JPH::Quat spotRot =
        JPH::Quat::sRotation( JPH::Vec3::sAxisY(), angleY );
    const JPH::Vec3 spotTrans( translation.x() - shiftOrigin.x(),
                               translation.y() - shiftOrigin.y(),
                               translation.z() - shiftOrigin.z() );

    JPH::BodyInterface &bodyInterface = pPhysicsSystem->GetBodyInterface();
    lockMutex();
    for ( const HavokToJolt::RigidBody &body : bodies ) {
        if ( pSlot->NumObjects >= pSlot->MaxNumObjects )
            break;

        const JPH::RVec3 position( spotRot * body.Position + spotTrans );
        const JPH::Quat rotation = ( spotRot * body.Rotation ).Normalized();

        JPH::BodyCreationSettings settings( body.Shape, position, rotation,
                                            JPH::EMotionType::Static,
                                            TDUPhysLayers::NON_MOVING );
        settings.mUserData = TDUSurface::Generic;
        settings.mFriction = TDUSurface::friction( TDUSurface::Generic );

        JPH::Body *pBody = bodyInterface.CreateBody( settings );
        if ( pBody != nullptr ) {
            bodyInterface.AddBody( pBody->GetID(),
                                   JPH::EActivation::DontActivate );
            PhysicsSectorObj &obj = pSlot->pObjects[pSlot->NumObjects++];
            obj.pRigidBody = pBody;
            obj.pShape = body.Shape;
        }
    }
    pSlot->pKey = pKey;
    unlockMutex();

    OTDU_LOG_DEBUG(
        "Spot collision: %d bodies at (%.0f,%.0f,%.0f) angle %.2f\n",
        pSlot->NumObjects, translation.x(), translation.y(), translation.z(),
        angleY );
}

void GSPhysics::addSectorRoadGround( const void *pKey,
                                     void *pBlob,
                                     uint32_t blobSize ) {
    // FUN_00848530
    if ( pPhysicsSystem == nullptr || pBlob == nullptr || pKey == nullptr ||
         blobSize < 0x124u )
        return;

    const uint8_t *blob = static_cast<const uint8_t *>( pBlob );
    auto readF = []( const uint8_t *p ) -> float {
        float v;
        memcpy( &v, p, 4 );
        return v;
    };
    auto readU = []( const uint8_t *p ) -> uint32_t {
        uint32_t v;
        memcpy( &v, p, 4 );
        return v;
    };
    auto readVec3 = [&]( const uint8_t *p ) -> JPH::Vec3 {
        return JPH::Vec3( readF( p ), readF( p + 4 ), readF( p + 8 ) );
    };

    JPH::TriangleList triangles;
    JPH::Vec3 originRef = JPH::Vec3::sZero(); // rebase for float precision
    bool bHaveOrigin = false;
    JPH::Vec3 prevLat( 0, 0, 1 ); // fallback lateral if the tangent degenerates
    int32_t recordCount = 0;

    uint32_t off = 0;
    while ( off + 0x124u <= blobSize ) {
        const uint8_t *rec = blob + off;
        if ( readU( rec ) != kSR20Magic )
            break;
        const int32_t N = static_cast<int32_t>( readU( rec + 0xac ) );
        if ( N < 2 || N > 256 )
            break;
        const uint32_t e0Off = readU( rec + 0xe0 ); // heights[N]
        const uint32_t e4Off = readU( rec + 0xe4 ); // lateral[N]
        const uint32_t footer = off + e4Off + static_cast<uint32_t>( N ) * 4u;
        if ( off + e0Off + static_cast<uint32_t>( N ) * 4u > blobSize ||
             footer + 4u > blobSize )
            break;
        if ( readU( blob + footer ) != kER20Magic )
            break;

        const JPH::Vec3 P0 = readVec3( rec + 0x10 );
        const JPH::Vec3 P1 = readVec3( rec + 0x20 );
        const JPH::Vec3 P2 = readVec3( rec + 0x30 );
        const JPH::Vec3 P3 = readVec3( rec + 0x40 );
        const float *heights = reinterpret_cast<const float *>( rec + e0Off );
        const float *lateral = reinterpret_cast<const float *>( rec + e4Off );
        if ( !bHaveOrigin ) {
            originRef = P0;
            bHaveOrigin = true;
        }

        std::vector<JPH::Vec3> grid;
        grid.reserve( static_cast<size_t>( kLongSteps + 1 ) * N );
        for ( int32_t j = 0; j <= kLongSteps; ++j ) {
            const float u = static_cast<float>( j ) / kLongSteps;
            const float iu = 1.0f - u;
            const JPH::Vec3 S = P0 * ( iu * iu * iu ) +
                                P1 * ( 3.0f * u * iu * iu ) +
                                P2 * ( 3.0f * u * u * iu ) + P3 * ( u * u * u );
            const JPH::Vec3 T = ( P1 - P0 ) * ( 3.0f * iu * iu ) +
                                ( P2 - P1 ) * ( 6.0f * u * iu ) +
                                ( P3 - P2 ) * ( 3.0f * u * u );
            JPH::Vec3 latDir( -T.GetZ(), 0.0f, T.GetX() );
            const float latLen = latDir.Length();
            if ( latLen > 1.0e-4f )
                prevLat = latDir / latLen;
            for ( int32_t k = 0; k < N; ++k ) {
                grid.push_back( ( S + prevLat * lateral[k] +
                                  JPH::Vec3( 0.0f, heights[k], 0.0f ) ) -
                                originRef );
            }
        }

        auto pushTri = [&]( const JPH::Vec3 &v0, const JPH::Vec3 &v1,
                            const JPH::Vec3 &v2 ) {
            const JPH::Vec3 n = ( v1 - v0 ).Cross( v2 - v0 );
            if ( n.LengthSq() < 1.0e-8f )
                return;
            if ( n.GetY() < 0.0f )
                triangles.push_back( JPH::Triangle( v0, v2, v1 ) );
            else
                triangles.push_back( JPH::Triangle( v0, v1, v2 ) );
        };
        for ( int32_t j = 0; j < kLongSteps; ++j ) {
            for ( int32_t k = 0; k < N - 1; ++k ) {
                const JPH::Vec3 &a = grid[j * N + k];
                const JPH::Vec3 &b = grid[( j + 1 ) * N + k];
                const JPH::Vec3 &c = grid[( j + 1 ) * N + k + 1];
                const JPH::Vec3 &d = grid[j * N + k + 1];
                pushTri( a, b, c );
                pushTri( a, c, d );
            }
        }
        recordCount++;
        off = ( footer + 4u + 0xfu ) & ~0xfu;
    }

    if ( triangles.empty() ) {
        OTDU_LOG_WARN( "SR20 road ground: no triangles produced (size=%u)\n",
                       blobSize );
        return;
    }

    JPH::MeshShapeSettings meshSettings( triangles );
    meshSettings.Sanitize();
    JPH::ShapeSettings::ShapeResult meshResult = meshSettings.Create();
    if ( !meshResult.IsValid() ) {
        OTDU_LOG_WARN( "SR20 road ground: mesh build failed: %s\n",
                       meshResult.GetError().c_str() );
        return;
    }

    PhysicsSHK *pSlot = nullptr;
    for ( PhysicsSHK *it = pSectorsBegin; it != pSectorsEnd; ++it ) {
        if ( it->pKey == nullptr && it->NumObjects == 0 ) {
            pSlot = it;
            break;
        }
    }
    if ( pSlot == nullptr ) {
        OTDU_LOG_WARN( "SR20 road ground: no free sector slot\n" );
        return;
    }

    const Eigen::Vector4f shiftOrigin = GetShiftOrigin();
    JPH::BodyInterface &bodyInterface = pPhysicsSystem->GetBodyInterface();
    lockMutex();
    JPH::BodyCreationSettings settings(
        meshResult.Get(),
        JPH::RVec3( originRef - JPH::Vec3( shiftOrigin.x(), shiftOrigin.y(),
                                           shiftOrigin.z() ) ),
        JPH::Quat::sIdentity(), JPH::EMotionType::Static, TDUPhysLayers::ROAD );
    settings.mUserData = TDUSurface::RoadMesh;
    settings.mFriction = TDUSurface::friction( TDUSurface::RoadMesh );
    JPH::Body *pBody = bodyInterface.CreateBody( settings );
    if ( pBody != nullptr ) {
        bodyInterface.AddBody( pBody->GetID(), JPH::EActivation::DontActivate );
        PhysicsSectorObj &obj = pSlot->pObjects[pSlot->NumObjects++];
        obj.pRigidBody = pBody;
        obj.pShape = meshResult.Get();
        pSlot->pKey = pKey;
    }
    unlockMutex();

    OTDU_LOG_DEBUG( "SR20 road ground: %d records, %u triangles\n", recordCount,
                    (uint32_t)triangles.size() );
}

void GSPhysics::queuePropCollisionFactory( const void *pBlob,
                                           uint32_t blobSize ) {
    // Copy the blob — the scenaric bank buffer that owns it may not stay valid,
    // and the build is deferred until Jolt is ready (see
    // buildProcessPendingProps).
    PhysicsLock lock;

    if ( pBlob == nullptr || blobSize == 0 || !pendingPropBlob.empty() ||
         pPropFactory != nullptr ) {
        return;
    }
    pendingPropBlob.assign( static_cast<const uint8_t *>( pBlob ),
                            static_cast<const uint8_t *>( pBlob ) + blobSize );
}

// The two hedge assets ("V_HAIE_M" / "V_HAIE_O") are authored with their
// collision box transposed relative to their render instance: the original
// compensates by swapping the X and Z scale components before instancing them
// (see FUN_00847d20).
static constexpr uint64_t kHedgeMediumHashcode = 0x4d5f454941485f56ull;
static constexpr uint64_t kHedgeOtherHashcode = 0x4f5f454941485f56ull;

void GSPhysics::placeSceneryInstance( const void *pSectorKey,
                                      uint64_t hashcode,
                                      const Matrix4x4f_t &worldTransform,
                                      const Eigen::Vector3f &objectOrigin,
                                      const Eigen::Vector3f &instanceScale ) {
    // FUN_00847d20
    PhysicsLock lock;

    static constexpr size_t kMaxSceneryBodies = 20000;
    if ( hashcode == 0 || sceneryInstanceBodies.size() >= kMaxSceneryBodies ) {
        return;
    }

    if ( pSceneryFactory == nullptr && pPropFactory == nullptr ) {
        if ( pendingSceneryInstances.size() < kMaxSceneryBodies ) {
            pendingSceneryInstances.push_back( { pSectorKey, hashcode,
                                                 worldTransform, objectOrigin,
                                                 instanceScale } );
        }
        return;
    }
    if ( !isPropCollisionType( hashcode ) ) {
        return;
    }

    Eigen::Vector3f shapeScale = instanceScale;
    if ( hashcode == kHedgeMediumHashcode || hashcode == kHedgeOtherHashcode ) {
        std::swap( shapeScale.x(), shapeScale.z() );
    }

    std::vector<std::pair<uint64_t, RigidBody_t>> outBodies;
    placeProp( hashcode, worldTransform, /*bDynamic=*/false, 0.0f, 0.0f,
               outBodies, /*outPrimaryLocalInv=*/nullptr,
               /*bSceneryInstance=*/true, objectOrigin, instanceScale,
               shapeScale );
    for ( const auto &ob : outBodies ) {
        sceneryInstanceBodies.emplace_back( pSectorKey, ob.second );
    }
}

void GSPhysics::removeSceneryInstances( const void *pSectorKey ) {
    if ( pSectorKey == nullptr || pPhysicsSystem == nullptr ) {
        return;
    }

    PhysicsLock lock;

    for ( auto it = sceneryInstanceBodies.begin();
          it != sceneryInstanceBodies.end(); ) {
        if ( it->first == pSectorKey ) {
            destroyRigidBody( it->second );
            it = sceneryInstanceBodies.erase( it );
        } else {
            ++it;
        }
    }

    for ( auto it = pendingSceneryInstances.begin();
          it != pendingSceneryInstances.end(); ) {
        if ( it->pSectorKey == pSectorKey ) {
            it = pendingSceneryInstances.erase( it );
        } else {
            ++it;
        }
    }
}

void GSPhysics::queueSceneryCollisionFactory( const void *pBlob,
                                              uint32_t blobSize ) {
    PhysicsLock lock;

    if ( pBlob == nullptr || blobSize == 0 || !pendingSceneryBlob.empty() ||
         pSceneryFactory != nullptr ) {
        return;
    }
    pendingSceneryBlob.assign( static_cast<const uint8_t *>( pBlob ),
                               static_cast<const uint8_t *>( pBlob ) +
                                   blobSize );
}

bool GSPhysics::isPropCollisionType( uint64_t hashcode ) const {
    if ( hashcode == 0 ) {
        return false;
    }
    const PhysFactory *factories[2] = { pPropFactory, pSceneryFactory };
    for ( const PhysFactory *pF : factories ) {
        if ( pF == nullptr ) {
            continue;
        }
        HavokFactoryData *pData = pF->getHavokData();
        if ( pData == nullptr ) {
            continue;
        }
        for ( const auto &m : pData->MechaMembers ) {
            if ( m.first == hashcode ) {
                return true;
            }
        }
    }
    return false;
}

void GSPhysics::buildProcessPendingProps() {
    if ( pendingPropBlob.empty() || !bJoltInit || pPropFactory != nullptr ) {
        return;
    }
    pPropFactory =
        createFromBinary( pendingPropBlob.data(),
                          static_cast<int32_t>( pendingPropBlob.size() ) );
    pendingPropBlob.clear();
    pendingPropBlob.shrink_to_fit();
    if ( pPropFactory == nullptr ) {
        OTDU_LOG_WARN( "Prop collision: factory parse failed\n" );
        return;
    }

    // Build the full CommonWorld scenery library.
    if ( pSceneryFactory == nullptr && !pendingSceneryBlob.empty() ) {
        pSceneryFactory = createFromBinary(
            pendingSceneryBlob.data(),
            static_cast<int32_t>( pendingSceneryBlob.size() ) );
        pendingSceneryBlob.clear();
        pendingSceneryBlob.shrink_to_fit();

        if ( pSceneryFactory != nullptr ) {
            HavokFactoryData *pD = pSceneryFactory->getHavokData();
            OTDU_LOG_DEBUG(
                "Scenery collision library: %zu mechas, %zu bodies\n",
                pD ? pD->MechaMembers.size() : 0, pD ? pD->Bodies.size() : 0 );

        } else {
            OTDU_LOG_WARN( "Scenery collision library parse failed\n" );
        }
    }

    // Drain scenery instances parked while the factory was still building.
    if ( !pendingSceneryInstances.empty() &&
         ( pSceneryFactory != nullptr || pPropFactory != nullptr ) ) {
        std::vector<PendingSceneryInstance> pending;
        pending.swap( pendingSceneryInstances );
        for ( const PendingSceneryInstance &p : pending ) {
            placeSceneryInstance( p.pSectorKey, p.hashcode, p.transform,
                                  p.objectOrigin, p.instanceScale );
        }
        OTDU_LOG_DEBUG(
            "Scenery collision: placed %zu deferred instances (%zu bodies)\n",
            pending.size(), sceneryInstanceBodies.size() );
    }
}

void GSPhysics::placeProp(
    uint64_t hashcode,
    const Matrix4x4f_t &worldTransform,
    bool bDynamic,
    float linearDamping,
    float angularDamping,
    std::vector<std::pair<uint64_t, RigidBody_t>> &outBodies,
    Matrix4x4f_t *outPrimaryLocalInv,
    bool bSceneryInstance,
    const Eigen::Vector3f &objectOrigin,
    const Eigen::Vector3f &offsetScale,
    const Eigen::Vector3f &shapeScale ) {
    // FUN_00453690
    if ( pPhysicsSystem == nullptr || hashcode == 0 ) {
        return;
    }

    HavokFactoryData *pData = nullptr;
    {
        PhysFactory *factories[2] = { pPropFactory, pSceneryFactory };
        for ( PhysFactory *pF : factories ) {
            if ( pF == nullptr ) {
                continue;
            }
            HavokFactoryData *pd = pF->getHavokData();
            if ( pd == nullptr ) {
                continue;
            }
            bool bHas = false;
            for ( const auto &mm : pd->MechaMembers ) {
                if ( mm.first == hashcode ) {
                    bHas = true;
                    break;
                }
            }
            if ( !bHas ) {
                for ( const HavokToJolt::RigidBody &b : pd->Bodies ) {
                    if ( b.Hashcode == hashcode &&
                         b.Shape.GetPtr() != nullptr ) {
                        bHas = true;
                        break;
                    }
                }
            }
            if ( bHas ) {
                pData = pd;
                break;
            }
        }
    }
    if ( pData == nullptr ) {
        return;
    }

    // Resolve the entity's own-ref hashcode to the list of rigid-body hashcodes
    // to place: a Mecha expands to its member bodies; otherwise the hashcode is
    // itself a bare rigid body.
    const std::vector<HavokToJolt::MechaMember> *pMembers = nullptr;
    for ( const auto &mm : pData->MechaMembers ) {
        if ( mm.first == hashcode ) {
            pMembers = &mm.second;
            break;
        }
    }
    HavokToJolt::MechaMember single;
    single.BodyHashcode = hashcode;
    const HavokToJolt::MechaMember *pList =
        pMembers ? pMembers->data() : &single;
    const size_t count = pMembers ? pMembers->size() : 1;

    struct GatheredMember {
        const HavokToJolt::RigidBody *pBody;
        uint64_t RenderKey;
    };
    std::vector<GatheredMember> members;
    members.reserve( count );
    for ( size_t i = 0; i < count; i++ ) {
        for ( const HavokToJolt::RigidBody &b : pData->Bodies ) {
            if ( b.Hashcode == pList[i].BodyHashcode &&
                 b.Shape.GetPtr() != nullptr ) {
                const uint64_t renderKey = pList[i].LogicIDs.empty()
                                               ? b.Hashcode
                                               : pList[i].LogicIDs.front();
                members.push_back( { &b, renderKey } );
                break;
            }
        }
    }
    if ( members.empty() ) {
        return;
    }

    // Matrix4x4f_t row-major -> Jolt column-major, rebased into shifted space.
    JPH::Mat44 world = JPH::Mat44::sIdentity();
    for ( int32_t c = 0; c < 4; c++ ) {
        world.SetColumn3( c, JPH::Vec3( worldTransform( c, 0 ),
                                        worldTransform( c, 1 ),
                                        worldTransform( c, 2 ) ) );
    }
    {
        const Eigen::Vector4f shiftOrigin = GetShiftOrigin();
        world.SetTranslation(
            world.GetTranslation() -
            JPH::Vec3( shiftOrigin.x(), shiftOrigin.y(), shiftOrigin.z() ) );
    }

    const float linDamp = std::min( std::max( linearDamping, 0.0f ), 1.0f );
    const float angDamp = std::min( std::max( angularDamping, 0.05f ), 1.0f );

    lockMutex();
    JPH::BodyInterface &bodyInterface = pPhysicsSystem->GetBodyInterface();

    if ( bDynamic ) {
        const uint32_t groupID = gNextPropGroupID++;
        JPH::GroupFilterTable *pFilter = GetPropGroupFilter();

        bool bPrimarySet = false;
        std::vector<JPH::Body *> placed( members.size(), nullptr );
        for ( size_t i = 0; i < members.size(); i++ ) {
            const HavokToJolt::RigidBody *m = members[i].pBody;
            const JPH::Mat44 local =
                JPH::Mat44::sRotationTranslation( m->Rotation, m->Position );
            const JPH::Mat44 placedM = world * local;
            const bool bAnchor = m->bFixedMotion;

            JPH::BodyCreationSettings settings(
                m->Shape, placedM.GetTranslation(),
                placedM.GetQuaternion().Normalized(),
                bAnchor ? JPH::EMotionType::Static : JPH::EMotionType::Dynamic,
                bAnchor ? TDUPhysLayers::NON_MOVING : TDUPhysLayers::MOVING );
            if ( !bAnchor ) {
                settings.mOverrideMassProperties =
                    JPH::EOverrideMassProperties::CalculateInertia;
                // Asset mass (hkMotion 1/mass @+0xc0); fall back to a default
                // if the motion carried none.
                settings.mMassPropertiesOverride.mMass =
                    m->Mass > 0.0f ? m->Mass : kDynamicPropMass;
                settings.mLinearDamping = linDamp;
                settings.mAngularDamping = angDamp;
            }
            settings.mCollisionGroup = JPH::CollisionGroup(
                pFilter, groupID,
                static_cast<uint32_t>( i % kMaxPropMembers ) );
            settings.mUserData = TDUSurface::Generic;
            settings.mFriction = TDUSurface::friction( TDUSurface::Generic );

            JPH::Body *pBody = bodyInterface.CreateBody( settings );
            if ( pBody != nullptr ) {
                bodyInterface.AddBody( pBody->GetID(),
                                       JPH::EActivation::DontActivate );
                placed[i] = pBody;
                outBodies.emplace_back( members[i].RenderKey, pBody );

                if ( !bPrimarySet && outPrimaryLocalInv != nullptr ) {
                    Matrix4x4f_t localRow;
                    for ( int32_t c = 0; c < 4; c++ ) {
                        const JPH::Vec3 v = local.GetColumn3( c );
                        localRow( c, 0 ) = v.GetX();
                        localRow( c, 1 ) = v.GetY();
                        localRow( c, 2 ) = v.GetZ();
                        localRow( c, 3 ) = ( c == 3 ) ? 1.0f : 0.0f;
                    }
                    *outPrimaryLocalInv = localRow.inverse();
                    bPrimarySet = true;
                }
            }
        }

        for ( const HavokToJolt::Constraint &jc : pData->Constraints ) {
            JPH::Body *bA = nullptr;
            JPH::Body *bB = nullptr;
            for ( size_t i = 0; i < members.size(); i++ ) {
                if ( placed[i] == nullptr ) {
                    continue;
                }
                if ( members[i].pBody->Hashcode == jc.BodyAHashcode ) {
                    bA = placed[i];
                }
                if ( members[i].pBody->Hashcode == jc.BodyBHashcode ) {
                    bB = placed[i];
                }
            }
            if ( bA == nullptr || bB == nullptr || bA == bB ) {
                continue;
            }
            if ( bA->GetMotionType() == JPH::EMotionType::Static &&
                 bB->GetMotionType() == JPH::EMotionType::Static ) {
                continue; // nothing to simulate
            }

            JPH::FixedConstraintSettings cs;
            cs.mAutoDetectPoint = true; // weld at the current relative pose
            JPH::TwoBodyConstraint *pC = cs.Create( *bA, *bB );
            if ( pC == nullptr ) {
                continue;
            }
            pPhysicsSystem->AddConstraint( pC );

            PropConstraintRecord rec;
            rec.pConstraint = static_cast<JPH::FixedConstraint *>( pC );
            rec.BodyA = bA->GetID();
            rec.BodyB = bB->GetID();
            rec.Threshold = jc.Threshold;
            rec.bBreakable = jc.bBreakable;
            gPropConstraints.push_back( rec );
        }
    } else {
        // Static prop.
        const JPH::Vec3 base =
            bSceneryInstance ? JPH::Vec3( objectOrigin.x(), objectOrigin.y(),
                                          objectOrigin.z() )
                             : JPH::Vec3::sZero();
        const JPH::Vec3 posScale( offsetScale.x(), offsetScale.y(),
                                  offsetScale.z() );
        const JPH::Vec3 shpScale( shapeScale.x(), shapeScale.y(),
                                  shapeScale.z() );
        const JPH::Vec3 kUnitScale = JPH::Vec3::sReplicate( 1.0f );
        const bool bScalePos =
            bSceneryInstance && !posScale.IsClose( kUnitScale );
        const bool bScaleShape =
            bSceneryInstance && !shpScale.IsClose( kUnitScale );
        for ( const GatheredMember &gm : members ) {
            const HavokToJolt::RigidBody *m = gm.pBody;
            JPH::Vec3 memberPos =
                bSceneryInstance ? ( m->Position - base ) : m->Position;
            JPH::RefConst<JPH::Shape> shape = m->Shape;
            if ( bScalePos ) {
                memberPos = memberPos * posScale;
            }
            if ( bScaleShape ) {
                shape = HavokToJolt::buildScaledShape( *m, shpScale );
            }
            const JPH::Mat44 local =
                JPH::Mat44::sRotationTranslation( m->Rotation, memberPos );
            const JPH::Mat44 placedM = world * local;
            JPH::BodyCreationSettings settings(
                shape, placedM.GetTranslation(),
                placedM.GetQuaternion().Normalized(), JPH::EMotionType::Static,
                TDUPhysLayers::NON_MOVING );
            settings.mUserData = TDUSurface::Generic;
            settings.mFriction = TDUSurface::friction( TDUSurface::Generic );

            JPH::Body *pBody = bodyInterface.CreateBody( settings );
            if ( pBody != nullptr ) {
                bodyInterface.AddBody( pBody->GetID(),
                                       JPH::EActivation::DontActivate );
                outBodies.emplace_back( gm.RenderKey, pBody );
            }
        }
    }
    unlockMutex();
}

void GSPhysics::removePropConstraintsFor( RigidBody_t pBody ) {
    if ( pBody == nullptr || pPhysicsSystem == nullptr ) {
        return;
    }
    const JPH::BodyID id = static_cast<JPH::Body *>( pBody )->GetID();
    lockMutex();
    for ( auto it = gPropConstraints.begin(); it != gPropConstraints.end(); ) {
        if ( it->BodyA == id || it->BodyB == id ) {
            if ( it->pConstraint != nullptr ) {
                pPhysicsSystem->RemoveConstraint( it->pConstraint );
            }
            it = gPropConstraints.erase( it );
        } else {
            ++it;
        }
    }
    unlockMutex();
}

bool GSPhysics::getBodyPose( RigidBody_t body,
                             Matrix4x4f_t &outMatrix,
                             bool &outActive ) {
    outActive = false;
    if ( body == nullptr || pPhysicsSystem == nullptr ) {
        return false;
    }
    JPH::Body *pBody = static_cast<JPH::Body *>( body );
    lockMutex();
    const JPH::BodyID id = pBody->GetID();
    JPH::BodyInterface &bodyInterface = pPhysicsSystem->GetBodyInterface();
    outActive = bodyInterface.IsActive( id );
    const JPH::Mat44 t = bodyInterface.GetWorldTransform( id );
    unlockMutex();

    const Eigen::Vector4f shiftOrigin = GetShiftOrigin();
    for ( int32_t c = 0; c < 4; c++ ) {
        const JPH::Vec3 v = t.GetColumn3( c );
        outMatrix( c, 0 ) = v.GetX();
        outMatrix( c, 1 ) = v.GetY();
        outMatrix( c, 2 ) = v.GetZ();
        outMatrix( c, 3 ) = ( c == 3 ) ? 1.0f : 0.0f;
    }
    outMatrix( 3, 0 ) += shiftOrigin.x();
    outMatrix( 3, 1 ) += shiftOrigin.y();
    outMatrix( 3, 2 ) += shiftOrigin.z();
    return true;
}

void GSPhysics::physicsStep( float dt, float accTime ) {
    // FUN_0081dfd0
    // Equivalent of one Havok world step + per-vehicle integration pass.
    lockMutex();

    // Clamp runaway delta times (e.g. after a breakpoint / window focus loss).
    if ( dt > kPhysicsDtClampThreshold )
        dt = kPhysicsMaxDt;

    // Apply global time-scale for slow-motion / replay speed control.
    dt *= gPhysicsTimeScale;

    buildProcessPendingProps();

    if ( pPhysicsSystem != nullptr && !bPauseJoltTick && !bFreezeSimulation &&
         dt > 0.0f ) {
        // Original: hkpWorld::stepDelta (Havok) — Jolt equivalent.
        // numCollisionSteps=1 matches the original single-pass Havok
        // broadphase.
        pPhysicsSystem->Update( dt, 1, pTempAllocator, pJobSystem );

        if ( !gPropConstraints.empty() ) {
            JPH::BodyInterface &bi = pPhysicsSystem->GetBodyInterface();
            for ( auto it = gPropConstraints.begin();
                  it != gPropConstraints.end(); ) {
                if ( it->bBreakable && it->pConstraint != nullptr ) {
                    const float impulse =
                        it->pConstraint->GetTotalLambdaPosition().Length();
                    if ( impulse > it->Threshold * kBreakThresholdScale ) {
                        pPhysicsSystem->RemoveConstraint( it->pConstraint );
                        // Wake the freed pieces so they respond to gravity.
                        bi.ActivateBody( it->BodyA );
                        bi.ActivateBody( it->BodyB );
                        it = gPropConstraints.erase( it );
                        continue;
                    }
                }
                ++it;
            }
        }
    }

    // Tick PhysicsVehicle vehicle dynamics (pushes driver input; the Jolt
    for ( PhysicsVehicle *it = pPhysicsVehiclesBegin; it != pPhysicsVehiclesEnd;
          ++it ) {
        it->tick( dt );
        if ( it->isActive() ) {
            it->publishStepPose();
        }
    }

    OTDU_UNIMPLEMENTED_ONCE; // TODO: FUN_0081cd10 (FFB)

    unlockMutex();
}

void GSPhysics::initializeJolt() {
    // FUN_0081d960
    lockMutex();

    pBpLayerInterface = new TDUBPLayerInterface();
    pObjectVsBPLayerFilter = new TDUObjectVsBroadPhaseLayerFilter();
    pObjectLayerPairFilter = new TDUObjectLayerPairFilter();

    pTempAllocator = new JPH::TempAllocatorImpl( 32 * 1024 * 1024 );
    pJobSystem = new JPH::JobSystemThreadPool(
        JPH::cMaxPhysicsJobs, JPH::cMaxPhysicsBarriers,
        static_cast<int>( std::thread::hardware_concurrency() ) - 1 );

    JPH::Factory::sInstance = new JPH::Factory();
    JPH::RegisterTypes();

    pPhysicsSystem = new JPH::PhysicsSystem();
    pPhysicsSystem->Init( 65536, 0, 65536, 20480, *pBpLayerInterface,
                          *pObjectVsBPLayerFilter, *pObjectLayerPairFilter );

    JPH::PhysicsSettings physicsSettings = pPhysicsSystem->GetPhysicsSettings();
    physicsSettings.mNumVelocitySteps = 24;
    physicsSettings.mNumPositionSteps = 4;
    physicsSettings.mPointVelocitySleepThreshold = 0.1f;
    pPhysicsSystem->SetPhysicsSettings( physicsSettings );

    pPhysicsSystem->SetGravity( JPH::Vec3( 0.0f, -9.81f, 0.0f ) );

    pContactListener = new PhysicsFXContactListener();
    pPhysicsSystem->SetContactListener( pContactListener );

    // Create world-reference body (equivalent of the Havok sphere at origin
    // stored in DAT_010e749c; used as a stable anchor in the physics world).
    Eigen::Vector4f origin = Eigen::Vector4f::Zero();
    worldRefBody = createSphere( 2, origin, 0.5f, 1.0f, 1.0f, false );

    bJoltInit = true;
    unlockMutex();
}

void GSPhysics::setWorldOrigin( const Eigen::Vector4f *pPosition ) {
    // FUN_00843cc0
    if ( gpWorldStreaming != nullptr && gpWorldStreaming->isEnabled() ) {
        return;
    }

    (void)pPosition;
    OTDU_UNIMPLEMENTED; // Havok broadphase shift
}

Eigen::Vector4f GSPhysics::GetShiftOrigin() {
    if ( gpWorldStreaming != nullptr && gpWorldStreaming->isEnabled() ) {
        Eigen::Vector4f origin = gpWorldStreaming->getPosition();
        origin.w() = 0.0f;
        return origin;
    }
    return Eigen::Vector4f::Zero();
}

bool GSPhysics::onWorldStreamed( Eigen::Vector4f *pOffset,
                                 Eigen::Vector4f *pLastPosition,
                                 Eigen::Vector4f *pPosition ) {
    // FUN_00847730
    (void)pLastPosition;
    (void)pPosition;

    if ( pOffset == nullptr || pPhysicsSystem == nullptr ) {
        return true;
    }

    // Content moves the opposite way to the origin.
    const JPH::Vec3 shift( -pOffset->x(), -pOffset->y(), -pOffset->z() );
    if ( shift.IsNearZero() ) {
        return true;
    }

    JPH::BodyIDVector bodyIDs;
    pPhysicsSystem->GetBodies( bodyIDs );

    JPH::BodyInterface &bodyInterface = pPhysicsSystem->GetBodyInterface();
    for ( const JPH::BodyID id : bodyIDs ) {
        bodyInterface.SetPosition( id, bodyInterface.GetPosition( id ) + shift,
                                   JPH::EActivation::DontActivate );
    }

    if ( pPhysicsVehiclesBegin != nullptr ) {
        const Eigen::Vector3f vehicleShift( shift.GetX(), shift.GetY(),
                                            shift.GetZ() );
        for ( PhysicsVehicle *it = pPhysicsVehiclesBegin;
              it != pPhysicsVehiclesEnd; ++it ) {
            if ( it->isActive() ) {
                it->onWorldShifted( vehicleShift );
            }
        }
    }

    OTDU_LOG_DEBUG( "World origin stepped by (%.1f, %.1f, %.1f) -> "
                    "(%.1f, %.1f, %.1f); moved %zu physics bodies\n",
                    pOffset->x(), pOffset->y(), pOffset->z(), pPosition->x(),
                    pPosition->y(), pPosition->z(), bodyIDs.size() );

    return true;
}
