#pragma once

#include "camera_transition.h"
#include "game/gs_base.h"
#include "render/frustum.h"

#include "camera_vehicle.h"
#include "camera_transition.h"

class CameraGame;
class CarState;
class PhysicsVehicle;
class CameraVehicle;
class CameraMouse;

// Runtime vehicle-camera entry. The on-disk camera DB stores 0xc-byte entries
// (CarIndex, NumCameras, 4-byte pointer placeholder), but the camera pointers
// are allocated natively and separately at load (see loadCameraDB) instead of
// being overlaid on the binary's 4-byte slots — so this is a native runtime
// struct, no longer x86Pointer-based.
struct CameraDBVehicleEntry {
    int32_t CarIndex = 0;
    int32_t NumCameras = 0;
    CameraGame **pCameras =
        nullptr; // native array of NumCameras camera pointers
};

static constexpr int32_t kNumCamerasCameraRegister = 0x10;

class GSCamera : public GameSystem {
public:
    inline void setViewChangeAllowed( const bool bAllowed ) {
        bViewChangeAllowed = bAllowed;
    }
public:
    const char *getName() const override { return "Camera"; }
    inline void setDeadZone( const float value ) { deadZone = value; }
    inline float getDeadZone() const { return deadZone; }

    inline void setNearPlane( const float value ) { nearPlane = value; }
    inline void setFarPlane( const float value ) { farPlane = value; }
    inline float getNearPlane() const { return nearPlane; }
    inline float getFarPlane() const { return farPlane; }
    inline bool isDepthAware() const { return bDepthAware; }
    inline void setInteriorNearPlane( const float value ) {
        nearPlaneInterior = value;
    }
    inline void setInteriorFarPlane( const float value ) {
        farPlaneInterior = value;
    }

    inline void setEnableDepthAware( const bool value ) { bDepthAware = value; }
    inline void setEnableDepthAwareInterior( const bool value ) {
        bDepthAwareInterior = value;
    }

    inline PhysicsVehicle *getTargetPhysEntity() { return pTargetPhysEntity; }
    inline void setTargetPhysEntity( PhysicsVehicle *pPhysEntity ) {
        pTargetPhysEntity = pPhysEntity;
    }
    inline CarState *getBoundCarState() const { return pTarget; }
    inline void setBoundCarState( CarState *pCarState ) { pTarget = pCarState; }

    inline CameraGame *getActiveCamera() { return pActiveVehicleCamera; }
    inline CameraGame *getPreviousCamera() { return pPrevCamera; }
    inline CameraVehicle *getActiveVehicleCamera() {
        return pActiveVehicleCamera;
    }
    inline CameraGame *getDefaultCamera() { return &defaultCamera; }
    inline CameraGame *getRenderCamera() { return &activeCamera; }
    inline Frustum &getNearFrustum() { return frustumNear; }

    inline bool isLocked() { return bLocked; }
    inline bool isInReplayMode() const { return bReplayMode; }
    inline bool isPaused() const { return bPaused; }
    inline bool isLookingBack() const { return bLookingBack; }
    inline bool isInteriorView() const { return bInterior; }
    inline void setInteriorView( const bool value ) { bInterior = value; }
    inline void setMouseCam( const bool value ) { bMouseCam = value; }
    inline void setForcePlayerCamCockpit( const bool value ) {
        bForcePlayerCamCockpit = value;
    }
    inline void setUseBlur( const bool value ) { bUseBlur = value; }
    inline float getCameraShake() const { return cameraShake; }
    inline float getRadialBlurIntensity() const { return radialBlurIntensity; }
    inline void setDrivingOptions( const float shake, const float blur ) {
        cameraShake = shake;
        radialBlurIntensity = blur;
    }
    inline float getLeftView() const { return leftView; }
    inline float getRightView() const { return rightView; }
    inline float getUpView() const { return upView; }
    inline float getDownView() const { return downView; }
    inline bool isViewUpdated() const { return bUpdatedView; }
    // x = fore/aft, y = height (metres, in-car space).
    inline const Eigen::Vector2f &getSeatPosition() const { return seatPos; }
    inline bool isHeadTrackingEnabled() const { return bHeadTracking; }
    inline void setHeadTracking( const bool value ) { bHeadTracking = value; }
    inline void setMouseSensitivity( const float value ) {
        field_0x958 = value;
    }
    inline void setInvertMouse( const bool value ) { field_0x955 = value; }

    // Freezes the vehicle camera selection while a scripted (front end) view
    // drives the active vehicle camera.
    inline void lockScriptedView() {
        timer = 99.0f; // DAT_00f417e8
        bMouseCam = false;
        bReplayMode = false;
        bDisableInteriorView = true;
        bNeedSwitch = true;
        activeModeVehicle = -1;
        activeMode = -1;
        timerToSwitch = 0.0f;
        activeModeOverride = -1;
        activeModeVehicleOverride = -1;
    }

    inline void resetVehicleCameraSelection() {
        timer = 99.0f; // DAT_00f417e8
        bReplayMode = false;
        bDisableInteriorView = false;
        bNeedSwitch = true;
        activeModeVehicle = -1;
        activeMode = -1;
        timerToSwitch = 0.0f;
        activeModeOverride = -1;
        activeModeVehicleOverride = -1;
    }

    inline void setActiveCameraAsDefault() {
        defaultCamera = pActiveVehicleCamera;
        applyCinematicOverride( defaultCamera );
    }

    inline void requestCameraMode( const int32_t mode, const float time ) {
        nextCameraMode = mode;
        pNextCamera = nullptr;
        transitionTime = time;
    }

    inline void requestCameraMode( const int32_t mode,
                                   const int32_t transition,
                                   const float time ) {
        nextCameraMode = mode;
        pNextCamera = nullptr;
        transitionType = transition;
        transitionTime = time;
    }

    inline void setReplayMode( const bool bReplay ) {
        FUN_008e1e80( false, bReplay );
    }

    inline void forceReplayCameraSwitch() { bForceSwitch = true; }

    inline int32_t getActiveModeOverride() const { return activeModeOverride; }
    inline int32_t getActiveModeVehicleOverride() const {
        return activeModeVehicleOverride;
    }

    inline void setNextCamera( CameraGame *pCamera,
                               const int32_t transition,
                               const float time ) {
        nextCameraMode = -1;
        transitionType = transition;
        transitionTime = time;
        pNextCamera = pCamera;
    }

    inline bool isRegisteredCamera( const CameraGame *pCamera ) const {
        for ( uint32_t i = 0; i < numCameraRegistered; i++ ) {
            if ( cameraRegister[i] == pCamera ) {
                return true;
            }
        }
        return false;
    }

    inline bool isInTransition() const {
        // TODO: Most likely incorrect
        return (uintptr_t)pActiveVehicleCamera == (uintptr_t)&transition;
    }

    inline int32_t getActiveTransitionIndex() const {
        return ( field83_0x92c < numTransition &&
                 pTransitionIndices != nullptr )
                   ? pTransitionIndices[field83_0x92c]
                   : -1;
    }

    inline void setActiveTransitionIndex( const int32_t transitionIndex ) {
        // FUN_008e1a30
        for ( uint32_t i = 0; i < numTransition; i++ ) {
            if ( pTransitionIndices[i] == transitionIndex ) {
                field83_0x92c = i;
            }
        }
    }

public:
    GSCamera();
    ~GSCamera();

    bool initialize( TestDriveGameInstance * ) override;
    void tick( float totalTime, float deltaTime ) override;
    void terminate() override;
    void pause() override;
    void reset() override;

    void setSeatPosition( float seatHeight, float seatPosition );

    void setCameraMode( const int32_t mode,
                        const int32_t transition,
                        const float transitionTime );

    CameraGame *findCameraWithMode( const int32_t mode,
                                    CarState *pState = nullptr );
    CameraGame *findCamera( CarState *pState );
    CameraGame *findCameraByCurrentTransition();
    int32_t findFirstVisibleTransitionMode();
    int32_t findFirstHoodTransitionMode();
    int32_t findFirstSeatAdjustableTransitionMode();
    void setTransitionCameraMode( const int32_t mode );
    void restoreDefaultVehicleCamera( CarState *pCar );
    void restoreCamera();

    CameraMouse *getMouseCamera();

    void findAndSetCameraMode( int param_1, int32_t transition, float param_3 );
    void bindCamera( CameraGame *param_1, int32_t transition, float param_3 );

    void tickActiveCamera( float param_2 );
    bool cycleReplayModeOverride();
    void lockReplayModeOverride( const bool bLock );
    bool updateActiveCamera( float deltaTime, float accTime );
    void bindToRender();

    void applyCinematicOverride( CameraGame &camera );
    void onWorldOriginStepped();

    void applyRenderInterpolation( float alpha );
    void setCinematicCameraOverride( const Eigen::Vector3f &position,
                                     const Eigen::Vector3f &up,
                                     const Eigen::Vector3f &target,
                                     float fov );
    void clearCinematicCameraOverride();
#ifdef OTDU_USE_IMGUI
    void drawDebug( float deltaTime ) override;
    void focusOnPoint( const Eigen::Vector4f &worldPos, float distance );
    void forceDebugCamera( CameraGame *pCamera );
    void restoreDebugCamera();
#endif
#ifdef OTDU_DEVBUILD
    void toggleFreeCam();
    inline bool isFreeCamEnabled() const { return bFlyCam; }
#endif

private:
    friend class CameraTransition;

    float nearPlane;
    float farPlane;

    float nearPlaneInterior;
    float farPlaneInterior;

    float deadZone;
    float unknownFloat;

    float leftView;
    float rightView;
    float downView;
    float upView;

    uint8_t bDepthAware : 1;
    uint8_t bDepthAwareInterior : 1;
    uint8_t bLookingBack : 1;
    uint8_t bInterior : 1;
    uint8_t bUnknownBool3 : 1;
    uint8_t bLocked : 1;
    float cameraShake;         // +0x960
    float radialBlurIntensity; // +0x964
    uint8_t bEditing : 1;
    uint8_t bPaused : 1;
    uint8_t bLookAtPlayerPos : 1;
    uint8_t bFinishedCamTransition : 1;
    uint8_t bMouseCam : 1;
    uint8_t bViewChangeAllowed : 1; // +0x939
    uint8_t field_0x932 : 1;
    uint8_t bUseBlur : 1;
    uint8_t field_0x955 : 1;
    uint8_t bReplayMode : 1;
    uint8_t bViewBackOnTrigger : 1;
    uint8_t bViewBackOffTrigger : 1;
    uint8_t bChangeCamTrigger : 1;
    uint8_t bDisableInteriorView : 1;
    uint8_t bNeedSwitch : 1;
    uint8_t bForceSwitch : 1;
    uint8_t bUpdatedView : 1;
    uint8_t field_0x954 : 1;
    uint8_t bHeadTracking : 1;

    CameraGame *cameraRegister[kNumCamerasCameraRegister];
    uint32_t currentCameraIndex;
    uint32_t numCameraRegistered;

    CameraGame defaultCamera;
    CameraGame activeCamera;
    CameraGame tmpCamera;

    Frustum frustumNear;
    Frustum frustumFar;

    int32_t nextCameraMode;
    CameraGame *pPrevCamera;
    CameraGame *pNextCamera;
    CameraVehicle *pActiveVehicleCamera;
    int32_t transitionType;
    float transitionTime;

    CameraTransition transition;
    CarState *pTarget;
    uint32_t currentTarget;
    uint32_t currentTransitionIndex;
    uint32_t numTransition;
    uint32_t numVehicleEntries;
    uint32_t numVehicleCamera;

    PhysicsVehicle *pTargetPhysEntity;

    Eigen::Vector2f angleX;
    Eigen::Vector2f seatPos;
    Eigen::Vector2f seatMinPos;
    Eigen::Vector2f seatMaxPos;

    void *pCameraDBBinary;
    int32_t *pTransitionIndices;
    CameraDBVehicleEntry *pCameraVehicleEntries;
    void *pVehicleMem;
    CameraVehicle *pCameraVehicles;
    // Native pool backing the per-vehicle CameraDBVehicleEntry::pCameras arrays
    // (replaces the x86Pointer arrays the original overlaid on the DB binary).
    CameraGame **pVehicleCameraPtrs;

    int32_t field_0x96c;
    uint32_t field83_0x92c;
    int32_t field82_0x928;
    float field_0x958;
    int32_t carID;

    float timer;
    float timerToSwitch;
    int32_t activeMode;
    int32_t activeModeVehicle;
    int32_t activeModeOverride;
    int32_t activeModeVehicleOverride;
    bool bForcePlayerCamCockpit = false; // +0x5d0

    bool bFlyCam;

    CameraGame *pPreDebugCamera = nullptr;
    Eigen::Vector4f *pPreDebugStreamPos = nullptr;

    // Cinematic camera override (debug live playback) — see bindToRender.
    bool bCinematicCameraOverride = false;
    Eigen::Vector3f cinematicCamPosition = Eigen::Vector3f::Zero();
    Eigen::Vector3f cinematicCamUp = Eigen::Vector3f::Zero();
    Eigen::Vector3f cinematicCamTarget = Eigen::Vector3f::Zero();
    float cinematicCamFov = 0.0f;

    struct RenderInterpolationPose {
        Eigen::Vector3f Position = Eigen::Vector3f::Zero();
        Eigen::Vector3f Target = Eigen::Vector3f::Zero();
        Eigen::Vector3f Up = Eigen::Vector3f::UnitY();
    };
    RenderInterpolationPose interpolationPoses[2];
    const CameraVehicle *pInterpolationCamera = nullptr;
    bool bInterpolationValid = false;

    void latchRenderInterpolation( bool bSnap );

private:
    template <typename T>
    bool registerCamera( const char *pName, const int32_t type );

    void loadCameraDB( uint8_t *param_1 );
    void FUN_008e4840( int32_t transition, float param_2 );
    void updateReplayCameraSwitch( const float deltaTime );
    bool FUN_008e1e80( bool param_1, bool param_3 );
    void editActiveCamera();
    void updateCameraInput( struct DeviceAction *peVar5,
                            float &input,
                            bool bNegate );
};

extern GSCamera *gpCamera;
extern float gCamViewLockHoldTime;
static constexpr float kCamViewLockHoldThreshold = 1.0f; // DAT_00f87fe4
