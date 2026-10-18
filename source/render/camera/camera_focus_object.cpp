#include "shared.h"
#include "camera_focus_object.h"

#include "core/maths_helper.h"
#include "input/ed_dev.h"
#include "input/input_device.h"
#include "physics/vehicle/physics_vehicle.h"
#include "render/camera/gs_camera.h"

static constexpr float DAT_00f41710 = 1e-05f;     // DAT_00f41710
static constexpr float DAT_00f417f0 = 0.02f;      // DAT_00f417f0
static constexpr float FLOAT_00fab2c8 = 0.01f;    // DAT_00fab2c8
static constexpr float _DAT_00f7c760 = 1.0f;      // DAT_00f7c760
static constexpr float DAT_00f416b0 = 2.0f;       // DAT_00f416b0
static constexpr float FLOAT_00f41668 = 0.1f;     // DAT_00f41668
static constexpr float DAT_00f418a8 = -10.0f;     // DAT_00f418a8
static constexpr float FLOAT_00f4166c = 10.0f;    // DAT_00f4166c
static constexpr float FLOAT_00fab2d0 = 40000.0f; // DAT_00fab2d0
static constexpr float _DAT_00f41f58 = 0.07f;     // DAT_00f41f58

static inline bool IsMouseButtonHeld( Device *pMouse, int32_t buttonIndex ) {
    int32_t channelIndex = pMouse->getChannelIndex(
        static_cast<int32_t>( eInputChannelType::ICT_Digital ), buttonIndex );
    if ( channelIndex < 0 ) {
        return false;
    }
    return ( pMouse->getChannels()[channelIndex].Flags >> 4 & 1 ) != 0;
}

static inline float GetMouseAxis( Device *pMouse, const char *pChannelName ) {
    InputChannel *pChannel = pMouse->getChannel( pChannelName );
    if ( pChannel == nullptr ) {
        return 0.0f;
    }
    float value = -pChannel->InputValue;
    return ( FLOAT_00fab2c8 <= fabsf( value ) ) ? value : 0.0f;
}

CameraFocusObject::CameraFocusObject()
    : CameraGame(),
      mouseIndex( -1 ),
      offset( Eigen::Vector4f::Zero() ),
      angle( Eigen::Vector4f::Zero() ),
      distance( FLOAT_00f4166c ),
      pObject( nullptr ) {
    // FUN_00411e70
}

CameraFocusObject::~CameraFocusObject() {}

void CameraFocusObject::initialize() {
    // FUN_008de330
    offset = Eigen::Vector4f::Zero();
    angle = Eigen::Vector4f::Zero();
    pObject = gpCamera->getTargetPhysEntity();
}

bool CameraFocusObject::tick( float deltaTime, bool param_1, bool param_2 ) {
    // FUN_008de890
    if ( deltaTime < DAT_00f41710 ) {
        deltaTime = DAT_00f417f0;
    }

    if ( pObject == nullptr ) {
        pObject = gpCamera->getTargetPhysEntity();
        if ( pObject == nullptr ) {
            return false;
        }
    }

    updateFromMouse( deltaTime );

    Matrix4x4f_t rotation = computeRotation();
    Eigen::Vector4f direction =
        TDUMat4x4MulVec( rotation, Eigen::Vector4f( 0.0f, 0.0f, 1.0f, 0.0f ) );

    Eigen::Vector4f objectPosition =
        pObject->getChassisShiftedMatrix().row( 3 ).transpose();
    objectPosition.w() = 1.0f;

    Eigen::Vector4f focusPoint = objectPosition + offset;
    setTargetOffseted( focusPoint );
    setPositionOffseted( direction * distance + focusPoint );

    upVector =
        TDUMat4x4MulVec( rotation, Eigen::Vector4f( 0.0f, 1.0f, 0.0f, 0.0f ) );
    updateMatrix( worldPosition.head<3>(), upVector.head<3>(),
                  worldTarget.head<3>() );

    farPlane = FLOAT_00fab2d0;
    gpCamera->setFarPlane( farPlane );
    nearPlane = distance * _DAT_00f41f58;
    gpCamera->setNearPlane( nearPlane );
    gpCamera->setEnableDepthAware( true );
    gpCamera->setInteriorNearPlane( interiorNearPlane );
    gpCamera->setInteriorFarPlane( interiorFarPlane );
    gpCamera->setEnableDepthAwareInterior( true );

    return true;
}

void CameraFocusObject::destroy() {
    // FUN_008de320
    // NOP
}

void CameraFocusObject::updateFromMouse( float deltaTime ) {
    // FUN_008de3e0
    Device *pMouse = gEdDev.getDevice( eDeviceType::DT_Mouse, 0 );
    if ( pMouse == nullptr ) {
        return;
    }

    bool bLeftButton = IsMouseButtonHeld( pMouse, 0 );
    bool bRightButton = IsMouseButtonHeld( pMouse, 1 );
    bool bMiddleButton = IsMouseButtonHeld( pMouse, 2 );

    // Retail reads analog channels 0 (X) and 2 (Y).
    float deltaX = GetMouseAxis( pMouse, "LEFT" );
    float deltaY = GetMouseAxis( pMouse, "UP" );

    if ( bLeftButton ) {
        if ( bRightButton ) {
            if ( bMiddleButton ) {
                angle.z() += deltaX;
                return;
            }

            setFOV( _DAT_00f7c760 * deltaY * deltaTime + fov );
            return;
        }

        distance -= distance * _DAT_00f7c760 * deltaTime * DAT_00f416b0;
        if ( distance < FLOAT_00f41668 ) {
            distance = FLOAT_00f41668;
        }
        return;
    }

    if ( bMiddleButton ) {
        distance += distance * _DAT_00f7c760 * deltaTime * DAT_00f416b0;
        return;
    }

    if ( bRightButton ) {
        float panScale = distance * _DAT_00f7c760;
        Eigen::Vector4f pan( panScale * deltaX * deltaTime * DAT_00f418a8,
                             panScale * deltaY * deltaTime * FLOAT_00f4166c,
                             0.0f, 0.0f );
        offset += TDUMat4x4MulVec( computeRotation(), pan );
        return;
    }

    angle.x() += deltaY;
    angle.y() += deltaX;
}

Matrix4x4f_t CameraFocusObject::computeRotation() const {
    // FUN_008de370
    Matrix4x4f_t rotation = Matrix4x4f_t::Identity();
    MatrixRotateZ( rotation, angle.z() );
    MatrixRotateX( rotation, angle.x() );
    MatrixRotateY( rotation, angle.y() );
    return rotation;
}
