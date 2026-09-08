#include "shared.h"
#include "camera_first_person.h"

#include "config/gs_config.h"
#include "gs_timer.h"
#include "input/ed_dev.h"
#include "input/input_device.h"
#include "physics/gs_physics.h"
#include "gs_camera.h"

#include "game/gm_base.h"
#include "game/gm_scene.h"
#include "system/gs_pc_options.h"

static constexpr float kLookSensitivity = 2.2f;

static constexpr float kWalkSpeed = 2.4f;
static constexpr float kRunSpeed = 5.2f;

static constexpr float kEyeHeight = 1.65f;

static constexpr float kMaxPitch = 1.5533431f; // 89 degrees

static constexpr float kFallResetDepth = 25.0f;
static constexpr float kMaxDeltaTime = 0.1f;

bool IsFirstPersonInputCaptured() {
    if ( gpActiveGameMode != nullptr ) {
        if ( gpActiveGameMode->isMessageBoxVisible() ||
             gpActiveGameMode->hasRequestedExit() ) {
            return true;
        }
    }

    if ( gpActiveGMScene != nullptr && gpActiveGMScene->isPaused() ) {
        return true;
    }

    if ( gpPCOptions != nullptr && gpPCOptions->isMainMenuOpened() ) {
        return true;
    }

    if ( gpTimer != nullptr && gpTimer->GameSpeed <= 0.0f ) {
        return true;
    }

    if ( gpCamera != nullptr && gpCamera->isPaused() ) {
        return true;
    }

    return false;
}

CameraFirstPerson::CameraFirstPerson()
    : CameraGame(),
      pCharacterController( nullptr ),
      yaw( 0.0f ),
      pitch( 0.0f ),
      eyeHeight( kEyeHeight ),
      walkSpeed( kWalkSpeed ),
      runSpeed( kRunSpeed ),
      lookSensitivity( kLookSensitivity ),
      spawnPosition( 0.0f, 0.0f, 0.0f, 1.0f ),
      bNoClip( false ) {
    cameraType = kCameraType;
    name = "FIRST_PERSON";
}

CameraFirstPerson::~CameraFirstPerson() {}

Eigen::Vector3f CameraFirstPerson::getForwardVector() const {
    const float cosPitch = cosf( pitch );
    return Eigen::Vector3f( sinf( yaw ) * cosPitch, sinf( pitch ),
                            -cosf( yaw ) * cosPitch );
}

void CameraFirstPerson::setViewDirection( const Eigen::Vector3f &direction ) {
    const Eigen::Vector3f flat( direction.x(), 0.0f, direction.z() );
    if ( flat.squaredNorm() > 1.0e-6f ) {
        // Inverse of getForwardVector(): x = sin(yaw), z = -cos(yaw).
        yaw = atan2f( flat.x(), -flat.z() );
    }

    const float length = direction.norm();
    pitch = ( length > 1.0e-6f ) ? Clamp( asinf( direction.y() / length ),
                                          -kMaxPitch, kMaxPitch )
                                 : 0.0f;
}

void CameraFirstPerson::destroy() {
    // NOP
}

bool CameraFirstPerson::tick( float /*deltaTime*/,
                              bool /*param_1*/,
                              bool /*param_2*/ ) {
    const float dt = ( gpTimer != nullptr ) ? Clamp( gpTimer->SystemDeltaTime,
                                                     0.0f, kMaxDeltaTime )
                                            : ( 1.0f / 60.0f );

    const bool bInputCaptured = IsFirstPersonInputCaptured();

    Device *pMouse = gEdDev.getDevice( eDeviceType::DT_Mouse, 0 );
    Device *pKeyboard = gEdDev.getDevice( eDeviceType::DT_Keyboard, 0 );

    float forwardInput = 0.0f;
    float strafeInput = 0.0f;
    bool bRunning = false;

    if ( !bInputCaptured && pMouse != nullptr ) {
        const float mouseX =
            pMouse->getChannelValue( "LEFT" ); // +X = moving right
        const float mouseY =
            pMouse->getChannelValue( "UP" ); // +Y = moving up

        const float invert =
            ( gpConfig != nullptr && gpConfig->bInvertMouse ) ? -1.0f : 1.0f;

        yaw += mouseX * lookSensitivity;
        pitch = Clamp( pitch + mouseY * lookSensitivity * invert, -kMaxPitch,
                       kMaxPitch );

        if ( yaw > (float)EIGEN_PI ) {
            yaw -= 2.0f * (float)EIGEN_PI;
        } else if ( yaw < -(float)EIGEN_PI ) {
            yaw += 2.0f * (float)EIGEN_PI;
        }
    }

    if ( !bInputCaptured && pKeyboard != nullptr ) {
        if ( pKeyboard->getChannelValueBinary( "W" ) ||
             pKeyboard->getChannelValueBinary( "UP" ) ) {
            forwardInput += 1.0f;
        }
        if ( pKeyboard->getChannelValueBinary( "S" ) ||
             pKeyboard->getChannelValueBinary( "DOWN" ) ) {
            forwardInput -= 1.0f;
        }
        if ( pKeyboard->getChannelValueBinary( "D" ) ||
             pKeyboard->getChannelValueBinary( "RIGHT" ) ) {
            strafeInput += 1.0f;
        }
        if ( pKeyboard->getChannelValueBinary( "A" ) ||
             pKeyboard->getChannelValueBinary( "LEFT" ) ) {
            strafeInput -= 1.0f;
        }

        bRunning = pKeyboard->getChannelValueBinary( "LEFT SHIFT" ) ||
                   pKeyboard->getChannelValueBinary( "RIGHT SHIFT" );
    }

    const float speed = bRunning ? runSpeed : walkSpeed;

    const Eigen::Vector3f forwardFlat( sinf( yaw ), 0.0f, -cosf( yaw ) );
    const Eigen::Vector3f rightFlat( cosf( yaw ), 0.0f, sinf( yaw ) );
    const Eigen::Vector3f moveForward =
        bNoClip ? getForwardVector() : forwardFlat;

    Eigen::Vector3f moveDirection =
        moveForward * forwardInput + rightFlat * strafeInput;
    const float moveLength = moveDirection.norm();
    if ( moveLength > 1.0e-4f ) {
        moveDirection /= moveLength;
    } else {
        moveDirection.setZero();
    }

    const Eigen::Vector3f desiredVelocity = moveDirection * speed;
    Eigen::Vector4f eyePosition;
    if ( !bNoClip && pCharacterController != nullptr && gpPhysics != nullptr ) {
        if ( !bInputCaptured ) {
            gpPhysics->updateCharacter( pCharacterController, desiredVelocity,
                                        dt );
        }

        // Cheap way to prevent OOB/falling under the map
        Eigen::Vector4f feet =
            gpPhysics->getCharacterPosition( pCharacterController );
        if ( !bInputCaptured &&
             feet.y() < spawnPosition.y() - kFallResetDepth ) {
            gpPhysics->setCharacterPosition( pCharacterController,
                                             spawnPosition );
            feet = spawnPosition;
        }

        eyePosition = feet;
    } else if ( !bInputCaptured ) {
        eyePosition = worldPosition;
        eyePosition.head<3>() += desiredVelocity * dt;
        eyePosition.y() -= eyeHeight;
    } else {
        eyePosition = worldPosition;
        eyePosition.y() -= eyeHeight;
    }

    eyePosition.y() += eyeHeight;
    eyePosition.w() = 1.0f;

    const Eigen::Vector3f forward = getForwardVector();
    Eigen::Vector4f target = eyePosition;
    target.head<3>() += forward;

    setPosition( eyePosition );
    setTarget( target );
    setUp( Eigen::Vector4f( 0.0f, 1.0f, 0.0f, 0.0f ) );
    setFOV( 90.0f );

    gpCamera->setNearPlane( 0.1f );
    gpCamera->setFarPlane( 40000.0f );
    gpCamera->setEnableDepthAware( true );
    gpCamera->setInteriorNearPlane( 0.1f );
    gpCamera->setInteriorFarPlane( 40000.0f );
    gpCamera->setEnableDepthAwareInterior( true );

    updateMatrix( eyePosition.head<3>(), Eigen::Vector3f( 0.0f, 1.0f, 0.0f ),
                  target.head<3>() );

    return true;
}
