#pragma once

#include "ai/ai_path.h"
#include "ai/ai_road_iterator.h"
#include "core/cubic_bezier.h"

class EntityCheckPoint;
class GMLobby;

// Scripted camera driven by the GMLobby state machine (retail objects with a
// hand-made {init, update, term} vtable embedded in GMLobby).
class LobbyCamera {
public:
    explicit LobbyCamera( GMLobby *pOwner )
        : bEnabled( true ), pOwner( pOwner ) {}
    virtual ~LobbyCamera() {}

    virtual bool init() = 0;
    virtual void update( float deltaTime ) = 0;
    virtual void term() = 0;

protected:
    bool bEnabled;
    GMLobby *pOwner;
};

// Fly-through of the event route (time attack and race events).
class LobbyCameraRoutePreview : public LobbyCamera {
public:
    explicit LobbyCameraRoutePreview( GMLobby *pOwner );

    bool init() override;
    void update( float deltaTime ) override;
    void term() override;

private:
    enum eTransitionState : int32_t {
        TS_OnRoad = 0,
        TS_Junction = 1,
        TS_UTurn = 2,
    };

private:
    void restart();
    void setRoadPosition( const AiRoadIt &it );
    void updateCruiseSpeed( const float deltaTime );
    bool updateCamera( const float deltaTime );
    void advance( const float deltaTime );
    void pickNextRoad( const float step );
    void applyUserControl( Eigen::Vector4f &target, const float deltaTime );
    void checkRouteReversal();

    EntityCheckPoint *swapCheckpointTemplate( EntityCheckPoint *pCheckpoint,
                                              const uint32_t ledType );

private:
    AiPath path;
    AiRouteElem *pCurrentElem;
    eTransitionState transitionState;
    AiRoadIt roadIt;
    float progress;
    float throttle;
    float speed;
    int32_t transitionCurveType;
    CubicBezier transitionCurve;
    float cameraDistance;
    float cameraPitch;
    Eigen::Vector4f roadPosition;
    Eigen::Vector4f cameraPosition;
    Eigen::Vector4f previousCameraPosition;
    Eigen::Vector4f roadDirection;
    Eigen::Vector4f roadSide;
    bool bWaitingForStreaming;
    float startDelay;
    bool bLoop;
    int32_t nextCheckpoint;
    bool bRunning;
    bool bUserControl;
    float userControlTime;
    float yawOffset;
    float heightOffset;
};

// Slow orbit around the start line (speed trap and driving events).
class LobbyCameraStartOrbit : public LobbyCamera {
public:
    explicit LobbyCameraStartOrbit( GMLobby *pOwner );

    bool init() override;
    void update( float deltaTime ) override;
    void term() override;

private:
    Eigen::Vector4f position;
    float angle;
    bool bWaitingForStreaming;
    bool bWaitingForFadeIn;
    uint32_t streamedTime;
};

// Hops from checkpoint to checkpoint (speed trap events with more than one
// radar).
class LobbyCameraCheckpointFlyby : public LobbyCamera {
public:
    explicit LobbyCameraCheckpointFlyby( GMLobby *pOwner );

    bool init() override;
    void update( float deltaTime ) override;
    void term() override;

private:
    enum eFlybyState : int32_t {
        FS_BuildSpline = 0,
        FS_WaitForStreaming = 1,
        FS_Flying = 2,
        FS_WaitForFade = 3,
    };

private:
    static void OnFadedOut( void *pData, void *pSource );

    void advance( const float deltaTime );

private:
    Eigen::Vector4f targetPosition;
    Eigen::Vector4f cameraPosition;
    Eigen::Vector4f roadDirection;
    Eigen::Vector4f roadSide;
    CubicBezier spline;
    int32_t splineType;
    float splineTime;
    eFlybyState state;
    int32_t nextCheckpoint;
};

// Orbit around the start grid while the players are listed.
class LobbyCameraPlayersView : public LobbyCamera {
public:
    explicit LobbyCameraPlayersView( GMLobby *pOwner );

    bool init() override;
    void update( float deltaTime ) override;
    void term() override;

    inline const Eigen::Vector4f &getTarget() const { return target; }
    inline const Eigen::Vector4f &getSpawnPosition() const {
        return spawnPosition;
    }
    inline void restoreTarget() { target = spawnPosition; }

private:
    float angle;
    Eigen::Vector4f target;
    Eigen::Vector4f spawnPosition;
    Matrix4x4f_t startFrame;
    float fovCoef;        // x PI
    float targetDistance; // along the start line
    float orbitPeriod;    // seconds per half turn
    float orbitRadius;
    float orbitOffsetY;
    float orbitOffsetZ;
    float orbitOffsetX;
};
