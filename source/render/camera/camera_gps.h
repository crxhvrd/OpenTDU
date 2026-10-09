#pragma once

#include "camera_game.h"
#include "core/mutex.h"

class FlashPlayer;

// GMMap Camera
class CameraGPS : public CameraGame {
public:
    enum eZoomLevel : int32_t {
        GZL_Fixed = 0,
        GZL_Space = 1,
        GZL_Island = 2,
        GZL_Far = 3,
        GZL_Medium = 4,
        GZL_Close = 5,

        GZL_Count
    };

    struct Pose {
        Eigen::Vector4f Position;
        Eigen::Vector4f Target;
        Eigen::Vector4f Up;
        float FOV;
        float AspectRatio;
        float NearPlane;
        float FarPlane;
    };

    using PoseCallback_t = void ( * )( void *pUser, Pose &pose );
    using TransitionEndCallback_t = void ( * )( void *pArg0, void *pArg1 );

public:
    inline int32_t getZoom() const { return zoom; }
    inline int32_t getPreviousZoom() const { return previousZoom; }
    inline bool isInTransition() const { return bInTransition; }
    inline float getSpaceViewAngle() const { return spaceViewAngle; }
    inline float getEarthRadius() const { return earthRadius; }
    inline float getTransitionProgress() const { return transitionProgress; }
    inline float getTransitionBlend() const { return transitionBlend; }
    inline const Eigen::Vector4f &getFocus() const { return focus; }
    inline float getZoomDistanceToCenter( const int32_t index ) const {
        return zoomDistanceToCenter[index];
    }
    inline float getSpaceViewDistanceToCenter() const {
        return spaceViewDistanceToCenter;
    }

public:
    CameraGPS();
    virtual ~CameraGPS();

    void initialize() override;
    bool tick( float deltaTime, bool param_1, bool param_2 ) override;
    void destroy() override;

    void reset( bool bResetZoom );
    void startZoom( int32_t fromZoom, int32_t toZoom );
    void setZoom( int32_t newZoom, bool bAnimated );
    bool zoomIn();
    bool zoomOut();
    void requestZoom( int32_t fromZoom,
                      int32_t toZoom,
                      TransitionEndCallback_t pCallback,
                      void *pArg0,
                      void *pArg1 );
    void setPoseCallback( PoseCallback_t pCallback, void *pUser );
    void setTransitionEndCallback( TransitionEndCallback_t pCallback,
                                   void *pArg0,
                                   void *pArg1 );
    void setFocus( const Eigen::Vector4f &position );
    void setSpaceViewAngle( float angle );
    void setFixedPose( const Eigen::Vector4f &position,
                       const Eigen::Vector4f &target,
                       const Eigen::Vector4f &up,
                       float nearPlane,
                       float farPlane,
                       float fov,
                       float aspectRatio );

    float getZoomFactor() const;
    const char *getZoomName() const;
    bool isSpaceView() const;
    float getTransitionDuration() const;
    float getDistanceToEarthCenter();
    float getAltitude();
    void updateFlashZoom( FlashPlayer *pPlayer ) const;
    void endTransition();

private:
    static float GetZoomTransitionDuration( int32_t fromZoom, int32_t toZoom );

private:
    void computeZoomPose( int32_t zoomLevel,
                          const Eigen::Vector4f &focus,
                          Pose &outPose ) const;
    void applyPose( const Pose &pose );
    void beginTransition( float duration );
    void updateTransition( float deltaTime );
    void computeTransitionPose( Pose &outPose ) const;
    void computeLinearTransitionPose( int32_t fromZoom,
                                      int32_t toZoom,
                                      float blend,
                                      Pose &outPose ) const;
    void computeOrbitalTransitionPose( int32_t fromZoom,
                                       int32_t toZoom,
                                       float blend,
                                       Pose &outPose ) const;

private:
    TestDriveMutex mutex;

    TransitionEndCallback_t pRequestedEndCallback;
    void *pRequestedEndArg0;
    void *pRequestedEndArg1;
    int32_t requestedFromZoom;
    int32_t requestedToZoom;

    Pose fixedPose;

    Eigen::Vector4f focus;
    Eigen::Vector4f previousFocus;

    int32_t zoom;
    int32_t previousZoom;
    float yawOffset;
    float pitchOffset;

    bool bInTransition;
    float transitionProgress;
    float transitionBlend;
    float transitionElapsed;
    float transitionDuration;

    float earthRadius;
    std::array<float, 4> zoomDistanceToCenter;
    float spaceViewDistanceToCenter;

    Matrix4x4f_t earthMatrix;
    Matrix4x4f_t invEarthMatrix;
    Eigen::Vector4f earthCenter;
    float spaceViewAngle;
    float spaceViewAltitude;

    TransitionEndCallback_t pEndCallback;
    void *pEndArg0;
    void *pEndArg1;

    PoseCallback_t pPoseCallback;
    void *pPoseCallbackUser;
};

extern bool gDisplayDemoZones;
