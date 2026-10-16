#include "shared.h"
#include "camera_showcase.h"

#include "config/gs_config.h"
#include "game/gm_base.h"
#include "gs_timer.h"

#include "input/gs_device.h"
#include "input/input_device.h"

#include "flash/gs_flash.h"

#include "game/freeride/mng_photo_mode.h"
#include "game/shop/gm_shop.h"
#include "game/vehicle/car_state.h"
#include "game/vehicle/gs_car_state.h"
#include "game/vehicle/vehicle_base.h"
#include "gs_camera.h"
#include "physics/gs_physics.h"
#include "physics/vehicle/physics_vehicle.h"
#include "core/maths_helper.h"
#include "render/postfx/gs_postfx.h"
#include "render/postfx/effects/postfx_radial_blur.h"
#include "world/streaming_manager.h"

#include <vector>

static float FLOAT_00fab354 = 0.000099999997f; // DAT_00fab354
static float FLOAT_00fab358 = 0.5f;            // DAT_00fab358
static float FLOAT_00fab35c = 10.0f;           // DAT_00fab35c
static float FLOAT_00fab360 = 0.3f;            // DAT_00fab360
static float FLOAT_00fab364 = 6.0f;            // DAT_00fab364
static float DAT_016a2c90 = 0.0f;              // DAT_016a2c90
static float DAT_016a2c94 = 0.0f;              // DAT_016a2c94
static float FLOAT_00fab36c = 20.0f;           // DAT_00fab36c
static float FLOAT_00fab368 = 20.0f;           // DAT_00fab368
static float FLOAT_00fab370 = 0.07f;           // DAT_00fab370
static float FLOAT_00fab374 = 1.0f;            // DAT_00fab374
static float FLOAT_00fab378 = 0.18f;           // DAT_00fab378
static float gShowcaseMinHeight = 0.0f;        // DAT_016a2c8c

static constexpr float kDomeLengthFactor = 2.5f;     // FLOAT_00fab348
static constexpr float kDomeWidthFactor = 2.5f;      // FLOAT_00fab344
static constexpr float kDomeHeightFactorBike = 2.5f; // FLOAT_00fab33c
static constexpr float kDomeHeightFactor = 1.5f;     // FLOAT_00fab340

inline bool IsShowcaseInputCaptured() {
    // FUN_008d80b0
    if ( gpActiveGameMode->hasRequestedExit() ) {
        return true;
    }

    if ( gpActiveGameMode->getCurrentGameMode() == GM_Shop ) {
        ShopLogic *pShopLogic =
            static_cast<const GMShop *>( gpActiveGameMode )->getShopLogic();
        if ( pShopLogic != nullptr &&
             pShopLogic->getType() == ShopLogic::SLT_Paint &&
             gpMngColorPalette != nullptr ) {
            const FlashRECT &viewport = gpMngColorPalette->getClipViewport();
            if ( gEdFlash.Input.MouseCoordinates[0] <
                 viewport.Width + viewport.X ) {
                return true;
            }
        }
    }

    if ( gpActiveGameMode->getCurrentGameMode() != GM_FreeRide ) {
        return false;
    }

    return ( gpMngPhotoMode != nullptr ) && gpMngPhotoMode->isFilterPopupOpen();
}

static bool IsBikeVehicle( const PhysicsVehicle *pVehicle ) {
    CarState *pCar = ( gpCarState != nullptr )
                         ? gpCarState->findCarState( pVehicle )
                         : nullptr;
    VehicleBase *pInstance =
        ( pCar != nullptr ) ? pCar->getVehicleInstance() : nullptr;
    return pInstance != nullptr &&
           ( pInstance->getType() == VehicleBase::eType::VBT_Bike ||
             pInstance->getType() == VehicleBase::eType::VBT_TrafficBike );
}

CameraShowcase::CameraShowcase()
    : CameraGame(),
      mutex(),
      pTarget( nullptr ),
      rollAngle( 0.0f ),
      translation( Eigen::Vector4f::Zero() ),
      offset( Eigen::Vector4f::Zero() ),
      angles( -0.5f, -0.5f, 0.0f, 0.0f ),
      cursorType( 0 ),
      mouseInput( 0.0f ),
      distanceToTarget( 5.0f ),
      maxDistance( 5000.0f ),
      minDistance( 0.18f ),
      minRotAngle( -1.0f ),
      maxRotAngle( 0.1f ),
      floorHeight( -3.4028235e+38f ),
      flagsShowcase( 0 ),
      pPhysicsBoundingSphere( nullptr ),
      pTargetCarState( nullptr ),
      pTargetPhysicsVehicle( nullptr ),
      lastTranslation( Eigen::Vector4f::Zero() ),
      lastAngles( Eigen::Vector4f::Zero() ),
      lastDistanceToTarget( 0.0f ),
      lastPlacementPosition( Eigen::Vector4f::Zero() ),
      bInputEnabled( false ),
      bCanMoveCamera( false ),
      bAllowRoll( false ) {
    // FUN_008d8f60
    Eigen::Vector4f local_20( 0, 0, 0, 1 );
    pPhysicsBoundingSphere =
        gpPhysics->createSphere( 2, local_20, 1.0f, 1.0f, 1.0f, false );
}

CameraShowcase::~CameraShowcase() {
    // FUN_008d8130
    if ( pPhysicsBoundingSphere != nullptr ) {
        gpPhysics->destroyRigidBody( pPhysicsBoundingSphere );
        pPhysicsBoundingSphere = nullptr;
    }
    mutex.terminate();
}

void CameraShowcase::initialize() {
    // FUN_008d8030
    bInputEnabled = true;
    cursorType = 3;
    mouseInput = 0.0f;
}

bool CameraShowcase::tick( float deltaTime, bool param_1, bool param_2 ) {
    // FUN_008da680
    ScopedMutexLock lock( &mutex );

    FUN_008d7fb0();
    if ( bInputEnabled && gpActiveGameMode != nullptr &&
         !gpActiveGameMode->isMessageBoxVisible() ) {
        updateMouse( gpTimer->SystemDeltaTime );
    }

    Eigen::Vector4f local_f0;
    Eigen::Vector4f local_100;
    FUN_008da540( local_f0, local_100 );
    float local_134 = distanceToTarget * FLOAT_00fab370;
    if ( FLOAT_00fab374 < local_134 ) {
        local_134 = FLOAT_00fab374;
    }
    if ( local_134 < FLOAT_00fab378 ) {
        local_134 = FLOAT_00fab378;
    }
    FUN_008d90e0( &local_f0, param_2, local_134 );

    Eigen::Vector4f local_130 =
        worldTargetWithUnknownOffset - worldPositionWithUnknownOffset;

    float local_130sqrt = local_130.norm();
    local_134 = local_130sqrt * FLOAT_00fab370;
    if ( FLOAT_00fab374 < local_130sqrt * FLOAT_00fab370 ) {
        local_134 = FLOAT_00fab374;
    }
    if ( local_134 < FLOAT_00fab378 ) {
        local_134 = FLOAT_00fab378;
    }

    Eigen::Vector4f auVar12;
    auVar12.y() = (float)-(int32_t)( ( local_f0.y() - local_100.y() ) *
                                         ( local_f0.y() - local_100.y() ) <=
                                     9.9999994e-11f );
    auVar12.x() = (float)-(int32_t)( ( local_f0.x() - local_100.x() ) *
                                         ( local_f0.x() - local_100.x() ) <=
                                     9.9999994e-11f );
    auVar12.z() = (float)-(int32_t)( ( local_f0.z() - local_100.z() ) *
                                         ( local_f0.z() - local_100.z() ) <=
                                     9.9999994e-11f );
    auVar12.w() = (float)-(int32_t)( ( local_f0.w() - local_100.w() ) *
                                         ( local_f0.w() - local_100.w() ) <=
                                     9.9999994e-11f );

    // Cheap movmskps replacement (TODO: x platform SIMD support)
    int32_t iVar4 = 0;
    if ( auVar12.x() < 0.0f ) {
        iVar4 |= 1;
    }
    if ( auVar12.y() < 0.0f ) {
        iVar4 |= 2;
    }
    if ( auVar12.z() < 0.0f ) {
        iVar4 |= 4;
    }
    if ( auVar12.w() < 0.0f ) {
        iVar4 |= 8;
    }
    if ( iVar4 == 0xf ) {
        local_f0.x() += 1.0f;
        local_f0.y() += 0.0f;
        local_f0.z() += 0.0f;
    }

    float fVar14 = 1.0f;
    local_f0.w() = 1.0f;
    setPosition( local_f0 );
    setTarget( local_100 );

    Eigen::Vector4f auVar13 = worldTarget - worldPosition;
    auVar13.normalize();

    Eigen::Vector4f local_a0 = auVar13;
    local_a0.w() = 0.0f;

    float local_120 = rollAngle;

    Matrix4x4f_t pTVar1 = Matrix4x4f_t::Zero();
    pTVar1 = MatrixRotateZ( pTVar1, local_a0, local_120 );

    Matrix4x4f_t TStack_e0 = Matrix4x4f_t::Identity();
    Matrix4x4f_t local_90 = TStack_e0 * pTVar1;

    upVector = local_90.row( 1 );

    updateMatrix( worldPosition.head<3>(), upVector.head<3>(),
                  worldTarget.head<3>() );

    updateRadialBlurCenter();

    nearPlane = local_134;
    farPlane = 5000.0f;

    gpCamera->setFarPlane( 5000.0f );
    gpCamera->setNearPlane( nearPlane );
    gpCamera->setInteriorFarPlane( interiorFarPlane );
    gpCamera->setInteriorNearPlane( interiorNearPlane );
    gpCamera->setEnableDepthAware( true );
    gpCamera->setEnableDepthAwareInterior( true );

    return true;
}

void CameraShowcase::destroy() {
    // FUN_008d8010
    pTargetCarState = nullptr;
    floorHeight = -3.4028235e+38f;
}

void CameraShowcase::lookAt( Eigen::Vector4f *param_1 ) {
    // FUN_008d8810
    ScopedMutexLock lock( &mutex );
    pTarget = param_1;
    translation = Eigen::Vector4f::Zero();
    lastTranslation = Eigen::Vector4f::Zero();
    rollAngle = 0.0f;
}

void CameraShowcase::updateOrbitFromPosition(
    const Eigen::Vector4f &worldPosition, const bool bWriteToPreviousSlots ) {
    // FUN_008d8540
    Eigen::Vector4f toCamera = worldPosition - offset;
    if ( pTarget != nullptr ) {
        toCamera -= *pTarget;
    }

    float *pDistance =
        bWriteToPreviousSlots ? &lastDistanceToTarget : &distanceToTarget;
    Eigen::Vector4f *pAngles = bWriteToPreviousSlots ? &lastAngles : &angles;

    const float distance = toCamera.norm();
    const Eigen::Vector4f direction = toCamera / distance;

    *pDistance = distance;
    if ( minDistance > distance ) {
        *pDistance = minDistance;
    } else if ( distance > maxDistance ) {
        *pDistance = maxDistance;
    }

    const float yaw = atan2f( direction.x(), direction.z() );

    Matrix4x4f_t unyaw = MakeRotationMatY( -yaw );
    const Eigen::Vector4f flattened = TDUMat4x4MulVec( unyaw, direction );

    float pitch = -atan2f( flattened.y(), flattened.z() );

    const float maxPitch =
        ( ( flagsShowcase >> 10 ) & 1 ) != 0 ? 1.0f : maxRotAngle;
    if ( minRotAngle > pitch ) {
        pitch = minRotAngle;
    } else if ( pitch > maxPitch ) {
        pitch = maxPitch;
    }

    *pAngles = Eigen::Vector4f( pitch, yaw, 0.0f, 0.0f );
}

bool CameraShowcase::placeAt( const Eigen::Vector4f &worldPosition,
                              const bool bSnapToTarget ) {
    // FUN_008da4b0
    setPosition( worldPosition );
    updateOrbitFromPosition( worldPosition, false );

    if ( bSnapToTarget ) {
        lastPlacementPosition = worldPosition;
        FUN_008d7fb0();
        tick( 0.0f, false, true );
    }

    Eigen::Vector4f resolvedPosition = worldPosition;
    return FUN_008d90e0( &resolvedPosition, true, 0.18f );
}

void CameraShowcase::FUN_008da540( Eigen::Vector4f &param_2,
                                   Eigen::Vector4f &param_3 ) {
    // FUN_008da540
    Matrix4x4f_t local_50;
    applyRotation( local_50 );

    Eigen::Vector4f local_70 =
        TDUMat4x4MulVec( local_50, Eigen::Vector4f( 0, 0, 1, 0 ) );
    param_2 = local_70;

    local_70.w() = distanceToTarget;
    local_70 = param_2 * local_70.w();
    param_2 = local_70;

    local_70 += translation + offset;
    param_2 = local_70;

    Eigen::Vector4f target =
        ( pTarget == nullptr ) ? Eigen::Vector4f( 0, 0, 0, 1 ) : *pTarget;

    param_2 = local_70 + target;

    FUN_008d8750( param_3 );
}

float CameraShowcase::sweepCameraSegment( Eigen::Vector4f &out,
                                          const Eigen::Vector4f &from,
                                          const Eigen::Vector4f &to ) {
    // FUN_008d88f0
    const Eigen::Vector4f delta = to - from;
    const float distance = delta.norm();
    if ( distance <= 0.0f ) {
        return -1.0f; // FLOAT_00f41664
    }
    const Eigen::Vector4f direction = delta / distance;

    gpPhysics->moveSphereBody( pPhysicsBoundingSphere, from );

    std::vector<CameraSegmentHit> hits;
    gpPhysics->castCameraSegment( from, to, hits );

    const Matrix4x4f_t *pWorldToCar = nullptr;
    PhysicsVehicle *pVehicle = (PhysicsVehicle *)pTargetPhysicsVehicle;
    if ( pVehicle != nullptr ) {
        pWorldToCar = &pVehicle->getInvChassisMatrix();
    }
    const Eigen::Vector4f shiftOrigin = GSPhysics::GetShiftOrigin();

    const float limit = distance + 1.0f;
    float furthestHit = -99.0f; // DAT_00f41e30
    int32_t numAccepted = 0;

    for ( const CameraSegmentHit &hit : hits ) {
        const uint32_t surface = hit.SurfaceClass;
        if ( surface == TDUSurface::Vehicle || surface == 0xbu ||
             surface == TDUSurface::RoadMesh || surface == TDUSurface::Paved ||
             ( surface == 8u && hit.Mass > 0.0f ) ) {
            continue;
        }

        if ( pWorldToCar != nullptr ) {
            Eigen::Vector4f pointInCar = hit.Point - shiftOrigin;
            pointInCar.w() = 1.0f;
            pointInCar = TDUMat4x4MulVec( *pWorldToCar, pointInCar );

            const Eigen::Vector4f normalInCar =
                TDUMat4x4MulVec( *pWorldToCar, hit.Normal );

            if ( normalInCar.y() > 0.75f && pointInCar.y() < 0.0f ) {
                continue;
            }
        }

        numAccepted++;

        const float alongSegment = hit.Fraction * distance;
        if ( furthestHit < alongSegment ) {
            furthestHit = alongSegment;
        }
    }

    if ( numAccepted != 0 && furthestHit < limit ) {
        out = from + direction * furthestHit;
        out.w() = 1.0f;
        return furthestHit;
    }

    return -1.0f; // FLOAT_00f41664
}

bool CameraShowcase::FUN_008d90e0( Eigen::Vector4f *param_2,
                                   bool param_3,
                                   float param_4 ) {
    // FUN_008d90e0
    if ( param_2 == nullptr ) {
        return false;
    }

    if ( pPhysicsBoundingSphere == nullptr ) {
        Eigen::Vector4f local_d30( 0.0f, 0.0f, 0.0f, 1.0f );
        pPhysicsBoundingSphere =
            gpPhysics->createSphere( 2, local_d30, 1.0f, 1.0f, 1.0f, false );
        return true;
    }

    float minHeight = -3.4028235e+38f; // DAT_00f41790

    if ( ( ( flagsShowcase & 1 ) != 0 ) &&
         ( pTargetCarState == nullptr || !pTargetCarState->isIndoor() ) ) {
        minHeight = gShowcaseMinHeight;

        Eigen::Vector4f probePosition = *param_2;
        probePosition.y() += 1000.0f; // DAT_00f41718

        float surfaceHeight = kNoSurfaceSentinel;
        Eigen::Vector4f surfaceNormal = Eigen::Vector4f::Zero();
        gpPhysics->probeSurface( probePosition, false, surfaceHeight,
                                 surfaceNormal );

        const float heightmapHeight =
            SampleHeightmapSurface( *param_2, nullptr );

        float radius = param_4;
        if ( 0.18f > radius ) {
            radius = 0.18f;
        } else if ( radius > 1.0f ) {
            radius = 1.0f;
        }
        const float clearance =
            ( radius - 0.18f ) * 1.2195122f * ( 2.0f - 0.5f ) + 0.5f;

        if ( surfaceHeight > kNoSurfaceSentinel &&
             clearance + surfaceHeight > gShowcaseMinHeight ) {
            minHeight = clearance + surfaceHeight;
        } else if ( heightmapHeight > kNoSurfaceSentinel &&
                    clearance + heightmapHeight > gShowcaseMinHeight ) {
            minHeight = clearance + heightmapHeight;
        }
    }

    if ( ( ( flagsShowcase >> 7 ) & 1 ) != 0 && minHeight < floorHeight ) {
        minHeight = floorHeight + 0.3f;
    }

    const Eigen::Vector4f shiftOrigin = GSPhysics::GetShiftOrigin();

    if ( ( ( flagsShowcase >> 1 ) & 1 ) != 0 ) {
        const Eigen::Vector4f shiftedPosition = *param_2 - shiftOrigin;
        for ( PhysicsVehicle *it = gpPhysics->getVehiclesBegin();
              it != gpPhysics->getVehiclesEnd(); ++it ) {
            if ( !it->isActive() ) {
                continue;
            }
            if ( it == pTargetPhysicsVehicle &&
                 ( ( flagsShowcase >> 8 ) & 1 ) == 0 ) {
                continue;
            }

            const float heightFactor =
                IsBikeVehicle( it ) ? kDomeHeightFactorBike : kDomeHeightFactor;
            const float domeHeight =
                it->getCameraDomeHeight( shiftedPosition, kDomeLengthFactor,
                                         kDomeWidthFactor, heightFactor );
            if ( minHeight < domeHeight ) {
                minHeight = domeHeight;
            }
        }
    }

    if ( param_2->y() <= minHeight ) {
        param_2->y() = minHeight;
    }

    if ( ( ( flagsShowcase >> 2 ) & 1 ) != 0 &&
         pPhysicsBoundingSphere != nullptr ) {
        if ( pTarget == nullptr ) {
            return false;
        }

        const Eigen::Vector4f cameraPosition = *param_2;
        const Eigen::Vector4f lookAt = *pTarget + translation;

        Eigen::Vector4f hit = lookAt;
        if ( sweepCameraSegment( hit, lookAt, cameraPosition ) > 0.0f ) {
            *param_2 = hit;

            const Eigen::Vector4f beyondHit =
                hit + Normalize( hit - lookAt ) * 100.0f;
            Eigen::Vector4f backHit = hit;
            if ( sweepCameraSegment( backHit, beyondHit, hit ) > 0.0f ) {
                *param_2 = backHit;

                Eigen::Vector4f unused;
                if ( sweepCameraSegment( unused, backHit, cameraPosition ) <
                     0.0f ) {
                    *param_2 = cameraPosition;
                }
            }
        }
    }

    if ( ( ( flagsShowcase >> 5 ) & 1 ) != 0 &&
         pPhysicsBoundingSphere != nullptr ) {
        if ( pTarget == nullptr ) {
            return false;
        }

        gpPhysics->moveSphereBody( pPhysicsBoundingSphere, *param_2 );

        std::vector<CameraPhantomOverlap> overlaps;
        gpPhysics->queryPhantomOverlaps( pPhysicsBoundingSphere, *param_2, 1.0f,
                                         overlaps );

        for ( const CameraPhantomOverlap &overlap : overlaps ) {
            if ( pushOutOfOverlap( *param_2, overlap ) ) {
                break;
            }
        }
    }

    PhysicsVehicle *pVehicle = (PhysicsVehicle *)pTargetPhysicsVehicle;
    if ( ( ( flagsShowcase >> 6 ) & 1 ) != 0 && pTargetCarState != nullptr &&
         pTargetCarState->isIndoor() && pPhysicsBoundingSphere != nullptr &&
         pVehicle != nullptr && pTarget != nullptr ) {
        const Matrix4x4f_t &carToWorld = pVehicle->getPhysicsMatrix();
        const Matrix4x4f_t &worldToCar = pVehicle->getInvChassisMatrix();

        auto toCarSpace = [&]( const Eigen::Vector4f &worldPoint ) {
            Eigen::Vector4f shifted = worldPoint - shiftOrigin;
            shifted.w() = 1.0f;
            return TDUMat4x4MulVec( worldToCar, shifted );
        };

        Eigen::Vector4f cameraInCar = toCarSpace( *param_2 );
        if ( cameraInCar.y() < 0.0f ) {
            cameraInCar.y() = 0.0f;
            *param_2 = TDUMat4x4MulVec( carToWorld, cameraInCar ) + shiftOrigin;
            param_2->w() = 1.0f;
        }

        const Eigen::Vector4f lookAt = *pTarget + translation;
        gpPhysics->moveSphereBody( pPhysicsBoundingSphere, lookAt );

        std::vector<CameraSegmentHit> hits;
        gpPhysics->castCameraSegment( lookAt, *param_2, hits );

        Eigen::Vector4f segment = *param_2 - lookAt;
        segment.w() = 0.0f;
        const float length = segment.norm();
        const float limit = length + 1.0f;
        float nearestHit = limit;
        int32_t numAccepted = 0;

        for ( const CameraSegmentHit &hit : hits ) {
            if ( hit.SurfaceClass != TDUSurface::RoadMesh ) {
                continue;
            }

            const Eigen::Vector4f pointInCar = toCarSpace( hit.Point );
            const Eigen::Vector4f normalInCar =
                TDUMat4x4MulVec( worldToCar, hit.Normal );
            if ( normalInCar.y() > 0.5f && pointInCar.y() < 0.0f ) {
                continue;
            }

            numAccepted++;

            const float alongSegment = hit.Fraction * length;
            if ( alongSegment < nearestHit ) {
                nearestHit = alongSegment;
            }
        }

        if ( numAccepted != 0 && nearestHit < limit ) {
            cameraInCar =
                toCarSpace( lookAt + segment * ( nearestHit / limit ) );
        }

        *param_2 = TDUMat4x4MulVec( carToWorld, cameraInCar ) + shiftOrigin;
        param_2->w() = 1.0f;
    }

    return true;
}

bool CameraShowcase::pushOutOfOverlap( Eigen::Vector4f &position,
                                       const CameraPhantomOverlap &overlap ) {
    // FUN_008d8cb0
    if ( overlap.Shape == eCameraOverlapShape::Other ) {
        return false;
    }

    Eigen::Vector4f center = overlap.Center;
    if ( overlap.Shape == eCameraOverlapShape::Capsule ) {
        center.y() = position.y();
    }

    const Eigen::Vector4f toCamera = position - center;
    const float distance = toCamera.norm();
    if ( distance >= overlap.Radius || distance == 0.0f ) {
        return false;
    }

    position = center + toCamera * ( overlap.Radius / distance );
    return true;
}

void CameraShowcase::FUN_008d7fb0() {
    // FUN_008d7fb0
    lastTranslation = translation;
    lastDistanceToTarget = distanceToTarget;
    lastAngles = angles;
}

void CameraShowcase::updateRadialBlurCenter() {
    // FUN_008d8180
    PhysicsVehicle *pVehicle = gpCamera->getTargetPhysEntity();
    if ( pVehicle == nullptr || !pVehicle->isActive() ) {
        return;
    }

    PostFXStep *pStep =
        ( gpPostFX != nullptr )
            ? gpPostFX->getEffect<ePostFXStep::PFXS_RadialBlur>()
            : nullptr;
    PostFXNodeRadialBlur *pNode =
        ( pStep != nullptr )
            ? static_cast<PostFXStepRadialBlur *>( pStep )->pNode
            : nullptr;
    if ( pNode == nullptr ) {
        return;
    }

    const Matrix4x4f_t carMatrix = pVehicle->getPhysicsMatrix();

    Matrix4x4f_t camToWorld;
    MakeWorldToCamMatrix( camToWorld, worldPosition.head<3>(),
                          upVector.head<3>(), worldTarget.head<3>() );

    const Eigen::Vector4f camRight = camToWorld.row( 0 ).transpose();
    const Eigen::Vector4f camUp = camToWorld.row( 1 ).transpose();
    const Eigen::Vector4f camBack = camToWorld.row( 2 ).transpose();

    const Eigen::Vector4f carUp = carMatrix.row( 1 ).transpose();
    const Eigen::Vector4f carForward = carMatrix.row( 2 ).transpose();

    const float angleHorizontal =
        -ProjectedAngle( camBack * -1.0f, carForward, camRight );
    const float angleVertical = ProjectedAngle( camUp, carUp, camBack );

    pNode->setCenter( 0.5f - angleHorizontal, 0.5f - angleVertical );
}

void CameraShowcase::updateMouse( float param_1 ) {
    // FUN_008da410
    updateMouseCursor();
    updateAngles( param_1 );

    if ( bCanMoveCamera ) {
        updateTranslation( param_1 );
    }

    if ( bAllowRoll ) {
        updateRoll( param_1 );
    }
}

void CameraShowcase::updateMouseCursor() {
    // FUN_008d84a0
    const MouseState &mouse = gpDevice->getMouseState();

    bool bVar1 = false;
    bool bVar2 = false;
    bool bVar3 = false;

    if ( ( mouse.pChannelButton0 == nullptr ) ||
         ( ( ( mouse.pChannelButton0 )->Flags & 0x10 ) == 0 ) ) {
        bVar3 = false;
    } else {
        bVar3 = true;
    }
    if ( ( mouse.pChannelButton2 == nullptr ) ||
         ( ( ( mouse.pChannelButton2 )->Flags & 0x10 ) == 0 ) ) {
        bVar2 = false;
    } else {
        bVar2 = true;
    }
    if ( ( mouse.pChannelButton1 == nullptr ) ||
         ( ( ( mouse.pChannelButton1 )->Flags & 0x10 ) == 0 ) ) {
        bVar1 = false;
    } else {
        bVar1 = true;
    }

    if ( gpFlash->getMouse()->ActiveCursorType ==
         eFlashCursorType::FCT_Camera ) {
        if ( bVar1 ) {
            cursorType = 1;
            goto LAB_008d8503;
        }
        if ( bVar2 ) {
            cursorType = 2;
            goto LAB_008d8507;
        }
        if ( !bVar3 )
            goto LAB_008d8518;
        cursorType = 0;
    LAB_008d850b:
        if ( !bVar3 )
            goto LAB_008d8518;
    } else {
    LAB_008d8503:
        if ( !bVar1 ) {
        LAB_008d8507:
            if ( !bVar2 )
                goto LAB_008d850b;
        }
    }
    bVar3 = IsShowcaseInputCaptured();
    if ( !bVar3 ) {
        return;
    }

LAB_008d8518:
    cursorType = 3;
}

void CameraShowcase::updateAngles( float param_1 ) {
    // FUN_008d9d30
    const MouseState &mouse = gpDevice->getMouseState();

    float local_8 = getAngleForChannel( mouse.pChannelUp );
    float local_4 = getAngleForChannel( mouse.pChannelLeft );

    float fVar5 = 0.0f;
    float fVar6 = 0.0f;
    bool bVar3 = IsShowcaseInputCaptured();
    if ( bVar3 ) {
        DAT_016a2c90 = 0.0f;
    } else {
        if ( ( ( ( mouse.pChannelLeft == nullptr ) ||
                 ( mouse.pChannelUp == nullptr ) ) ||
               ( mouse.pChannelWheelUp == nullptr ) ) ||
             ( fVar5 = 1.0f,
               ( ( mouse.pChannelWheelUp )->Flags & 0x80 ) == 0 ) ) {
            fVar5 = 0.0f;
        }
        if ( ( ( mouse.pChannelLeft == nullptr ) ||
               ( mouse.pChannelUp == nullptr ) ) ||
             ( ( mouse.pChannelWheelDown == nullptr ||
                 ( fVar6 = 1.0f,
                   -1 < (char)( mouse.pChannelWheelDown )->Flags ) ) ) ) {
            fVar6 = 0.0f;
        }
        DAT_016a2c90 =
            ( fVar5 - fVar6 ) * FLOAT_00fab35c - FLOAT_00fab358 * DAT_016a2c94;
    }
    DAT_016a2c94 = DAT_016a2c90 * param_1 + DAT_016a2c94;
    if ( ( ( 0.0 < DAT_016a2c94 ) && ( DAT_016a2c94 < FLOAT_00fab354 ) ) ||
         ( ( DAT_016a2c94 < 0.0f &&
             ( 0.0f - FLOAT_00fab354 < DAT_016a2c94 ) ) ) ) {
        DAT_016a2c94 = 0.0f;
    } else if ( DAT_016a2c94 <= 0.0f ) {
        if ( ( DAT_016a2c94 < 0.0f ) && ( 0.0f < abs( DAT_016a2c94 ) ) ) {
            fVar5 = ( distanceToTarget / maxDistance ) * FLOAT_00fab364 *
                        abs( DAT_016a2c94 ) * param_1 * maxDistance * 0.02f +
                    distanceToTarget;
            distanceToTarget = fVar5;
            if ( maxDistance <= fVar5 && fVar5 != maxDistance ) {
                distanceToTarget = maxDistance;
            }
        }
    } else if ( ( 0.0f < abs( DAT_016a2c94 ) ) &&
                ( fVar5 = distanceToTarget -
                          ( distanceToTarget / maxDistance ) * FLOAT_00fab364 *
                              abs( DAT_016a2c94 ) * param_1 * maxDistance *
                              0.02f,
                  distanceToTarget = fVar5, fVar5 < minDistance ) ) {
        distanceToTarget = minDistance;
    }
    fVar5 = angles.x() - local_8;
    bool bVar4 = false;
    if ( 0.0f <= local_8 ) {
        bVar3 = fVar5 == minRotAngle;
        bVar4 = fVar5 < minRotAngle;
    } else if ( ( flagsShowcase >> 10 & 1 ) == 0 ) {
        bVar3 = maxRotAngle == fVar5;
        bVar4 = maxRotAngle < fVar5;
    } else {
        bVar3 = fVar5 == 1.0f;
        bVar4 = 1.0 < fVar5;
    }
    if ( !bVar4 && !bVar3 ) {
        angles.x() = fVar5;
    }
    angles.y() = angles.y() - local_4;
}

float CameraShowcase::getAngleForChannel( InputChannel *pChannel ) {
    const MouseState &mouse = gpDevice->getMouseState();

    bool bVar3 = IsShowcaseInputCaptured();
    if ( bVar3 ) {
        cursorType = 3;
    }

    float fVar5 = 0.0f;
    float local_4 = 0.0f;
    if ( cursorType == 0 && mouse.pChannelLeft != nullptr &&
         mouse.pChannelUp != nullptr ) {
        int32_t iVar1 = pChannel->field_0x2c;
        InputChannel *peVar2 = pChannel->pParent->getChannels();
        if ( ( peVar2[iVar1].Unknown == 1 ) &&
             ( ( peVar2[iVar1].Flags & 3 ) == 1 ) ) {
            fVar5 = peVar2[iVar1 + 1].InputValue - peVar2[iVar1].InputValue;
        } else {
            fVar5 = pChannel->InputValue;
        }

        if ( fVar5 != 0.0f ) {
            local_4 = fVar5 * FLOAT_00fab360;
        }
    }

    if ( -0.3f <= local_4 ) {
        if ( 0.3f < local_4 ) {
            local_4 = 0.3f;
        }
    } else {
        local_4 = -0.3f;
    }

    return local_4;
}

void CameraShowcase::updateTranslation( float param_1 ) {
    // FUN_008da1a0
    const MouseState &mouse = gpDevice->getMouseState();

    bool bVar3 = IsShowcaseInputCaptured();
    if ( bVar3 ) {
        cursorType = 3;
    }

    float fVar4 = 0.0f;
    if ( ( ( cursorType == 1 ) && ( mouse.pChannelLeft != nullptr ) ) &&
         ( mouse.pChannelUp != nullptr ) ) {
        int32_t iVar1 = mouse.pChannelUp->field_0x2c;
        InputChannel *peVar2 = mouse.pChannelUp->pParent->getChannels();
        if ( ( peVar2[iVar1].Unknown == 1 ) &&
             ( ( peVar2[iVar1].Flags & 3 ) == 1 ) ) {
            fVar4 = peVar2[iVar1 + 1].InputValue - peVar2[iVar1].InputValue;
        } else {
            fVar4 = mouse.pChannelUp->InputValue;
        }

        if ( fVar4 != 0.0f ) {
            float fVar5 =
                invWorldToCam.row( 1 )[1] * fVar4 * param_1 * FLOAT_00fab36c;
            float fVar6 =
                invWorldToCam.row( 1 )[2] * fVar4 * param_1 * FLOAT_00fab36c;
            float fVar7 =
                invWorldToCam.row( 1 )[3] * fVar4 * param_1 * FLOAT_00fab36c;

            translation.x() = translation.x() - invWorldToCam.row( 1 )[0] *
                                                    fVar4 * param_1 *
                                                    FLOAT_00fab36c;
            translation.y() = translation.y() - fVar5;
            translation.z() = translation.z() - fVar6;
            translation.w() = translation.w() - fVar7;
        }
    }

    bVar3 = IsShowcaseInputCaptured();
    if ( bVar3 ) {
        cursorType = 3;
    }

    if ( ( ( cursorType == 1 ) && ( mouse.pChannelLeft != nullptr ) ) &&
         ( mouse.pChannelUp != nullptr ) ) {
        int32_t iVar1 = mouse.pChannelLeft->field_0x2c;
        InputChannel *peVar2 = mouse.pChannelLeft->pParent->getChannels();
        if ( ( peVar2[iVar1].Unknown == 1 ) &&
             ( ( peVar2[iVar1].Flags & 3 ) == 1 ) ) {
            fVar4 = peVar2[iVar1 + 1].InputValue - peVar2[iVar1].InputValue;
        } else {
            fVar4 = mouse.pChannelLeft->InputValue;
        }

        if ( fVar4 != 0.0f ) {
            float fVar5 =
                invWorldToCam.row( 0 )[1] * fVar4 * param_1 * FLOAT_00fab36c;
            float fVar6 =
                invWorldToCam.row( 0 )[2] * fVar4 * param_1 * FLOAT_00fab36c;
            float fVar7 =
                invWorldToCam.row( 0 )[3] * fVar4 * param_1 * FLOAT_00fab36c;

            translation.x() = translation.x() + invWorldToCam.row( 0 )[0] *
                                                    fVar4 * param_1 *
                                                    FLOAT_00fab36c;
            translation.y() = translation.y() + fVar5;
            translation.z() = translation.z() + fVar6;
            translation.w() = translation.w() + fVar7;
        }
    }
    float fVar6 = ( distanceToTarget / maxDistance ) * 6.0f + 2.0f;
    fVar4 = translation.x();
    float fVar7 = 0.0f - fVar6;
    float fVar5 = fVar7;
    if ( ( fVar7 <= fVar4 ) && ( fVar5 = fVar4, fVar6 < fVar4 ) ) {
        fVar5 = fVar6;
    }
    translation.x() = fVar5;
    fVar4 = translation.y();
    fVar5 = fVar7;
    if ( ( fVar7 <= fVar4 ) && ( fVar5 = fVar4, fVar6 < fVar4 ) ) {
        fVar5 = fVar6;
    }
    translation.y() = fVar5;
    fVar4 = translation.z();
    if ( fVar7 <= fVar4 ) {
        if ( fVar4 <= fVar6 ) {
            translation.z() = fVar4;
            return;
        }
        translation.z() = fVar6;
        return;
    }
    translation.z() = fVar7;
}

void CameraShowcase::updateRoll( float param_1 ) {
    // FUN_008da0d0
    const MouseState &mouse = gpDevice->getMouseState();
    bool bVar3 = IsShowcaseInputCaptured();
    if ( bVar3 ) {
        cursorType = 3;
    }

    float fVar4 = 0.0f;
    if ( ( ( cursorType == 2 ) && ( mouse.pChannelLeft != nullptr ) ) &&
         ( mouse.pChannelUp != nullptr ) ) {
        int32_t iVar1 = mouse.pChannelLeft->field_0x2c;
        InputChannel *peVar2 = mouse.pChannelLeft->pParent->getChannels();
        if ( ( peVar2[iVar1].Unknown == 1 ) &&
             ( ( peVar2[iVar1].Flags & 3 ) == 1 ) ) {
            fVar4 = peVar2[iVar1 + 1].InputValue - peVar2[iVar1].InputValue;
        } else {
            fVar4 = mouse.pChannelLeft->InputValue;
        }
        if ( fVar4 == 0.0f ) {
            rollAngle = fVar4 * FLOAT_00fab368 * param_1 * 0.1f + rollAngle;
        }
    }
    fVar4 = -0.6f;
    if ( -0.6f <= rollAngle ) {
        fVar4 = 0.6f;
        if ( rollAngle <= 0.6f ) {
            fVar4 = rollAngle;
        }
    }
    rollAngle = fVar4;
}

void CameraShowcase::applyRotation( Matrix4x4f_t &matrix ) {
    // FUN_008da450
    matrix = Matrix4x4f_t::Identity();
    matrix = MatrixRotateX( matrix, angles.x() );
    matrix = MatrixRotateY( matrix, angles.y() );
}

void CameraShowcase::FUN_008d8750( Eigen::Vector4f &param_2 ) {
    // FUN_008d8750
    if ( pTarget == nullptr ) {
        param_2 = Eigen::Vector4f( 0, 0, 0, 1 ) + offset;
        return;
    }

    param_2 = *pTarget + offset + translation;
    param_2.w() = 1.0f;
}
