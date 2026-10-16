#include "shared.h"
#include "game_mechanism.h"

#include "game/scenaric/entity/base_entity.h"
#include "game/scenaric/entity/entity_factory.h"
#include "game/scenaric/entity/entity_traffic_light.h"
#include "render/serialized_skinned_model.h"
#include "physics/gs_physics.h"

GameMechanism::GameMechanism()
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
      primaryLocalInv( Matrix4x4f_t::Identity() ),
      bControlledByAvatar( false ),
      bInitialized( false ),
      bTransformSet( false ),
      bPlacementTried( false ),
      bDynamicProp( false ) {}

GameMechanism::~GameMechanism() {}

void GameMechanism::setEntity( Entity *pEntityInstance ) {
    // FUN_007d8bc0
    pEntity = pEntityInstance;

    if ( pParent == nullptr && !bTransformSet && !bControlledByAvatar ) {
        Matrix4x4f_t *pTVar3 = &initialTransformMatrix;
        const Matrix4x4f_t &pTVar2 = pEntityInstance->getModelMatrix();
        Matrix4x4f_t *pTVar4 = pTVar3;

        initialTransformMatrix = pEntityInstance->getModelMatrix();
        transformMatrix = initialTransformMatrix;

        Matrix4x4f_t local_50 = initialTransformMatrix.inverse();

        OTDU_UNIMPLEMENTED; // TODO: implement setWorldMatrix
        // pController->setWorldMatrix(local_50);

        bTransformSet = true;
    }
}

void GameMechanism::setPureDynamicControllerDesc(
    SerializedPureDynamicController *pDesc ) {
    pPureDynamicController = pDesc;
}

void GameMechanism::loadResources( ScenaricBank *param_2 ) {
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
    bInitialized = true;
}

void GameMechanism::tick() {
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

    if ( bDynamicProp && !propBodies.empty() ) {
        Matrix4x4f_t pose;
        bool bActive = false;
        if ( gpPhysics->getBodyPose(
                 static_cast<RigidBody_t>( propBodies[0].second ), pose,
                 bActive ) &&
             bActive ) {
            Matrix4x4f_t entityPose = primaryLocalInv * pose;
            pEntity->setTransform( entityPose, true );
        }

        pEntity->refreshMechanismRender( propBodies );
    }
}

int32_t GameMechanism::getBrokenJointCount() const {
    // FUN_00843140
    if ( gpPhysics == nullptr ) {
        return 0;
    }

    int32_t numBroken = 0;
    for ( const std::pair<uint64_t, void *> &member : propBodies ) {
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

void GameMechanism::placeCollisionBody( const Matrix4x4f_t &worldTransform ) {
    // FUN_004563a0
    if ( propCollisionHashcode == 0 || gpPhysics == nullptr ) {
        return;
    }
    for ( const auto &member : propBodies ) {
        gpPhysics->destroyRigidBody(
            static_cast<RigidBody_t>( member.second ) );
    }
    propBodies.clear();

    bDynamicProp = !isStaticController();

    primaryLocalInv = Matrix4x4f_t::Identity();
    std::vector<std::pair<uint64_t, RigidBody_t>> placedBodies;
    gpPhysics->placeProp( propCollisionHashcode, worldTransform, bDynamicProp,
                          pPureDynamicController != nullptr
                              ? pPureDynamicController->LinearDamping
                              : linearDamping,
                          pPureDynamicController != nullptr
                              ? pPureDynamicController->AngularDamping
                              : angularDamping,
                          placedBodies, &primaryLocalInv );
    for ( const auto &placed : placedBodies ) {
        propBodies.emplace_back( placed.first,
                                 static_cast<void *>( placed.second ) );
    }
}

void GameMechanism::terminate() {
    // FUN_007d8930
    if ( bInitialized || !propBodies.empty() ) {
        removeAndDestroy();
    }

    if ( pController != nullptr ) {
        // TODO: destroy the physics controller (vtable destructor) once ported.
        OTDU_UNIMPLEMENTED;
        pController = nullptr;
    }
    if ( pParentController != nullptr ) {
        // TODO: destroy the original physics controller once ported.
        OTDU_UNIMPLEMENTED;
        pParentController = nullptr;
    }
}

void GameMechanism::reInit() {
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

void GameMechanism::instanciate( GameMechanism *pInstance ) {
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
    pInstance->propBodies.clear();
}

void GameMechanism::initInstance() {
    // FUN_007d86b0
    if ( pParent == nullptr ) {
        pParentController = pController;
    }

    OTDU_UNIMPLEMENTED; // TODO: PHYSICS
    pController = nullptr;

    if ( pParent != nullptr ) {
        int32_t instance = ( pParent->numInstance + 1 ) & 0x3ff;
        pParent->numInstance = instance;
        numInstance = instance;
    } else {
        numInstance = 1;
    }
}

void GameMechanism::removeAndDestroy() {
    // FUN_007d8810
    if ( gpPhysics != nullptr ) {
        for ( const auto &member : propBodies ) {
            gpPhysics->destroyRigidBody(
                static_cast<RigidBody_t>( member.second ) );
        }
    }
    propBodies.clear();
    bInitialized = false;
}

void GameMechanism::setMechanismTransform( const Matrix4x4f_t &matrix ) {
    transformMatrix = matrix;
}

void GameMechanism::setMechanismPosition( const Eigen::Vector4f &position ) {
    // FUN_007d87c0
    transformMatrix.row( 3 ) = position;
}

void GameMechanism::setMechanismInitialTransform( const Matrix4x4f_t &matrix ) {
    // FUN_009c8070 (inlined)
    initialTransformMatrix = matrix;
    bPlacementTried = false;
}
