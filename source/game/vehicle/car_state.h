#pragma once

#include "game/game_object.h"
#include "game/material_types.h"
#include "game/police_state.h"
#include "physics/collision_event.h"

class DynamicRigidBody;
class VehicleBase;
struct VehicleBaseResources;
class PhysicsVehicle;
struct DirectionalLight;

struct WheelState {
    float Omega;
    float SteerAngle;
    float CamberAngle;
    float CamberBreak;
    float SuspensionTravelLength;
    float SlipRatio;
    float SlipAngle;
    float LongitudinalSpeed;
    float LateralSpeed;
    float Load;
    float WheelAngle;
    float DistanceFromGround;
    float WheelRadius;
    float Position;
    int32_t WheelIndex;
    eMaterialType ContactMaterial;
    float AudioGroundSpeedKmh;
    float AudioLatSpeedKmh;
    float AudioSlipLong;
    float AudioSlipLat;
    Eigen::Vector4f ShadowColor;
    Eigen::Vector4f GroundColor;
    Eigen::Vector4f RelativeWorldPosition;
    Eigen::Vector4f WorldPosition;
    Eigen::Vector4f ProjectedPositionOnGround;
    Matrix4x4f_t WorldMatrixRotor;
    Matrix4x4f_t WorldMatrixFork;
    uint8_t bInitialized : 1;
    uint8_t bGrounded : 1;
    uint8_t bOnRoad : 1;
    uint8_t bOnBridge : 1;
    uint8_t bInsideTunnel : 1;

    WheelState()
        : bInitialized( false ),
          bGrounded( false ),
          bOnRoad( false ),
          bOnBridge( false ),
          bInsideTunnel( false ) {}

    void reset();
    Eigen::Vector4f getShadowColor() const;
};

struct CarBodyState {
    Matrix4x4f_t WorldMatrix;
    Matrix4x4f_t OffsetedWorldMatrix;
    Matrix4x4f_t PreviousWorldMatrix;
    Matrix4x4f_t PhysicsWorldMatrix; // Looks like this is used to keep the
                                     // Havok instance in sync with the
                                     // "logic" scene instance. Remove it?

    Eigen::Vector4f AngularVelocity;
    Eigen::Vector4f LinearVelocity;
    Eigen::Vector4f Accel;
    Eigen::Vector4f AccelDiff;

    float MaxOmega;
    float MaxTorque;
    float MinTorque;

    float Omega;
    float Torque;
    float SpeedInKmh;
    float EngineRPM;      // omega * 60/2pi
    float EnginePowerPS;  // torque * Omega / 735.5
    float EngineTorqueNm; // the SMOOTHED net torque / FakeCoef(1)
    float AudioSpeedKmh;  // chassis speed * 3.6

    uint8_t bInitialized : 1;
    uint8_t bIsBraking : 1;
    uint8_t bIsReversing : 1;
    uint8_t bHeadlightsOn : 1;
    uint8_t bFlashingLights : 1;
    uint8_t bHazardLightsOn : 1;
    uint8_t bHonking : 1;
    uint8_t bIndicatingLeft : 1;
    uint8_t bIndicatingRight : 1;
    uint8_t bPoliceGyroEnabled : 1;
    uint8_t bBackOnTracked : 1;
    uint8_t bUseWheelMatrix : 1;
    uint8_t bIsInTunnel : 1;
    uint8_t bHasBrokenPart : 1; // Seems only to be used for traffic?
    uint8_t bIsBike : 1;
    uint8_t bBikeIdle : 1;
    uint8_t bHasBikeAvatar : 1;

    CarBodyState()
        : bInitialized( false ),
          bIsBraking( false ),
          bIsReversing( false ),
          bHeadlightsOn( false ),
          bFlashingLights( false ),
          bHazardLightsOn( false ),
          bHonking( false ),
          bIndicatingLeft( false ),
          bIndicatingRight( false ),
          bPoliceGyroEnabled( false ),
          bBackOnTracked( false ),
          bUseWheelMatrix( false ),
          bIsInTunnel( false ),
          bHasBrokenPart( false ),
          bIsBike( false ),
          bBikeIdle( false ),
          bHasBikeAvatar( false ) {}

    int32_t WindowState;
    int32_t Gears;
    int32_t GearState;
    int32_t OdometerKms;
    int32_t OdometerTenMeters;

    float Distance;
    int32_t NumWheelsOnRoad;
    int32_t NumWheelsGrounded;

    float GearShiftTimer;
    float BikeLeanLateral;
    float BikeLeanLongitudinal;
    float SteeringRaw;

    // Inputs.
    float Steering;
    float Throttle;
    float Brake;
    float Handbrake;
    float Clutch;
    float Lean;          // bike fore/aft lean
    int32_t GearRequest; // 0 = R, 1 = N, 2..8 = 1st..7th
    bool bGearUpEdge;
    bool bGearDownEdge;
    bool bIgnition;     // starter held (engine not run)
    bool bIgnitionOnce; // press while running = cut off
    bool bBackOnTrackRequest;

    Eigen::Vector4f Shaking;
    Eigen::Vector4f BikeVelocity;
    Matrix4x4f_t WorldBikeMatrix;
    Matrix4x4f_t BikeHelmetMatrix;

    Eigen::Vector4f PositionOnRoad;
    Eigen::Vector4f PrevPositionOnRoad;
    int32_t ActiveRoadID;
    float RoadX;
    float RoadZ;

    int32_t BackOnTrackState;
    float BackOnTrackTimer;
    Matrix4x4f_t BackOnTrackWorldMatrix;
    Eigen::Vector4f BackOnTrackVelocity;

    Eigen::Vector4f BodyDimensions;
    float WheelBase;
    float TrackWidth;
    float TrackLength;

    std::string Name;
    std::string NamePhysicsWorld;

    std::vector<CollisionEvent> CollisionEvents;

    void reset();
    void resetBackOnTrack();
};

class CarState : public GameObject {
public:
    enum class eOwner {
        CSO_LocalPlayer = 0,
        CSO_NetPlayer = 1,
        CSO_Traffic = 2,
        CSO_Bot = 3
    };

    enum class eSimulationType {
        CSST_ReplicatedFromNetwork = 0,
        CSST_Local = 1,
        CSST_None = 2 // Seems to be used for cutscenes. Could actually be some
                      // keyframed mode (or some cutscene specific mode)?
    };

    enum class eActiveMode {
        CSAM_Paused = 0,
        CSAM_Idle = 1,
        CSAM_Freeride = 2,
        CSAM_TimeAttack = 3,
        CSAM_InstantChallengeAsk = 4,
        CSAM_InstantChallengeRefuse = 5,
        CSAM_InstantChallengePreLobby = 6,
        CSAM_InstantChallenge = 7,
        CSAM_InstantChallengePostLobby = 8
    };

    using OnCollisionCallback_t =
        std::function<void( uint32_t, CarState *, DynamicRigidBody *, void * )>;

public:
    inline int32_t getCarID() const { return carID; }
    inline int32_t getRimID() const { return rimID; }

    inline CarBodyState *getBody() { return &body; }
    inline const CarBodyState *getBody() const { return &body; }
    inline const PoliceState &getPoliceState() const { return policeState; }
    inline VehicleBaseResources *getResource() const { return pResource; };

    inline bool isInstanceAlive() const { return bInstanceAlive; }

    inline uint32_t getNumWheels() const { return numWheels; }
    inline const WheelState &getWheel( uint32_t i ) const { return wheels[i]; }
    inline PhysicsVehicle *getPhysicsVehicle() const { return pPhysicsVehicle; }

    inline eOwner getOwner() const { return owner; }
    inline eSimulationType getSimulationType() const { return simulationType; }
    inline eActiveMode getActiveMode() const { return activeMode; }

    inline VehicleBase *getVehicleInstance() { return pVehicleBase; }
    inline bool hasAvatar() const { return pDriver != nullptr; }
    inline class EntityDriver *getDriver() const { return pDriver; }
    inline struct VehicleSoundInstance *getVehicleSound() const {
        return pVehicleSound;
    }
    inline void setVehicleSound( struct VehicleSoundInstance *pInstance ) {
        pVehicleSound = pInstance;
    }

    inline bool isInShowcaseMode() const { return bUseShowcaseControl; }

public:
    CarState();
    ~CarState();

    int32_t getType() override;
    Matrix4x4f_t &getWorldMatrix() override;
    Eigen::Vector4f getWorldPosition() override;
    void retrieveBoundingSphere( Eigen::Vector4f *pOutSpherePosition,
                                 float *pOutSphereRadius ) override;
    Eigen::Vector4f &getLinearVelocity() override;
    Eigen::Vector4f &getDimensions() override;
    void retrieveBoundingBox( Matrix4x4f_t &pOutBoundingBox ) override;
    void retrieveAcceleration( Eigen::Vector4f &param_1 ) override;

    void reset();
    void setInstanceID( const int32_t index );

    bool isIndoor() const;

    bool isReversing() const;
    bool isBraking() const;
    bool isIndicatingLeft() const;
    bool isIndicatingRight() const;
    bool headlightsOn() const;
    bool hazardLightsOn() const;

    void updateBackOnTrack( float param_2 );
    void resolveCollisionEvents();

    void updateGroundColor();
    void retrieveLights( DirectionalLight *param_1, DirectionalLight *param_2 );

    bool FUN_00406ce0();

    bool unloadDriver();

private:
    static constexpr const uint32_t kMaxNumWheels = 4;

    friend class GMCarShowcase;
    friend class GameModeWorld; // installs the local-player car on reset
    friend class CinResCollection; // spawns the PLAYER_CAR cinematic actor
    friend class BrandCar;      // installs the spot showcase ("brand") cars
    friend class GSScenaric;    // GSScenaric::placeDriver seats pDriver
    friend class GSCarState; // TODO: Remove once visibility for bInstanceAlive
                             // is fixed
    friend class VehicleCar;
    friend class VehicleBike;
    friend class VehicleTraffic;

private:
    std::string name;

    CarState::eOwner owner;
    CarState::eSimulationType simulationType;
    CarState::eActiveMode activeMode;

    PoliceState policeState;

    int32_t carInstanceID;
    uint32_t numWheels;
    CarBodyState body;
    std::array<WheelState, kMaxNumWheels> wheels;

    int32_t carID;
    int32_t tireID;
    int32_t rimID;
    int32_t colorID;

    VehicleBase *pVehicleBase;
    struct VehicleSoundInstance *pVehicleSound;
    class EntityDriver *pDriver;
    void *field_0x39cc;
    void *field_0x39d4;
    void *field_0x39d8;
    void *field_0x39e0;
    void *field_0x39e4;
    void *field_0x39ec;
    void *field_0x39fc;

    uint8_t bInstanceAlive : 1;
    uint8_t bIsIndoor : 1;
    uint8_t bUseShowcaseControl : 1;

    uint64_t packHashcode;
    VehicleBaseResources *pResource;
    PhysicsVehicle *pPhysicsVehicle;
};
