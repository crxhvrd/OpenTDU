#pragma once

#include "camera_game.h"

class Path;
struct TCBKeyFrame;
struct TCBSplinePosition;
struct TCBSplineRotation;

struct CameraTransitionPath {
    uint8_t bInvert : 1;
    uint8_t bDoCameraChange : 1;
    TCBKeyFrame *pKeyFrames;
    TCBKeyFrame *pKeyFramesTarget;
    int32_t NumKeyFrames;
    int32_t NumKeyFramesTarget;
    uint8_t bSwapYZ : 1;
    Path *pPath;
};

class CameraTransitionInterpolator {
public:
    virtual void initialize( class CameraTransition *pTransition ) = 0;
    virtual float ease( float t ) = 0;
    virtual void apply( class CameraTransition *pTransition, float t ) = 0;
};

enum eCameraTransitionType {
    CTT_Default = 0,
    CTT_Lerp = 1,
    CTT_LerpInterpol = 2,
    CTT_Zoom = 3,
    CTT_Path = 4,
    CTT_LerpRot = 5,
    CTT_LerpRotInterpol = 6,
    CTT_EaseIn = 7,
    CTT_EaseOut = 8,
    CTT_Custom = 9
};

using TransitionCallback_t = void ( * )( void *, int32_t );

class CameraTransition : public CameraGame {
public:
    CameraTransition();
    ~CameraTransition();

    void initialize() override;
    bool tick( float deltaTime, bool param_1, bool param_2 ) override;
    void destroy() override;

    void begin( float transitionTime, int32_t transitionType );
    void setCameras( CameraGame *pSrc, CameraGame *pDst );

private:
    float ease( float t );
    void applyInterpolation( float t );
    void loadZoomKeys();
    void loadPathKeys();

protected:
    CameraGame *pSource;
    CameraGame *pDest;

    CameraTransitionPath transitionPath;

    int32_t cameraTransition;
    float time;
    float interpolationFactor;

    TCBSplinePosition *pOrigin;
    TCBSplinePosition *pLookTarget;
    TCBSplineRotation *pRotation;

    TransitionCallback_t pCompletionCallback;
    void *pCallbackArg0;
    int32_t callbackArg1;

    CameraTransitionInterpolator *pInterpolator;

    uint8_t bPaused : 1;
};

void SnapCameraToPathKey( const Path *pPath,
                          const int32_t keyIndex,
                          CameraGame *pCamera );
