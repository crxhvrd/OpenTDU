#pragma once

#include <Jolt/Jolt.h>
#include <Jolt/Physics/Collision/ObjectLayer.h>
#include <Jolt/Physics/Collision/BroadPhase/BroadPhaseLayer.h>

// Collision layering mirrors the original TDU 16x16 collision matrix
// (FUN_00841270)
namespace TDUPhysLayers {
    static constexpr JPH::ObjectLayer NON_MOVING =
        0; // Static: terrain (incl. SR20-graded road), buildings/objects (.shk)
    static constexpr JPH::ObjectLayer MOVING =
        1; // Dynamic: props, debris, query proxies
    static constexpr JPH::ObjectLayer SENSOR = 2; // Triggers: road phantoms
    static constexpr JPH::ObjectLayer ROAD =
        3; // Drivable road/sidewalk/curb mesh (-R.shk)
    static constexpr JPH::ObjectLayer VEHICLE = 4; // Player/AI car chassis body
    static constexpr JPH::ObjectLayer WHEEL =
        5; // Vehicle wheel cast (collision tester)
    static constexpr JPH::ObjectLayer NUM_LAYERS = 6;

    inline constexpr bool ShouldCollide( JPH::ObjectLayer a,
                                         JPH::ObjectLayer b ) {
        constexpr bool kMatrix[NUM_LAYERS][NUM_LAYERS] = {
            //          NM     MV     SN     RD     VH     WH
            /* NM */ {false, true,  false, false, true,  true },
            /* MV */
            {true,  true,  true,  true,  true,  false},
            /* SN */
            {false, true,  false, false, true,  false},
            /* RD */
            {false, true,  false, false, false, true },
            /* VH */
            {true,  true,  true,  false, true,  false},
            /* WH */
            {true,  false, false, true,  false, false},
        };
        return kMatrix[a][b];
    }

    // Object -> broadphase grouping
    inline constexpr JPH::BroadPhaseLayer::Type
    ObjectToBroadPhaseType( JPH::ObjectLayer inLayer ) {
        switch ( inLayer ) {
        case NON_MOVING:
        case ROAD:
            return 0; // BP NON_MOVING
        case MOVING:
        case VEHICLE:
        case WHEEL:
            return 1; // BP MOVING
        case SENSOR:
            return 2; // BP SENSOR
        default:
            return 1;
        }
    }
} // namespace TDUPhysLayers

namespace TDUBroadPhaseLayers {
    static constexpr JPH::BroadPhaseLayer NON_MOVING( 0 );
    static constexpr JPH::BroadPhaseLayer MOVING( 1 );
    static constexpr JPH::BroadPhaseLayer SENSOR( 2 );
    static constexpr JPH::uint NUM_LAYERS( 3 );
} // namespace TDUBroadPhaseLayers

class TDUBPLayerInterface final : public JPH::BroadPhaseLayerInterface {
public:
    JPH::uint GetNumBroadPhaseLayers() const override {
        return TDUBroadPhaseLayers::NUM_LAYERS;
    }

    JPH::BroadPhaseLayer
    GetBroadPhaseLayer( JPH::ObjectLayer inLayer ) const override {
        JPH_ASSERT( inLayer < TDUPhysLayers::NUM_LAYERS );
        return JPH::BroadPhaseLayer(
            TDUPhysLayers::ObjectToBroadPhaseType( inLayer ) );
    }

#if defined( JPH_EXTERNAL_PROFILE ) || defined( JPH_PROFILE_ENABLED )
    const char *
    GetBroadPhaseLayerName( JPH::BroadPhaseLayer inLayer ) const override {
        switch ( (JPH::BroadPhaseLayer::Type)inLayer ) {
        case (JPH::BroadPhaseLayer::Type)TDUBroadPhaseLayers::NON_MOVING:
            return "NON_MOVING";
        case (JPH::BroadPhaseLayer::Type)TDUBroadPhaseLayers::MOVING:
            return "MOVING";
        case (JPH::BroadPhaseLayer::Type)TDUBroadPhaseLayers::SENSOR:
            return "SENSOR";
        default:
            return "INVALID";
        }
    }
#endif
};

class TDUObjectVsBroadPhaseLayerFilter final
    : public JPH::ObjectVsBroadPhaseLayerFilter {
public:
    bool ShouldCollide( JPH::ObjectLayer inLayer,
                        JPH::BroadPhaseLayer inBPLayer ) const override {
        const JPH::BroadPhaseLayer::Type bp =
            (JPH::BroadPhaseLayer::Type)inBPLayer;
        for ( JPH::ObjectLayer j = 0; j < TDUPhysLayers::NUM_LAYERS; ++j ) {
            if ( TDUPhysLayers::ObjectToBroadPhaseType( j ) == bp &&
                 TDUPhysLayers::ShouldCollide( inLayer, j ) ) {
                return true;
            }
        }
        return false;
    }
};

class TDUObjectLayerPairFilter final : public JPH::ObjectLayerPairFilter {
public:
    bool ShouldCollide( JPH::ObjectLayer inObject1,
                        JPH::ObjectLayer inObject2 ) const override {
        return TDUPhysLayers::ShouldCollide( inObject1, inObject2 );
    }
};
