#pragma once

#include "game/gs_base.h"
#include "core/mutex.h"
#include "core/thread.h"

#include "filesystem/bank.h"

#include "physics_factory.h"
#include "road_phantom_manager.h"

#include <utility>
#include <vector>

namespace JPH {
    class PhysicsSystem;
    class TempAllocatorImpl;
    class JobSystemThreadPool;
    class Body;
} // namespace JPH

class TDUBPLayerInterface;
class TDUObjectVsBroadPhaseLayerFilter;
class TDUObjectLayerPairFilter;

class PhysicsVehicle;
class DrawList;
struct PhysicsSHK;
struct PhysicsSHKRoad;
struct DBEntryCarPhysicsData;

using RigidBody_t = JPH::Body *;

namespace TDUSurface {
    enum Class : uint32_t {
        Unknown = 0,
        Road = 1,    // -R.shk road mesh (orig phys-type 3) -> asphalt, on-road
        Terrain = 2, // heightfield (orig phys-type 4) -> off-road (dirt/grass)
        Object = 3,  // .shk world mesh/post (orig phys-type 2/7): paved/solid
                     // drive surface (junctions, curbs, floors) -> ON-ROAD
    };

    inline float friction( uint32_t surfaceClass ) {
        switch ( surfaceClass ) {
        case Road:
            return 1.0f; // asphalt: full grip (FLOAT_00f4165c)
        case Terrain:
            return 0.75f; // off-road: reduced grip
        case Object:      // paved/solid on-road surface: full grip
        default:
            return 1.0f;
        }
    }
} // namespace TDUSurface

struct CameraCollisionHit {
    Eigen::Vector4f contactPoint; // World-space contact point on the geometry
    Eigen::Vector4f exitNormal; // normalize(-mPenetrationAxis): push sphere OUT
    float penetrationDepth;
};

struct TirePacejka {
    float MaxSlipRatio = 0.0f;
    float MaxSlipAngle = 0.0f;
    float SlipRatioLatitude = 0.0f;
    float MinSlipRatioLatitude = 0.0f;

    int32_t NumPoints = 0;
    float MaxLoad = 0.0f;
    float pCurvePointsY[11];
    float pCurvePointsX[15];

    float pSlipAngleCurve[32];
    float pSlipRatioCurve[32];
};
OTDU_SIZE_MUST_MATCH( TirePacejka, 0x180 );

enum ePhysicsThreadMode : int32_t {
    PTM_Off = 0,
    PTM_Normal = 1,
    PTM_Realtime = 2
};
extern ePhysicsThreadMode gPhysicsThreadPriority;
extern int32_t gPhysicsThreadFrequency;

class GSPhysics : public GameSystem, public Bank {
public:
    const char *getName() const override { return "Service : Physics"; }

    inline void lockMutex() {
        mutex.lock();
        isLocked = 1;
    }

    inline void unlockMutex() {
        mutex.unlock();
        isLocked = 0;
    }

    friend void *FUN_0081f0b0( void *pArg );
    inline void setDebugModeEnabled( bool bState ) {
        bHavokDebugEnabled = bState;
    }
    inline void setDebugDrawEnabled( bool bState ) { bDrawDebugInfos = bState; }
    inline bool isPropFactoryReady() const { return pPropFactory != nullptr; }

    inline int32_t getNumTireProfiles() const { return numTires; }
    inline const TirePacejka *getTireProfile( int32_t index ) const {
        return ( index >= 0 && index < numTires ) ? pTireProfiles[index]
                                                  : nullptr;
    }

public:
    GSPhysics();
    ~GSPhysics();

    bool initialize( TestDriveGameInstance * ) override;
    void terminate() override;
    void tick( float totalTime, float deltaTime ) override;
    void draw( float deltaTime ) override;
    void drawDebug( float deltaTime ) override;
    void pause() override { /* FUN_00841760 (NOP) */ }
    void reset() override { /* FUN_00841850 (NOP) */ }

    void FUN_00843b40();

    PhysFactory *createFromBinary( void *param_3, int32_t param_2 );
    void addInfinitePlane( float planeHeight );
    void removeAndDestroyFactory( PhysFactory *param_2 );
    RigidBody_t addHeightfield( const void *pKey,
                                const float *pHeights,
                                int32_t stride,
                                float cellSize,
                                float originX,
                                float originZ );
    void removeHeightfield( const void *pKey );

    void addSectorCollision( const void *pKey,
                             void *pBlob,
                             uint32_t blobSize,
                             float baseX,
                             float baseZ,
                             uint32_t surfaceClass );
    void removeSectorCollision( const void *pKey );
    void
    addSectorRoadGround( const void *pKey, void *pBlob, uint32_t blobSize );
    void addSpotCollision( const void *pKey,
                           void *pBlob,
                           uint32_t blobSize,
                           float angleY,
                           const Eigen::Vector3f &translation );

    void queuePropCollisionFactory( const void *pBlob, uint32_t blobSize );
    void queueSceneryCollisionFactory( const void *pBlob, uint32_t blobSize );

    void placeSceneryInstance(
        const void *pSectorKey,
        uint64_t hashcode,
        const Matrix4x4f_t &worldTransform,
        const Eigen::Vector3f &objectOrigin = Eigen::Vector3f::Zero() );
    void removeSceneryInstances( const void *pSectorKey );

    void installSceneryCollision();
    bool isPropCollisionType( uint64_t hashcode ) const;

    void
    placeProp( uint64_t hashcode,
               const Matrix4x4f_t &worldTransform,
               bool bDynamic,
               float linearDamping,
               float angularDamping,
               std::vector<std::pair<uint64_t, RigidBody_t>> &outBodies,
               Matrix4x4f_t *outPrimaryLocalInv = nullptr,
               bool bSceneryInstance = false,
               const Eigen::Vector3f &objectOrigin = Eigen::Vector3f::Zero() );

    void removePropConstraintsFor( RigidBody_t pBody );

    bool
    getBodyPose( RigidBody_t body, Matrix4x4f_t &outMatrix, bool &outActive );

    void instantiateFactory( PhysFactory *pFactory,
                             const Matrix4x4f_t &worldTransform );

    PhysicsVehicle *createVehicle( PhysFactory *pFactory,
                                   const Matrix4x4f_t &worldTransform,
                                   const DBEntryCarPhysicsData &db,
                                   const void *pTorqueBinary = nullptr );

    RigidBody_t createSphere( uint32_t collisionGroup,
                              const Eigen::Vector4f &worldPosition,
                              float radius,
                              float mass,
                              float massScale,
                              bool registerNow,
                              bool bPhantom = true );

    void destroyRigidBody( RigidBody_t pRigidBody );

    void moveSphereBody( RigidBody_t body, const Eigen::Vector4f &position );

    int32_t queryCameraCollisions( const Eigen::Vector4f &center,
                                   float radius,
                                   std::vector<CameraCollisionHit> &hits );
    float getGroundHeight( float x, float z );
    int32_t gatherRoadShadowTriangles( const Eigen::Vector4f &bbMin,
                                       const Eigen::Vector4f &bbMax,
                                       Eigen::Vector4f *pOutVerts,
                                       int32_t maxVerts );

    void setWorldOrigin( const Eigen::Vector4f *param_1 );

private:
    static constexpr int32_t kMaxNumTireProfiles = 16;
    static constexpr int32_t kMaxNumPhysicsVehicles = 32;
    static constexpr int32_t kMaxNumPhysicsFactory = 64; // World/scene entities
    static constexpr int32_t kMaxNumObjectsPerSector = 256;
    static constexpr int32_t kNumHeightfields = 7; // One per streamed sector
    // Fixed mass for a destructible prop body (temp for test; to be removed)
    static constexpr float kDynamicPropMass = 40.0f;

private:
    struct PendingSceneryInstance {
        const void *pSectorKey;
        uint64_t hashcode;
        Matrix4x4f_t transform;
        Eigen::Vector3f objectOrigin;
    };

private:
    TestDriveThread simulationThread;
    TestDriveMutex mutex;
    int32_t isLocked;

    RoadPhantomManager roadPhantomManager;

    int32_t numTires;
    std::array<TirePacejka *, kMaxNumTireProfiles> pTireProfiles;

    int32_t numPhysicsVehicle;
    PhysicsVehicle *pPhysicsVehicles;
    PhysicsVehicle *pPhysicsVehiclesBegin;
    PhysicsVehicle *pPhysicsVehiclesEnd;
    void *field_0x2cc;

    std::array<PhysFactory, kMaxNumPhysicsFactory> pFactories;
    PhysFactory pmiFactory;

    uint32_t numFreeSectors;
    uint32_t maxNumSectors;

    PhysicsSHK *pSectors;
    PhysicsSHK *pSectorsBegin;
    PhysicsSHK *pSectorsEnd;

    uint32_t numFreeRoadSectors;
    uint32_t maxNumRoadSectors;

    PhysicsSHKRoad *pRoads;
    PhysicsSHKRoad *pRoadsBegin;
    PhysicsSHKRoad *pRoadsEnd;

    void *field27335_0x8670;

    uint32_t field_0x31a0[2];
    uint32_t field_0x4dc0[2];
    uint32_t field_0x5bd0[2];
    uint32_t field_0x69e0[2];
    uint32_t field_0x8600[2];

    uint8_t bJoltInit : 1;
    uint8_t bFreezeSimulation : 1;
    uint8_t bPauseJoltTick : 1;
    uint8_t bHeightfieldInit : 1;
    uint8_t bSectorsInit : 1;
    uint8_t bHavokDebugEnabled : 1;
    uint8_t bDrawDebugInfos : 1;

    // Jolt physics runtime objects — only valid after initializeJolt()
    JPH::PhysicsSystem *pPhysicsSystem = nullptr;
    JPH::TempAllocatorImpl *pTempAllocator = nullptr;
    JPH::JobSystemThreadPool *pJobSystem = nullptr;
    TDUBPLayerInterface *pBpLayerInterface = nullptr;
    TDUObjectVsBroadPhaseLayerFilter *pObjectVsBPLayerFilter = nullptr;
    TDUObjectLayerPairFilter *pObjectLayerPairFilter = nullptr;

    // Body IDs (stored as raw uint32 to avoid Jolt includes; value ==
    // JPH::BodyID::cInvalidBodyID when invalid)
    RigidBody_t worldRefBody = nullptr;
    uint32_t infinitePlaneBodyID = 0xffffffff;

    struct PhysicsHeightfield {
        RigidBody_t pBody = nullptr;
        const void *pKey = nullptr;
    };
    std::array<PhysicsHeightfield, kNumHeightfields> pHeightfields;

    std::vector<uint8_t> pendingPropBlob;
    PhysFactory *pPropFactory = nullptr;

    std::vector<uint8_t> pendingSceneryBlob;
    PhysFactory *pSceneryFactory = nullptr;

#if OTDU_DEVBUILD
    DrawList *pCollisionDebugList = nullptr;
#endif

    std::vector<RigidBody_t> sceneryCollisionBodies;
    std::vector<uint64_t> sceneryPlacedHashes;

    std::vector<std::pair<const void *, RigidBody_t>> sceneryInstanceBodies;
    std::vector<PendingSceneryInstance> pendingSceneryInstances;

private:
    void createHeightfields();
    void initializeJolt();
    void buildProcessPendingProps();
    void physicsStep( float dt, float accTime );
    void drawCollisionShapes();
};

extern GSPhysics *gpPhysics;
extern bool gDrawCollisionVis;
extern bool gDrawCollisionVisTerrain;

// Mirrors ScopedMutexLock. This is required; as GSPhysics uses a
// separate integer to track the mutex lock state.
struct PhysicsLock {
    PhysicsLock() { gpPhysics->lockMutex(); }
    ~PhysicsLock() { gpPhysics->unlockMutex(); }
};

extern int32_t INT_00f87fec;
extern int32_t INT_00f87ff4;
extern int32_t INT_00f87ff8;
