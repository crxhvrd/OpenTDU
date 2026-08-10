#pragma once

#include "game/gs_base.h"
#include "game/scenaric/mng_actor.h"

class AudioClient;
class AudioDispatcherPlaybackSoundInstance;
class Entity;
class EntityBoatPath;

struct BoatPathEntry {
    enum ePathEntryType : int32_t {
        PET_Curve = 0,    // Bezier segment between two markers
        PET_Waypoint = 1, // absolute world coords point
        PET_Wait = 2,     // stop and idle for getWaitDuration()
        PET_Reverse = 3,  // turn back: the travel direction is negated
    };

    int32_t Type = PET_Curve;
    float Height = 0.0f;
    float CurvePointsX[4] = {};
    float CurvePointsZ[4] = {};

    inline Eigen::Vector4f getMarkerPosition() const {
        return Eigen::Vector4f( CurvePointsX[0], Height, CurvePointsX[1],
                                1.0f );
    }
    inline void setMarkerPosition( const Eigen::Vector4f &position ) {
        CurvePointsX[0] = position.x();
        CurvePointsX[1] = position.z();
        Height = position.y();
    }
    inline float getWaitDuration() const { return CurvePointsX[2]; }
};

class BoatPathInstance {
public:
    static constexpr uint32_t kMaxPathElements = 40;

public:
    inline uint32_t getNumElements() const { return NumPathElements; }
    inline float getTotalLength() const { return TotalPathLength; }
    inline void setSpeed( float param_1 ) { Speed = param_1; }

public:
    void addWaypoint( const Eigen::Vector4f &position,
                      uint32_t mode,
                      float waitDuration );
    void build( const BoatPathInstance *pSource, bool bLooping );
    void update( float deltaTime );
    void evaluate( Eigen::Vector4f &outPosition,
                   Eigen::Vector4f &outTangent ) const;
    void seek( float startU );

private:
    float findElementForU( float u, uint32_t &outIndex ) const;

    Eigen::Vector4f getMarkerPosition( uint32_t markerIndex ) const;
    int32_t getMarkerType( uint32_t markerIndex ) const;

    static void solveCurve( BoatPathEntry &curve,
                            const Eigen::Vector4f &from,
                            const Eigen::Vector4f &to,
                            const Eigen::Vector4f &inTangent,
                            const Eigen::Vector4f &outTangent );

private:
    std::array<BoatPathEntry *, kMaxPathElements> pPathElements = {};
    uint32_t NumPathElements = 0;
    float TotalPathLength = 0.0f;
    float PathElemensU[kMaxPathElements + 2] = {};
    float CurrentU = 0.0f;
    uint32_t CurrentPathElementID = 0;
    float TimeInThisState = 0.0f;
    float Speed = 0.0f;
    float LastSpeed = 0.0f;
};

struct BoatState {
    BoatPathInstance Path;
    Entity *pBoatInstance = nullptr;
};

class BoatPathTemplate {
public:
    static constexpr uint32_t kMaxBoatsPerPath = 20;

public:
    inline uint32_t getNumBoats() const {
        return static_cast<uint32_t>( travels.size() );
    }
    Entity *getBoat( uint32_t index ) const;

public:
    void initialize( EntityBoatPath *pEntity );
    void spawnBoats( MngActor &mngActor );
    void tick( float deltaTime );
    void refreshHullRenderMode( bool bMapView );

private:
    BoatPathInstance path;
    EntityBoatPath *pPathEntity = nullptr;
    std::vector<std::unique_ptr<BoatState>> travels;
};

class GSBoat : public GameSystem {
public:
    const char *getName() const override { return "Service : Boat"; }

    static constexpr uint32_t kMaxOriginalPaths = 10;
    static constexpr uint32_t kPathElementPoolSize = 1000;
    static constexpr uint32_t kNumBoatGroups = 3;
    static constexpr uint32_t kMaxBoatModelsPerGroup = 10;

public:
    GSBoat();
    ~GSBoat();

    bool initialize( TestDriveGameInstance * ) override;
    void tick( float totalTime, float deltaTime ) override;
    void draw( float deltaTime ) override;
    void terminate() override;

    void spawnBoats();

    BoatPathEntry *allocatePathElement();
    Entity *pickBoatModel( int32_t groupIndex );
    void setEcumeEnabled( bool bEnabled );
    uint32_t getNumBoats() const;
    Entity *getBoat( uint32_t index ) const;

#ifdef OTDU_USE_IMGUI
    void drawDebug( float deltaTime ) override;
#endif

private:
    void buildBoatModelGroups();

private:
    struct BoatModelGroup {
        std::array<Entity *, kMaxBoatModelsPerGroup> pModels = {};
        uint32_t numModels = 0;
    };

private:
    MngActor mngActor;
    std::unique_ptr<BoatPathEntry[]> pathElementPool;
    uint32_t numUsedPathElements;
    std::vector<std::unique_ptr<BoatPathTemplate>> originalPaths;
    std::array<BoatModelGroup, kNumBoatGroups> boatModelGroups;

    AudioClient *pAudioClient;
    AudioDispatcherPlaybackSoundInstance *pHornSoundInstance;
    Entity *pHornBoat;

    float timeSinceLastHorn;
    float hornInterval;
    bool bBoatsSpawned;
};

extern GSBoat *gpBoat;
