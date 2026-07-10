#include "shared.h"
#include "havok_to_jolt.h"
#include "havok_packfile.h"

#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>
#include <Jolt/Physics/Collision/Shape/StaticCompoundShape.h>
#include <Jolt/Physics/Collision/Shape/MeshShape.h>
#include <Jolt/Physics/Collision/Shape/ConvexHullShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>

#include <cmath>
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

        for ( int32_t s = 0; s < numStorage; s++ ) {
            const int32_t storObj =
                pf.resolvePointer( static_cast<uint32_t>( storArr ) + s * 4 );
            if ( storObj < 0 ) {
                continue;
            }
            const uint8_t *pStor = pData + storObj;
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
                            0 ) );
                    }
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
                                         std::move( triangles ) );
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
        JPH::ConvexHullShapeSettings settings( points );
        JPH::ShapeSettings::ShapeResult result = settings.Create();
        return result.IsValid() ? result.Get() : JPH::ShapeRefC();
    }

    // Build a Jolt capsule from a Havok hkCapsuleShape (size 0x30, extends
    // hkConvexShape): radius@+0x0C, m_vertexA(Vector4)@+0x10, m_vertexB@+0x20.
    // Havok capsules run between two arbitrary local endpoints; Jolt capsules
    // are Y-axis-aligned + origin-centred, so wrap in a rotated/translated
    // shape to place the segment A..B.
    JPH::ShapeRefC buildCapsule( const HavokPackfile &pf,
                                 int32_t shapeOffset ) {
        const uint8_t *pData = pf.getDataBytes();
        if ( pData == nullptr || shapeOffset < 0 ) {
            return JPH::ShapeRefC();
        }
        const uint8_t *p = pData + shapeOffset;
        const float radius = *reinterpret_cast<const float *>( p + 0x0C );
        const float *pA = reinterpret_cast<const float *>( p + 0x10 );
        const float *pB = reinterpret_cast<const float *>( p + 0x20 );
        if ( !std::isfinite( radius ) || radius <= 0.0f ) {
            return JPH::ShapeRefC();
        }
        const JPH::Vec3 a( pA[0], pA[1], pA[2] );
        const JPH::Vec3 b( pB[0], pB[1], pB[2] );
        const JPH::Vec3 axis = b - a;
        const float len = axis.Length();
        const JPH::Vec3 center = ( a + b ) * 0.5f;

        JPH::ShapeSettings::ShapeResult inner;
        JPH::Quat rotation = JPH::Quat::sIdentity();
        if ( len < 1.0e-4f ) {
            inner = JPH::SphereShapeSettings( radius ).Create(); // degenerate
        } else {
            inner = JPH::CapsuleShapeSettings( len * 0.5f, radius ).Create();
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
} // namespace

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
        const float *pHE = reinterpret_cast<const float *>( p + 0x10 );
        const float radius = *reinterpret_cast<const float *>( p + 0x0c );
        // Reject non-finite / non-positive extents (Jolt asserts/crashes on
        // them). TDU's real boxes are all small positive half-extents.
        if ( !std::isfinite( pHE[0] ) || !std::isfinite( pHE[1] ) ||
             !std::isfinite( pHE[2] ) || pHE[0] <= 0.0f || pHE[1] <= 0.0f ||
             pHE[2] <= 0.0f || pHE[0] > 1.0e5f || pHE[1] > 1.0e5f ||
             pHE[2] > 1.0e5f ) {
            return JPH::ShapeRefC();
        }
        const JPH::Vec3 halfExtents( pHE[0], pHE[1], pHE[2] );

        // Jolt requires the convex radius to be <= the smallest half extent;
        // TDU's thin panel boxes go down to 0.001, so clamp accordingly.
        const float minHalfExtent = std::min( { pHE[0], pHE[1], pHE[2] } );
        float convexRadius = Min( radius, minHalfExtent * 0.8f );
        if ( convexRadius < 0.0f ) {
            convexRadius = 0.0f;
        }

        JPH::BoxShapeSettings settings( halfExtents, convexRadius );
        JPH::ShapeSettings::ShapeResult result = settings.Create();
        return result.IsValid() ? result.Get() : JPH::ShapeRefC();
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
        return buildCapsule( pf, shapeOffset );
    }

    // hkSphereShape (extends hkConvexShape): radius @ +0x0C, centred at origin.
    if ( std::strcmp( pClass, "hkSphereShape" ) == 0 ) {
        const float radius = *reinterpret_cast<const float *>( p + 0x0c );
        if ( !std::isfinite( radius ) || radius <= 0.0f ) {
            return JPH::ShapeRefC();
        }
        JPH::ShapeSettings::ShapeResult r =
            JPH::SphereShapeSettings( radius ).Create();
        return r.IsValid() ? r.Get() : JPH::ShapeRefC();
    }

    // Unhandled shape class — logged (temporary bring-up diagnostic) so any
    // remaining world/road shape type without a converter is surfaced.
    OTDU_LOG_WARN( "HavokToJolt: unhandled shape class '%s' @0x%x\n", pClass,
                   shapeOffset );
    return JPH::ShapeRefC();
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
        }

        // Recover the registry index from the hkRigidBody's property array
        // (FUN_00452060 / FUN_004126e0): property array @body+0x44, count
        // @body+0x48; each property is 0x10 bytes { key@0x00, ..., value@0x08
        // }. The property with key 0x0FFFFFFF holds the registry index of this
        // body's entry (== its LocalHashcode). The original uses that index to
        // bind registry[index].pPointer = body; the port keeps it so
        // createFromBinary can stamp registry[index].GlobalHashcode onto the
        // body — identity-correct, unlike stamping RigidBody entries by order
        // (which breaks whenever a body's shape fails to build).
        constexpr uint32_t kRegistryIndexProperty = 0x0FFFFFFFu;
        const int32_t propArray =
            pf.resolvePointer( static_cast<uint32_t>( bodyOffset ) + 0x44 );
        if ( propArray >= 0 &&
             static_cast<uint32_t>( bodyOffset ) + 0x48 + 4 <= dataSize ) {
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
                    body.RegistryIndex = *reinterpret_cast<const uint32_t *>(
                        pData + propOff + 0x08 );
                    break;
                }
            }
        }

        outBodies.push_back( body );
    }
}
