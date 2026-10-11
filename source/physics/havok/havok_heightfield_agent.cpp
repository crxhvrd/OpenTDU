#include "shared.h"
#include "havok_heightfield_agent.h"

#include <Jolt/Jolt.h>
#include <Jolt/Physics/Collision/CollideShape.h>
#include <Jolt/Physics/Collision/CollisionDispatch.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/HeightFieldShape.h>

namespace HavokHeightFieldAgent {

    namespace {
        constexpr int32_t kNumBoxSpheres = 8; // FUN_00493570
        constexpr float kCollisionTolerance =
            0.1f; // hkpWorldCinfo default (FUN_004836d0 @0x00483728)
        constexpr float kMaxPenetration = -0.5f; // DAT_00f4186c

        struct CollisionSphere {
            JPH::Vec3 Center;
            float Radius;
        };

        void GetBoxCollisionSpheres( const JPH::BoxShape &box,
                                     JPH::Vec3Arg scale,
                                     JPH::Mat44Arg comTransform,
                                     CollisionSphere *pOutSpheres ) {
            // FUN_00493ab0: the corners of the core box (half extents
            // without the convex radius), each with the convex radius
            const float radius = box.GetConvexRadius();
            const JPH::Vec3 core = box.GetHalfExtent() * scale.Abs() -
                                   JPH::Vec3::sReplicate( radius );
            for ( int32_t i = 0; i < kNumBoxSpheres; i++ ) {
                const JPH::Vec3 corner( ( i & 1 ) ? -core.GetX() : core.GetX(),
                                        ( i & 2 ) ? -core.GetY() : core.GetY(),
                                        ( i & 4 ) ? -core.GetZ()
                                                  : core.GetZ() );
                pOutSpheres[i].Center = comTransform * corner;
                pOutSpheres[i].Radius = radius;
            }
        }

        // FUN_00823270: vertical line through the point against one triangle
        bool IntersectVertical( JPH::Vec3Arg point,
                                JPH::Vec3Arg v0,
                                JPH::Vec3Arg v1,
                                JPH::Vec3Arg v2,
                                float &outDistance,
                                JPH::Vec3 &outNormal ) {
            const JPH::Vec3 e1 = v1 - v0;
            const JPH::Vec3 e2 = v2 - v0;
            const float det = e1.GetX() * e2.GetZ() - e1.GetZ() * e2.GetX();
            if ( JPH::abs( det ) < 1.0e-12f ) {
                return false;
            }
            const float dx = point.GetX() - v0.GetX();
            const float dz = point.GetZ() - v0.GetZ();
            const float a = ( dx * e2.GetZ() - dz * e2.GetX() ) / det;
            const float b = ( e1.GetX() * dz - e1.GetZ() * dx ) / det;
            if ( a < 0.0f || b < 0.0f || a + b > 1.0f ) {
                return false;
            }
            const float surfaceY = v0.GetY() + a * e1.GetY() + b * e2.GetY();
            outDistance = point.GetY() - surfaceY;
            JPH::Vec3 normal = e1.Cross( e2 );
            if ( normal.GetY() < 0.0f ) {
                normal = -normal;
            }
            outNormal = normal.Normalized();
            return true;
        }

        void CollideSpheresVsTerrain(
            const CollisionSphere *pSpheres,
            const int32_t numSpheres,
            const JPH::HeightFieldShape &heightField,
            JPH::Vec3Arg scale2,
            JPH::Mat44Arg comTransform2,
            const JPH::SubShapeIDCreator &inSubShapeIDCreator1,
            const JPH::SubShapeIDCreator &inSubShapeIDCreator2,
            JPH::CollideShapeCollector &ioCollector ) {
            // FUN_008243c0
            const JPH::Mat44 toLocal =
                comTransform2.InversedRotationTranslation();
            const uint32_t numSamples = heightField.GetSampleCount();
            const JPH::Vec3 origin = heightField.GetPosition( 0, 0 ) * scale2;
            const JPH::Vec3 step =
                heightField.GetPosition( 1, 1 ) * scale2 - origin;

            for ( int32_t i = 0; i < numSpheres; i++ ) {
                const CollisionSphere &sphere = pSpheres[i];
                const JPH::Vec3 local = toLocal * sphere.Center;
                const int32_t cellX = static_cast<int32_t>( std::floor(
                    ( local.GetX() - origin.GetX() ) / step.GetX() ) );
                const int32_t cellZ = static_cast<int32_t>( std::floor(
                    ( local.GetZ() - origin.GetZ() ) / step.GetZ() ) );
                if ( cellX < 0 || cellZ < 0 ||
                     cellX + 1 >= static_cast<int32_t>( numSamples ) ||
                     cellZ + 1 >= static_cast<int32_t>( numSamples ) ) {
                    continue;
                }

                // FUN_00822fc0: the cell's triangles split along the
                // (x0, z1) - (x1, z0) diagonal
                const uint32_t x0 = static_cast<uint32_t>( cellX );
                const uint32_t z0 = static_cast<uint32_t>( cellZ );
                const bool bHole00 = heightField.IsNoCollision( x0, z0 );
                const bool bHole01 = heightField.IsNoCollision( x0, z0 + 1 );
                const bool bHole10 = heightField.IsNoCollision( x0 + 1, z0 );
                const bool bHole11 =
                    heightField.IsNoCollision( x0 + 1, z0 + 1 );
                const JPH::Vec3 p00 =
                    heightField.GetPosition( x0, z0 ) * scale2;
                const JPH::Vec3 p01 =
                    heightField.GetPosition( x0, z0 + 1 ) * scale2;
                const JPH::Vec3 p10 =
                    heightField.GetPosition( x0 + 1, z0 ) * scale2;
                const JPH::Vec3 p11 =
                    heightField.GetPosition( x0 + 1, z0 + 1 ) * scale2;

                float distance = 0.0f;
                JPH::Vec3 normal;
                bool bHit =
                    !bHole00 && !bHole01 && !bHole10 &&
                    IntersectVertical( local, p00, p01, p10, distance, normal );
                if ( !bHit ) {
                    bHit = !bHole01 && !bHole11 && !bHole10 &&
                           IntersectVertical( local, p01, p11, p10, distance,
                                              normal );
                }
                if ( !bHit ) {
                    continue;
                }

                const float separation =
                    distance - sphere.Radius - kCollisionTolerance;
                if ( separation >= 0.0f || separation <= kMaxPenetration ) {
                    continue;
                }
                const float contactDistance = separation + kCollisionTolerance;
                if ( contactDistance >= ioCollector.GetEarlyOutFraction() ) {
                    continue;
                }

                JPH::Vec3 surfacePoint;
                JPH::SubShapeID localSubShapeID;
                if ( !heightField.ProjectOntoSurface(
                         local / scale2, surfacePoint, localSubShapeID ) ) {
                    continue;
                }

                // processCollision: the contact sits on the sphere surface
                // along the heightfield normal
                const JPH::Vec3 worldNormal =
                    comTransform2.Multiply3x3( normal ).Normalized();
                const JPH::Vec3 onSphere =
                    sphere.Center - worldNormal * sphere.Radius;
                const JPH::Vec3 onSurface =
                    onSphere - worldNormal * contactDistance;
                const JPH::SubShapeID subShapeID2 =
                    inSubShapeIDCreator2
                        .PushID( localSubShapeID.GetValue(),
                                 heightField.GetSubShapeIDBitsRecursive() )
                        .GetID();
                JPH::CollideShapeResult result(
                    onSphere, onSurface, -worldNormal, -contactDistance,
                    inSubShapeIDCreator1.GetID(), subShapeID2, JPH::BodyID() );
                ioCollector.AddHit( result );
                if ( ioCollector.ShouldEarlyOut() ) {
                    return;
                }
            }
        }

        void sCollideBoxVsHeightField(
            const JPH::Shape *inShape1,
            const JPH::Shape *inShape2,
            JPH::Vec3Arg inScale1,
            JPH::Vec3Arg inScale2,
            JPH::Mat44Arg inCenterOfMassTransform1,
            JPH::Mat44Arg inCenterOfMassTransform2,
            const JPH::SubShapeIDCreator &inSubShapeIDCreator1,
            const JPH::SubShapeIDCreator &inSubShapeIDCreator2,
            const JPH::CollideShapeSettings &,
            JPH::CollideShapeCollector &ioCollector,
            const JPH::ShapeFilter & ) {
            CollisionSphere spheres[kNumBoxSpheres];
            GetBoxCollisionSpheres(
                *static_cast<const JPH::BoxShape *>( inShape1 ), inScale1,
                inCenterOfMassTransform1, spheres );
            CollideSpheresVsTerrain(
                spheres, kNumBoxSpheres,
                *static_cast<const JPH::HeightFieldShape *>( inShape2 ),
                inScale2, inCenterOfMassTransform2, inSubShapeIDCreator1,
                inSubShapeIDCreator2, ioCollector );
        }
    } // namespace

    void Register() {
        using JPH::CollisionDispatch;
        using JPH::EShapeSubType;
        CollisionDispatch::sRegisterCollideShape( EShapeSubType::Box,
                                                  EShapeSubType::HeightField,
                                                  sCollideBoxVsHeightField );
        CollisionDispatch::sRegisterCollideShape(
            EShapeSubType::HeightField, EShapeSubType::Box,
            CollisionDispatch::sReversedCollideShape );
    }

} // namespace HavokHeightFieldAgent
