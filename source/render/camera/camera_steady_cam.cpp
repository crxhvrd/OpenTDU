#include "shared.h"
#include "camera_steady_cam.h"

#include "config/gs_config.h"
#include "core/maths_helper.h"
#include "flash/gs_flash.h"
#include "flash/movie_player.h"
#include "game/vehicle/car_state.h"
#include "game/vehicle/gs_car_state.h"
#include "input/ed_dev.h"
#include "input/input_device.h"
#include "physics/gs_physics.h"
#include "physics/vehicle/physics_vehicle.h"
#include "render/camera/gs_camera.h"
#include "system/ed_sys.h"
#include "world/gs_world.h"
#include "world/mng_world.h"
#include "world/streaming_manager.h"

static constexpr float kDefaultDistance = 4.0f;        // DAT_00f418d4
static constexpr float kDefaultSensitivity = 0.1f;     // FLOAT_00f41668
static constexpr float kDefaultMouseFov = 60.0f;       // FLOAT_00f41744
static constexpr float kDefaultAutoTiltFactor = 90.0f; // FLOAT_00f4180c

static constexpr float kAngleXFromMatrix = 0.63661975f; // DAT_00f41a30

static constexpr float kTargetDistance = 10.0f;

static constexpr float kMouseFovMin = 0.01f; // DAT_00f416d4
static constexpr float kMouseFovMax = 85.0f; // DAT_00f41f28

static constexpr float kFarPlane = 40000.0f; // DAT_00f41c78

static constexpr float kNearPlaneRefFov = 60.0f; // DAT_00fab404
static constexpr float kNearPlaneLowFov = 45.0f; // DAT_00fab400
static constexpr float kNearPlaneSpan = 0.9f;    // DAT_00f416a0
static constexpr float kNearPlaneBase = 0.1f;    // FLOAT_00f41668

static constexpr int32_t kMoviePauseBit = 0x400;
static constexpr float kFallbackDeltaTime = 0.011111111f; // DAT_00f41c04
static constexpr float kToggleHoldOff = 0.5f;             // FLOAT_00f41674
static constexpr float kPadDeadZone = 0.2f;               // DAT_00f41730

static constexpr float kActionPitchScale = 10.0f; // DAT_00fab474
static constexpr float kActionYawScale = 0.2f;    // DAT_00fab478

static constexpr float kStickPitchScale = 5.0f; // DAT_00fab47c
static constexpr float kStickYawScale = 0.2f;   // DAT_00fab480

static constexpr float kWalkFactor = 1.15f; // DAT_00f416a8
static constexpr float kRunFactor = 2.0f;   // DAT_00f416b0

static constexpr float kMoveRetain = 0.95f;          // DAT_00fab470
static constexpr float kLookRetain = 0.95f;          // DAT_00fab46c
static constexpr float kZoomRetain = 0.975f;         // DAT_00fab468
static constexpr float kPositionRetain = 0.9f;       // DAT_00fab428
static constexpr float kRollRetain = 0.975f;         // DAT_00fab41c
static constexpr float kFollowRetain = 0.9f;         // DAT_00fab420
static constexpr float kFollowFovRetain = 0.95f;     // DAT_00fab408
static constexpr float kSmoothingScale = 1.1111112f; // DAT_00f41ddc

static constexpr float kSensitivityFpsTarget = 0.5f;   // DAT_00fab460
static constexpr float kSensitivityWheelStep = 0.025f; // DAT_00fab45c
static constexpr float kSensitivityMax = 100.0f;       // DAT_00f41708
static constexpr float kSensitivityMin = 0.01f;        // DAT_00f416d4

static constexpr float kFovZoomStep = 0.0075f; // DAT_00fab464

static constexpr float kFlyClearance = 0.15f;     // DAT_00f41af8
static constexpr float kGravityClearance = 1.0f;  // FLOAT_00f4165c
static constexpr float kStandingClearance = 1.8f; // DAT_00f41a04
static constexpr float kCrouchedClearance = 0.8f; // DAT_00f416d0

static constexpr float kGravityAccel = 9.81f;    // DAT_00f419bc
static constexpr float kJumpImpulse = 3.5f;      // DAT_00fab44c
static constexpr float kGravityModeSink = 0.01f; // DAT_00f416d4
static constexpr float kGroundedSpeed = 0.01f;   // DAT_00f416d4

static constexpr float kFpsPitchLimit =
    1.2217305f; // DAT_00fab43c / DAT_00fab438

static constexpr float kHeadBobRate = 5.0f;        // DAT_00fab440
static constexpr float kHeadBobLift = 0.25f;       // DAT_00fab448
static constexpr float kHeadBobRoll = -0.25f;      // DAT_00fab444
static constexpr float kHeadBobThreshold = 0.1f;   // FLOAT_00f41668
static constexpr float kFpsRollInputScale = 0.25f; // DAT_00fab414
static constexpr float kFpsSideStepScale = 5.0f;   // DAT_00fab410

static constexpr float kForwardSpeedFactor = 1.0f;  // DAT_00fab458
static constexpr float kStrafeSpeedFactor = 0.75f;  // DAT_00fab454
static constexpr float kVerticalSpeedFactor = 0.5f; // DAT_00fab450
static constexpr float kSpeedScale = 90.0f;         // FLOAT_00f4180c

static constexpr float kAngleFromInput = 0.1f; // DAT_00fab424
static constexpr float kRollFromInput = 0.5f;  // DAT_00fab418

static constexpr float kFollowFovDistanceScale = 50.0f; // DAT_00fab40c
static constexpr float kFollowFovMin = 0.017453293f;    // DAT_00f41864

static constexpr float kCarSnapBack = 2.0f;
static constexpr float kCarSnapSide = 0.5f;
static constexpr float kCarBehindDistance = 20.0f; // DAT_00f03ee0
static constexpr float kCarBehindSide = 5.0f;      // DAT_00f41750

static float gToggleTimer = -1.0f; // DAT_00fab484

// Smoothed axes. All of these live in the .bss in the original, so they are
// shared by every steady cam instance and start at zero.
static float gHeadBobPhase = 0.0f;  // DAT_016a2cc4
static float gRollInput = 0.0f;     // DAT_016a2cc8
static float gRollAngle = 0.0f;     // DAT_016a2ccc
static float gRollAnglePrev = 0.0f; // DAT_016a2cd0
static float gPitchInput = 0.0f;    // DAT_016a2cd4
static float gYawInput = 0.0f;      // DAT_016a2cd8
static float gStrafeInput = 0.0f;   // DAT_016a2cdc
static float gForwardInput = 0.0f;  // DAT_016a2ce0
// Doubles as the vertical velocity in gravity / FPS mode.
static float gVerticalInput = 0.0f; // DAT_016a2ce4

static float gSensitivityTarget = 1.0f; // DAT_00fab490
static float gFovZoom = 1.0f;           // DAT_00fab48c
static float gFollowZoom = 75.0f;       // DAT_00fab488

struct SteadyCamInput {
    float Forward = 0.0f;
    float Pitch = 0.0f;
    float Yaw = 0.0f;
    float Roll = 0.0f;
    float Strafe = 0.0f;
    float Vertical = 0.0f;
    float Wheel = 0.0f;
    float Fov = 0.0f;
    bool bJump = false;
    bool bCrouch = false;
    bool bRun = false;
};

struct SteadyCamToggles {
    bool bAutoTilt = false;  // PAD_1
    bool bGravity = false;   // PAD_2
    bool bFPS = false;       // PAD_DOT
    bool bFollowCar = false; // PAD_3
    bool bNextCar = false;   // PAD_0
    bool bPadMode = false;   // PAD_MUL
    bool bCarBehind = false; // B
    bool bResetRoll = false; // PAD_DIV
};

static inline float ReadChannel( Device *pDevice, const char *pName ) {
    return ( pDevice != nullptr ) ? pDevice->getChannelValue( pName ) : 0.0f;
}

static inline bool IsChannelDown( Device *pDevice, const char *pName ) {
    return ( pDevice != nullptr ) && pDevice->getChannelValueBinary( pName );
}

static inline bool WasChannelPressed( Device *pDevice, const char *pName ) {
    return ( pDevice != nullptr ) &&
           IsChannelJustPressed( pDevice->getChannel( pName ) );
}

static inline float ReadActionWithDeadZone( DeviceAction *pAction ) {
    const float value = ReadActionAxis( pAction );
    return ( kPadDeadZone <= value ) ? value - kPadDeadZone : 0.0f;
}

static inline float ReadStickAxis( Device *pDevice, const char *pName ) {
    const float value = ReadChannel( pDevice, pName );
    if ( value < kPadDeadZone ) {
        return 0.0f;
    }
    return ( kOne < value ) ? kOne : value;
}

static inline float SmoothTowards( float current, float target, float retain ) {
    return ( target - current ) * ( kOne - retain ) * kSmoothingScale + current;
}

static void ReadKeyboardAndMouse( SteadyCamInput &input,
                                  SteadyCamToggles &toggles ) {
    Device *pKeyboard = gEdDev.getDevice( eDeviceType::DT_Keyboard, 0 );
    Device *pMouse = gEdDev.getDevice( eDeviceType::DT_Mouse, 0 );

    input.Forward =
        ReadChannel( pKeyboard, "PAD_8" ) - ReadChannel( pKeyboard, "PAD_5" );
    input.Strafe =
        ReadChannel( pKeyboard, "PAD_4" ) - ReadChannel( pKeyboard, "PAD_6" );
    input.Roll =
        ReadChannel( pKeyboard, "PAD_7" ) - ReadChannel( pKeyboard, "PAD_9" );

    input.Pitch = -ReadChannel( pMouse, "UP" );
    input.Yaw = -ReadChannel( pMouse, "LEFT" );

    input.Vertical =
        ReadChannel( pMouse, "BUTTON_1" ) - ReadChannel( pMouse, "BUTTON_0" );
    input.bJump = WasChannelPressed( pMouse, "BUTTON_1" );
    input.bCrouch = WasChannelPressed( pMouse, "BUTTON_0" );
    input.bRun = WasChannelPressed( pMouse, "WHEEL_UP" );

    input.Wheel = (float)IsChannelDown( pMouse, "WHEEL_UP" ) -
                  (float)IsChannelDown( pMouse, "WHEEL_DOWN" );

    input.Fov = ReadChannel( pKeyboard, "PAD_MINUS" ) -
                ReadChannel( pKeyboard, "PAD_PLUS" );

    toggles.bAutoTilt = WasChannelPressed( pKeyboard, "PAD_1" );
    toggles.bGravity = WasChannelPressed( pKeyboard, "PAD_2" );
    toggles.bFPS = WasChannelPressed( pKeyboard, "PAD_DOT" );
    toggles.bFollowCar = WasChannelPressed( pKeyboard, "PAD_3" );
    toggles.bNextCar = WasChannelPressed( pKeyboard, "PAD_0" );
    toggles.bPadMode = WasChannelPressed( pKeyboard, "PAD_MUL" );
    toggles.bCarBehind = WasChannelPressed( pKeyboard, "B" );
    toggles.bResetRoll = IsChannelDown( pKeyboard, "PAD_DIV" );
}

static void ReadRemappedActions( SteadyCamInput &input ) {
    PlayerConfiguration *pPlayer =
        ( gpConfig != nullptr ) ? gpConfig->pPlayerConfig : nullptr;
    if ( pPlayer == nullptr ) {
        return;
    }

    input.Forward = ReadActionWithDeadZone( pPlayer->pCarHeadUp ) -
                    ReadActionWithDeadZone( pPlayer->pCarHeadDown );
    input.Strafe = ReadActionWithDeadZone( pPlayer->pCarHeadLeft ) -
                   ReadActionWithDeadZone( pPlayer->pCarHeadRight );
    input.Pitch = ( ReadActionWithDeadZone( pPlayer->pBikeLeanBackward ) -
                    ReadActionWithDeadZone( pPlayer->pCarGearUp ) ) *
                  kActionPitchScale;
    input.Yaw = ( ReadActionWithDeadZone( pPlayer->pCarRight ) -
                  ReadActionWithDeadZone( pPlayer->pCarClutch ) ) *
                kActionYawScale;
    input.Roll = ReadActionAxis( pPlayer->pMenuLeft ) -
                 ReadActionAxis( pPlayer->pMenuRight );
    input.Wheel = ReadActionAxis( pPlayer->pMenuUp ) -
                  ReadActionAxis( pPlayer->pMenuDown );
    input.Fov = ReadActionAxis( pPlayer->pCarLight ) -
                ReadActionAxis( pPlayer->pCarBackview );

    input.bCrouch = IsActionPressed( pPlayer->pBikeGearUp );
    input.bRun = IsActionPressed( pPlayer->pMenuUp );

    input.Vertical = 0.0f;
}

void ReadGamepad( Device *pGamepad, float sensitivity, SteadyCamInput &input ) {
    input.Forward = ReadStickAxis( pGamepad, "STICK_1_UP" ) -
                    ReadStickAxis( pGamepad, "STICK_1_DOWN" );
    input.Strafe = ReadStickAxis( pGamepad, "STICK_1_LEFT" ) -
                   ReadStickAxis( pGamepad, "STICK_1_RIGHT" );

    input.Pitch = ( ReadStickAxis( pGamepad, "STICK_0_UP" ) -
                    ReadStickAxis( pGamepad, "STICK_0_DOWN" ) ) *
                  sensitivity * kStickPitchScale;
    input.Yaw = ( ReadStickAxis( pGamepad, "STICK_0_LEFT" ) -
                  ReadStickAxis( pGamepad, "STICK_0_RIGHT" ) ) *
                sensitivity * kStickYawScale;

    input.Roll =
        ReadStickAxis( pGamepad, "LEFT" ) - ReadStickAxis( pGamepad, "RIGHT" );

    input.bJump = WasChannelPressed( pGamepad, "BUTTON_0" );
    input.bCrouch = WasChannelPressed( pGamepad, "BUTTON_2" );
    input.bRun = WasChannelPressed( pGamepad, "BUTTON_3" );

    input.Wheel =
        ReadChannel( pGamepad, "UP" ) - ReadChannel( pGamepad, "DOWN" );

    input.Fov = ReadStickAxis( pGamepad, "BUTTON_4" ) -
                ReadStickAxis( pGamepad, "BUTTON_5" );
    input.Vertical = ReadStickAxis( pGamepad, "BUTTON_7" ) -
                     ReadStickAxis( pGamepad, "BUTTON_6" );
}

static PhysicsVehicle *GetPlayerPhysicsVehicle() {
    if ( gpCarState == nullptr ) {
        return nullptr;
    }

    PhysicsVehicle *pVehicle =
        gpCarState->getCarStates()[0].getPhysicsVehicle();
    return ( pVehicle != nullptr && pVehicle->isActive() ) ? pVehicle : nullptr;
}

CameraSteadyCam::CameraSteadyCam()
    : CameraGame(),
      pBody( nullptr ),
      rootPosition( Eigen::Vector4f::Zero() ),
      rootTarget( Eigen::Vector4f::Zero() ),
      mousePort( 0xffffffff ),
      distance( kDefaultDistance ),
      angleX( 0.0f ),
      angleY( 0.0f ),
      rootAngleX( 0.0f ),
      rootAngleY( 0.0f ),
      sensitivity( kDefaultSensitivity ),
      stepRoll( 0 ),
      mouseFov( kDefaultMouseFov ),
      speedForward( kOne ),
      autoTilt( 0 ),
      autoTiltFactor( kDefaultAutoTiltFactor ),
      forceCarBehind( 0 ),
      modeFPS( 0 ),
      modeGravity( 0 ),
      followCar( 0 ),
      carIndex( 0 ),
      modePad( 0 ),
      modeCrouch( 0 ),
      modeRun( 0 ),
      bInvertMouse( gpConfig != nullptr && gpConfig->bInvertMouse != 0 ),
      bCollision( false ),
      bBlockPosition( false ) {
    // FUN_008ca3f0
}

CameraSteadyCam::~CameraSteadyCam() {}

void CameraSteadyCam::initialize() {
    // FUN_008ccbf0
    if ( gpMngWorld != nullptr ) {
        gpMngWorld->setDisableStreamingOnCar( true );
    }

    MoviePlayer *pMoviePlayer = gpFlash->getMoviePlayer( 0, 0 );
    if ( pMoviePlayer != nullptr ) {
        pMoviePlayer->IsMoviePaused |= kMoviePauseBit;
    }

    OTDU_UNIMPLEMENTED; // TODO: AI (culling config flag at +0x1c0)

    angleX = worldToCam.row( 1 )[2] * kAngleXFromMatrix;
    angleY = atan2f( worldToCam.row( 2 )[0], worldToCam.row( 2 )[2] );

    rootAngleX = 0.0f;
    rootAngleY = 0.0f;

    gEdSys.setRelativeMouseMode( true );
}

bool CameraSteadyCam::tick( float deltaTime, bool param_1, bool param_2 ) {
    // FUN_008ca620
    if ( bBlockPosition && !param_2 ) {
        return true;
    }

    if ( deltaTime <= 0.0f ) {
        deltaTime = kFallbackDeltaTime;
    }

    SteadyCamInput input;
    SteadyCamToggles toggles;
    ReadKeyboardAndMouse( input, toggles );

    PhysicsVehicle *pPlayerVehicle = GetPlayerPhysicsVehicle();

    gToggleTimer -= deltaTime;

    auto acceptToggle = [&]( bool bPressed ) {
        if ( !bPressed || 0.0f <= gToggleTimer ) {
            return false;
        }
        gToggleTimer = kToggleHoldOff;
        return true;
    };

    if ( acceptToggle( toggles.bAutoTilt ) ) {
        autoTilt = ( autoTilt == 0 );
    }
    if ( acceptToggle( toggles.bGravity ) ) {
        modeGravity = ( modeGravity == 0 );
    }
    if ( acceptToggle( toggles.bFPS ) ) {
        modeFPS = ( modeFPS == 0 );
    }
    if ( acceptToggle( toggles.bFollowCar ) ) {
        followCar = ( followCar == 0 );
        if ( followCar == 0 ) {
            angleX = worldToCam.row( 1 )[2] * kAngleXFromMatrix;
            angleY = atan2f( worldToCam.row( 2 )[0], worldToCam.row( 2 )[2] );
        }
    }
    if ( acceptToggle( toggles.bNextCar ) ) {
        carIndex++;
    }
    if ( acceptToggle( toggles.bPadMode ) ) {
        modePad = ( modePad == 0 );
    }
    if ( acceptToggle( toggles.bCarBehind ) ) {
        forceCarBehind = ( forceCarBehind == 0 );
        if ( pPlayerVehicle != nullptr && gpPhysics != nullptr ) {
            gpPhysics->lockMutex();
            pPlayerVehicle->setPhysicsPaused( forceCarBehind != 0 );
            gpPhysics->unlockMutex();
        }
    }

    Device *pGamepad = gEdDev.getDevice( eDeviceType::DT_Other,
                                         eDeviceSpecialization::DS_Gamepad, 0 );
    if ( pGamepad != nullptr && pGamepad->isEnabled() ) {
        modePad = 1;
    }

    if ( modePad != 0 ) {
        if ( pGamepad == nullptr || !pGamepad->isEnabled() ) {
            ReadRemappedActions( input );
        } else {
            ReadGamepad( pGamepad, sensitivity, input );
        }
    }

    if ( bInvertMouse ) {
        input.Pitch = -input.Pitch;
    }

    if ( acceptToggle( input.bCrouch ) ) {
        modeCrouch = ( modeCrouch == 0 );
    }
    if ( acceptToggle( input.bRun ) ) {
        modeRun = ( modeRun == 0 );
    }

    if ( toggles.bResetRoll ) {
        gRollAngle = 0.0f;
        input.Roll = 0.0f;
    }

    if ( modeFPS != 0 ) {
        gSensitivityTarget = kSensitivityFpsTarget;
        autoTilt = 0;
        input.Forward *=
            ( modeRun != 0 && modeCrouch == 0 ) ? kRunFactor : kWalkFactor;
        input.Wheel = 0.0f;
    }

    if ( modeGravity == 0 && modeFPS == 0 ) {
        gVerticalInput =
            SmoothTowards( gVerticalInput, input.Vertical, kMoveRetain );
    }
    gForwardInput = SmoothTowards( gForwardInput, input.Forward, kMoveRetain );
    gStrafeInput = SmoothTowards( gStrafeInput, input.Strafe, kMoveRetain );
    gYawInput = SmoothTowards( gYawInput, input.Yaw, kLookRetain );
    gPitchInput = SmoothTowards( gPitchInput, input.Pitch, kLookRetain );
    gFovZoom =
        SmoothTowards( gFovZoom, kFovZoomStep * input.Fov + kOne, kZoomRetain );

    Eigen::Vector4f forwardVector =
        getCamToWorld().row( 2 ).transpose() * kMinusOne;
    Eigen::Vector4f sideVector =
        getCamToWorld().row( 0 ).transpose() * kMinusOne;
    const Eigen::Vector4f worldUp = kWorldUpVector;

    Eigen::Vector4f groundNormal = kWorldUpVector;
    if ( !bBlockPosition ) {
        float surfaceHeight = kNoSurfaceSentinel;
        Eigen::Vector4f surfaceNormal = kWorldUpVector;
        if ( gpPhysics != nullptr ) {
            gpPhysics->probeSurface( rootPosition, false, surfaceHeight,
                                     surfaceNormal );
        }
        if ( surfaceHeight == kNoSurfaceSentinel ) {
            if ( SampleHeightmapSurface( rootPosition, &surfaceNormal ) !=
                 kNoSurfaceSentinel ) {
                groundNormal = surfaceNormal;
            }
        } else {
            groundNormal = surfaceNormal;
        }
    }

    if ( modeFPS != 0 ) {
        forwardVector.head<3>() =
            sideVector.head<3>().cross( groundNormal.head<3>() );
        groundNormal.normalize();
        sideVector.head<3>() =
            groundNormal.head<3>().cross( forwardVector.head<3>() );
        autoTilt = 0;
    }

    if ( param_1 && !bBlockPosition ) {
        if ( pPlayerVehicle != nullptr ) {
            const Eigen::Vector4f carPosition =
                pPlayerVehicle->getChassisWorldMatrix().row( 3 ).transpose();
            rootPosition = carPosition - forwardVector * kCarSnapBack +
                           sideVector * kCarSnapSide;
        } else if ( gpCamera != nullptr &&
                    gpCamera->getPreviousCamera() != nullptr ) {
            rootPosition = *gpCamera->getPreviousCamera()->getWorldPositionRW();
        }
    }

    gSensitivityTarget *= kSensitivityWheelStep * input.Wheel + kOne;
    gSensitivityTarget =
        Clamp( gSensitivityTarget, kSensitivityMin, kSensitivityMax );
    sensitivity = SmoothTowards( sensitivity, gSensitivityTarget, kMoveRetain );

    float clearance = kFlyClearance;
    if ( modeGravity != 0 ) {
        forwardVector.y() = 0.0f;
        sideVector.y() = 0.0f;
        gVerticalInput -= kGravityModeSink;
        clearance = kGravityClearance;
    } else if ( modeFPS != 0 ) {
        gHeadBobPhase += kHeadBobRate * gForwardInput * deltaTime;
        if ( kTwoPI < gHeadBobPhase ) {
            gHeadBobPhase -= kTwoPI;
        }
        if ( fabsf( sinf( gHeadBobPhase ) ) < kHeadBobThreshold ) {
            gVerticalInput += kHeadBobLift * gForwardInput;
            input.Roll = kHeadBobRoll * gForwardInput;
        }
        if ( fabsf( cosf( gHeadBobPhase ) ) < kHeadBobThreshold ) {
            gVerticalInput += kHeadBobLift * gForwardInput;
            input.Roll = -kHeadBobRoll * gForwardInput;
        }

        if ( input.bJump && gVerticalInput < kGroundedSpeed &&
             modeCrouch == 0 ) {
            gVerticalInput += kJumpImpulse;
        }
        clearance =
            ( modeCrouch != 0 ) ? kCrouchedClearance : kStandingClearance;

        gVerticalInput -= deltaTime * kGravityAccel;
        angleX = Clamp( angleX, -kFpsPitchLimit, kFpsPitchLimit );
    }

    const float forwardSpeed =
        sensitivity * kForwardSpeedFactor * gForwardInput * kSpeedScale;
    const float strafeSpeed =
        sensitivity * kStrafeSpeedFactor * gStrafeInput * kSpeedScale;
    const float verticalSpeed =
        sensitivity * kVerticalSpeedFactor * gVerticalInput * kSpeedScale;

    if ( modeFPS == 0 || pBody == nullptr ) {
        Eigen::Vector4f candidate =
            rootPosition +
            ( forwardVector * forwardSpeed + sideVector * strafeSpeed +
              worldUp * verticalSpeed ) *
                deltaTime;

        if ( !bBlockPosition ) {
            const float heightmapFloor =
                SampleHeightmapSurface( candidate, nullptr ) + clearance;
            if ( candidate.y() < heightmapFloor ) {
                candidate.y() = heightmapFloor;
                gVerticalInput = 0.0f;
            }

            float physicsHeight = kNoSurfaceSentinel;
            Eigen::Vector4f unusedNormal = kWorldUpVector;
            if ( gpPhysics != nullptr ) {
                gpPhysics->probeSurface( candidate, false, physicsHeight,
                                         unusedNormal );
            }
            if ( candidate.y() < physicsHeight + clearance ) {
                candidate.y() = physicsHeight + clearance;
                gVerticalInput = 0.0f;
            }

            rootPosition.x() = SmoothTowards( rootPosition.x(), candidate.x(),
                                              kPositionRetain );
            rootPosition.y() = SmoothTowards( rootPosition.y(), candidate.y(),
                                              kPositionRetain );
            rootPosition.z() = SmoothTowards( rootPosition.z(), candidate.z(),
                                              kPositionRetain );
            rootPosition.w() = kOne;
        }
    } else {
        OTDU_UNIMPLEMENTED_ONCE; // TODO: PHYSICS (FPS body velocity update)
    }

    float newAngleX = kAngleFromInput * gPitchInput;
    if ( modePad == 0 ) {
        newAngleX += angleX;
    }
    angleX = newAngleX;
    angleY += kAngleFromInput * gYawInput;

    Matrix4x4f_t rotation = MakeRotationMatX( rootAngleX + angleX );
    MatrixRotateY( rotation, rootAngleY + angleY );

    const Eigen::Vector4f forward = TDUMat4x4MulVec(
        rotation, Eigen::Vector4f( 0.0f, 0.0f, kTargetDistance, 0.0f ) );

    if ( followCar == 0 ) {
        rootTarget = rootPosition + forward;
        rootTarget.w() = kOne;
    } else {
        PhysicsVehicle *pFollowed = nullptr;
        for ( uint32_t i = 0; i < kMaxNumVehicles; i++ ) {
            if ( carIndex < 0 || carIndex >= (int32_t)kMaxNumVehicles ) {
                carIndex = 0;
            }
            PhysicsVehicle *pCandidate =
                ( gpCarState != nullptr )
                    ? gpCarState->getCarStates()[carIndex].getPhysicsVehicle()
                    : nullptr;
            if ( pCandidate != nullptr && pCandidate->isActive() ) {
                pFollowed = pCandidate;
                break;
            }
            carIndex++;
        }

        if ( pFollowed != nullptr ) {
            const Eigen::Vector4f followedPosition =
                pFollowed->getChassisWorldMatrix().row( 3 ).transpose();
            rootTarget.x() = SmoothTowards(
                rootTarget.x(), followedPosition.x(), kFollowRetain );
            rootTarget.y() = SmoothTowards(
                rootTarget.y(), followedPosition.y(), kFollowRetain );
            rootTarget.z() = SmoothTowards(
                rootTarget.z(), followedPosition.z(), kFollowRetain );
        }
    }

    if ( modeFPS == 0 ) {
        if ( pBody != nullptr ) {
            OTDU_UNIMPLEMENTED_ONCE; // TODO: PHYSICS (release the FPS body)
        }
    } else if ( pBody == nullptr ) {
        OTDU_UNIMPLEMENTED_ONCE; // TODO: PHYSICS (create the FPS body)
    }

    Matrix4x4f_t viewBasis = Matrix4x4f_t::Identity();
    Eigen::Vector3f sideAxis =
        worldUp.head<3>().cross( forwardVector.head<3>() );
    const float sideLength = sideAxis.norm();
    if ( 0.0f < sideLength ) {
        sideAxis /= sideLength;
    }
    viewBasis.row( 0 ).head<3>() = sideAxis;
    viewBasis.row( 1 ) = worldUp.transpose();
    viewBasis.row( 2 ) = forwardVector.transpose();

    Eigen::Vector4f positionOffset = Eigen::Vector4f::Zero();
    float rollTarget = 0.0f;
    if ( modeFPS == 0 ) {
        if ( autoTilt == 0 ) {
            gRollInput = SmoothTowards( gRollInput, input.Roll, kRollRetain );
            rollTarget = kRollFromInput * gRollInput + gRollAngle;
        } else {
            rollTarget = autoTiltFactor * kAngleFromInput * gYawInput;
        }
    } else {
        gRollAnglePrev = SmoothTowards(
            gRollAngle, kFpsRollInputScale * input.Roll, kRollRetain );
        positionOffset = sideVector * gRollAnglePrev * kFpsSideStepScale;
        rollTarget = gRollAnglePrev;
    }

    gRollAngle = SmoothTowards( gRollAnglePrev, rollTarget, kRollRetain );
    gRollAnglePrev = gRollAngle;

    upVector = TDUMat4x4MulVec(
        viewBasis,
        Eigen::Vector4f( sinf( gRollAngle ), cosf( gRollAngle ), 0.0f, 0.0f ) );

    setTarget( rootTarget );
    setPosition( rootPosition + positionOffset );

    if ( followCar == 0 ) {
        mouseFov = mouseFov * gFovZoom;
    } else {
        gFollowZoom = gFollowZoom * gFovZoom;

        const float followedDistance = ( rootTarget - rootPosition ).norm();
        float halfAngle =
            ( kHalfPI -
              atan2f( followedDistance * kFollowFovDistanceScale, kOne ) ) *
            gFollowZoom;
        if ( halfAngle < kFollowFovMin ) {
            halfAngle = kFollowFovMin;
        }
        mouseFov =
            SmoothTowards( mouseFov, halfAngle * kRadToDeg, kFollowFovRetain );
    }

    if ( kMouseFovMax < mouseFov ) {
        mouseFov = kMouseFovMax;
    }
    if ( mouseFov < kMouseFovMin ) {
        mouseFov = kMouseFovMin;
    }
    setFOV( mouseFov * kDegToRad );

    updateMatrix( worldPosition.head<3>(), upVector.head<3>(),
                  worldTarget.head<3>() );

    if ( forceCarBehind != 0 && pPlayerVehicle != nullptr ) {
        Matrix4x4f_t carTransform = pPlayerVehicle->getChassisWorldMatrix();
        const Eigen::Vector4f carPlacement =
            rootPosition - forwardVector * kCarBehindDistance +
            upVector * kCarBehindSide;
        carTransform.row( 3 ) = carPlacement.transpose();
        pPlayerVehicle->resetToTransform( carTransform, false );
    }

    float nearPlaneValue =
        ( ( mouseFov - kNearPlaneRefFov ) * kNearPlaneSpan ) /
            ( kNearPlaneLowFov - kNearPlaneRefFov ) +
        kNearPlaneBase;
    if ( kOne <= nearPlaneValue ) {
        nearPlaneValue = kOne;
    } else if ( nearPlaneValue <= kNearPlaneBase ) {
        nearPlaneValue = kNearPlaneBase;
    }

    nearPlane = nearPlaneValue;
    farPlane = kFarPlane;

    gpCamera->setFarPlane( kFarPlane );
    gpCamera->setNearPlane( nearPlaneValue );
    gpCamera->setEnableDepthAware( true );
    gpCamera->setInteriorFarPlane( interiorFarPlane );
    gpCamera->setInteriorNearPlane( interiorNearPlane );
    gpCamera->setEnableDepthAwareInterior( true );

    if ( !bBlockPosition && gpWorld != nullptr ) {
        gpWorld->setStreamingPosition( &rootPosition );
    }

    return true;
}

void CameraSteadyCam::destroy() {
    // FUN_008ccb90
    if ( gpMngWorld != nullptr ) {
        gpMngWorld->setDisableStreamingOnCar( false );
    }

    MoviePlayer *pMoviePlayer = gpFlash->getMoviePlayer( 0, 0 );
    if ( pMoviePlayer != nullptr ) {
        pMoviePlayer->IsMoviePaused &= ~kMoviePauseBit;
    }

    OTDU_UNIMPLEMENTED; // TODO: AI (culling config flag at +0x1c0)

    gEdSys.setRelativeMouseMode( false );
}
