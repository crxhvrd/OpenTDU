#pragma once

#include "core/cubic_bezier.h"
#include "core/response_curve.h"
#include "game/gs_base.h"
#include "game/scenaric/gs_scenaric.h"
#include "game/scenaric/mng_actor.h"

class AudioClient;
class AudioDispatcherPlaybackSoundInstance;
class edAudioSample;
class Entity;
class EntityAircraftPath;
class EntityCinematic;

struct AircraftLandingTrack {
    Eigen::Vector4f PointStart = Eigen::Vector4f::Zero();
    Eigen::Vector4f PointEnd = Eigen::Vector4f::Zero();
    float Length = 0.0f;
};

class AircraftPath {
public:
    static constexpr uint32_t kMaxPathPoints = 25;
    static constexpr uint32_t kMaxPathCurves = kMaxPathPoints + 1;
    static constexpr uint32_t kInvalidIndex = 0xffffffffu;

public:
    inline uint32_t getNumPathPoints() const { return NumPathPoints; }
    inline float getTotalLength() const { return TotalPathLength; }
    inline uint32_t getLandingEndIndex() const { return IndiceLandingEnd; }
    inline const AircraftLandingTrack &getLandingTrack() const {
        return LandingTrack;
    }
    inline const ResponseCurve<10> &getSpeedCurve() const { return SpeedCurve; }
    inline uint32_t getCineStartIndex() const { return IndiceCineStart; }
    inline void setCineStartIndex( uint32_t index ) { IndiceCineStart = index; }
    inline void setCineStopIndex( uint32_t index ) { IndiceCineStop = index; }

public:
    void addPathPoint( const Eigen::Vector4f &position );
    void setLandingTrack( const Eigen::Vector4f &start,
                          const Eigen::Vector4f &end,
                          uint32_t endIndex );

    void buildCurves();
    void buildSpeedCurve( float speedOnGround, float speedOnAir );

    bool evaluate( float u,
                   Eigen::Vector4f &outPosition,
                   Eigen::Vector4f &outTangent ) const;
    float getCurveU( uint32_t index ) const;

protected:
    Eigen::Vector4f pPathPoints[kMaxPathPoints] = {};
    uint32_t NumPathPoints = 0;

    CubicBezier pPathCurves[kMaxPathCurves] = {};
    float pPathCurvesLength[kMaxPathCurves] = {};
    float pPathCurvesU[kMaxPathCurves + 1] = {};
    uint32_t NumPathCurves = 0;

    AircraftLandingTrack LandingTrack;
    float TotalPathLength = 0.0f;

    ResponseCurve<10> SpeedCurve;
    ResponseCurve<10> AngleXCurve;

    uint32_t IndiceCineStart = kInvalidIndex;
    uint32_t IndiceCineStop = kInvalidIndex;
    uint32_t IndiceLandingEnd = kInvalidIndex;
};

class AircraftPathFlights;

// One aircraft travelling along its own (jittered) copy of a path.
class Aircraft : public AircraftPath, public CinematicPlayer {
public:
    inline Entity *getInstance() const { return pInstance; }
    inline float getCurrentU() const { return CurrentU; }
    inline float getCurrentSpeed() const { return CurrentSpeed; }
    inline bool isOnLandingTrack() const { return State == 1; }

public:
    Aircraft();
    virtual ~Aircraft();

    void spawn( Entity *pAircraftInstance,
                AudioDispatcherPlaybackSoundInstance *pSound,
                const edAudioSample *pSoundOnGround,
                const edAudioSample *pSoundInAir,
                float startU );
    void seedSpeed();
    void tick( AircraftPathFlights &owner, float deltaTime );
    void terminateSound();

    bool executeAction( int32_t event, void *pParameters ) override;

private:
    void updateSound( bool bAtListener );
    float computeTurnAngle( const Eigen::Vector4f &tangent,
                            float lookAheadU,
                            float divisor ) const;
    void updateLandingState( float landingEndU );
    void updateCinematic( AircraftPathFlights &owner,
                          const Matrix4x4f_t &aircraftMatrix,
                          float landingEndU );

private:
    float CurrentU;
    Entity *pInstance;
    float CurrentSpeed;
    float CurrentRoll;

    uint8_t bCineWindowLatched : 1;
    uint8_t bCineArmed : 1;
    uint8_t bCinePauseRequested : 1;
    uint8_t bSoundDirty : 1;
    uint8_t bCineDriven : 1;
    uint8_t bCinePauseCounted : 1;

    AudioDispatcherPlaybackSoundInstance *pSoundInstance;
    const edAudioSample *pSoundInAir;
    const edAudioSample *pSoundOnGround;

    // 1 while the aircraft is on the landing track, 0 while airborne
    int32_t State;
};

// All the aircrafts sharing one ET_ACPA path entity.
class AircraftPathFlights {
public:
    static constexpr uint32_t kMaxAircraftsPerPath = 20;

public:
    inline uint32_t getNumAircrafts() const {
        return static_cast<uint32_t>( flights.size() );
    }
    Entity *getAircraft( uint32_t index ) const;
    const Aircraft *getFlight( uint32_t index ) const;
    inline EntityCinematic *getCinematic() const { return pCinematic; }

    inline void requestCinematicPause() { PauseCineRefCount++; }
    inline void releaseCinematicPause() { PauseCineRefCount--; }

    inline const AircraftPath &getOriginalPath() const { return originalPath; }
    inline EntityAircraftPath *getPathEntity() const { return pPathEntity; }

public:
    void initialize( EntityAircraftPath *pEntity );
    void spawnAircrafts( MngActor &mngActor, AudioClient *pAudioClient );
    void tick( float deltaTime );
    void terminate();

private:
    void buildFlightPath( Aircraft &aircraft );

private:
    EntityCinematic *pCinematic = nullptr;
    int32_t PauseCineRefCount = 0;
    bool bLaunchCine = false;

    AircraftPath originalPath;
    EntityAircraftPath *pPathEntity = nullptr;
    std::vector<std::unique_ptr<Aircraft>> flights;
};

class GSAircraft : public GameSystem {
public:
    const char *getName() const override { /* FUN_0040cd90 */
        return "Service : Aircraft";
    }

public:
    static constexpr uint32_t kMaxOriginalPaths = 10;
    static constexpr uint32_t kNumAircraftGroups = 2; // AET_Count
    static constexpr uint32_t kMaxAircraftModelsPerGroup = 10;

public:
    GSAircraft();
    ~GSAircraft();

    bool initialize( TestDriveGameInstance * ) override;
    void tick( float totalTime, float deltaTime ) override;
    void draw( float deltaTime ) override;
    void terminate() override;

    void spawnAircrafts();

    Entity *pickAircraftModel( int32_t groupIndex );
    uint32_t getNumAircrafts() const;
    Entity *getAircraft( uint32_t index ) const;
    const Aircraft *getFlight( uint32_t index ) const;

#ifdef OTDU_USE_IMGUI
    void drawDebug( float deltaTime ) override;
#endif

private:
    void buildAircraftModelGroups();

private:
    struct AircraftModelGroup {
        std::array<Entity *, kMaxAircraftModelsPerGroup> pModels = {};
        uint32_t numModels = 0;
    };

private:
    MngActor mngActor;
    std::vector<std::unique_ptr<AircraftPathFlights>> originalPaths;
    std::array<AircraftModelGroup, kNumAircraftGroups> aircraftModelGroups;

    AudioClient *pAudioClient;
    bool bDrawAircrafts;
    bool bAircraftsSpawned;
};

extern GSAircraft *gpAircraft;
