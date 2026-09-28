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
public:
    const char *getName() const override { return "Camera"; }
    inline void setDeadZone( const float value ) { deadZone = value; }

    inline void setNearPlane( const float value ) { nearPlane = value; }
    inline void setFarPlane( const float value ) { farPlane = value; }
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
    inline void setBoundCarState( CarState *pCarState ) {
        pTarget = pCarState;
    }

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
    inline void setUseBlur( const bool value ) { bUseBlur = value; }
    inline float getLeftView() const { return leftView; }
    inline float getRightView() const { return rightView; }
    inline float getUpView() const { return upView; }
    inline float getDownView() const { return downView; }
    inline bool isViewUpdated() const { return bUpdatedView; }
    // x = fore/aft, y = height (metres, in-car space).
    inline const Eigen::Vector2f &getSeatPosition() const { return seatPos; }
    inline bool isHeadTrackingEnabled() const { return bHeadTracking; }

    inline void setActiveCameraAsDefault() {
        defaultCamera = pActiveVehicleCamera;
        applyCinematicOverride( defaultCamera );
    }

    inline void setNextCamera( CameraGame *pCamera,
                               CameraGame *pTransition,
                               const float time ) {
        nextCameraMode = -1;
        pTransitionCamera = pTransition;
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
                        CameraGame *pCamera,
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

    void
    findAndSetCameraMode( int param_1, CameraGame *param_2, float param_3 );
    void bindCamera( CameraGame *param_1, CameraGame *param_2, float param_3 );

    void tickActiveCamera( float param_2 );
    bool updateActiveCamera( float deltaTime, float accTime );
    void bindToRender();

    void applyCinematicOverride( CameraGame &camera );
    void onWorldOriginStepped();
    void setCinematicCameraOverride( const Eigen::Vector3f &position,
                                     const Eigen::Vector3f &up,
                                     const Eigen::Vector3f &target,
                                     float fov );
    void clearCinematicCameraOverride();
#ifdef OTDU_USE_IMGUI
    void drawDebug( float deltaTime ) override;
    // Debug: frame the active camera on a world point (placed 'distance' back +
    // up, looking at the point). Best used with the free-fly camera enabled.
    void focusOnPoint( const Eigen::Vector4f &worldPos, float distance );
#endif
#ifdef OTDU_DEVBUILD
    void toggleFreeCam();
    inline bool isFreeCamEnabled() const { return bFlyCam; }
#endif

private:
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
    uint8_t bEditing : 1;
    uint8_t bPaused : 1;
    uint8_t bLookAtPlayerPos : 1;
    uint8_t bFinishedCamTransition : 1;
    uint8_t bMouseCam : 1;
    uint8_t field_0x939 : 1;
    uint8_t field_0x932 : 1;
    uint8_t bUseBlur : 1;
    uint8_t field_0x955 : 1;
    uint8_t bReplayMode : 1;
    uint8_t bViewBackOnTrigger : 1;
    uint8_t bViewBackOffTrigger : 1;
    uint8_t bChangeCamTrigger : 1;
    uint8_t bDisableInteriorView : 1;
    uint8_t bNeedSwitch : 1;
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
    CameraGame *pTransitionCamera;
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

    bool bFlyCam;

    // Cinematic camera override (debug live playback) — see bindToRender.
    bool bCinematicCameraOverride = false;
    Eigen::Vector3f cinematicCamPosition = Eigen::Vector3f::Zero();
    Eigen::Vector3f cinematicCamUp = Eigen::Vector3f::Zero();
    Eigen::Vector3f cinematicCamTarget = Eigen::Vector3f::Zero();
    float cinematicCamFov = 0.0f;

private:
    template <typename T>
    bool registerCamera( const char *pName, const int32_t type );

    void loadCameraDB( uint8_t *param_1 );
    void FUN_008e4840( CameraGame *param_1, float param_2 );
    bool FUN_008e1e80( bool param_1, bool param_3 );
    void editActiveCamera();
    void updateCameraInput( struct DeviceAction *peVar5,
                            float &input,
                            bool bNegate );
};

extern GSCamera *gpCamera;
extern float gCamViewLockHoldTime;
