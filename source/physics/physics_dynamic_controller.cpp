#include "shared.h"
#include "physics_dynamic_controller.h"

#include "physics/gs_physics.h"

PhysicsDynamicController::PhysicsDynamicController()
    : templateHashcode( 0ull ),
      linearDamping( 0.0f ),
      angularDamping( 0.0f ),
      bStatic( false ),
      primaryLocalInv( Matrix4x4f_t::Identity() ) {
    // FUN_00454b60
}

PhysicsDynamicController::~PhysicsDynamicController() {
    // FUN_00454ba0
}

void PhysicsDynamicController::bindTemplate( const uint64_t hashcode,
                                             const float linear,
                                             const float angular,
                                             const bool bStaticBody ) {
    // FUN_00454bc0
    templateHashcode = hashcode;
    linearDamping = linear;
    angularDamping = angular;
    bStatic = bStaticBody;
}

void PhysicsDynamicController::cloneAndAdd(
    const PhysicsDynamicController &original,
    const Matrix4x4f_t &worldTransform ) {
    // FUN_00454e40
    destroyAsClone();
    if ( gpPhysics == nullptr || original.templateHashcode == 0ull ) {
        return;
    }

    templateHashcode = original.templateHashcode;
    linearDamping = original.linearDamping;
    angularDamping = original.angularDamping;
    bStatic = original.bStatic;

    primaryLocalInv = Matrix4x4f_t::Identity();
    std::vector<std::pair<uint64_t, RigidBody_t>> placedBodies;
    gpPhysics->placeProp( templateHashcode, worldTransform, !bStatic,
                          linearDamping, angularDamping, placedBodies,
                          &primaryLocalInv );

    const Matrix4x4f_t worldInverse = worldTransform.inverse();
    members.reserve( placedBodies.size() );
    memberLocalTransforms.reserve( placedBodies.size() );
    for ( const std::pair<uint64_t, RigidBody_t> &placed : placedBodies ) {
        Matrix4x4f_t bodyWorld = worldTransform;
        bool bActive = false;
        gpPhysics->getBodyPose( placed.second, bodyWorld, bActive );

        members.emplace_back( placed.first,
                              static_cast<void *>( placed.second ) );
        memberLocalTransforms.push_back( bodyWorld * worldInverse );
    }
}

void PhysicsDynamicController::destroyAsClone() {
    // FUN_00454f10
    if ( gpPhysics != nullptr ) {
        for ( const std::pair<uint64_t, void *> &member : members ) {
            gpPhysics->destroyRigidBody(
                static_cast<RigidBody_t>( member.second ) );
        }
    }
    members.clear();
    memberLocalTransforms.clear();
}

void PhysicsDynamicController::setWorld( const PhysicsDynamicController &source,
                                         const Matrix4x4f_t &worldTransform ) {
    // FUN_004563a0
    if ( gpPhysics == nullptr ) {
        return;
    }

    const size_t numMembers =
        std::min( members.size(), source.memberLocalTransforms.size() );
    for ( size_t i = 0; i < numMembers; i++ ) {
        gpPhysics->setBodyPose( static_cast<RigidBody_t>( members[i].second ),
                                source.memberLocalTransforms[i] *
                                    worldTransform );
    }
}

bool PhysicsDynamicController::getWorldMatrix( Matrix4x4f_t &outMatrix,
                                               bool &outActive ) const {
    // FUN_0047a8a0
    outActive = false;
    if ( gpPhysics == nullptr || members.empty() ) {
        return false;
    }

    return gpPhysics->getBodyRenderPose( // OTDU_ADDITION
        static_cast<RigidBody_t>( members[0].second ), outMatrix, outActive );
}
