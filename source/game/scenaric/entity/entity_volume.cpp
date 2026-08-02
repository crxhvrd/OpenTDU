#include "shared.h"
#include "entity_volume.h"

#include "core/maths_helper.h"
#include "filesystem/binary.h"

#include <cstring>

static constexpr float kVolumeHalfExtent =
    0.5f; // (DAT_00f4186c / FLOAT_00f41674)
static constexpr float kVolumeEpsilon = 1.0e-5f; // DAT_00f41710

EntityVolume::EntityVolume()
    : Entity(),
      InverseTransform( Matrix4x4f_t::Identity() ),
      LocalTransform( Matrix4x4f_t::Identity() ),
      Type( VT_Plane ),
      InstanceFlags( 0 ) {
    // FUN_009da130
}

EntityVolume::~EntityVolume() {
    // FUN_00424120
}

void EntityVolume::initialize() {
    // FUN_007ea3c0
    Entity::initialize();
    modelMatrix = InverseTransform.inverse();
}

void EntityVolume::unloadResources() {
    // FUN_007e9c00
    // NOP
}

void EntityVolume::deserialize( BinaryReader *param_2 ) {
    // FUN_007e9f20
    Entity::deserialize( param_2 );

    std::memcpy( InverseTransform.data(), param_2->pCursor,
                 sizeof( float ) * 16 );
    param_2->pCursor += sizeof( float ) * 16;

    Type = *reinterpret_cast<int32_t *>( param_2->pCursor );
    param_2->pCursor += sizeof( int32_t );
}

void EntityVolume::propagateTransform( Matrix4x4f_t &transformMatrix,
                                       bool param_1 ) {
    // FUN_007e9e30
    Entity::propagateTransform( transformMatrix, param_1 );
    InverseTransform = transformMatrix.inverse();
}

void EntityVolume::propagatePosition( Eigen::Vector4f &position,
                                      bool param_1 ) {
    // FUN_007e9e60
    Entity::propagatePosition( position, param_1 );

    Matrix4x4f_t forward = InverseTransform.inverse();
    forward.row( 3 ) = position;
    InverseTransform = forward.inverse();
}

void EntityVolume::instanciate( MngActor *pMngActor, Entity *param_1 ) {
    // FUN_007e9ec0
    Entity::instanciate( pMngActor, param_1 );

    EntityVolume *pInstance = static_cast<EntityVolume *>( param_1 );
    pInstance->InverseTransform = InverseTransform;
    pInstance->LocalTransform = LocalTransform;
    pInstance->Type = Type;
}

bool EntityVolume::isPointInside(
    const Eigen::Vector4f &point,
    const Eigen::Vector4f *pPreviousPoint ) const {
    // FUN_007e9f90
    const Eigen::Vector4f local = TDUMat4x4MulVec( InverseTransform, point );

    switch ( Type ) {
    case VT_Plane: {
        if ( pPreviousPoint == nullptr ) {
            break;
        }
        Eigen::Vector4f previousLocal =
            TDUMat4x4MulVec( InverseTransform, *pPreviousPoint );

        if ( !( local.z() >= 0.0f || previousLocal.z() >= 0.0f ) ) {
            break;
        }
        if ( !( local.z() <= 0.0f || previousLocal.z() <= 0.0f ) ) {
            break;
        }
        if ( AreVectorsNearlyEqual( local, previousLocal, kVolumeEpsilon ) ) {
            break;
        }

        const Eigen::Vector4f direction =
            ( local - previousLocal ).normalized();
        const Eigen::Vector4f planeNormal( 0.0f, 0.0f, 1.0f, 0.0f );

        const float alongNormal = direction.dot( planeNormal );
        const float absAlongNormal =
            ( alongNormal < 0.0f ) ? -alongNormal : alongNormal;
        if ( absAlongNormal < kVolumeEpsilon ) {
            break;
        }

        previousLocal.w() = 0.0f;
        const float distanceToPlane =
            -( previousLocal.dot( planeNormal ) / alongNormal );
        const Eigen::Vector4f hit = previousLocal + direction * distanceToPlane;

        return hit.x() >= -kVolumeHalfExtent && hit.x() <= kVolumeHalfExtent &&
               hit.y() >= -kVolumeHalfExtent && hit.y() <= kVolumeHalfExtent;
    }
    case VT_Sphere: {
        Eigen::Vector4f centerToPoint = local;
        centerToPoint.w() = 0.0f;
        return centerToPoint.norm() <= 1.0f;
    }
    case VT_Box:
        return local.x() >= -kVolumeHalfExtent &&
               local.x() <= kVolumeHalfExtent &&
               local.y() >= -kVolumeHalfExtent &&
               local.y() <= kVolumeHalfExtent &&
               local.z() >= -kVolumeHalfExtent &&
               local.z() <= kVolumeHalfExtent;
    case VT_Cylinder:
        return local.y() >= -kVolumeHalfExtent &&
               local.y() <= kVolumeHalfExtent &&
               ( local.z() * local.z() + local.x() * local.x() ) <= 1.0f;
    case VT_Cone:
    case VT_ConeAlt: {
        const float radiusAtHeight = kVolumeHalfExtent - local.y();
        return local.y() >= -kVolumeHalfExtent &&
               local.y() <= kVolumeHalfExtent &&
               ( local.z() * local.z() + local.x() * local.x() ) <=
                   ( radiusAtHeight * radiusAtHeight );
    }
    default:
        break;
    }

    return false;
}
