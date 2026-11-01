#include "shared.h"
#include "entity_mechanism.h"

#include "game/scenaric/entity/base_entity.h"
#include "game/scenaric/entity/entity_factory.h"
#include "game/scenaric/entity/entity_traffic_light.h"
#include "game/scenaric/gs_scenaric.h"
#include "game/vehicle/car_state.h"
#include "render/serialized_skinned_model.h"
#include "physics/gs_physics.h"
#include "physics/physics_dynamic_controller.h"
#include "physics/vehicle/physics_vehicle.h"
#include "world/gs_world_streaming.h"

static constexpr float kStreamCellScale = 0.1f; // FLOAT_00f41668
static constexpr int32_t kCloneCellDistance = 2;
static constexpr int32_t kKeepCellDistance = 10;
static constexpr float kReInitMinDistanceSq = 10000.0f; // DAT_00f416c4

static bool IsLocalPlayerPhysicsRunning() {
    // FUN_007d8670
    CarState *pCarState = gpScenaric->getLocalPlayer().getCarState();
    if ( pCarState == nullptr ) {
        return false;
    }
    PhysicsVehicle *pVehicle = pCarState->getPhysicsVehicle();
    return pVehicle != nullptr && !pVehicle->isPaused();
}

static Eigen::Vector4f GetLocalPlayerPosition() {
    return gpScenaric->getLocalPlayer().getCarState()->getWorldMatrix().row(
        3 );
}

static Eigen::Vector4f GetWorldShift() {
    return gpWorldStreaming->isEnabled() ? gpWorldStreaming->getPosition()
                                         : Eigen::Vector4f::Zero();
}

static Matrix4x4f_t OrthonormalizeMechaMatrix( const Matrix4x4f_t &matrix ) {
    const Eigen::Vector3f up = matrix.row( 1 ).head<3>().normalized();
    const Eigen::Vector3f right =
        up.cross( Eigen::Vector3f( matrix.row( 2 ).head<3>() ) ).normalized();
    const Eigen::Vector3f forward = right.cross( up ).normalized();

    Matrix4x4f_t result = matrix;
    result.row( 0 ) << right.transpose(), 0.0f;
    result.row( 1 ) << up.transpose(), 0.0f;
    result.row( 2 ) << forward.transpose(), 0.0f;
    result( 3, 3 ) = 1.0f;
    return result;
}

EntityMechanism::EntityMechanism()
    : initialTransformMatrix( Matrix4x4f_t::Identity() ),
      transformMatrix( Matrix4x4f_t::Identity() ),
      pController( nullptr ),
      pParentController( nullptr ),
      pDynamicController( nullptr ),
      pPureDynamicController( nullptr ),
      linearDamping( 0.0f ),
      angularDamping( 0.05f ),
      pEntity( nullptr ),
      pParent( nullptr ),
      numInstance( 0u ),
      propCollisionHashcode( 0u ),
      bControlledByAvatar( false ),
      bInitialized( false ),
      bTransformSet( false ) {}

EntityMechanism::~EntityMechanism() {}

void EntityMechanism::setEntity( Entity *pEntityInstance ) {
    // FUN_007d8bc0
    pEntity = pEntityInstance;

    if ( pParent == nullptr && !bTransformSet && !bControlledByAvatar ) {
        initialTransformMatrix = pEntityInstance->getModelMatrix();
        transformMatrix = initialTransformMatrix;

        Matrix4x4f_t local_50 = initialTransformMatrix.inverse();

        if ( pController != nullptr ) {
            pController->setWorld( *pController, local_50 );
        }

        bTransformSet = true;
    }
}

void EntityMechanism::setPureDynamicControllerDesc(
    SerializedPureDynamicController *pDesc ) {
    pPureDynamicController = pDesc;
}

void EntityMechanism::loadResources( ScenaricBank *param_2 ) {
    // FUN_007d8a40
    (void)param_2;
    if ( bControlledByAvatar ) {
        numInstance = 0;
        return;
    }

    if ( pPureDynamicController == nullptr || gpPhysics == nullptr ) {
        return;
    }

    propCollisionHashcode = pPureDynamicController->Hashcode;
    pController = new PhysicsDynamicController();
    pController->bindTemplate( propCollisionHashcode,
                               pPureDynamicController->LinearDamping,
                               pPureDynamicController->AngularDamping,
                               pPureDynamicController->bStatic != 0 );
}

void EntityMechanism::tick() {
    // FUN_007d9070
    if ( !bControlledByAvatar && pController != nullptr && pEntity != nullptr &&
         gpPhysics != nullptr ) {
        updateStreaming();
    }

    syncEntityTransform();
}

void EntityMechanism::updateStreaming() {
    const bool bPlayerPhysicsRunning = IsLocalPlayerPhysicsRunning();
    if ( bPlayerPhysicsRunning ) {
        Eigen::Vector4f playerPosition = GetLocalPlayerPosition();
        if ( pEntity->isShifted() ) {
            const Eigen::Vector4f shift = GetWorldShift();
            playerPosition.x() -= shift.x();
            playerPosition.z() -= shift.z();
        }

        const Matrix4x4f_t &modelMatrix = pEntity->getModelMatrix();
        const int32_t distanceZ = std::abs(
            static_cast<int32_t>( playerPosition.z() * kStreamCellScale ) -
            static_cast<int32_t>( modelMatrix( 3, 2 ) * kStreamCellScale ) );
        const int32_t distanceX = std::abs(
            static_cast<int32_t>( playerPosition.x() * kStreamCellScale ) -
            static_cast<int32_t>( modelMatrix( 3, 0 ) * kStreamCellScale ) );
        const int32_t distance = std::max( distanceX, distanceZ );

        if ( distance < kCloneCellDistance && !bInitialized ) {
            if ( gpPhysics->isPropFactoryReady() ) { // OTDU_ADDITION
                cloneAndAdd();
            }
            return;
        }
        if ( distance < kKeepCellDistance ) {
            return;
        }
    }

    if ( !bInitialized ) {
        return;
    }

    if ( !pEntity->isTileVisible() && !pController->isActive() ) {
        if ( bPlayerPhysicsRunning ) {
            const Eigen::Vector4f delta =
                initialTransformMatrix.row( 3 ).transpose() -
                GetLocalPlayerPosition();
            if ( delta.squaredNorm() <= kReInitMinDistanceSq ) {
                return;
            }
        }
        reInit();
    }
}

void EntityMechanism::syncEntityTransform() {
    if ( !bInitialized || pEntity == nullptr || pController == nullptr ||
         isStaticController() ) {
        return;
    }

    Matrix4x4f_t pose;
    bool bActive = false;
    if ( pController->getWorldMatrix( pose, bActive ) ) {
        transformMatrix = pController->getPrimaryLocalInverse() * pose;
        transformMatrix( 3, 3 ) = 1.0f;
        pEntity->setTransform( transformMatrix, true );
    }

    pEntity->refreshMechanismRender( pController->getMembers() );
}

int32_t EntityMechanism::getBrokenJointCount() const {
    // FUN_00843140
    if ( gpPhysics == nullptr || pController == nullptr ) {
        return 0;
    }

    int32_t numBroken = 0;
    for ( const std::pair<uint64_t, void *> &member :
          pController->getMembers() ) {
        if ( member.second == nullptr ) {
            continue;
        }

        const GSPhysics::PropMemberAttachment attachment =
            gpPhysics->getPropMemberAttachment(
                static_cast<RigidBody_t>( member.second ) );
        numBroken += attachment.NumBroken;
    }

    return numBroken;
}

const PhysicsDynamicController *EntityMechanism::getOriginalController() const {
    if ( pParent != nullptr ) {
        return pParent->pController;
    }
    return pParentController != nullptr ? pParentController : pController;
}

void EntityMechanism::cloneAndAdd() {
    // FUN_007d8c40
    const PhysicsDynamicController *pOriginal = getOriginalController();
    if ( pController == nullptr || pOriginal == nullptr ||
         gpPhysics == nullptr ) {
        return;
    }

    PhysicsLock lock;
    pController->cloneAndAdd( *pOriginal,
                              OrthonormalizeMechaMatrix( transformMatrix ) );
    bInitialized = pController->isCloned();
}

void EntityMechanism::terminate() {
    // FUN_007d8930
    if ( bInitialized ||
         ( pController != nullptr && pController->isCloned() ) ) {
        removeAndDestroy();
    }

    if ( pController != nullptr ) {
        delete pController;
        pController = nullptr;
    }
    if ( pParentController != nullptr ) {
        delete pParentController;
        pParentController = nullptr;
    }
}

void EntityMechanism::reInit() {
    // FUN_007d8970
    if ( bInitialized ) {
        removeAndDestroy();
    }

    SerializedSkinnedModel *pModel =
        pEntity != nullptr ? static_cast<SerializedSkinnedModel *>(
                                 pEntity->getGeometryData() )
                           : nullptr;
    if ( pModel != nullptr ) {
        FileCollection3D *p3DFiles = pModel->p3DFiles;
        pModel->uninstanciate();
        pModel->instanciate( p3DFiles, true );
        if ( pEntity->getType() == eEntityType::ET_TRLI ) {
            static_cast<EntityTrafficLight *>( pEntity )
                ->computeROGHiearchies();
        }
    }

    if ( pParent == nullptr && pEntity != nullptr ) {
        initialTransformMatrix = pEntity->getModelMatrix();
    }

    Matrix4x4f_t matrix = initialTransformMatrix;

    if ( pParent != nullptr ) {
        pParent->instanciate( this );
    }

    if ( pEntity != nullptr ) {
        pEntity->propagateTransform( matrix, true );
    }
    initialTransformMatrix = matrix;
}

void EntityMechanism::instanciate( EntityMechanism *pInstance ) {
    // FUN_007d85e0
    pInstance->bControlledByAvatar = bControlledByAvatar;
    pInstance->bInitialized = false;
    pInstance->pDynamicController = pDynamicController;
    pInstance->pPureDynamicController = pPureDynamicController;
    if ( !bControlledByAvatar ) {
        pInstance->linearDamping = linearDamping;
        pInstance->angularDamping = angularDamping;
    }
    pInstance->pParent = this;
    pInstance->transformMatrix = transformMatrix;
    pInstance->initialTransformMatrix = initialTransformMatrix;
    pInstance->propCollisionHashcode = propCollisionHashcode;
}

void EntityMechanism::initInstance() {
    // FUN_007d86b0
    if ( pParent == nullptr ) {
        pParentController = pController;
    }

    pController = new PhysicsDynamicController();

    if ( pParent != nullptr ) {
        int32_t instance = ( pParent->numInstance + 1 ) & 0x3ff;
        pParent->numInstance = instance;
        numInstance = instance;
    } else {
        numInstance = 1;
    }
}

void EntityMechanism::removeAndDestroy() {
    // FUN_007d8810
    if ( pController != nullptr ) {
        pController->destroyAsClone();
    }
    bInitialized = false;
}

void EntityMechanism::setMechanismTransform( const Matrix4x4f_t &matrix ) {
    transformMatrix = matrix;
}

void EntityMechanism::setMechanismPosition( const Eigen::Vector4f &position ) {
    // FUN_007d87c0
    transformMatrix.row( 3 ) = position;
}
