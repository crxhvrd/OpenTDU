#include "shared.h"
#include "camera_path.h"

#include "gs_camera.h"

CameraPath::CameraPath()
    : CameraGame(),
      mutex(),
      pPath( nullptr ),
      time( 0.0f ),
      duration( 0.0f ),
      bInverted( false ),
      bPaused( false ),
      bHasPath( false ),
      bT2Quaternion( false ),
      offset( Matrix4x4f_t::Identity() ),
      bOffset( false ),
      coordChange( Matrix4x4f_t::Identity() ) {
    // FUN_008d78a0
    OTDU_UNIMPLEMENTED; // TODO: CAMERA (TCBSplinePos/TCBSplineRot allocation)

    setFOVAndAspectRatio( kDefaultReferenceAspect, kDefaultFOV );
}

CameraPath::~CameraPath() { mutex.terminate(); }

void CameraPath::initialize() {
    // FUN_008d7410
    // NOP
}

void CameraPath::destroy() {
    // FUN_008d7400
    // NOP
}

void CameraPath::setStaticView( const Eigen::Vector4f &position,
                                const Eigen::Vector4f &target ) {
    setPosition( position );
    setTarget( target );
    upVector = Eigen::Vector4f( 0.0f, 1.0f, 0.0f, 0.0f );
}

void CameraPath::setPath( Path * /*pNewPath*/,
                          bool /*bInvert*/,
                          float /*newDuration*/,
                          const Matrix4x4f_t * /*pOffsetMatrix*/,
                          bool /*bStartAtEnd*/ ) {
    // FUN_008d7b70
    OTDU_UNIMPLEMENTED; // TODO: CAMERA (TCB spline path playback)
}

bool CameraPath::tick( float deltaTime, bool /*param_1*/, bool /*param_2*/ ) {
    // FUN_008d7650
    ScopedMutexLock lock( &mutex );

    if ( bHasPath ) {
        if ( !bPaused ) {
            time += deltaTime / duration;
        }

        if ( time > 1.0f ) {
            time = 1.0f;
        }

        OTDU_UNIMPLEMENTED_ONCE; // TODO: CAMERA (sample the position/rotation
                                 // splines at `time`, apply `coordChange` when
                                 // bOffset, then push position/target/up)
    }

    updateMatrix( worldPosition.head<3>(), upVector.head<3>(),
                  worldTarget.head<3>() );

    gpCamera->setFarPlane( farPlane );
    gpCamera->setNearPlane( nearPlane );
    gpCamera->setEnableDepthAware( true );
    gpCamera->setInteriorFarPlane( interiorFarPlane );
    gpCamera->setInteriorNearPlane( interiorNearPlane );
    gpCamera->setEnableDepthAwareInterior( true );

    return true;
}
