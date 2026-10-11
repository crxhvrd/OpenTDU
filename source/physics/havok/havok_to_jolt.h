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

    // The instancing path (FUN_00453690) rebuilds a SCALED copy of a member's
    // shape when placing a scaled instance, but only for the three primitive
    // Havok types it knows how to scale: hkSphereShape (type 4, radius *
    // scale.x), hkBoxShape (type 7, half-extents * scale) and hkCapsuleShape
    // (type 8, both endpoints * scale, radius * scale.x). Every other type
    // (hkTransformShape, hkListShape, meshes...) falls through to
    // LAB_00453990 and is instanced with the template shape UNSCALED. These
    // are the raw Havok parameters buildScaledShape needs to rebuild them.
    struct ScalablePrimitive {
        enum EKind : uint8_t { None = 0, Sphere, Box, Capsule };

        EKind Kind = None;
        float Radius = 0.0f;   // sphere/capsule radius, box convex radius
        JPH::Vec3 HalfExtents; // box
        JPH::Vec3 VertexA;     // capsule endpoints
        JPH::Vec3 VertexB;
        // Surface material read from hkpShape::m_userData (+0x08); carried here
        // so a rescaled instance keeps it. Null when the shape names none.
        const JPH::PhysicsMaterial *pMaterial = nullptr;
    };

    struct RigidBody {
        JPH::ShapeRefC
            Shape;          // collision shape (compound/box), local to the body
        JPH::Vec3 Position; // body position, vehicle-local
        JPH::Quat Rotation; // body orientation, vehicle-local
        // Raw parameters of the top-level Havok shape, when it is one of the
        // primitives the instancer can rescale (see ScalablePrimitive).
        ScalablePrimitive Scalable;
        uint64_t Hashcode =
            0; // registry key (scenaric prop-type lookup); 0 if none
        // Registry index recovered from the hkRigidBody's 0x0FFFFFFF property
        // (FUN_00452060): registry[RegistryIndex] is this body's entry, and its
        // GlobalHashcode is stamped onto Hashcode. 0xFFFFFFFF if the body
        // carries no such property (e.g. the world/road .shk collision, which
        // has no registry).
        uint32_t RegistryIndex = 0xFFFFFFFFu;
        // hkFixedRigidMotion (anchor / kinematic) vs a dynamic motion
        // (hkBoxMotion/hkSphereMotion). Anchored breakable props keep a fixed
        // "base" member; the shatter pieces are dynamic. Recovered from the
        // motion's class name in buildBodies.
        bool bFixedMotion = false;
        // True when the body's TOP-LEVEL Havok shape is a BV-tree/MOPP shape
        // (hkpShapeType 0x18). FUN_00845380 tags only these bodies of the
        // -R.shk blob as phys type 3 (road); every other body in the same blob
        // stays type 7 (generic), so this is what separates the drivable road
        // surface from the solid geometry shipped alongside it.
        bool bBvTreeShape = false;
        // Asset-authored mass in kg, recovered from the motion's inverse mass
        // (hkMotion+0xc0 == 1/mass; verified: round 60..1000 kg values). 0 if
        // unknown -> caller falls back to a default.
        float Mass = 0.0f;
        // hkEntity::m_material (+0x58 responseType, +0x5c friction, +0x60
        // restitution); the cinfo defaults (FUN_00482810) when unreadable.
        float Friction = 0.5f;
        float Restitution = 0.4f;
    };

    // A joint between two member rigid bodies of a Mecha, recovered from the
    // scene's hkConstraintInstance list (hkPhysicsSystem.m_constraints @root+
    // 0x14 / count @root+0x18). hkConstraintInstance: constraintData@+0x0c,
    // bodyA@+0x10, bodyB@+0x14, registryIndex@+0x20. If the constraintData is
    // an hkBreakableConstraintData (getType==0xc), the joint breaks when the
    // solver impulse exceeds Threshold (constraintData+0x14). The clone path
    // FUN_00453690 remaps bodyA/bodyB to the placed instance bodies and creates
    // FUN_0047d410 (breakable) or FUN_0047d800 (plain fixed).
    struct Constraint {
        uint32_t BodyARegistryIndex = 0xFFFFFFFFu;
        uint32_t BodyBRegistryIndex = 0xFFFFFFFFu;
        uint64_t BodyAHashcode =
            0; // resolved from registry in createFromBinary
        uint64_t BodyBHashcode = 0;
        bool bBreakable = false;
        float Threshold =
            0.0f; // break impulse (m_solverResultLimit); breakable
    };

    // A member body of a Mecha: its registry hashcode (which resolves to a
    // built rigid body) plus the render sub-node hashcodes it drives. LogicIDs
    // come from the mecha's pArrayIDMapping table (Mapping{LogicID u64,
    // Index}), read by the original's HKGetBodyHashcode to map a body -> its
    // render node for per-piece shatter render. A body may map to several
    // nodes; it maps to none when LogicIDs is empty (then the body hashcode is
    // used as the render key, matching the physics<->render same-name
    // convention). See
    // [[scenaric_prop_collision]].
    struct MechaMember {
        uint64_t BodyHashcode = 0;
        std::vector<uint64_t> LogicIDs;
    };

    // Build a Jolt shape from the Havok shape at `shapeOffset` (in the
    // packfile's
    // __data__ section). Handles hkBoxShape, hkTransformShape and hkListShape;
    // returns a null ref for unsupported types.
    JPH::ShapeRefC buildShape( const HavokPackfile &packfile,
                               int32_t shapeOffset,
                               int32_t depth = 0 );

    // Read the raw parameters of the top-level Havok shape at `shapeOffset`
    // when it is one of the three primitives the instancer can rescale.
    ScalablePrimitive readScalablePrimitive( const HavokPackfile &packfile,
                                             int32_t shapeOffset );

    // Rebuild `body`'s shape scaled by `scale`, the way FUN_00453690 does.
    // Returns body.Shape untouched for any shape the original leaves unscaled.
    JPH::ShapeRefC buildScaledShape( const RigidBody &body,
                                     const JPH::Vec3 &scale );

    // Walk hkPhysicsSystem -> rigid bodies, building each body's Jolt shape +
    // vehicle-local transform.
    void buildBodies( const HavokPackfile &packfile,
                      std::vector<RigidBody> &outBodies );

    // Walk hkPhysicsSystem.m_constraints -> the joints linking member bodies of
    // the breakable Mechas. Body references are stored as registry indices;
    // createFromBinary resolves them to hashcodes.
    void buildConstraints( const HavokPackfile &packfile,
                           std::vector<Constraint> &outConstraints );

} // namespace HavokToJolt

// The payload a PhysFactory owns: the reconstructed collision bodies (a
// per-vehicle-type template) plus the live Jolt body IDs created when an
// instance is placed in the world (GSPhysics::createVehicle / placeProp).
struct HavokFactoryData {
    std::vector<HavokToJolt::RigidBody> Bodies;
    std::vector<uint32_t> LiveBodyIDs;
    std::vector<std::pair<uint64_t, uint32_t>> Registry;
    std::vector<std::pair<uint64_t, std::vector<HavokToJolt::MechaMember>>>
        MechaMembers;
    std::vector<HavokToJolt::Constraint> Constraints;
};
