#include "shared.h"
#include "camera_path.h"

#include "core/maths_helper.h"
#include "game/scenaric/bank_scene_arrays.h"
#include "gs_camera.h"

// A path key, as stored in the scenary bank (8 dwords per key).
struct PathKey {
    float Position;
    float Value[4];
    float T;
    float C;
    float B;
};
OTDU_SIZE_MUST_MATCH( PathKey, 32 );

static int32_t GetPathKeyCount( const void *pArray ) {
    return pArray == nullptr ? 0 : *reinterpret_cast<const int32_t *>( pArray );
}

static const PathKey *GetPathKeys( const void *pArray ) {
    return pArray == nullptr
               ? nullptr
               : reinterpret_cast<const PathKey *>(
                     reinterpret_cast<const int32_t *>( pArray ) + 1 );
}

CameraPath::CameraPath()
    : CameraGame(),
      mutex(),
      pPath( nullptr ),
      splinePosition(),
      splineRotation(),
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
    splinePosition.initialize( kMaxNumKeys );
    splineRotation.initialize( kMaxNumKeys );

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

void CameraPath::stopPath() {
    ScopedMutexLock lock( &mutex );
    bHasPath = false;
}

void CameraPath::setPath( Path *pNewPath,
                          bool bInvert,
                          float newDuration,
                          const Matrix4x4f_t *pOffsetMatrix,
                          bool bStartAtEnd ) {
    // FUN_008d7b70
    ScopedMutexLock lock( &mutex );

    bHasPath = false;

    if ( pPath == pNewPath && bInverted != bInvert ) {
        time = kOne - time;
    } else {
        time = 0.0f;
    }

    if ( bStartAtEnd ) {
        time = kOne;
    }

    pPath = pNewPath;
    bInverted = bInvert;
    bPaused = false;
    bT2Quaternion = true;

    if ( pNewPath == nullptr ) {
        return;
    }

    duration = *reinterpret_cast<const float *>( &pNewPath->field_0x8 );
    if ( 0.0f < newDuration ) {
        duration = newDuration;
    }

    if ( pOffsetMatrix == nullptr ) {
        bOffset = false;
        offset = Matrix4x4f_t::Identity();
    } else {
        offset = *pOffsetMatrix;
        coordChange = *pOffsetMatrix;
        bOffset = true;
    }

    // Position keys.
    const int32_t numPositionKeys = GetPathKeyCount( pNewPath->pArray0 );
    const PathKey *pPositionKeys = GetPathKeys( pNewPath->pArray0 );

    splinePosition.NumKeys = numPositionKeys;
    OTDU_ASSERT( numPositionKeys <= splinePosition.MaxNumKeys );

    for ( int32_t i = 0; i < numPositionKeys; i++ ) {
        const PathKey &srcKey = pPositionKeys[i];
        if ( i < splinePosition.NumKeys ) {
            TCBKeyPos &dstKey = splinePosition.pKeys[i];
            dstKey.Position = srcKey.Position / duration;
            dstKey.Vec4 = Eigen::Vector4f( srcKey.Value[0], srcKey.Value[1],
                                           srcKey.Value[2], srcKey.Value[3] );
            dstKey.T = srcKey.T;
            dstKey.C = srcKey.C;
            dstKey.B = srcKey.B;
        }
    }

    // Rotation keys.
    const int32_t numRotationKeys = GetPathKeyCount( pNewPath->pArray1 );
    const PathKey *pRotationKeys = GetPathKeys( pNewPath->pArray1 );

    splineRotation.NumKeys = numRotationKeys;
    OTDU_ASSERT( numRotationKeys <= splineRotation.MaxNumKeys );

    for ( int32_t i = 0; i < numRotationKeys; i++ ) {
        const PathKey &srcKey = pRotationKeys[i];
        if ( i < splineRotation.NumKeys ) {
            TCBKeyQuat &dstKey = splineRotation.pKeys[i];
            dstKey.Position = srcKey.Position / duration;
            dstKey.Quat =
                Eigen::Quaternionf( srcKey.Value[3], srcKey.Value[0],
                                    srcKey.Value[1], srcKey.Value[2] );
            dstKey.T = srcKey.T;
            dstKey.C = srcKey.C;
            dstKey.B = srcKey.B;
        }
    }

    splinePosition.compute();
    splineRotation.compute();

    bHasPath = true;
}

void CameraPath::blendTo( const Matrix4x4f_t &from,
                          const Matrix4x4f_t &to,
                          float newDuration,
                          const Matrix4x4f_t *pOffsetMatrix ) {
    // FUN_008d7950
    ScopedMutexLock lock( &mutex );

    time = 0.0f;
    bHasPath = false;
    pPath = nullptr;
    bInverted = false;
    bPaused = false;
    bT2Quaternion = false;
    duration = newDuration;

    const Eigen::Quaternionf fromRotation(
        Eigen::Matrix3f( from.topLeftCorner<3, 3>() ) );
    const Eigen::Quaternionf toRotation(
        Eigen::Matrix3f( to.topLeftCorner<3, 3>() ) );

    splinePosition.NumKeys = 2;
    splinePosition.pKeys[0].Position = 0.0f;
    splinePosition.pKeys[0].Vec4 = from.row( 3 ).transpose();
    splinePosition.pKeys[0].T = 0.0f;
    splinePosition.pKeys[0].C = 0.0f;
    splinePosition.pKeys[0].B = 0.0f;
    splinePosition.pKeys[1].Position = kOne;
    splinePosition.pKeys[1].Vec4 = to.row( 3 ).transpose();
    splinePosition.pKeys[1].T = kOne;
    splinePosition.pKeys[1].C = 0.0f;
    splinePosition.pKeys[1].B = 0.0f;

    splineRotation.NumKeys = 2;
    splineRotation.pKeys[0].Position = 0.0f;
    splineRotation.pKeys[0].Quat = fromRotation;
    splineRotation.pKeys[0].T = 0.0f;
    splineRotation.pKeys[0].C = 0.0f;
    splineRotation.pKeys[0].B = 0.0f;
    splineRotation.pKeys[1].Position = kOne;
    splineRotation.pKeys[1].Quat = toRotation;
    splineRotation.pKeys[1].T = kOne;
    splineRotation.pKeys[1].C = 0.0f;
    splineRotation.pKeys[1].B = 0.0f;

    splinePosition.compute();
    splineRotation.compute();

    if ( pOffsetMatrix == nullptr ) {
        bOffset = false;
        offset = Matrix4x4f_t::Identity();
    } else {
        offset = *pOffsetMatrix;
        coordChange = *pOffsetMatrix;
        bOffset = true;
    }

    bHasPath = true;
}

bool CameraPath::tick( float deltaTime, bool /*param_1*/, bool /*param_2*/ ) {
    // FUN_008d7650
    ScopedMutexLock lock( &mutex );

    if ( bHasPath ) {
        if ( !bPaused ) {
            time += deltaTime / duration;
        }

        if ( time > kOne ) {
            time = kOne;
        }

        Eigen::Vector4f splinePos = splinePosition.evaluate( time );
        const Eigen::Quaternionf splineRot = splineRotation.evaluate( time );

        splinePos.w() = kOne;
        setTransformFromQuaternion( splinePos, splineRot, bT2Quaternion, kOne );

        if ( bOffset ) {
            Matrix4x4f_t camToWorld = getCamToWorld() * coordChange;

            setCamToWorld( camToWorld );
            setWorldToCam( OrthoInvert( camToWorld ) );

            setPositionOffseted( camToWorld.row( 3 ).transpose() );
            updateTargetAndUpFromCamToWorld( camToWorld, kMinusOne );
        }
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
