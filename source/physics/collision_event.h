#pragma once

#include "game/game_object.h"
#include "game/material_types.h"

enum class eCollisionType : uint8_t {
    CT_None = 0,
    CT_Vehicle = 1,
    CT_RigidBody = 2,
    CT_DynamicBody = 3
};

class PhysicsVehicle;

namespace JPH {
    class Body;
}

// Vehicle contact event
struct CollisionEvent {
    int32_t EventID;
    eCollisionType Type;

    union {
        PhysicsVehicle *pOtherVehicle;
        const JPH::Body *pOtherBody;
        void *pOtherObject;
    };

    float RelativeVelocity;
    float Energy;

    eMaterialType CarMaterial;
    eMaterialType OtherMaterial;

    Eigen::Vector4f ContactPosition; // vehicle space
    Eigen::Vector4f Velocity;        // vehicle space

    ObjectWorldLocation Location;

    CollisionEvent()
        : EventID( 0 ),
          Type( eCollisionType::CT_None ),
          pOtherObject( nullptr ),
          RelativeVelocity( 0.0f ),
          Energy( 0.0f ),
          CarMaterial( eMaterialType::MT_Default ),
          OtherMaterial( eMaterialType::MT_Default ),
          ContactPosition( Eigen::Vector4f::Zero() ),
          Velocity( Eigen::Vector4f::Zero() ) {}
};
