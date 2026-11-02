#pragma once

#include "game/gs_base.h"
#include "core/mutex.h"
#include "core/thread.h"

#include "filesystem/bank.h"

#include "physics_factory.h"
#include "road_phantom_manager.h"

#include "world/gs_world_streaming.h"
#include "vehicle/physics_vehicle.h"

#include <utility>
#include <vector>

namespace JPH {
    class PhysicsSystem;
    class TempAllocatorImpl;
    class JobSystemThreadPool;
    class Body;
    class ContactListener;
} // namespace JPH

class TDUBPLayerInterface;
class TDUObjectVsBroadPhaseLayerFilter;
class TDUObjectLayerPairFilter;

class PhysicsVehicle;
class DrawList;
struct HavokFactoryData;

namespace HavokToJolt {
    struct RigidBody;
}
struct PhysicsSHK;
struct PhysicsSHKRoad;
struct DBEntryCarPhysicsData;
struct DBEntryAftermarketPack;
class PhysicsCharacter;

using RigidBody_t = JPH::Body *;
using CharacterController_t = PhysicsCharacter *;

namespace TDUSurface {
    enum Class : uint32_t {
        None = 0,       // nothing under the wheel
        RoadGround = 1, // SR20 .rd
        Paved = 2,      // .shk world MESH (FUN_00844f00) + AddInfinitePlane
        RoadMesh = 3,   // -R.shk road MESH (FUN_00845380)
        Terrain = 4,    // heightfield — never tagged; the only off-road surface
        Generic = 7,    // .shk/-R.shk NON-mesh (posts, boxes) + placed props
        Vehicle = 9,    // car chassis (FUN_0082fc80)
        RoadPhantom = 0x10, // SR20 record sensor (FUN_00819ec0)
        BikeRider = 0x11,   // ejected bike rider (FUN_0081d760)
    };

    // Mask applied to a body's Jolt UserData to recover the phys-type byte.
    constexpr uint64_t kClassMask = 0xFF;
    constexpr uint32_t kOwnerShift = 8;

    inline bool isOnRoad( uint32_t physType ) {
        // FUN_0083bbd0
        return physType == RoadGround || physType == Paved ||
               physType == RoadMesh || physType == Generic;
    }
} // namespace TDUSurface

extern bool gbBackOnTrackFadeGuard; // DAT_00f7f1d8

// Screen fade requests queued for the replay manager's fade sequencer
// (FUN_0081c330)
enum class eFadeRequest : int32_t {
    FR_ReplayPlayback = 1,
    FR_FadeIn = 2,
    FR_FadeOut = 3,
    FR_Idle = 4
};

// Havok shape type of a collidable overlapping a camera phantom; only the
// shapes the camera code reacts to are distinguished.
enum class eCameraOverlapShape : int32_t {
    Other = 0,
    Sphere = 4,
    Capsule = 8,
};

struct CameraPhantomOverlap {
    eCameraOverlapShape Shape = eCameraOverlapShape::Other;
    Eigen::Vector4f Center; // World space
    float Radius = 0.0f;
};

// One hit from GSPhysics::castCameraSegment, in absolute world space.
struct CameraSegmentHit {
    Eigen::Vector4f Point;
    Eigen::Vector4f Normal;
    float Fraction = 0.0f;     // [0,1] along the segment
    uint32_t SurfaceClass = 0; // TDUSurface::Class from the body user data
    float Mass = 0.0f;         // FUN_0047aa70 (0 for static/kinematic bodies)
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
extern float gPhysicsRefreshRate;

static constexpr float kNoSurfaceSentinel = -100000.0f; // DAT_00f418fc

struct PhysicsRoadCurve {
    const void *pKey;
    Eigen::Vector4f ControlPoints[4];
    bool bCubic;
    uint16_t RoadID;
    uint8_t CurveIndex;
    uint8_t EntityIndex;
};

class GSPhysics : public GameSystem,
                  public Bank,
                  public PhysicsStreamingCallback,
                  public PhysicsDrivingControl {
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
    friend void FUN_0081ee50( GSPhysics *pPhysics );
    inline void setDebugModeEnabled( bool bState ) {
        bHavokDebugEnabled = bState;
    }
    inline void setDebugDrawEnabled( bool bState ) { bDrawDebugInfos = bState; }
    inline void setForceFeedbackAllowed( bool bState ) {
        bForceFeedbackAllowed = bState;
    }
    inline void setPhysicsThreadPaused( bool bState ) {
        bPausePhysicsThread = bState;
    }
    inline bool isPlayerSlowDownForced() const { return bForcePlayerSlowDown; }
    inline void setPlayerSlowDownForced( bool bState ) {
        bForcePlayerSlowDown = bState;
    }
    inline bool isPlayerParkedForced() const { return bForcePlayerParked; }
    inline void setPlayerParkedForced( bool bState ) {
        bForcePlayerParked = bState;
    }
    inline bool isPropFactoryReady() const { return !propFactories.empty(); }

    inline int32_t getNumTireProfiles() const { return numTires; }
    inline const TirePacejka *getTireProfile( int32_t index ) const {
        return ( index >= 0 && index < numTires ) ? pTireProfiles[index]
                                                  : nullptr;
    }
    // DANGER: NOT THREAD SAFE
    inline const std::vector<PhysicsRoadCurve> &getRoadCurves() const {
        return roadCurves;
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

    float getRenderInterpolationAlpha() const;
    bool getBodyRenderPose( RigidBody_t body,
                            Matrix4x4f_t &outMatrix,
                            bool &outActive );

    void FUN_00843b40();

    PhysFactory *createFromBinary( void *param_3, int32_t param_2 );
    void addInfinitePlane( float planeHeight );
    void removeInfinitePlane();
    void removeAndDestroyFactory( PhysFactory *param_2 );
    RigidBody_t addHeightfield( const void *pKey,
                                const float *pHeights,
                                int32_t stride,
                                float cellSize,
                                float originX,
                                float originZ,
                                const float *pTileErrors = nullptr,
                                int32_t numTileX = 0,
                                int32_t numTileZ = 0 );
    void removeHeightfield( const void *pKey );
    void refreshHeightmapCollision();
    void forwardInput( PhysicsVehicle &vehicle,
                       float deltaTime,
                       int32_t argument ) override;

    void addSectorCollision( const void *pKey,
                             void *pBlob,
                             uint32_t blobSize,
                             float baseX,
                             float baseZ,
                             bool bRoadBlob );
    void removeSectorCollision( const void *pKey );
    void
    addSectorRoadGround( const void *pKey, void *pBlob, uint32_t blobSize );

    PhysFactory *installStaticCollision( void *pBlob,
                                         uint32_t blobSize,
                                         uint64_t factoryHashcode,
                                         const Matrix4x4f_t *pTransform );

    void queuePropCollisionFactory( const void *pBlob,
                                    uint32_t blobSize,
                                    PhysFactory **ppOwnerSlot = nullptr );
    void cancelPendingPropFactory( PhysFactory **ppOwnerSlot );
    void queueSceneryCollisionFactory( const void *pBlob, uint32_t blobSize );

    void placeSceneryInstance(
        const void *pSectorKey,
        uint64_t hashcode,
        const Matrix4x4f_t &worldTransform,
        const Eigen::Vector3f &objectOrigin = Eigen::Vector3f::Zero(),
        const Eigen::Vector3f &instanceScale = Eigen::Vector3f::Ones() );
    void removeSceneryInstances( const void *pSectorKey );

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
               const Eigen::Vector3f &objectOrigin = Eigen::Vector3f::Zero(),
               const Eigen::Vector3f &offsetScale = Eigen::Vector3f::Ones(),
               const Eigen::Vector3f &shapeScale = Eigen::Vector3f::Ones() );

    void removePropConstraintsFor( RigidBody_t pBody );

    struct PropMemberAttachment {
        int32_t NumUnbreakable = 0;
        int32_t NumBreakable = 0;
        int32_t NumBroken = 0;
    };
    PropMemberAttachment getPropMemberAttachment( RigidBody_t pBody );

    void moveVehicleParts( const PhysicsVehicle &vehicle,
                           const JPH::RMat44 &newChassisWorld,
                           bool bActivate );
    bool hasBrokenVehiclePart( const PhysicsVehicle &vehicle );

    bool
    getBodyPose( RigidBody_t body, Matrix4x4f_t &outMatrix, bool &outActive );
    void setBodyPose( RigidBody_t body, const Matrix4x4f_t &matrix );
    bool isBodyActive( RigidBody_t body );

    PhysicsVehicle *
    createVehicle( PhysFactory *pFactory,
                   const Matrix4x4f_t &worldTransform,
                   const DBEntryCarPhysicsData &db,
                   const void *pTorqueBinary = nullptr,
                   const DBEntryAftermarketPack *pPack = nullptr,
                   const bool bIsAI = false,
                   const bool bIsNetwork = false );
    PhysicsVehicle *installVehicle( struct VehicleInstallRequest &request );
    void destroyVehicle( PhysicsVehicle *pVehicle );
    void setVehicleOwner( PhysicsVehicle *pVehicle, void *pOwner );
    PhysicsVehicle *findVehicle( const JPH::Body &chassis ) const;
    inline PhysicsVehicle *getVehiclesBegin() const {
        return pPhysicsVehiclesBegin;
    }
    inline PhysicsVehicle *getVehiclesEnd() const {
        return pPhysicsVehiclesEnd;
    }
    // PhysicsVehicle +0xa3c
    inline int32_t getVehicleSlotIndex( const PhysicsVehicle *pVehicle ) const {
        return static_cast<int32_t>( pVehicle - pPhysicsVehiclesBegin );
    }

    static void pushFadeRequest( const eFadeRequest request );
    static eFadeRequest peekFadeRequest();
    static eFadeRequest popFadeRequest();
    static void ClearFadeRequests();

    void clearInstantReplayVehicles();
    void markInstantReplayVehicles();
    inline int32_t getNumVehiclesInInstantReplay() const {
        return numVehiclesInInstantReplay;
    }

    RigidBody_t createSphere( uint32_t collisionGroup,
                              const Eigen::Vector4f &worldPosition,
                              float radius,
                              float mass,
                              float massScale,
                              bool registerNow,
                              bool bPhantom = true );
    RigidBody_t addStaticMesh( const void *pKey,
                               const Eigen::Vector3f *pVertices,
                               uint32_t numVertices,
                               const uint32_t *pIndices,
                               uint32_t numIndices,
                               uint32_t surfaceClass = TDUSurface::Generic );
    void removeStaticMesh( const void *pKey );
    CharacterController_t createCharacter( const Eigen::Vector4f &worldPosition,
                                           float radius,
                                           float cylinderHeight,
                                           float maxSlopeAngle = 0.87266463f );
    void destroyCharacter( CharacterController_t pCharacter );
    void setCharacterCollisionRestriction( CharacterController_t pCharacter,
                                           RigidBody_t pOnlyBody );
    void setCharacterExtraCollisionBodies(
        CharacterController_t pCharacter,
        const std::vector<const JPH::Body *> &bodies );
    int32_t rayCastBodyBounds( const Eigen::Vector3f &worldOrigin,
                               const Eigen::Vector3f &direction,
                               float maxDistance,
                               const std::vector<const JPH::Body *> &bodies,
                               float *pHitDistance = nullptr );
    void updateCharacter( CharacterController_t pCharacter,
                          const Eigen::Vector3f &desiredVelocity,
                          float deltaTime );
    Eigen::Vector4f getCharacterPosition( CharacterController_t pCharacter );
    void setCharacterPosition( CharacterController_t pCharacter,
                               const Eigen::Vector4f &worldPosition );
    bool isCharacterOnGround( CharacterController_t pCharacter );
    const char *getCharacterGroundState( CharacterController_t pCharacter,
                                         Eigen::Vector3f &outGroundNormal );
    Eigen::Vector3f getCharacterVelocity( CharacterController_t pCharacter );
    float getCharacterMass( CharacterController_t pCharacter );
    void integrateCharacterForce( CharacterController_t pCharacter,
                                  const Eigen::Vector3f &force,
                                  float deltaTime );

    void destroyRigidBody( RigidBody_t pRigidBody );

    void moveSphereBody( RigidBody_t body, const Eigen::Vector4f &position );

    int32_t queryPhantomOverlaps( RigidBody_t phantom,
                                  const Eigen::Vector4f &center,
                                  float radius,
                                  std::vector<CameraPhantomOverlap> &overlaps );
    float getGroundHeight( float x, float z );
    float castWorldRay( const Eigen::Vector4f &origin,
                        const Eigen::Vector4f &direction,
                        float length );
    int32_t castCameraSegment( const Eigen::Vector4f &from,
                               const Eigen::Vector4f &to,
                               std::vector<CameraSegmentHit> &hits );
    bool probeSurface( const Eigen::Vector4f &position,
                       bool bSearchAbove,
                       float &outHeight,
                       Eigen::Vector4f &outNormal,
                       bool bStaticAndRoadOnly = false );
    int32_t gatherRoadShadowTriangles( const Eigen::Vector4f &bbMin,
                                       const Eigen::Vector4f &bbMax,
                                       Eigen::Vector4f *pOutVerts,
                                       int32_t maxVerts,
                                       int32_t *pOutNumBodies = nullptr );

    void setWorldOrigin( const Eigen::Vector4f *param_1 );

    bool onWorldStreamed( Eigen::Vector4f *pOffset,
                          Eigen::Vector4f *pLastPosition,
                          Eigen::Vector4f *pPosition ) override;

    static Eigen::Vector4f GetShiftOrigin();

    RoadPhantomManager &getRoadPhantomManager() { return roadPhantomManager; }

private:
    static constexpr int32_t kMaxNumTireProfiles = 16;
    static constexpr int32_t kMaxNumPhysicsVehicles = 32;
    static constexpr int32_t kMaxNumPhysicsFactory = 64; // World/scene entities
    static constexpr int32_t kMaxNumObjectsPerSector = 256;
    static constexpr int32_t kNumHeightfields = 7; // One per streamed sector
    // Fixed mass for a destructible prop body (temp for test; to be removed)
    static constexpr float kDynamicPropMass = 40.0f;

private:
    void createVehicleParts( PhysicsVehicle &vehicle,
                             JPH::Body &chassis,
                             const HavokFactoryData &data,
                             const HavokToJolt::RigidBody &chassisMember,
                             const JPH::Mat44 &factoryWorld,
                             uint32_t groupID );

private:
    struct PendingSceneryInstance {
        const void *pSectorKey;
        uint64_t hashcode;
        Matrix4x4f_t transform;
        Eigen::Vector3f objectOrigin;
        Eigen::Vector3f instanceScale;
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
    int32_t numVehiclesInInstantReplay;
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

    std::vector<PhysicsRoadCurve> roadCurves;

    void *field27335_0x8670;
    Eigen::Vector4f broadPhaseOrigin = Eigen::Vector4f::Zero();

    uint32_t field_0x31a0[2];
    uint32_t field_0x4dc0[2];
    uint32_t field_0x5bd0[2];
    uint32_t field_0x69e0[2];
    uint32_t field_0x8600[2];

    uint8_t bJoltInit : 1;
    uint8_t bFreezeSimulation : 1;
    uint8_t bPauseJoltTick : 1;
    uint8_t bPausePhysicsThread : 1;
    uint8_t bHeightfieldInit : 1;
    uint8_t bSectorsInit : 1;
    uint8_t bHavokDebugEnabled : 1;
    uint8_t bDrawDebugInfos : 1;

    bool bForcePlayerSlowDown = false;  // +0x8674
    bool bForcePlayerParked = false;    // +0x8675
    bool bForceFeedbackAllowed = false; // +0x8679
    uint32_t debugDrawFlags;

    // Jolt physics runtime objects — only valid after initializeJolt()
    JPH::PhysicsSystem *pPhysicsSystem = nullptr;
    JPH::TempAllocatorImpl *pTempAllocator = nullptr;
    JPH::JobSystemThreadPool *pJobSystem = nullptr;
    TDUBPLayerInterface *pBpLayerInterface = nullptr;
    TDUObjectVsBroadPhaseLayerFilter *pObjectVsBPLayerFilter = nullptr;
    TDUObjectLayerPairFilter *pObjectLayerPairFilter = nullptr;

    JPH::ContactListener *pContactListener = nullptr;

    // Body IDs (stored as raw uint32 to avoid Jolt includes; value ==
    // JPH::BodyID::cInvalidBodyID when invalid)
    RigidBody_t worldRefBody = nullptr;
    uint32_t infinitePlaneBodyID = 0xffffffff;

    struct PhysicsHeightfield {
        RigidBody_t pBody = nullptr;
        const void *pKey = nullptr;
    };
    std::array<PhysicsHeightfield, kNumHeightfields> pHeightfields;

    struct PendingPropFactory {
        std::vector<uint8_t> Blob;
        PhysFactory **ppOwnerSlot = nullptr;
    };
    std::vector<PendingPropFactory> pendingPropFactories;
    std::vector<PhysFactory *> propFactories;

    std::vector<uint8_t> pendingSceneryBlob;
    PhysFactory *pSceneryFactory = nullptr;

    DrawList *pGraphDrawList = nullptr;

#if OTDU_DEVBUILD
    DrawList *pCollisionDebugList = nullptr;
    DrawList *pVehicleDebugList = nullptr;
#endif

    std::vector<std::pair<const void *, RigidBody_t>> sceneryInstanceBodies;
    std::vector<std::pair<const void *, RigidBody_t>> staticMeshBodies;
    std::vector<PhysicsCharacter *> characters;
    std::vector<PendingSceneryInstance> pendingSceneryInstances;

private:
    void createHeightfields();
    void initializeJolt();
    void buildProcessPendingProps();
    void physicsStep( float dt, float accTime );
    void captureRenderBodyPoses(); // OTDU_ADDITION
    void updateReplayVehicleModes();
    void updateForceFeedback( float dt );
    void createGraph();
    void drawGraph();
    void drawCollisionShapes();
    void drawVehicleShapes();
    void updateGMeterToggle();
    void updateSecretIslandTeleporter();
    bool
    hasCollisionBelow( float worldX, float worldZ, float startY, float depth );
    bool isRegionCovered( float minX,
                          float minZ,
                          float maxX,
                          float maxZ,
                          float centreY,
                          float halfY );
};

extern GSPhysics *gpPhysics;
extern bool gDrawCollisionVis;
extern bool gDrawCollisionVisTerrain;
extern bool gDrawVehicleCollisionVis;
extern bool gDrawVehicleCoMVis;
extern bool gbShowGMeter;

// Mirrors ScopedMutexLock. This is required; as GSPhysics uses a
// separate integer to track the mutex lock state.
struct PhysicsLock {
    PhysicsLock() { gpPhysics->lockMutex(); }
    ~PhysicsLock() { gpPhysics->unlockMutex(); }
};

extern int32_t INT_00f87fec;
extern int32_t INT_00f87ff4;
extern int32_t INT_00f87ff8;
