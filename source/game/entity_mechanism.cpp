#include "shared.h"
#include "entity_mechanism.h"

#include "game/scenaric/entity/base_entity.h"
#include "game/scenaric/entity/entity_factory.h"
#include "game/scenaric/entity/entity_traffic_light.h"
#include "render/serialized_skinned_model.h"
#include "physics/gs_physics.h"
#include "physics/physics_dynamic_controller.h"

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
      bTransformSet( false ),
      bPlacementTried( false ),
      bDynamicProp( false ) {}

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
    if ( bControlledByAvatar || pEntity == nullptr || gpPhysics == nullptr ) {
        return;
    }
    if ( propCollisionHashcode == 0 && pPureDynamicController != nullptr ) {
        propCollisionHashcode = pPureDynamicController->Hashcode;
    }
    // Defer placement until the shared prop-collision factory has been built.
    if ( propCollisionHashcode != 0 && !bPlacementTried &&
         gpPhysics->isPropFactoryReady() ) {
        bPlacementTried = true;
        placeCollisionBody( pEntity->getWorldMatrix() );
    }

    if ( bDynamicProp && pController != nullptr && pController->isCloned() ) {
        Matrix4x4f_t pose;
        bool bActive = false;
        if ( pController->getWorldMatrix( pose, bActive ) && bActive ) {
            Matrix4x4f_t entityPose =
                pController->getPrimaryLocalInverse() * pose;
            pEntity->setTransform( entityPose, true );
        }

        pEntity->refreshMechanismRender( pController->getMembers() );
    }
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

void EntityMechanism::placeCollisionBody( const Matrix4x4f_t &worldTransform ) {
    // FUN_007d8c40
    const PhysicsDynamicController *pOriginal = getOriginalController();
    if ( pController == nullptr || pOriginal == nullptr ||
         gpPhysics == nullptr ) {
        return;
    }

    bDynamicProp = !isStaticController();
    pController->cloneAndAdd( *pOriginal, worldTransform );
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
    pInstance->bPlacementTried = false;
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

void EntityMechanism::setMechanismInitialTransform(
    const Matrix4x4f_t &matrix ) {
    // FUN_009c8070 (inlined)
    initialTransformMatrix = matrix;
    bPlacementTried = false;
}
