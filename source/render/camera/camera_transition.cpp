#include "shared.h"
#include "camera_transition.h"

#include "core/maths_helper.h"
#include "core/tcb_spline.h"
#include "camera_vehicle.h"
#include "gs_camera.h"
#include "render/gs_render.h"
#include "game/scenaric/bank_scene_arrays.h"
#include "world/gs_world_streaming.h"

float gCameraEaseInExponent = 4.0f;  // DAT_00fab2b4
float gCameraEaseOutExponent = 4.0f; // DAT_00fab2b0

static inline Eigen::Vector4f GetStreamOrigin() {
    return gpWorldStreaming->isEnabled() ? gpWorldStreaming->getPosition()
                                         : Eigen::Vector4f::Zero();
}

static inline Eigen::Vector4f ToKeyPosition( const TCBKeyFrame &key,
                                             const bool bSwapYZ ) {
    return bSwapYZ ? Eigen::Vector4f( key.Position[0], key.Position[2],
                                      -key.Position[1], 1.0f )
                   : Eigen::Vector4f( key.Position[0], key.Position[1],
                                      key.Position[2], 1.0f );
}

static void LoadKeyFrames( TCBSplinePosition &spline,
                           const TCBKeyFrame *pKeys,
                           const int32_t numKeys,
                           const bool bSwapYZ ) {
    spline.NumKeys = numKeys;
    const float lastFrame = float( uint32_t( pKeys[numKeys - 1].FrameIndex ) );
    for ( int32_t i = 0; i < numKeys; i++ ) {
        TCBKeyPos &dstKey = spline.pKeys[i];
        dstKey.Position = float( uint32_t( pKeys[i].FrameIndex ) ) / lastFrame;
        dstKey.Vec4 = ToKeyPosition( pKeys[i], bSwapYZ );
        dstKey.T = pKeys[i].T;
        dstKey.C = pKeys[i].C;
        dstKey.B = pKeys[i].B;
    }
}

static void SetBlendKeys( TCBSplinePosition &position,
                          TCBSplineRotation &rotation,
                          const int32_t index,
                          const float keyTime,
                          CameraGame *pCamera ) {
    TCBKeyPos &positionKey = position.pKeys[index];
    positionKey.Position = keyTime;
    positionKey.Vec4 =
        Eigen::Vector4f( pCamera->getWorldPositionOffset().x(),
                         pCamera->getWorldPositionOffset().y(),
                         pCamera->getWorldPositionOffset().z(), 1.0f );
    positionKey.T = 1.0f;
    positionKey.C = -1.0f;
    positionKey.B = -1.0f;

    TCBKeyQuat &rotationKey = rotation.pKeys[index];
    rotationKey.Position = keyTime;
    rotationKey.Quat = Eigen::Quaternionf(
        Eigen::Matrix3f( pCamera->getCamToWorld().topLeftCorner<3, 3>() ) );
    rotationKey.T = 1.0f;
    rotationKey.C = -1.0f;
    rotationKey.B = -1.0f;
}

void SnapCameraToPathKey( const Path *pPath,
                          const int32_t keyIndex,
                          CameraGame *pCamera ) {
    // FUN_007d8530
    if ( pCamera == nullptr ) {
        return;
    }

    const int32_t positionIndex =
        Min( keyIndex, GetPathKeyCount( pPath->pArray0 ) - 1 );
    const int32_t rotationIndex =
        Min( keyIndex, GetPathKeyCount( pPath->pArray1 ) - 1 );
    const TCBKeyPos &positionKey = GetPathKeys( pPath->pArray0 )[positionIndex];
    const TCBKeyPos &rotationKey = GetPathKeys( pPath->pArray1 )[rotationIndex];

    const Eigen::Vector4f position( positionKey.Vec4[0], positionKey.Vec4[1],
                                    positionKey.Vec4[2],
                                    positionKey.Vec4[3] );
    const Eigen::Quaternionf rotation(
        rotationKey.Vec4[3], rotationKey.Vec4[0], rotationKey.Vec4[1],
        rotationKey.Vec4[2] );
    pCamera->setTransformFromQuaternion( position, rotation, true, 1.0f );
}

CameraTransition::CameraTransition()
    : CameraGame(),
      pSource( nullptr ),
      pDest( nullptr ),
      pOrigin( nullptr ),
      pLookTarget( nullptr ),
      pRotation( nullptr ),
      pCompletionCallback( nullptr ),
      pInterpolator( nullptr ),
      bPaused( false ) {
    // FUN_008e0050
}

CameraTransition::~CameraTransition() {}

void CameraTransition::initialize() {
    // FUN_008dff60
    pDest->initialize();

    pOrigin = new TCBSplinePosition();
    pLookTarget = new TCBSplinePosition();
    pRotation = new TCBSplineRotation();

    pOrigin->initialize( 0x10 );
    pLookTarget->initialize( 0x10 );
    pRotation->initialize( 0x10 );

    interpolationFactor = 0.0f;
}

bool CameraTransition::tick( float deltaTime, bool param_1, bool param_2 ) {
    // FUN_008e0ff0
    bool bApply = interpolationFactor < kOne;
    if ( !bPaused ) {
        interpolationFactor += deltaTime / time;
    }
    interpolationFactor = Min( interpolationFactor, kOne );

    if ( param_1 &&
         ( cameraTransition == CTT_Lerp || cameraTransition == CTT_Zoom ||
           cameraTransition == CTT_Path || cameraTransition == CTT_LerpRot ) ) {
        interpolationFactor = 0.0f;
        bApply = true;
    }

    pSource->tick( deltaTime, param_1, param_2 );
    pDest->tick( deltaTime, param_1, param_2 );

    if ( interpolationFactor < kOne || bApply ) {
        ease( interpolationFactor );
        if ( pSource != nullptr && pDest != nullptr &&
             interpolationFactor < kOne ) {
            applyInterpolation( interpolationFactor );
        }
    }

    if ( interpolationFactor == kOne ) {
        pDest->setPositionOffseted( worldPositionWithUnknownOffset );
        pDest->setTargetOffseted( worldTargetWithUnknownOffset );
        pDest->upVector = upVector;
        pDest->worldToCam = worldToCam;
        pDest->invWorldToCam = invWorldToCam;
        pDest->setFOVAndAspectRatio( aspectRatio, fov );
        upVector = pDest->upVector;

        GSCamera *pGSCamera = gpCamera;
        pGSCamera->pPrevCamera = pGSCamera->pActiveVehicleCamera;
        if ( pGSCamera->pActiveVehicleCamera != nullptr ) {
            pGSCamera->pActiveVehicleCamera->destroy();
        }
        pGSCamera->bFinishedCamTransition = true;
        pGSCamera->pActiveVehicleCamera = static_cast<CameraVehicle *>( pDest );
        pDest->initialize();
        pGSCamera->pActiveVehicleCamera->markAsInitialized();
        pGSCamera->bUpdatedView = false;

        if ( pCompletionCallback != nullptr ) {
            pCompletionCallback( pCallbackArg0, callbackArg1 );
            pCompletionCallback = nullptr;
        }
    }

    return true;
}

void CameraTransition::destroy() {
    // FUN_008dfd40
    delete pRotation;
    delete pOrigin;
    delete pLookTarget;
    pRotation = nullptr;
    pOrigin = nullptr;
    pLookTarget = nullptr;
}

void CameraTransition::begin( float transitionTime, int32_t transitionType ) {
    // FUN_008e0c60
    time = transitionTime;
    cameraTransition = transitionType;
    interpolationFactor = 0.0f;

    if ( transitionType == CTT_Zoom ) {
        const TCBKeyFrame &positionKey = transitionPath.pKeyFrames[0];
        const TCBKeyFrame &targetKey = transitionPath.pKeyFramesTarget[0];
        setPosition( ToKeyPosition( positionKey, transitionPath.bSwapYZ ) );
        setTarget( ToKeyPosition( targetKey, transitionPath.bSwapYZ ) );
        updateMatrix( worldPosition.head<3>(), upVector.head<3>(),
                      worldTarget.head<3>() );
        loadZoomKeys();
        return;
    }

    if ( transitionType == CTT_Path ) {
        SnapCameraToPathKey( transitionPath.pPath, 0, this );
        loadPathKeys();
        return;
    }

    if ( transitionType == CTT_LerpRot ||
         transitionType == CTT_LerpRotInterpol ) {
        pOrigin->NumKeys = 2;
        pRotation->NumKeys = 2;
        SetBlendKeys( *pOrigin, *pRotation, 0, 0.0f, pSource );
        SetBlendKeys( *pOrigin, *pRotation, 1, transitionTime, pDest );
        pOrigin->compute();
        pRotation->compute();

        setPositionOffseted( pSource->worldPositionWithUnknownOffset );
        setTargetOffseted( pSource->worldTargetWithUnknownOffset );
        upVector = pSource->upVector;
        updateMatrix( worldPosition.head<3>(), upVector.head<3>(),
                      worldTarget.head<3>() );
    } else if ( transitionType == CTT_Custom && pInterpolator != nullptr ) {
        pInterpolator->initialize( this );
    }
}

float CameraTransition::ease( float t ) {
    // FUN_008dfc60
    switch ( cameraTransition ) {
    case CTT_LerpInterpol:
    case CTT_LerpRotInterpol:
        return ( cosf( ( t + kOne ) * kPI ) + kOne ) * 0.5f;
    case CTT_EaseIn:
        return ( cosf( ( powf( t, gCameraEaseInExponent ) + kOne ) * kPI ) +
                 kOne ) *
               0.5f;
    case CTT_EaseOut:
        return kOne -
               ( cosf( ( powf( kOne - t, gCameraEaseOutExponent ) + kOne ) *
                       kPI ) +
                 kOne ) *
                   0.5f;
    case CTT_Custom:
        if ( pInterpolator != nullptr ) {
            pInterpolator->ease( t );
        }
        return t;
    default:
        return t;
    }
}

void CameraTransition::applyInterpolation( float rawT ) {
    // FUN_008e00a0
    const float t = ease( rawT );

    switch ( cameraTransition ) {
    case CTT_Zoom: {
        setPosition( pOrigin->evaluate( t ) );
        setTarget( pLookTarget->evaluate( t ) );
        setFOV( ( kOne - t ) * pSource->fov + t * pDest->fov );
        upVector = Eigen::Vector4f( 0.0f, 1.0f, 0.0f, 0.0f );
        updateMatrix( worldPosition.head<3>(), upVector.head<3>(),
                      worldTarget.head<3>() );
        break;
    }
    case CTT_Path:
    case CTT_LerpRot:
    case CTT_LerpRotInterpol: {
        const float u = transitionPath.bInvert ? kOne - t : t;
        const Eigen::Vector4f position = pOrigin->evaluate( u );
        const Eigen::Quaternionf rotation = pRotation->evaluate( u );
        setFOV( ( kOne - u ) * pSource->fov + u * pDest->fov );
        setPositionOffseted( position );
        setTarget( Eigen::Vector4f( 0.0f, 0.0f, kOne, 0.0f ) );
        upVector = Eigen::Vector4f( 0.0f, kOne, 0.0f, 0.0f );
        setTransformFromQuaternion( worldPosition, rotation,
                                    cameraTransition == CTT_Path, kOne );
        break;
    }
    case CTT_Custom:
        if ( pInterpolator != nullptr ) {
            pInterpolator->apply( this, rawT );
        }
        break;
    default: {
        const float s = kOne - t;
        setFOV( t * pDest->fov + s * pSource->fov );
        aspectRatio = t * pDest->aspectRatio + s * pSource->aspectRatio;
        invAspectRatio = kOne / aspectRatio;

        const Eigen::Vector4f origin = GetStreamOrigin();
        Eigen::Vector4f target = ( pDest->worldTarget - origin ) * t +
                                 ( pSource->worldTarget - origin ) * s;
        target.w() = kOne;
        setTargetOffseted( target );

        Eigen::Vector4f position = ( pDest->worldPosition - origin ) * t +
                                   ( pSource->worldPosition - origin ) * s;
        position.w() = kOne;
        setPositionOffseted( position );

        updateMatrix( worldPosition.head<3>(), upVector.head<3>(),
                      worldTarget.head<3>() );
        break;
    }
    }
}

void CameraTransition::loadZoomKeys() {
    // FUN_008e08b0
    LoadKeyFrames( *pOrigin, transitionPath.pKeyFrames,
                   transitionPath.NumKeyFrames, transitionPath.bSwapYZ );
    LoadKeyFrames( *pLookTarget, transitionPath.pKeyFramesTarget,
                   transitionPath.NumKeyFramesTarget, transitionPath.bSwapYZ );
    pOrigin->compute();
    pLookTarget->compute();
}

void CameraTransition::loadPathKeys() {
    // FUN_008e05d0
    const Path *pPath = transitionPath.pPath;
    const float duration =
        *reinterpret_cast<const float *>( &pPath->field_0x8 );

    const int32_t numPositionKeys = GetPathKeyCount( pPath->pArray0 );
    const TCBKeyPos *pPositionKeys = GetPathKeys( pPath->pArray0 );
    pOrigin->NumKeys = numPositionKeys;
    for ( int32_t i = 0; i < numPositionKeys; i++ ) {
        const TCBKeyPos &key = pPositionKeys[i];
        TCBKeyPos &dstKey = pOrigin->pKeys[i];
        dstKey.Position = key.Position / duration;
        dstKey.Vec4 = Eigen::Vector4f( key.Vec4[0], key.Vec4[1], key.Vec4[2],
                                       key.Vec4[3] );
        dstKey.T = key.T;
        dstKey.C = key.C;
        dstKey.B = key.B;
    }

    const int32_t numRotationKeys = GetPathKeyCount( pPath->pArray1 );
    const TCBKeyPos *pRotationKeys = GetPathKeys( pPath->pArray1 );
    pRotation->NumKeys = numRotationKeys;
    for ( int32_t i = 0; i < numRotationKeys; i++ ) {
        const TCBKeyPos &key = pRotationKeys[i];
        TCBKeyQuat &dstKey = pRotation->pKeys[i];
        dstKey.Position = key.Position / duration;
        dstKey.Quat = Eigen::Quaternionf( key.Vec4[3], key.Vec4[0],
                                          key.Vec4[1], key.Vec4[2] );
        dstKey.T = key.T;
        dstKey.C = key.C;
        dstKey.B = key.B;
    }

    pOrigin->compute();
    pRotation->compute();

    if ( time < 0.0f ) {
        time = duration;
    }
}

void CameraTransition::setCameras( CameraGame *pSrc, CameraGame *pDst ) {
    pSource = pSrc;
    pDest = pDst;
}
