#include "shared.h"
#include "havok_to_jolt.h"
#include "havok_packfile.h"
#include "physics/physics_material.h"

#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>
#include <Jolt/Physics/Collision/Shape/StaticCompoundShape.h>
#include <Jolt/Physics/Collision/Shape/MeshShape.h>
#include <Jolt/Physics/Collision/Shape/ConvexHullShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <utility>

namespace {
    // Havok stores rotations column-major (hkRotation = 3 column vectors);
    // Jolt's Mat44 is also column-major, so the columns map directly.
    JPH::Quat rotationFromColumns( const float *pCol0,
                                   const float *pCol1,
                                   const float *pCol2 ) {
        JPH::Mat44 m = JPH::Mat44::sIdentity();
        m.SetColumn3( 0, JPH::Vec3( pCol0[0], pCol0[1], pCol0[2] ) );
        m.SetColumn3( 1, JPH::Vec3( pCol1[0], pCol1[1], pCol1[2] ) );
        m.SetColumn3( 2, JPH::Vec3( pCol2[0], pCol2[1], pCol2[2] ) );
        return m.GetQuaternion().Normalized();
    }

    // Every hkpShape carries `m_userData` at +0x08, and TDU stores the surface
    // EMaterial there. Mesh shapes get their material per triangle from the
    // subpart storage table instead; convex primitives (the guardrail posts,
    // barriers and panels) only have this. Verified in-engine: the shipped
    // world's hkBoxShape / hkCapsuleShape report 6 (Metal) and 7 (Plastic).
    const JPH::PhysicsMaterial *readShapeMaterial( const HavokPackfile &pf,
                                                   int32_t shapeOffset ) {
        const uint8_t *pData = pf.getDataBytes();
        if ( pData == nullptr || shapeOffset < 0 ||
             static_cast<uint32_t>( shapeOffset ) + 0x0c > pf.getDataSize() ) {
            return nullptr;
        }
        const uint32_t code = TDUMaterialValue::decode(
            *reinterpret_cast<const uint32_t *>( pData + shapeOffset + 0x08 ) );
        if ( code >= TDUMaterialValue::kNumCodes ) {
            return nullptr;
        }
        return TDUPhysicsMaterials::get( code );
    }


    // Build a Jolt triangle MeshShape from a Havok hkMeshShape /
    // hkFastMeshShape (size 0x40). Layout recovered from Havok's class
    // reflection:
    //   +0x10 m_scaling (Vector4, xyz used)
    //   +0x24 m_subparts : hkArray { Subpart* data@+0x24, int size@+0x28 }
    //   Subpart (stride 0x30): vertexBase@0x00(ptr) vertexStriding@0x04(bytes)
    //     numVertices@0x08 indexBase@0x0C(ptr) stridingType@0x10(1=u16,2=u32)
    //     indexStriding@0x14(bytes) numTriangles@0x18.
    JPH::ShapeRefC buildMeshCollection( const HavokPackfile &pf,
                                        int32_t meshOffset ) {
        const uint8_t *pData = pf.getDataBytes();
        const uint32_t dataSize = pf.getDataSize();
        if ( pData == nullptr || meshOffset < 0 ) {
            return JPH::ShapeRefC();
        }
        const uint8_t *pMesh = pData + meshOffset;

        const float *pScale = reinterpret_cast<const float *>( pMesh + 0x10 );
        JPH::Vec3 scale( pScale[0], pScale[1], pScale[2] );
        if ( scale.IsNearZero() ) {
            scale = JPH::Vec3::sReplicate( 1.0f );
        }

        const int32_t subpartArray =
            pf.resolvePointer( static_cast<uint32_t>( meshOffset ) + 0x24 );
        const int32_t numSubparts =
            *reinterpret_cast<const int32_t *>( pMesh + 0x28 );
        if ( subpartArray < 0 || numSubparts <= 0 || numSubparts > 4096 ) {
            return JPH::ShapeRefC();
        }

        JPH::VertexList vertices;
        JPH::IndexedTriangleList triangles;

        for ( int32_t s = 0; s < numSubparts; s++ ) {
            const uint32_t subOff =
                static_cast<uint32_t>( subpartArray ) + s * 0x30;
            if ( subOff + 0x30 > dataSize ) {
                break;
            }
            const uint8_t *pSub = pData + subOff;

            const int32_t vertexBase = pf.resolvePointer( subOff + 0x00 );
            const int32_t vertexStriding =
                *reinterpret_cast<const int32_t *>( pSub + 0x04 );
            const int32_t numVertices =
                *reinterpret_cast<const int32_t *>( pSub + 0x08 );
            const int32_t indexBase = pf.resolvePointer( subOff + 0x0C );
            const uint8_t stridingType = pSub[0x10];
            const int32_t indexStriding =
                *reinterpret_cast<const int32_t *>( pSub + 0x14 );
            const int32_t numTriangles =
                *reinterpret_cast<const int32_t *>( pSub + 0x18 );

            const int32_t indexSize = ( stridingType == 1 ) ? 2 : 4;
            if ( vertexBase < 0 || indexBase < 0 || numVertices <= 0 ||
                 numTriangles <= 0 || vertexStriding < 12 ||
                 ( stridingType != 1 && stridingType != 2 ) ) {
                continue;
            }

            // The vertex + index arrays must lie fully inside __data__.
            if ( static_cast<uint64_t>( vertexBase ) +
                     static_cast<uint64_t>( numVertices ) * vertexStriding >
                 dataSize ) {
                continue;
            }
            if ( static_cast<uint64_t>( indexBase ) +
                     static_cast<uint64_t>( numTriangles ) * indexStriding +
                     3 * indexSize >
                 dataSize ) {
                continue;
            }

            const uint32_t baseVertex =
                static_cast<uint32_t>( vertices.size() );
            const uint8_t *pVerts = pData + vertexBase;
            for ( int32_t v = 0; v < numVertices; v++ ) {
                const float *p = reinterpret_cast<const float *>(
                    pVerts + v * vertexStriding );
                vertices.push_back( JPH::Float3( p[0] * scale.GetX(),
                                                 p[1] * scale.GetY(),
                                                 p[2] * scale.GetZ() ) );
            }

            // indexStriding == indexSize marks a triangle strip (overlapping
            // triplets, winding flips each step); otherwise an indexed list.
            const uint8_t *pIdx = pData + indexBase;
            const bool bStrip = ( indexStriding == indexSize );
            for ( int32_t t = 0; t < numTriangles; t++ ) {
                const uint8_t *q = pIdx + t * indexStriding;
                uint32_t i0, i1, i2;
                if ( stridingType == 1 ) {
                    const uint16_t *w = reinterpret_cast<const uint16_t *>( q );
                    i0 = w[0];
                    i1 = w[1];
                    i2 = w[2];
                } else {
                    const uint32_t *w = reinterpret_cast<const uint32_t *>( q );
                    i0 = w[0];
                    i1 = w[1];
                    i2 = w[2];
                }
                if ( bStrip && ( t & 1 ) ) {
                    std::swap( i1, i2 );
                }
                if ( i0 >= static_cast<uint32_t>( numVertices ) ||
                     i1 >= static_cast<uint32_t>( numVertices ) ||
                     i2 >= static_cast<uint32_t>( numVertices ) ) {
                    continue;
                }
                triangles.push_back( JPH::IndexedTriangle(
                    baseVertex + i0, baseVertex + i1, baseVertex + i2, 0 ) );
            }
        }

        if ( vertices.size() < 3 || triangles.empty() ) {
            return JPH::ShapeRefC();
        }

        JPH::MeshShapeSettings settings( std::move( vertices ),
                                         std::move( triangles ) );
        JPH::ShapeSettings::ShapeResult result = settings.Create();
        return result.IsValid() ? result.Get() : JPH::ShapeRefC();
    }

    // Build a Jolt MeshShape from a Havok hkStorageMeshShape (extends
    // hkMeshShape). Its subpart vertex/index pointers are null in the packfile
    // (runtime-patched), so the geometry is read from the owned storage:
    //   +0x10 m_scaling; +0x40 m_storage hkArray<Storage*>
    //   {data@+0x40,size@+0x44} Storage (hkStorageMeshShapeSubpartStorage):
    //     +0x00 m_vertices  hkArray<float> {data, size=numVerts*3}
    //     +0x0c m_indices16  hkArray<u16>  {data, size=numTris*3}
    //     +0x18 m_indices32  hkArray<u32>
    JPH::ShapeRefC buildStorageMesh( const HavokPackfile &pf,
                                     int32_t shapeOffset ) {
        const uint8_t *pData = pf.getDataBytes();
        const uint32_t dataSize = pf.getDataSize();
        if ( pData == nullptr || shapeOffset < 0 ) {
            return JPH::ShapeRefC();
        }
        const uint8_t *pMesh = pData + shapeOffset;

        const float *pScale = reinterpret_cast<const float *>( pMesh + 0x10 );
        JPH::Vec3 scale( pScale[0], pScale[1], pScale[2] );
        if ( scale.IsNearZero() ) {
            scale = JPH::Vec3::sReplicate( 1.0f );
        }

        const int32_t storArr =
            pf.resolvePointer( static_cast<uint32_t>( shapeOffset ) + 0x40 );
        const int32_t numStorage =
            *reinterpret_cast<const int32_t *>( pMesh + 0x44 );
        if ( storArr < 0 || numStorage <= 0 || numStorage > 256 ) {
            return JPH::ShapeRefC();
        }

        JPH::VertexList vertices;
        JPH::IndexedTriangleList triangles;
        JPH::PhysicsMaterialList materials;
        std::vector<uint32_t> materialCodes;

        for ( int32_t s = 0; s < numStorage; s++ ) {
            const int32_t storObj =
                pf.resolvePointer( static_cast<uint32_t>( storArr ) + s * 4 );
            if ( storObj < 0 ) {
                continue;
            }
            const uint8_t *pStor = pData + storObj;
            const int32_t matIdxPtr = pf.resolvePointer( storObj + 0x24 );
            const int32_t numMatIdx =
                *reinterpret_cast<const int32_t *>( pStor + 0x28 );
            const int32_t matPtr = pf.resolvePointer( storObj + 0x30 );
            const int32_t numMat =
                *reinterpret_cast<const int32_t *>( pStor + 0x34 );
            std::vector<uint32_t> localSlots;
            if ( matPtr >= 0 && numMat > 0 && numMat < 256 &&
                 static_cast<uint64_t>( matPtr ) + numMat * 4u <= dataSize ) {
                const uint32_t *pMatVals =
                    reinterpret_cast<const uint32_t *>( pData + matPtr );
                for ( int32_t m = 0; m < numMat; m++ ) {
                    const uint32_t code =
                        TDUMaterialValue::decode( pMatVals[m] );
                    uint32_t slot = 0;
                    bool bFound = false;
                    for ( ; slot < materialCodes.size(); slot++ ) {
                        if ( materialCodes[slot] == code ) {
                            bFound = true;
                            break;
                        }
                    }
                    if ( !bFound ) {
                        materialCodes.push_back( code );
                        materials.push_back( TDUPhysicsMaterials::get( code ) );
                    }
                    localSlots.push_back( slot );
                }
            }
            const uint8_t *pMatIdx =
                ( matIdxPtr >= 0 && numMatIdx > 0 &&
                  static_cast<uint64_t>( matIdxPtr ) + numMatIdx <= dataSize )
                    ? pData + matIdxPtr
                    : nullptr;
            const int32_t numMatIdxSafe =
                ( pMatIdx != nullptr ) ? numMatIdx : 0;
            int32_t triInStorage = 0;
            auto materialSlot = [&]() -> uint32_t {
                if ( pMatIdx == nullptr || triInStorage >= numMatIdxSafe ) {
                    return 0;
                }
                const uint32_t local = pMatIdx[triInStorage];
                return ( local < localSlots.size() ) ? localSlots[local] : 0;
            };
            const int32_t vertsPtr = pf.resolvePointer( storObj + 0x00 );
            const int32_t numFloats =
                *reinterpret_cast<const int32_t *>( pStor + 0x04 );
            const int32_t idx16Ptr = pf.resolvePointer( storObj + 0x0c );
            const int32_t numIdx16 =
                *reinterpret_cast<const int32_t *>( pStor + 0x10 );
            const int32_t idx32Ptr = pf.resolvePointer( storObj + 0x18 );
            const int32_t numIdx32 =
                *reinterpret_cast<const int32_t *>( pStor + 0x1c );
            if ( vertsPtr < 0 || numFloats < 9 ||
                 static_cast<uint64_t>( vertsPtr ) +
                         static_cast<uint64_t>( numFloats ) * 4 >
                     dataSize ) {
                continue;
            }
            const int32_t numVerts = numFloats / 3;
            const uint32_t baseVertex =
                static_cast<uint32_t>( vertices.size() );
            const float *pV =
                reinterpret_cast<const float *>( pData + vertsPtr );
            for ( int32_t v = 0; v < numVerts; v++ ) {
                vertices.push_back( JPH::Float3(
                    pV[v * 3 + 0] * scale.GetX(), pV[v * 3 + 1] * scale.GetY(),
                    pV[v * 3 + 2] * scale.GetZ() ) );
            }

            auto addTris = [&]( auto *pIdx, int32_t numIdx ) {
                for ( int32_t t = 0; t + 3 <= numIdx; t += 3 ) {
                    const uint32_t i0 = pIdx[t], i1 = pIdx[t + 1],
                                   i2 = pIdx[t + 2];
                    if ( i0 < static_cast<uint32_t>( numVerts ) &&
                         i1 < static_cast<uint32_t>( numVerts ) &&
                         i2 < static_cast<uint32_t>( numVerts ) ) {
                        triangles.push_back( JPH::IndexedTriangle(
                            baseVertex + i0, baseVertex + i1, baseVertex + i2,
                            materialSlot() ) );
                    }
                    triInStorage++;
                }
            };
            if ( idx16Ptr >= 0 && numIdx16 >= 3 &&
                 static_cast<uint64_t>( idx16Ptr ) +
                         static_cast<uint64_t>( numIdx16 ) * 2 <=
                     dataSize ) {
                addTris( reinterpret_cast<const uint16_t *>( pData + idx16Ptr ),
                         numIdx16 );
            } else if ( idx32Ptr >= 0 && numIdx32 >= 3 &&
                        static_cast<uint64_t>( idx32Ptr ) +
                                static_cast<uint64_t>( numIdx32 ) * 4 <=
                            dataSize ) {
                addTris( reinterpret_cast<const uint32_t *>( pData + idx32Ptr ),
                         numIdx32 );
            }
        }

        if ( vertices.size() < 3 || triangles.empty() ) {
            return JPH::ShapeRefC();
        }
        JPH::MeshShapeSettings settings( std::move( vertices ),
                                         std::move( triangles ),
                                         std::move( materials ) );
        JPH::ShapeSettings::ShapeResult result = settings.Create();
        return result.IsValid() ? result.Get() : JPH::ShapeRefC();
    }

    // Build a Jolt MeshShape from a Havok hkSimpleMeshShape (size 0x38):
    //   +0x10 m_vertices  : hkArray<Vector4> { data@+0x10, size@+0x14 }
    //   +0x1C m_triangles : hkArray<Triangle{int a,b,c}> { data@+0x1C,
    //   size@+0x20 }
    JPH::ShapeRefC buildSimpleMesh( const HavokPackfile &pf,
                                    int32_t meshOffset ) {
        const uint8_t *pData = pf.getDataBytes();
        const uint32_t dataSize = pf.getDataSize();
        if ( pData == nullptr || meshOffset < 0 ) {
            return JPH::ShapeRefC();
        }
        const uint8_t *pMesh = pData + meshOffset;

        const int32_t vertsPtr =
            pf.resolvePointer( static_cast<uint32_t>( meshOffset ) + 0x10 );
        const int32_t numVerts =
            *reinterpret_cast<const int32_t *>( pMesh + 0x14 );
        const int32_t trisPtr =
            pf.resolvePointer( static_cast<uint32_t>( meshOffset ) + 0x1C );
        const int32_t numTris =
            *reinterpret_cast<const int32_t *>( pMesh + 0x20 );
        if ( vertsPtr < 0 || trisPtr < 0 || numVerts <= 0 || numTris <= 0 ) {
            return JPH::ShapeRefC();
        }
        if ( static_cast<uint64_t>( vertsPtr ) + numVerts * 0x10u > dataSize ||
             static_cast<uint64_t>( trisPtr ) + numTris * 0x0Cu > dataSize ) {
            return JPH::ShapeRefC();
        }

        JPH::VertexList vertices;
        const uint8_t *pV = pData + vertsPtr;
        for ( int32_t i = 0; i < numVerts; i++ ) {
            const float *p = reinterpret_cast<const float *>( pV + i * 0x10 );
            vertices.push_back( JPH::Float3( p[0], p[1], p[2] ) );
        }
        JPH::IndexedTriangleList triangles;
        const uint8_t *pT = pData + trisPtr;
        for ( int32_t i = 0; i < numTris; i++ ) {
            const int32_t *t =
                reinterpret_cast<const int32_t *>( pT + i * 0x0C );
            if ( t[0] < 0 || t[1] < 0 || t[2] < 0 || t[0] >= numVerts ||
                 t[1] >= numVerts || t[2] >= numVerts ) {
                continue;
            }
            triangles.push_back( JPH::IndexedTriangle(
                static_cast<uint32_t>( t[0] ), static_cast<uint32_t>( t[1] ),
                static_cast<uint32_t>( t[2] ), 0 ) );
        }
        if ( vertices.size() < 3 || triangles.empty() ) {
            return JPH::ShapeRefC();
        }
        JPH::MeshShapeSettings settings( std::move( vertices ),
                                         std::move( triangles ) );
        JPH::ShapeSettings::ShapeResult result = settings.Create();
        return result.IsValid() ? result.Get() : JPH::ShapeRefC();
    }

    // Build a Jolt ConvexHullShape from a Havok hkConvexVerticesShape (size
    // 0x50). Vertices are SIMD-swizzled in FourVectors blocks (stride 0x30 =
    // 3x Vector4): block+0x00=x[4], +0x10=y[4], +0x20=z[4].
    //   +0x30 m_rotatedVertices : hkArray<FourVectors> { data@+0x30 }
    //   +0x3C m_numVertices (real count; last block lane-padded)
    JPH::ShapeRefC buildConvexVertices( const HavokPackfile &pf,
                                        int32_t shapeOffset ) {
        const uint8_t *pData = pf.getDataBytes();
        const uint32_t dataSize = pf.getDataSize();
        if ( pData == nullptr || shapeOffset < 0 ) {
            return JPH::ShapeRefC();
        }
        const uint8_t *pShape = pData + shapeOffset;

        const int32_t blocksPtr =
            pf.resolvePointer( static_cast<uint32_t>( shapeOffset ) + 0x30 );
        const int32_t numVertices =
            *reinterpret_cast<const int32_t *>( pShape + 0x3C );
        if ( blocksPtr < 0 || numVertices <= 0 || numVertices > 4096 ) {
            return JPH::ShapeRefC();
        }
        const uint32_t numBlocks =
            ( static_cast<uint32_t>( numVertices ) + 3 ) / 4;
        if ( static_cast<uint64_t>( blocksPtr ) + numBlocks * 0x30u >
             dataSize ) {
            return JPH::ShapeRefC();
        }

        JPH::Array<JPH::Vec3> points;
        const uint8_t *pBlocks = pData + blocksPtr;
        for ( int32_t v = 0; v < numVertices; v++ ) {
            const uint8_t *b = pBlocks + ( v >> 2 ) * 0x30;
            const int32_t lane = v & 3;
            const float *fx = reinterpret_cast<const float *>( b + 0x00 );
            const float *fy = reinterpret_cast<const float *>( b + 0x10 );
            const float *fz = reinterpret_cast<const float *>( b + 0x20 );
            points.push_back( JPH::Vec3( fx[lane], fy[lane], fz[lane] ) );
        }
        JPH::ConvexHullShapeSettings settings(
            points, JPH::cDefaultConvexRadius,
            readShapeMaterial( pf, shapeOffset ) );
        JPH::ShapeSettings::ShapeResult result = settings.Create();
        return result.IsValid() ? result.Get() : JPH::ShapeRefC();
    }

    // Build a Jolt capsule from a Havok hkCapsuleShape's parameters. Havok
    // capsules run between two arbitrary local endpoints; Jolt capsules are
    // Y-axis-aligned + origin-centred, so wrap in a rotated/translated shape
    // to place the segment A..B.
    JPH::ShapeRefC makeCapsule( const JPH::Vec3 &a,
                                const JPH::Vec3 &b,
                                float radius,
                                const JPH::PhysicsMaterial *pMaterial ) {
        if ( !std::isfinite( radius ) || radius <= 0.0f ) {
            return JPH::ShapeRefC();
        }
        const JPH::Vec3 axis = b - a;
        const float len = axis.Length();
        const JPH::Vec3 center = ( a + b ) * 0.5f;

        JPH::ShapeSettings::ShapeResult inner;
        JPH::Quat rotation = JPH::Quat::sIdentity();
        if ( len < 1.0e-4f ) {
            inner = JPH::SphereShapeSettings( radius, pMaterial )
                        .Create(); // degenerate
        } else {
            inner = JPH::CapsuleShapeSettings( len * 0.5f, radius, pMaterial )
                        .Create();
            rotation = JPH::Quat::sFromTo( JPH::Vec3( 0, 1, 0 ), axis / len );
        }
        if ( !inner.IsValid() ) {
            return JPH::ShapeRefC();
        }
        JPH::RotatedTranslatedShapeSettings placed( center, rotation,
                                                    inner.Get() );
        JPH::ShapeSettings::ShapeResult result = placed.Create();
        return result.IsValid() ? result.Get() : JPH::ShapeRefC();
    }

    // Build a Jolt box from a Havok hkBoxShape's half-extents + convex radius.
    JPH::ShapeRefC makeBox( const JPH::Vec3 &halfExtents,
                            float radius,
                            const JPH::PhysicsMaterial *pMaterial ) {
        // Reject non-finite / non-positive extents (Jolt asserts/crashes on
        // them). TDU's real boxes are all small positive half-extents.
        for ( int32_t i = 0; i < 3; i++ ) {
            const float he = halfExtents[i];
            if ( !std::isfinite( he ) || he <= 0.0f || he > 1.0e5f ) {
                return JPH::ShapeRefC();
            }
        }

        // Jolt requires the convex radius to be <= the smallest half extent;
        // TDU's thin panel boxes go down to 0.001, so clamp accordingly.
        const float minHalfExtent = std::min(
            { halfExtents.GetX(), halfExtents.GetY(), halfExtents.GetZ() } );
        float convexRadius = Min( radius, minHalfExtent * 0.8f );
        if ( convexRadius < 0.0f ) {
            convexRadius = 0.0f;
        }

        JPH::BoxShapeSettings settings( halfExtents, convexRadius, pMaterial );
        JPH::ShapeSettings::ShapeResult result = settings.Create();
        return result.IsValid() ? result.Get() : JPH::ShapeRefC();
    }

    JPH::ShapeRefC makeSphere( float radius,
                               const JPH::PhysicsMaterial *pMaterial ) {
        if ( !std::isfinite( radius ) || radius <= 0.0f ) {
            return JPH::ShapeRefC();
        }
        JPH::ShapeSettings::ShapeResult r =
            JPH::SphereShapeSettings( radius, pMaterial ).Create();
        return r.IsValid() ? r.Get() : JPH::ShapeRefC();
    }
} // namespace

HavokToJolt::ScalablePrimitive
HavokToJolt::readScalablePrimitive( const HavokPackfile &pf,
                                    int32_t shapeOffset ) {
    // FUN_00453690:124 — the shape-type switch the instancer scales through.
    ScalablePrimitive out;
    const uint8_t *pData = pf.getDataBytes();
    if ( pData == nullptr || shapeOffset < 0 ) {
        return out;
    }
    const char *pClass =
        pf.getClassName( static_cast<uint32_t>( shapeOffset ) );
    if ( pClass == nullptr ) {
        return out;
    }
    const uint8_t *p = pData + shapeOffset;

    // hkSphereShape (type 4): radius @ +0x0C.
    if ( std::strcmp( pClass, "hkSphereShape" ) == 0 ) {
        out.Kind = ScalablePrimitive::Sphere;
        out.Radius = *reinterpret_cast<const float *>( p + 0x0c );
        out.pMaterial = readShapeMaterial( pf, shapeOffset );
        return out;
    }
    // hkBoxShape (type 7): convex radius @ +0x0C, half-extents @ +0x10.
    if ( std::strcmp( pClass, "hkBoxShape" ) == 0 ) {
        const float *pHE = reinterpret_cast<const float *>( p + 0x10 );
        out.Kind = ScalablePrimitive::Box;
        out.Radius = *reinterpret_cast<const float *>( p + 0x0c );
        out.HalfExtents = JPH::Vec3( pHE[0], pHE[1], pHE[2] );
        out.pMaterial = readShapeMaterial( pf, shapeOffset );
        return out;
    }
    // hkCapsuleShape (type 8): radius @ +0x0C, m_vertexA @ +0x10, B @ +0x20.
    if ( std::strcmp( pClass, "hkCapsuleShape" ) == 0 ) {
        const float *pA = reinterpret_cast<const float *>( p + 0x10 );
        const float *pB = reinterpret_cast<const float *>( p + 0x20 );
        out.Kind = ScalablePrimitive::Capsule;
        out.Radius = *reinterpret_cast<const float *>( p + 0x0c );
        out.VertexA = JPH::Vec3( pA[0], pA[1], pA[2] );
        out.VertexB = JPH::Vec3( pB[0], pB[1], pB[2] );
        out.pMaterial = readShapeMaterial( pf, shapeOffset );
        return out;
    }
    return out;
}

JPH::ShapeRefC HavokToJolt::buildScaledShape( const RigidBody &body,
                                              const JPH::Vec3 &scale ) {
    // FUN_00453690:124-163 — only the three primitives are rescaled; anything
    // else (hkTransformShape, hkListShape, meshes) is instanced as-is.
    JPH::ShapeRefC scaled;
    switch ( body.Scalable.Kind ) {
    case ScalablePrimitive::Sphere:
        // A sphere only ever takes the X component of the scale.
        scaled = makeSphere( body.Scalable.Radius * scale.GetX(),
                             body.Scalable.pMaterial );
        break;
    case ScalablePrimitive::Box:
        scaled = makeBox( body.Scalable.HalfExtents * scale,
                          body.Scalable.Radius, body.Scalable.pMaterial );
        break;
    case ScalablePrimitive::Capsule:
        scaled = makeCapsule( body.Scalable.VertexA * scale,
                              body.Scalable.VertexB * scale,
                              body.Scalable.Radius * scale.GetX(),
                              body.Scalable.pMaterial );
        break;
    case ScalablePrimitive::None:
    default:
        break;
    }
    return scaled.GetPtr() != nullptr ? scaled : body.Shape;
}

JPH::ShapeRefC HavokToJolt::buildShape( const HavokPackfile &pf,
                                        int32_t shapeOffset,
                                        int32_t depth ) {
    const uint8_t *pData = pf.getDataBytes();
    if ( shapeOffset < 0 || pData == nullptr || depth > 16 ) {
        return JPH::ShapeRefC();
    }

    const char *pClass =
        pf.getClassName( static_cast<uint32_t>( shapeOffset ) );
    if ( pClass == nullptr ) {
        return JPH::ShapeRefC();
    }
    const uint8_t *p = pData + shapeOffset;

    if ( std::strcmp( pClass, "hkBoxShape" ) == 0 ) {
        const ScalablePrimitive prim = readScalablePrimitive( pf, shapeOffset );
        return makeBox( prim.HalfExtents, prim.Radius, prim.pMaterial );
    }

    if ( std::strcmp( pClass, "hkTransformShape" ) == 0 ) {
        const int32_t childOffset =
            pf.resolvePointer( static_cast<uint32_t>( shapeOffset ) + 0x08 );
        JPH::ShapeRefC child = buildShape( pf, childOffset, depth + 1 );
        if ( child.GetPtr() == nullptr ) {
            return JPH::ShapeRefC();
        }

        const float *pT = reinterpret_cast<const float *>( p + 0x50 );
        const JPH::Quat rotation =
            rotationFromColumns( reinterpret_cast<const float *>( p + 0x20 ),
                                 reinterpret_cast<const float *>( p + 0x30 ),
                                 reinterpret_cast<const float *>( p + 0x40 ) );

        JPH::RotatedTranslatedShapeSettings settings(
            JPH::Vec3( pT[0], pT[1], pT[2] ), rotation, child );
        JPH::ShapeSettings::ShapeResult result = settings.Create();
        return result.IsValid() ? result.Get() : JPH::ShapeRefC();
    }

    if ( std::strcmp( pClass, "hkListShape" ) == 0 ) {
        const int32_t childArray =
            pf.resolvePointer( static_cast<uint32_t>( shapeOffset ) + 0x10 );
        int32_t count = *reinterpret_cast<const int32_t *>( p + 0x14 );
        if ( count < 0 || count > 4096 ) {
            return JPH::ShapeRefC(); // guard against a garbage child count
        }

        std::vector<JPH::ShapeRefC> children;
        for ( int32_t i = 0; i < count && childArray >= 0; i++ ) {
            const int32_t childOffset = pf.resolvePointer(
                static_cast<uint32_t>( childArray ) + i * 8 );
            JPH::ShapeRefC child = buildShape( pf, childOffset, depth + 1 );
            if ( child.GetPtr() != nullptr ) {
                children.push_back( child );
            }
        }

        if ( children.empty() ) {
            return JPH::ShapeRefC();
        }
        if ( children.size() == 1 ) {
            return children[0]; // StaticCompound needs >= 2 children
        }

        JPH::StaticCompoundShapeSettings settings;
        for ( const JPH::ShapeRefC &child : children ) {
            settings.AddShape( JPH::Vec3::sZero(), JPH::Quat::sIdentity(),
                               child );
        }
        JPH::ShapeSettings::ShapeResult result = settings.Create();
        return result.IsValid() ? result.Get() : JPH::ShapeRefC();
    }

    // NOTE: keep in sync with isBvTreeShapeClass below.
    // --- World/road static collision shapes (Havok MOPP mesh family) ---

    // hkBvTreeShape family (incl. hkMoppBvTreeShape): the wrapped collection is
    // at +0x0C. The MOPP acceleration structure is discarded (Jolt builds its
    // own BVH); the child is a mesh, or an hkListShape of meshes (recursion).
    if ( std::strcmp( pClass, "hkMoppBvTreeShape" ) == 0 ||
         std::strcmp( pClass, "hkTriSampledHeightFieldBvTreeShape" ) == 0 ||
         std::strcmp( pClass, "hkBvTreeShape" ) == 0 ) {
        const int32_t childOffset =
            pf.resolvePointer( static_cast<uint32_t>( shapeOffset ) + 0x0C );
        return buildShape( pf, childOffset, depth + 1 );
    }

    if ( std::strcmp( pClass, "hkMeshShape" ) == 0 ||
         std::strcmp( pClass, "hkFastMeshShape" ) == 0 ) {
        return buildMeshCollection( pf, shapeOffset );
    }

    // hkStorageMeshShape owns its geometry in storage arrays (its base subpart
    // vertex/index pointers are null in the packfile), so it needs its own
    // path.
    if ( std::strcmp( pClass, "hkStorageMeshShape" ) == 0 ) {
        return buildStorageMesh( pf, shapeOffset );
    }

    if ( std::strcmp( pClass, "hkSimpleMeshShape" ) == 0 ) {
        return buildSimpleMesh( pf, shapeOffset );
    }

    if ( std::strcmp( pClass, "hkConvexVerticesShape" ) == 0 ) {
        return buildConvexVertices( pf, shapeOffset );
    }

    if ( std::strcmp( pClass, "hkCapsuleShape" ) == 0 ) {
        const ScalablePrimitive prim = readScalablePrimitive( pf, shapeOffset );
        return makeCapsule( prim.VertexA, prim.VertexB, prim.Radius,
                            prim.pMaterial );
    }

    // hkSphereShape (extends hkConvexShape): radius @ +0x0C, centred at origin.
    if ( std::strcmp( pClass, "hkSphereShape" ) == 0 ) {
        const ScalablePrimitive prim = readScalablePrimitive( pf, shapeOffset );
        return makeSphere( prim.Radius, prim.pMaterial );
    }

    // Unhandled shape class — logged (temporary bring-up diagnostic) so any
    // remaining world/road shape type without a converter is surfaced.
    OTDU_LOG_WARN( "HavokToJolt: unhandled shape class '%s' @0x%x\n", pClass,
                   shapeOffset );
    return JPH::ShapeRefC();
}

namespace {
    // Recover a hkRigidBody's registry index from its 0x0FFFFFFF property
    // (property array @body+0x44, count @body+0x48, each property 0x10 bytes
    // { key@0x00, value@0x08 }); see FUN_00452060. 0xFFFFFFFF if absent.
    uint32_t readRegistryIndexProperty( const HavokPackfile &pf,
                                        int32_t bodyOffset ) {
        const uint8_t *pData = pf.getDataBytes();
        const uint32_t dataSize = pf.getDataSize();
        constexpr uint32_t kRegistryIndexProperty = 0x0FFFFFFFu;
        const int32_t propArray =
            pf.resolvePointer( static_cast<uint32_t>( bodyOffset ) + 0x44 );
        if ( propArray < 0 ||
             static_cast<uint32_t>( bodyOffset ) + 0x48 + 4 > dataSize ) {
            return 0xFFFFFFFFu;
        }
        const int32_t propCount =
            *reinterpret_cast<const int32_t *>( pData + bodyOffset + 0x48 );
        for ( int32_t p = 0; p < propCount && p < 4096; p++ ) {
            const uint32_t propOff =
                static_cast<uint32_t>( propArray ) + p * 0x10;
            if ( propOff + 0x10 > dataSize ) {
                break;
            }
            const uint32_t key =
                *reinterpret_cast<const uint32_t *>( pData + propOff );
            if ( key == kRegistryIndexProperty ) {
                return *reinterpret_cast<const uint32_t *>( pData + propOff +
                                                            0x08 );
            }
        }
        return 0xFFFFFFFFu;
    }
} // namespace

// hkpShapeType 0x18 (the hkBvTreeShape family) as tested by FUN_00845380.
static bool isBvTreeShapeClass( const HavokPackfile &pf, int32_t shapeOffset ) {
    if ( shapeOffset < 0 ) {
        return false;
    }
    const char *pClass = pf.getClassName( static_cast<uint32_t>( shapeOffset ) );
    if ( pClass == nullptr ) {
        return false;
    }
    return std::strcmp( pClass, "hkMoppBvTreeShape" ) == 0 ||
           std::strcmp( pClass, "hkTriSampledHeightFieldBvTreeShape" ) == 0 ||
           std::strcmp( pClass, "hkBvTreeShape" ) == 0;
}

void HavokToJolt::buildBodies( const HavokPackfile &pf,
                               std::vector<RigidBody> &outBodies ) {
    const uint8_t *pData = pf.getDataBytes();
    if ( pData == nullptr ) {
        return;
    }

    const uint32_t dataSize = pf.getDataSize();

    // hkPhysicsSystem root @ __data__+0: +0x08 -> rigid-body array, +0x0c
    // count.
    const int32_t bodiesArray = pf.resolvePointer( 0x08 );
    const int32_t numBodies =
        *reinterpret_cast<const int32_t *>( pData + 0x0c );

    for ( int32_t i = 0; i < numBodies && bodiesArray >= 0; i++ ) {
        const int32_t bodyOffset =
            pf.resolvePointer( static_cast<uint32_t>( bodiesArray ) + i * 4 );
        if ( bodyOffset < 0 ) {
            continue;
        }

        const int32_t shapeOffset =
            pf.resolvePointer( static_cast<uint32_t>( bodyOffset ) + 0x14 );
        JPH::ShapeRefC shape = buildShape( pf, shapeOffset );
        if ( shape.GetPtr() == nullptr ) {
            continue;
        }

        RigidBody body;
        body.Shape = shape;
        body.bBvTreeShape = isBvTreeShapeClass( pf, shapeOffset );
        body.Scalable = readScalablePrimitive( pf, shapeOffset );
        body.Position = JPH::Vec3::sZero();
        body.Rotation = JPH::Quat::sIdentity();

        const int32_t motionOffset =
            pf.resolvePointer( static_cast<uint32_t>( bodyOffset ) + 0x50 );
        if ( motionOffset >= 0 ) {
            const float *pPos =
                reinterpret_cast<const float *>( pData + motionOffset + 0x40 );
            body.Position = JPH::Vec3( pPos[0], pPos[1], pPos[2] );
            body.Rotation = rotationFromColumns(
                reinterpret_cast<const float *>( pData + motionOffset + 0x10 ),
                reinterpret_cast<const float *>( pData + motionOffset + 0x20 ),
                reinterpret_cast<const float *>( pData + motionOffset +
                                                 0x30 ) );
            // hkFixedRigidMotion = an anchored (infinite-mass) member: the
            // "base" that breakable pieces hang off of. Dynamic pieces use
            // hkBoxMotion / hkSphereMotion / hkKeyframedRigidMotion etc.
            const char *pMotionClass =
                pf.getClassName( static_cast<uint32_t>( motionOffset ) );
            body.bFixedMotion =
                pMotionClass != nullptr &&
                std::strcmp( pMotionClass, "hkFixedRigidMotion" ) == 0;

            // Asset mass: hkMotion stores 1/mass at +0xc0 (verified: round
            // 60..1000 kg values). Jolt derives the inertia tensor from the
            // shape scaled to this mass. 0 -> the caller uses a default.
            if ( static_cast<uint32_t>( motionOffset ) + 0xc4 <= dataSize ) {
                const float invMass = *reinterpret_cast<const float *>(
                    pData + motionOffset + 0xc0 );
                if ( invMass > 1.0e-6f ) {
                    body.Mass =
                        std::min( std::max( 1.0f / invMass, 0.1f ), 1.0e5f );
                }
            }
        }

        // Recover the registry index from the hkRigidBody's property array
        // (FUN_00452060 / FUN_004126e0), used so createFromBinary can stamp
        // registry[index].GlobalHashcode onto the body — identity-correct,
        // unlike stamping RigidBody entries by order (which breaks whenever a
        // body's shape fails to build).
        body.RegistryIndex = readRegistryIndexProperty( pf, bodyOffset );

        if ( static_cast<uint32_t>( bodyOffset ) + 0x64 <= dataSize ) {
            const float *pMaterial =
                reinterpret_cast<const float *>( pData + bodyOffset + 0x5c );
            body.Friction = pMaterial[0];
            body.Restitution = pMaterial[1];
        }

        outBodies.push_back( body );
    }
}

void HavokToJolt::buildConstraints( const HavokPackfile &pf,
                                    std::vector<Constraint> &outConstraints ) {
    // hkPhysicsSystem root @ __data__+0: m_constraints ptr @+0x14, count @+0x18
    // (m_rigidBodies is @+0x08/+0x0c; see buildBodies + FUN_00452310).
    const uint8_t *pData = pf.getDataBytes();
    if ( pData == nullptr ) {
        return;
    }
    const uint32_t dataSize = pf.getDataSize();

    const int32_t constraintArray = pf.resolvePointer( 0x14 );
    if ( constraintArray < 0 || 0x18 + 4 > dataSize ) {
        return;
    }
    const int32_t numConstraints =
        *reinterpret_cast<const int32_t *>( pData + 0x18 );

    for ( int32_t i = 0; i < numConstraints && i < 65536; i++ ) {
        const int32_t instOffset = pf.resolvePointer(
            static_cast<uint32_t>( constraintArray ) + i * 4 );
        if ( instOffset < 0 ) {
            continue;
        }

        // hkConstraintInstance: constraintData@+0x0c, bodyA@+0x10, bodyB@+0x14.
        const int32_t bodyAOffset =
            pf.resolvePointer( static_cast<uint32_t>( instOffset ) + 0x10 );
        const int32_t bodyBOffset =
            pf.resolvePointer( static_cast<uint32_t>( instOffset ) + 0x14 );
        if ( bodyAOffset < 0 || bodyBOffset < 0 ) {
            continue;
        }

        Constraint c;
        c.BodyARegistryIndex = readRegistryIndexProperty( pf, bodyAOffset );
        c.BodyBRegistryIndex = readRegistryIndexProperty( pf, bodyBOffset );
        if ( c.BodyARegistryIndex == 0xFFFFFFFFu ||
             c.BodyBRegistryIndex == 0xFFFFFFFFu ) {
            continue; // a constraint whose bodies we can't identify
        }

        // hkBreakableConstraintData (getType==0xc) wraps a child joint and
        // breaks when the solver impulse exceeds m_threshold (constraintData+
        // 0x14). Non-breakable data (hkGenericConstraintData/hkRagdoll...) is a
        // permanent fixed joint here.
        const int32_t dataOffset =
            pf.resolvePointer( static_cast<uint32_t>( instOffset ) + 0x0c );
        if ( dataOffset >= 0 ) {
            const char *pClass =
                pf.getClassName( static_cast<uint32_t>( dataOffset ) );
            if ( pClass != nullptr &&
                 std::strcmp( pClass, "hkBreakableConstraintData" ) == 0 &&
                 static_cast<uint32_t>( dataOffset ) + 0x18 <= dataSize ) {
                c.bBreakable = true;
                c.Threshold = *reinterpret_cast<const float *>(
                    pData + dataOffset + 0x14 );
            }
        }

        outConstraints.push_back( c );
    }
}
