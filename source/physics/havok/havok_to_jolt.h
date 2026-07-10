#pragma once

#include <Jolt/Jolt.h>
#include <Jolt/Physics/Collision/Shape/Shape.h>

#include <utility>
#include <vector>

class HavokPackfile;

// Translates the reconstructed Havok ~3.0 collision graph (parsed by
// HavokPackfile) into Jolt shapes/bodies. Layouts are documented in
// havok_packfile and were recovered from real vehicle scenes.
namespace HavokToJolt {

    struct RigidBody {
        JPH::ShapeRefC
            Shape;          // collision shape (compound/box), local to the body
        JPH::Vec3 Position; // body position, vehicle-local
        JPH::Quat Rotation; // body orientation, vehicle-local
        uint64_t Hashcode =
            0; // registry key (scenaric prop-type lookup); 0 if none
        // Registry index recovered from the hkRigidBody's 0x0FFFFFFF property
        // (FUN_00452060): registry[RegistryIndex] is this body's entry, and its
        // GlobalHashcode is stamped onto Hashcode. 0xFFFFFFFF if the body
        // carries no such property (e.g. the world/road .shk collision, which
        // has no registry).
        uint32_t RegistryIndex = 0xFFFFFFFFu;
    };

    // Build a Jolt shape from the Havok shape at `shapeOffset` (in the
    // packfile's
    // __data__ section). Handles hkBoxShape, hkTransformShape and hkListShape;
    // returns a null ref for unsupported types.
    JPH::ShapeRefC buildShape( const HavokPackfile &packfile,
                               int32_t shapeOffset,
                               int32_t depth = 0 );

    // Walk hkPhysicsSystem -> rigid bodies, building each body's Jolt shape +
    // vehicle-local transform.
    void buildBodies( const HavokPackfile &packfile,
                      std::vector<RigidBody> &outBodies );

} // namespace HavokToJolt

// The payload a PhysFactory owns: the reconstructed collision bodies (a
// per-vehicle-type template) plus the live Jolt body IDs created when an
// instance is placed in the world (GSPhysics::instantiateFactory).
struct HavokFactoryData {
    std::vector<HavokToJolt::RigidBody> Bodies;
    std::vector<uint32_t> LiveBodyIDs;
    std::vector<std::pair<uint64_t, uint32_t>> Registry;
    std::vector<std::pair<uint64_t, std::vector<uint64_t>>> MechaMembers;
};
