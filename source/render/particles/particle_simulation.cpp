#include "shared.h"
#include "particle_file.h"

#include "gs_particles.h"

#include "core/maths_helper.h"
#include "render/camera/camera_base.h"
#include "render/draw_list.h"
#include "render/gs_render.h"
#include "render/render_scene.h"
#include "core/binary_helpers.h"
#include "core/random/lcg.h"

namespace {
    // DAT_016a8120
    static constexpr const float kCubeCorners[8][3] = {
        {0.5f,  -0.5f, -0.5f},
        {0.5f,  0.5f,  -0.5f},
        {0.5f,  0.5f,  0.5f },
        {0.5f,  -0.5f, 0.5f },
        {-0.5f, -0.5f, -0.5f},
        {-0.5f, 0.5f,  -0.5f},
        {-0.5f, 0.5f,  0.5f },
        {-0.5f, -0.5f, 0.5f },
    };
    static constexpr const int kCubeEdges[12][2] = {
        {0, 1},
        {1, 2},
        {2, 3},
        {3, 0},
        {4, 5},
        {5, 6},
        {6, 7},
        {7, 4},
        {0, 4},
        {1, 5},
        {2, 6},
        {3, 7}
    };
    static constexpr const float kSquareCorners[4][2] = {
        // (x, z)
        {0.5f,  0.5f },
        {0.5f,  -0.5f},
        {-0.5f, -0.5f},
        {-0.5f, 0.5f }
    };
    static constexpr const int kSquareEdges[4][2] = {
        {0, 1},
        {1, 2},
        {2, 3},
        {3, 0}
    };
    static constexpr const float kSquareEdgeNormals[4][2] = {
        // (x, z), outward
        {1.0f,  0.0f },
        {0.0f,  -1.0f},
        {-1.0f, 0.0f },
        {0.0f,  1.0f }
    };

    constexpr float kRandScale = 1.0f / 32768.0f; // DAT_00f41658 (2^-15)
    const float kEmitDistance = BitsToFloat( 0x3b008081 );     // DAT_00f41d18
    const float kEmitThreshold = BitsToFloat( 0x358637bd );    // FLOAT_00f41888
    constexpr float kOldestAgeSentinel = -99.0f;               // DAT_00f41e30
    const float kGravityThreshold = BitsToFloat( 0x38d1b717 ); // DAT_00f41760

    uint8_t *gScratchAttr[0x800]; // DAT_010e3238
    uint8_t *gScratchPos[0x800];  // DAT_010e4238
    uint8_t *gScratchVel[0x800];  // DAT_010e5238

    float gScratchTransformedPos[0x800 * 4]; // DAT_0169ac00

    constexpr float kCurveLutScale = 15.0f;    // DAT_00f41700
    constexpr float kCurveClamp = 0.99000001f; // DAT_00f41a44

    uint32_t gSpriteOrder[0x800];

    uint32_t LerpColorBytes( uint32_t from, uint32_t to, uint32_t t ) {
        // FUN_0045ad50
        uint32_t result = 0;
        for ( uint32_t shift = 0; shift < 32; shift += 8 ) {
            const uint32_t a = ( from >> shift ) & 0xff;
            const uint32_t b = ( to >> shift ) & 0xff;
            result |= ( ( ( a * ( 0xff - t ) + b * t ) >> 8 ) & 0xff ) << shift;
        }
        return result;
    }

    uint32_t ScaleColorBytes( uint32_t color, uint32_t scale ) {
        // FUN_0045adc0 (corner tint)
        uint32_t result = 0;
        for ( uint32_t shift = 0; shift < 32; shift += 8 ) {
            result |= ( ( ( ( color >> shift ) & 0xff ) * scale ) >> 8 )
                      << shift;
        }
        return result;
    }

    enum eSpriteUVFlags : uint16_t {
        SUV_Enabled = 0x0001, // Without this the raw quad UVs are used
        SUV_FlipU = 0x0002,   // Mirror horizontally (particle seed bit 6)
        SUV_FlipV = 0x0004,   // Mirror vertically (particle seed bit 7)
        SUV_RandomSpinSign = 0x0008, // Spin direction from particle seed bit 5
        SUV_RandomFrame = 0x0010,    // Random sub-texture start frame
        SUV_Rotate = 0x0020,         // Rotate the UVs around their center
        SUV_SubTexture = 0x0040,     // Pick one cell out of the atlas
        SUV_FrameRate = 0x0080, // Animate at +0x90 fps instead of over life
        SUV_Scale = 0x0300,     // Scale the UVs around their center
        SUV_RandomRotation = 0x0400, // Randomize the base rotation per particle
        SUV_VelocityAligned = 0x0800, // Align the UVs with the screen velocity
    };

    constexpr float kSeedToUnit = 1.0f / 65536.0f; //  DAT_00f41678

    using ParticleSpawnCallback = void ( * )( uint8_t *pDef,
                                              uint8_t *pAttr,
                                              uint8_t *pVel,
                                              uint8_t *pUpdateState,
                                              uint8_t *pDrawState );

    using ParticleForceCallback = void ( * )( uint8_t *pEmitter,
                                              uint8_t *pDef,
                                              uint8_t **ppAttr,
                                              uint8_t **ppVel,
                                              uint8_t **ppPos,
                                              int32_t count );

    template <typename TCallback>
    TCallback ReadCallback( const uint8_t *pRecord, uint32_t offset ) {
        const x86Pointer_t *pSlot =
            reinterpret_cast<const x86Pointer_t *>( pRecord + offset );
        return reinterpret_cast<TCallback>( static_cast<void *>( *pSlot ) );
    }

    static Matrix4x4f_t BuildDefinitionTransform( const uint8_t *pDef ) {
        // FUN_00458a30 (inlined in FUN_0045dec0)
        Eigen::Vector3f scale( Read<float>( pDef, 0x10 ),
                               Read<float>( pDef, 0x14 ),
                               Read<float>( pDef, 0x18 ) );
        Matrix4x4f_t transform = Matrix4x4f_t::Identity();
        transform = transform * MakeScaleMatrix( scale );
        MatrixRotateX( transform, Read<float>( pDef, 0x20 ) );
        MatrixRotateY( transform, Read<float>( pDef, 0x24 ) );
        MatrixRotateZ( transform, Read<float>( pDef, 0x28 ) );
        transform( 3, 0 ) += Read<float>( pDef, 0x30 );
        transform( 3, 1 ) += Read<float>( pDef, 0x34 );
        transform( 3, 2 ) += Read<float>( pDef, 0x38 );
        return transform;
    }

    int32_t GetGridCellIndex( const uint8_t *pGrid, const float *pPosition ) {
        // FUN_0045e2f0
        const float invCellSize = 1.0f / Read<float>( pGrid, 4 );
        const int32_t size = Read<int32_t>( pGrid, 8 );
        const int32_t x = static_cast<int32_t>(
            ( pPosition[0] - Read<float>( pGrid, 0x10 ) ) * invCellSize );
        const int32_t y = static_cast<int32_t>(
            ( pPosition[1] - Read<float>( pGrid, 0x14 ) ) * invCellSize );
        const int32_t z = static_cast<int32_t>(
            ( pPosition[2] - Read<float>( pGrid, 0x18 ) ) * invCellSize );
        if ( x < 0 || y < 0 || z < 0 || size <= x || size <= y || size <= z ) {
            return -1;
        }
        return ( x * size + y ) * size + z;
    }

    float ParticleDistance( const uint8_t *pPositionA,
                            const uint8_t *pPositionB ) {
        // FUN_0045e0a0
        const float dx =
            Read<float>( pPositionA, 0 ) - Read<float>( pPositionB, 0 );
        const float dy =
            Read<float>( pPositionA, 4 ) - Read<float>( pPositionB, 4 );
        const float dz =
            Read<float>( pPositionA, 8 ) - Read<float>( pPositionB, 8 );
        return std::sqrt( dy * dy + dz * dz + dx * dx );
    }

    void WriteLatticeLink( uint8_t *pLink,
                           int32_t slot,
                           uint32_t particleOffset,
                           float restLength ) {
        // FUN_0045e100
        Write<float>( pLink, 0x20 + slot * 4, restLength );
        Write<uint32_t>( pLink, slot * 4, particleOffset );
        Write<float>( pLink, 0x40 + slot * 4, restLength );
        Write<float>( pLink, 0x60 + slot * 4, -1.0f ); // FLOAT_00f41664
    }

    // A 2x3 UV affine {a, b, c, d, tx, ty}; corners are transformed as
    //   u' = a * u + c * v + tx
    //   v' = b * u + d * v + ty
    struct UVTransform {
        float m[6];
    };

    // Mirror matrices
    constexpr float kFlipUMatrix[6] = { -1.0f, 0.0f, 0.0f,
                                        1.0f,  1.0f, 0.0f }; // DAT_00fac008
    constexpr float kFlipVMatrix[6] = { 1.0f,  0.0f, 0.0f,
                                        -1.0f, 0.0f, 1.0f }; // DAT_00fabff0

    void ComposeUVTransform( UVTransform &accumulated,
                             const float ( &applied )[6] ) {
        // FUN_0045ac60
        const float *pA = applied;
        const float *pB = accumulated.m;
        const float composed[6] = {
            pA[0] * pB[0] + pA[2] * pB[1],
            pA[1] * pB[0] + pA[3] * pB[1],
            pA[0] * pB[2] + pA[2] * pB[3],
            pA[1] * pB[2] + pA[3] * pB[3],
            pA[0] * pB[4] + pA[2] * pB[5] + pA[4],
            pA[1] * pB[4] + pA[3] * pB[5] + pA[5],
        };
        std::memcpy( accumulated.m, composed, sizeof( composed ) );
    }

    UVTransform MakeUVRotation( float angle ) {
        const float cosAngle = std::cos( angle );
        const float sinAngle = std::sin( angle );
        return UVTransform{
            { cosAngle, sinAngle, -sinAngle, cosAngle,
             ( sinAngle * 0.5f - cosAngle * 0.5f ) + 0.5f,
             ( -sinAngle * 0.5f - cosAngle * 0.5f ) + 0.5f }
        };
    }

    UVTransform BuildSpriteUVTransform( const uint8_t *pRecord,
                                        const uint8_t *pAttr,
                                        const float *pVelocity,
                                        const Eigen::Vector3f &cameraRight,
                                        const Eigen::Vector3f &cameraUp ) {
        // FUN_0045adc0 (UV block)
        const uint16_t uvFlags = Read<uint16_t>( pRecord, 0x78 );
        const uint32_t seed = Read<uint32_t>( pAttr, 0x3c );
        const float age = Read<float>( pAttr, 0x1c );
        const float lifetime = Read<float>( pAttr, 0x20 );
        // The original divides unconditionally (particles always have a
        // lifetime); guard anyway to keep a stray zero out of the UVs.
        const float ageFraction = ( lifetime != 0.0f ) ? age / lifetime : 0.0f;

        UVTransform transform = {
            { 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f }
        };

        if ( ( uvFlags & SUV_Rotate ) != 0 ) {
            float baseRotation = Read<float>( pRecord, 0x88 );
            if ( ( uvFlags & SUV_RandomRotation ) != 0 ) {
                baseRotation = static_cast<float>( seed & 0xffff ) *
                               baseRotation * kSeedToUnit;
            }
            const float rotationSpeed = Read<float>( pRecord, 0x8c );

            if ( ( uvFlags & SUV_VelocityAligned ) != 0 ) {
                // Rotate the UVs to follow the velocity in screen space.
                const Eigen::Vector3f velocity( pVelocity[0], pVelocity[1],
                                                pVelocity[2] );
                const float positionScale = Read<float>( pAttr, 0x30 );
                const float screenX =
                    cameraRight.dot( velocity ) * positionScale;
                const float screenY = cameraUp.dot( velocity ) * positionScale;

                float length =
                    std::sqrt( screenX * screenX + screenY * screenY );
                if ( length == 0.0f ) {
                    length = kEmitThreshold;
                }
                const float dirX = screenX / length;
                const float dirY = screenY / length;
                const float alignMatrix[6] = { dirY,
                                               -dirX,
                                               dirX,
                                               dirY,
                                               0.5f - ( dirY + dirX ) * 0.5f,
                                               0.5f - ( dirY - dirX ) * 0.5f };

                if ( baseRotation == 0.0f && rotationSpeed == 0.0f ) {
                    std::memcpy( transform.m, alignMatrix,
                                 sizeof( alignMatrix ) );
                } else {
                    float angle = rotationSpeed * ageFraction;
                    if ( dirX > 0.0f ) {
                        angle = -angle;
                    }
                    transform = MakeUVRotation( angle + baseRotation );
                    ComposeUVTransform( transform, alignMatrix );
                }
            } else {
                float angle = rotationSpeed * ageFraction;
                if ( ( uvFlags & SUV_RandomSpinSign ) != 0 &&
                     ( seed & 0x20 ) != 0 ) {
                    angle = -angle;
                }
                transform = MakeUVRotation( angle + baseRotation );
            }
        }

        if ( ( uvFlags & SUV_Scale ) != 0 ) {
            const float scaleU = Read<float>( pRecord, 0x80 );
            const float scaleV = Read<float>( pRecord, 0x84 );
            const float scaleMatrix[6] = { scaleU,
                                           0.0f,
                                           0.0f,
                                           scaleV,
                                           0.5f - scaleU * 0.5f,
                                           0.5f - scaleV * 0.5f };
            ComposeUVTransform( transform, scaleMatrix );
        }

        if ( ( uvFlags & SUV_FlipU ) != 0 && ( seed & 0x40 ) != 0 ) {
            ComposeUVTransform( transform, kFlipUMatrix );
        }
        if ( ( uvFlags & SUV_FlipV ) != 0 && ( seed & 0x80 ) != 0 ) {
            ComposeUVTransform( transform, kFlipVMatrix );
        }

        if ( ( uvFlags & SUV_SubTexture ) != 0 ) {
            const int32_t frameCount = Read<int16_t>( pRecord, 0x7a );
            const int32_t columnCount = Read<int16_t>( pRecord, 0x7c );
            const int32_t rowCount = Read<int16_t>( pRecord, 0x7e );

            // Not in the original: a malformed sheet would divide by zero.
            if ( frameCount > 0 && columnCount > 0 && rowCount > 0 ) {
                int32_t frame = 0;
                if ( ( uvFlags & SUV_RandomFrame ) != 0 ) {
                    frame = static_cast<int32_t>( ( ( seed >> 8 ) & 0xffff ) *
                                                  frameCount ) >>
                            16;
                }

                if ( ( uvFlags & SUV_FrameRate ) == 0 ) {
                    // Play the sheet once over the particle lifetime.
                    frame += static_cast<int32_t>(
                        static_cast<float>( frameCount ) * ageFraction );
                    if ( frame < 0 ) {
                        frame = -frame;
                    }
                    if ( frameCount <= frame ) {
                        frame = frameCount - 1;
                    }
                } else {
                    // Loop the sheet at a fixed frame rate.
                    const int32_t frameRate = Read<int32_t>( pRecord, 0x90 );
                    frame = ( static_cast<int32_t>(
                                  static_cast<float>( frameRate ) * age ) +
                              frame ) %
                            frameCount;
                    if ( frame < 0 ) {
                        frame = -frame;
                    }
                }

                const int32_t cellX = frame % columnCount;
                const int32_t cellY = frame / columnCount;
                const float cellWidth =
                    1.0f / static_cast<float>( columnCount );
                const float cellHeight = 1.0f / static_cast<float>( rowCount );
                const float subTextureMatrix[6] = {
                    cellWidth,
                    0.0f,
                    0.0f,
                    cellHeight,
                    static_cast<float>( cellX ) * cellWidth,
                    static_cast<float>( cellY ) * cellHeight };
                ComposeUVTransform( transform, subTextureMatrix );
            }
        }

        return transform;
    }

    void emitParticle( float *pSphere, const float *pPoint, float dist ) {
        // FUN_0045c0e0
        if ( pSphere[3] == -1.0f ) { // FLOAT_00f41664: first emission
            pSphere[0] = pPoint[0];
            pSphere[1] = pPoint[1];
            pSphere[2] = pPoint[2];
            pSphere[3] = dist;
            return;
        }

        float d[3] = { pPoint[0] - pSphere[0], pPoint[1] - pSphere[1],
                       pPoint[2] - pSphere[2] };
        float length = std::sqrt( d[0] * d[0] + d[1] * d[1] + d[2] * d[2] );
        if ( length > kEmitThreshold ) {
            d[0] /= length;
            d[1] /= length;
            d[2] /= length;
        } else {
            d[0] = d[1] = d[2] = 0.0f;
            length = 0.0f;
        }

        const float reach = length + dist;
        if ( pSphere[3] < reach ) {
            float behind = length - dist;
            const float clampToOrigin = -pSphere[3];
            if ( clampToOrigin < behind ) {
                behind = clampToOrigin;
            }
            const float step = ( behind + reach ) * 0.5f;
            pSphere[0] += d[0] * step;
            pSphere[1] += d[1] * step;
            pSphere[2] += d[2] * step;
            pSphere[3] = ( reach - behind ) * 0.5f;
        }
    }

    // One rand() masked to the Windows 15-bit range (RAND_MAX = 0x7fff). The
    // spawn code assumes rand() in [0, 0x7fff]; on hosts with a wider RAND_MAX
    // the raw value must be masked or the [0,1) normalisation doubles its
    // range.
    inline uint32_t Rand15() {
        return static_cast<uint32_t>( rand() ) & 0x7fff;
    }

    // base + variance * ( rand01 - 0.5 ), i.e. base +/- variance/2
    // (FLOAT_00f41674).
    inline float spawnRandFloat( float base, float variance ) {
        return static_cast<float>( Rand15() ) * kRandScale * variance + base -
               variance * 0.5f;
    }

    inline uint8_t FloatToByte( float value ) {
        return static_cast<uint8_t>(
            static_cast<int32_t>( std::lrint( value ) ) & 0xff );
    }

    // Byte-domain variant of spawnRandFloat (start / current colour, alpha).
    inline uint8_t SpawnRandByte( uint32_t base, uint32_t variance ) {
        return FloatToByte( static_cast<float>( Rand15() ) * kRandScale *
                                static_cast<float>( variance ) +
                            static_cast<float>( base ) -
                            static_cast<float>( variance ) * 0.5f );
    }
} // namespace

uint8_t *ParticleFile::poolBase( uint32_t objectField ) const {
    const uint32_t offset = Read<uint32_t>( pObject, objectField );
    return offset != 0 ? pBlob + offset : nullptr;
}

uint32_t ParticleFile::poolCount( uint32_t objectField ) const {
    return Read<uint32_t>( pObject, objectField );
}

void ParticleFile::reseed( uint32_t seed ) {
    // FUN_004587d0
    if ( pObject == nullptr || Read<uint8_t>( pObject, prto::kState ) == 0 ) {
        return;
    }

    if ( uint8_t *pEmitters = poolBase( prto::kEmitterBase ) ) {
        const int32_t count =
            static_cast<int32_t>( poolCount( prto::kEmitterActiveCount ) );
        for ( int32_t i = 0; i < count; i++ ) {
            uint8_t *pEmitter = pEmitters + i * prto::kEmitterStride;
            Write<uint32_t>( pEmitter, 0x14, Read<uint32_t>( pEmitter, 0x10 ) );
            Write<uint32_t>( pEmitter, 0x5c, 0 );
            if ( Read<uint32_t>( pEmitter, 0x18 ) != 0 ) {
                Write<uint16_t>( pEmitter, 6, 0 );
            }
            pEmitter[8] &= 0xfb;
        }
    }

    if ( uint8_t *pDefs = poolBase( prto::kDefABase ) ) {
        const int32_t count =
            static_cast<int32_t>( poolCount( prto::kDefAActiveCount ) );
        for ( int32_t i = 0; i < count; i++ ) {
            uint8_t *pDef = pDefs + i * prto::kDefStride;
            Write<uint32_t>( pDef, 0x140, 0 );
            Write<uint32_t>( pDef, 0x144, 0 );
            Write<uint32_t>( pDef, 0x228, Read<uint32_t>( pDef, 0x178 ) );
            Write<uint32_t>( pDef, 0x21c, seed );
        }
    }

    if ( uint8_t *pAttrs = poolBase( prto::kAttrBase ) ) {
        const int32_t count =
            static_cast<int32_t>( poolCount( prto::kAttrActiveCount ) );
        for ( int32_t i = 0; i < count; i++ ) {
            uint8_t *pAttr = pAttrs + i * prto::kAttrStride;
            pAttr[0] = 1; // allocation state
            pAttr[3] = 4; // phase: recycle
        }
    }
}

uint8_t *ParticleFile::findPrimaryDefinition() {
    // FUN_0080ca40
    uint8_t *pDefs = poolBase( prto::kDefBBase );
    const int32_t count =
        static_cast<int32_t>( poolCount( prto::kDefBActiveCount ) );
    for ( int32_t i = 0; i < count && pDefs != nullptr; i++ ) {
        uint8_t *pDef = pDefs + i * prto::kDefStride;
        if ( ( ( pDef[0x226] >> 5 ) & 1 ) != 0 ) {
            pDef[0x224] = 1;
            Write<float>( pDef, 0x140, 0.15f ); // DAT_00f41af8
            return pDef;
        }
    }
    return nullptr;
}

void ParticleFile::applyMaterialDescriptor( bool bHasDescriptor,
                                            uint8_t alphaReject,
                                            uint8_t coverageBudget ) {
    // FUN_0080ca40
    uint8_t *pRecords = poolBase( prto::kMatBase );
    const int32_t count =
        static_cast<int32_t>( poolCount( prto::kMatActiveCount ) );
    for ( int32_t i = 0; i < count && pRecords != nullptr; i++ ) {
        uint8_t *pRecord = pRecords + i * prto::kMatStride;
        if ( !bHasDescriptor ) {
            pRecord[0xf] = 0;
            continue;
        }
        if ( coverageBudget != 0 ) {
            pRecord[0xb] |= 0x10;
            pRecord[0xd] = coverageBudget;
        }
        pRecord[0xf] = alphaReject;
    }
}

void ParticleFile::applyStartColor( uint32_t packedColor, float alpha ) {
    // FUN_00458720
    float clamped = alpha;
    if ( clamped > 1.0f ) {
        clamped = 1.0f;
    } else if ( clamped < 0.0f ) {
        clamped = 0.0f;
    }
    const uint32_t scale = static_cast<uint32_t>( clamped * 255.0f ) & 0xff;

    uint8_t *pDefs = poolBase( prto::kDefABase );
    const int32_t count =
        static_cast<int32_t>( poolCount( prto::kDefATotalCount ) );
    if ( pDefs == nullptr || count == 0 ) {
        return;
    }

    const uint32_t r = packedColor & 0xff;
    const uint32_t g = ( packedColor >> 8 ) & 0xff;
    const uint32_t b = ( packedColor >> 16 ) & 0xff;
    for ( int32_t i = 0; i < count; i++ ) {
        uint8_t *pDef = pDefs + i * prto::kDefStride;
        pDef[0x1a4] = static_cast<uint8_t>( r );
        pDef[0x1a5] = static_cast<uint8_t>( g );
        pDef[0x1a6] = static_cast<uint8_t>( b );
        pDef[0x1a7] = 0xff;
        pDef[0x1e8] = static_cast<uint8_t>( ( r * scale ) >> 8 );
        pDef[0x1e9] = static_cast<uint8_t>( ( g * scale ) >> 8 );
        pDef[0x1ea] = static_cast<uint8_t>( ( b * scale ) >> 8 );
        pDef[0x1eb] = 0;
    }
}

void ParticleFile::updateInstance( const Eigen::Vector4f &wind,
                                   uint8_t *pPrimaryDef,
                                   const Eigen::Vector4f &color,
                                   float alpha,
                                   float genSize,
                                   float genDensity,
                                   float genSpeed,
                                   const Matrix4x4f_t &instanceMatrix,
                                   const Eigen::Vector4f &shiftPosition ) {
    // FUN_0080cd50
    if ( pObject == nullptr ) {
        return;
    }

    if ( pPrimaryDef != nullptr ) {
        Write<float>( pPrimaryDef, 0x150, wind.x() );
        Write<float>( pPrimaryDef, 0x154, wind.y() );
        Write<float>( pPrimaryDef, 0x158, wind.z() );
        Write<float>( pPrimaryDef, 0x15c, wind.w() );
    }

    const uint32_t r = ColorByte( color.x() );
    const uint32_t g = ColorByte( color.y() );
    const uint32_t b = ColorByte( color.z() );
    applyStartColor( ( 0xffu << 24 ) | ( b << 16 ) | ( g << 8 ) | r, 0.0f );

    const uint32_t a = ColorByte( alpha );
    Write<uint32_t>( pObject, prto::kColorId, ( a << 24 ) | 0x00ffffff );
    Write<float>( pObject, prto::kGenSize, genSize );
    if ( genDensity >= 0.0f ) {
        Write<float>( pObject, prto::kGenDensity, genDensity );
    }
    Write<float>( pObject, prto::kGenSpeed, genSpeed );

    uint8_t *pDefs = poolBase( prto::kDefABase );
    const int32_t count =
        static_cast<int32_t>( poolCount( prto::kDefATotalCount ) );
    for ( int32_t i = 0; i < count && pDefs != nullptr; i++ ) {
        uint8_t *pDef = pDefs + i * prto::kDefStride;
        Eigen::Map<Matrix4x4f_t, Eigen::Unaligned> current(
            reinterpret_cast<float *>( pDef + 0x40 ) );
        Eigen::Map<const Matrix4x4f_t, Eigen::Unaligned> base(
            reinterpret_cast<const float *>( pDef + 0x80 ) );
        current = base * instanceMatrix;
        current( 3, 0 ) -= shiftPosition.x();
        current( 3, 1 ) -= shiftPosition.y();
        current( 3, 2 ) -= shiftPosition.z();
        current( 3, 3 ) -= shiftPosition.w();
    }
}

void ParticleFile::simulate( float deltaTime ) {
    // FUN_00458570
    uint8_t *pEmitters = poolBase( prto::kEmitterBase );
    const int32_t count =
        static_cast<int32_t>( poolCount( prto::kEmitterActiveCount ) );
    if ( pEmitters == nullptr || count <= 0 ) {
        return;
    }
    for ( int32_t i = 0; i < count; i++ ) {
        uint8_t *pEmitter = pEmitters + i * prto::kEmitterStride;
        if ( pEmitter[0] == 0 || pEmitter[2] == 0 ) {
            continue;
        }
        const float rate = Read<float>( pEmitter, 0x4c );
        const float scaledDt = ( deltaTime >= 0.0f )
                                   ? rate * deltaTime
                                   : rate * Read<float>( pObject, 0x14 );
        integrateEmitter( pEmitter, scaledDt );
    }
}

void ParticleFile::applyDefinitionForces( uint8_t *pEmitter,
                                          int32_t scratchCount ) {
    // FUN_0045c250
    const uint32_t tableOffset = Read<uint32_t>( pEmitter, 0x30 );
    const int32_t defCount = Read<int32_t>( pEmitter, 0x2c );
    Write<uint32_t>( pEmitter, 0x70, 0 );
    if ( tableOffset == 0 || scratchCount <= 0 ) {
        return;
    }
    uint8_t *pTable = pBlob + tableOffset;

    for ( int32_t d = 0; d < defCount; d++ ) {
        const uint32_t defOffset = Read<uint32_t>( pTable, d * 4 );
        if ( defOffset == 0 ) {
            continue;
        }
        uint8_t *pDef = pBlob + defOffset;
        Write<uint32_t>( pEmitter, 0x70, defOffset );
        if ( pDef[2] == 0 ) {
            continue;
        }

        if ( pDef[0x224] == 0 ) {
            if ( pDef[0x225] >= 1 && pDef[0x225] < 5 ) {
                for ( int32_t k = 0; k < scratchCount; k++ ) {
                    const float *pPos =
                        reinterpret_cast<const float *>( gScratchPos[k] );
                    Matrix4x4f_t *pMat = (Matrix4x4f_t *)( pDef + 0x80 );
                    const Eigen::Vector4f transformed = TDUMat4x4MulVec(
                        *pMat,
                        Eigen::Vector4f( pPos[0], pPos[1], pPos[2], 1.0f ) );
                    float *pDst = &gScratchTransformedPos[k * 4];
                    pDst[0] = transformed.x();
                    pDst[1] = transformed.y();
                    pDst[2] = transformed.z();
                    pDst[3] = transformed.w();
                }
            }
        } else {
            for ( int32_t k = 0; k < scratchCount; k++ ) {
                const float *pPos =
                    reinterpret_cast<const float *>( gScratchPos[k] );
                float *pDst = &gScratchTransformedPos[k * 4];
                pDst[0] = pPos[0];
                pDst[1] = pPos[1];
                pDst[2] = pPos[2];
                pDst[3] = pPos[3];
            }
        }

        const uint8_t flags = pDef[0x226];

        if ( ( flags >> 6 ) & 1 ) {
            const int32_t curveCount = Read<int32_t>( pDef, 0x214 );
            for ( int32_t c = 0; c < curveCount; c++ ) {
                uint8_t *pCurve = pDef + 0x1b0 + c * 0x10;
                const uint32_t curveFlags = Read<uint32_t>( pCurve, 0 );
                const float divisor = Read<float>( pCurve, 4 );
                const uint32_t attrOffset = Read<uint32_t>( pCurve, 8 );
                const float *pLut = reinterpret_cast<const float *>(
                    pBlob + Read<uint32_t>( pCurve, 0xc ) );
                const bool byteOutput = ( ( curveFlags >> 6 ) & 1 ) != 0;

                if ( ( curveFlags & 1 ) == 0 ) {
                    // Distance / size-driven input (the delta is scaled by the
                    // curve divisor).
                    float input = 0.0f;
                    for ( int32_t k = 0; k < scratchCount; k++ ) {
                        uint8_t *pAttr = gScratchAttr[k];
                        const float *pTp = &gScratchTransformedPos[k * 4];
                        if ( ( curveFlags >> 1 ) & 1 ) {
                            input =
                                std::sqrt( pTp[0] * pTp[0] + pTp[1] * pTp[1] +
                                           pTp[2] * pTp[2] );
                        } else if ( ( curveFlags >> 2 ) & 1 ) {
                            input =
                                std::sqrt( pTp[0] * pTp[0] + pTp[2] * pTp[2] );
                        } else if ( ( curveFlags >> 3 ) & 1 ) {
                            input = Read<float>( pAttr, 0x24 );
                        }
                        input = ( input < 0.0f )
                                    ? 0.0f
                                    : ( input > 1.0f ? kCurveClamp : input );
                        int32_t index =
                            static_cast<int32_t>( input * kCurveLutScale );
                        float frac = input * kCurveLutScale - index;
                        if ( index >= 15 ) {
                            index = 14;
                            frac = 1.0f;
                        }
                        const float value =
                            pLut[index] +
                            ( ( pLut[index + 1] - pLut[index] ) * frac ) /
                                divisor;
                        if ( byteOutput ) {
                            pAttr[attrOffset] = FloatToByte( value );
                        } else {
                            Write<float>( pAttr, attrOffset, value );
                        }
                    }
                } else {
                    // Age / lifetime-driven input (a plain LUT lerp).
                    const float clampHigh = byteOutput ? 1.0f : kCurveClamp;
                    for ( int32_t k = 0; k < scratchCount; k++ ) {
                        uint8_t *pAttr = gScratchAttr[k];
                        float input = Read<float>( pAttr, 0x1c ) /
                                      Read<float>( pAttr, 0x20 );
                        input = ( input < 0.0f )
                                    ? 0.0f
                                    : ( input > 1.0f ? clampHigh : input );
                        int32_t index =
                            static_cast<int32_t>( input * kCurveLutScale );
                        float frac = input * kCurveLutScale - index;
                        if ( index >= 15 ) {
                            index = 14;
                            frac = 1.0f;
                        }
                        const float value =
                            pLut[index] +
                            ( pLut[index + 1] - pLut[index] ) * frac;
                        if ( byteOutput ) {
                            pAttr[attrOffset] = FloatToByte( value );
                        } else {
                            Write<float>( pAttr, attrOffset, value );
                        }
                    }
                }
            }
        }

        if ( flags & 1 ) {
            const float wx = Read<float>( pDef, 0x170 );
            const float wy = Read<float>( pDef, 0x174 );
            const float wz = Read<float>( pDef, 0x178 );
            for ( int32_t k = 0; k < scratchCount; k++ ) {
                float *pAccel = reinterpret_cast<float *>( gScratchVel[k] );
                pAccel[0] += wx;
                pAccel[1] += wy;
                pAccel[2] += wz;
                pAccel[3] = 0.0f;
            }
        }

        if ( ( flags >> 5 ) & 1 ) {
            const float tx = Read<float>( pDef, 0x150 );
            const float ty = Read<float>( pDef, 0x154 );
            const float tz = Read<float>( pDef, 0x158 );
            const float bias = Read<float>( pDef, 0x140 );
            for ( int32_t k = 0; k < scratchCount; k++ ) {
                float *pVel = reinterpret_cast<float *>( gScratchVel[k] );
                const float strength =
                    -( Read<float>( gScratchAttr[k], 0x2c ) + bias );
                pVel[0] += ( pVel[4] - tx ) * strength;
                pVel[1] += ( pVel[5] - ty ) * strength;
                pVel[2] += ( pVel[6] - tz ) * strength;
                pVel[3] = 0.0f;
            }
        }

        if ( ( flags >> 3 ) & 1 ) {
            const float ax = Read<float>( pDef, 0x160 );
            const float ay = Read<float>( pDef, 0x164 );
            const float az = Read<float>( pDef, 0x168 );
            for ( int32_t k = 0; k < scratchCount; k++ ) {
                float *pVel = reinterpret_cast<float *>( gScratchVel[k] );
                const float vx = pVel[4];
                const float vy = pVel[5];
                const float vz = pVel[6];
                pVel[0] += vy * az - vz * ay;
                pVel[1] += vz * ax - vx * az;
                pVel[2] += vx * ay - vy * ax;
                pVel[3] = 0.0f;
            }
        }

        if ( ( Read<uint16_t>( pDef, 0x226 ) & 0x200 ) != 0 ) {
            const ParticleForceCallback pForceCallback =
                ReadCallback<ParticleForceCallback>( pDef, 0x220 );
            pForceCallback( pEmitter, pDef, gScratchAttr, gScratchVel,
                            gScratchPos, scratchCount );
        }
    }
}

bool ParticleFile::hasLiveParticles() const {
    // FUN_00458690
    if ( pObject == nullptr ) {
        return false;
    }
    const int32_t count = static_cast<int32_t>(
        Read<uint32_t>( pObject, prto::kEmitterTotalCount ) );
    uint8_t *pEmitters = poolBase( prto::kEmitterBase );
    if ( pEmitters == nullptr || count <= 0 ) {
        return false;
    }

    for ( int32_t i = 0; i < count; i++ ) {
        if ( Read<uint32_t>( pEmitters + i * prto::kEmitterStride, 0x5c ) !=
             0 ) {
            return true;
        }
    }
    for ( int32_t i = 0; i < count; i++ ) {
        if ( ( ~( pEmitters[i * prto::kEmitterStride + 8] >> 2 ) & 1 ) != 0 ) {
            for ( int32_t j = 0; j < count; j++ ) {
                if ( ( ~( pEmitters[j * prto::kEmitterStride + 8] >> 1 ) &
                       1 ) != 0 ) {
                    return true;
                }
            }
            return false;
        }
    }
    return false;
}

void ParticleFile::buildGeometry( const Matrix4x4f_t &worldToCam,
                                  const Matrix4x4f_t &projection,
                                  DrawList &out ) {
    // FUN_004585e0
    uint8_t *pEmitters = poolBase( prto::kEmitterBase );
    const int32_t count =
        static_cast<int32_t>( poolCount( prto::kEmitterActiveCount ) );
    if ( pEmitters == nullptr || count <= 0 ) {
        return;
    }

    const uint32_t colorId = Read<uint32_t>( pObject, prto::kColorId );
    for ( int32_t i = 0; i < count; i++ ) {
        uint8_t *pEmitter = pEmitters + i * prto::kEmitterStride;
        if ( pEmitter[0] == 0 ) {
            continue;
        }
        const uint32_t maxParticles = Read<uint32_t>( pEmitter, 0x10 );
        const uint32_t tableOffset = Read<uint32_t>( pEmitter, 0x48 );
        const int32_t recordCount =
            static_cast<int32_t>( Read<uint32_t>( pEmitter, 0x40 ) );
        Write<uint32_t>( pEmitter, 0x70, 0 );
        if ( tableOffset == 0 ) {
            continue;
        }
        uint8_t *pTable = pBlob + tableOffset;
        for ( int32_t j = 0; j < recordCount; j++ ) {
            const uint32_t recordOffset = Read<uint32_t>( pTable, j * 4 );
            if ( recordOffset == 0 ) {
                continue;
            }
            uint8_t *pRecord = pBlob + recordOffset;
            if ( pRecord[2] == 0 ) {
                continue;
            }
            Write<uint32_t>( pEmitter, 0x70, recordOffset );
            emitDef( pEmitter, pRecord, maxParticles, colorId, worldToCam,
                     projection, out );
        }
    }
}

void ParticleFile::spawnParticles( uint8_t *pEmitter,
                                   uint8_t *pDef,
                                   int32_t maxParticles,
                                   float genDensity,
                                   float genSpeed,
                                   float genSize ) {
    // FUN_0045a040
    uint8_t *pAttrPool = poolBase( prto::kAttrBase );
    uint8_t *pVelPool = poolBase( prto::kVelBase );
    uint8_t *pPosPool = poolBase( prto::kPosBase );
    if ( pEmitter == nullptr || pDef == nullptr || pAttrPool == nullptr ||
         pVelPool == nullptr || pPosPool == nullptr ) {
        return;
    }

    const float deltaTime = Read<float>( pEmitter, 0x50 );

    const float accumulator = Read<float>( pDef, 0x228 ) - deltaTime;
    Write<float>( pDef, 0x228, accumulator );
    if ( accumulator > 0.0f ) {
        return;
    }

    const float interval = Read<float>( pDef, 0x170 );
    const float intervalVariance = Read<float>( pDef, 0x174 );
    const float invInterval = ( interval == 0.0f ) ? 0.0f : 1.0f / interval;
    const float invVariance =
        ( intervalVariance == 0.0f ) ? 0.0f : 1.0f / intervalVariance;

    Write<float>( pDef, 0x140, deltaTime + Read<float>( pDef, 0x140 ) );

    int32_t ringIndex = static_cast<int32_t>( Read<int16_t>( pEmitter, 6 ) );
    int32_t batchRemaining = 0;

    if ( ( pEmitter[8] & 4 ) == 0 ) {
        uint8_t *pAttr = pAttrPool;
        uint8_t *pVel = pVelPool;
        for ( int32_t count = 0; count < maxParticles; count++ ) {
            // No pending allocation: claim the next free ring slot.
            if ( Read<int32_t>( pEmitter, 0x14 ) == 0 ) {
                if ( pDef[4] == 1 || ( pEmitter[8] & 2 ) != 0 ) {
                    break;
                }
                pAttr = pAttrPool + ringIndex * prto::kAttrStride;
                pVel = pVelPool + ringIndex * prto::kVelStride;
                int32_t nextIndex = ringIndex + 1;
                if ( Read<int32_t>( pEmitter, 0x10 ) <= nextIndex ) {
                    nextIndex = 0;
                }
                Write<int16_t>( pEmitter, 6,
                                static_cast<int16_t>( nextIndex ) );
                if ( pAttr[0] == 2 && ( pAttr[3] == 5 || pAttr[3] == 2 ) ) {
                    Write<int32_t>( pEmitter, 0x5c,
                                    Read<int32_t>( pEmitter, 0x5c ) - 1 );
                }
                pAttr[0] = 1;
                Write<int32_t>( pEmitter, 0x14,
                                Read<int32_t>( pEmitter, 0x14 ) + 1 );
                ringIndex = nextIndex;
            }

            if ( pAttr[0] == 1 ) {
                // Particles are emitted in density-scaled batches.
                if ( batchRemaining == 0 ) {
                    float density;
                    if ( pDef[0xe] == 6 ) {
                        density =
                            static_cast<float>( Read<int32_t>( pDef, 0x164 ) *
                                                Read<int32_t>( pDef, 0x160 ) ) *
                            Read<float>( pDef, 0x14c );
                    } else {
                        density = Read<float>( pDef, 0x14c ) *
                                  Read<float>( pDef, 0x148 );
                    }
                    batchRemaining =
                        static_cast<int32_t>( density * genDensity );
                }
                batchRemaining--;

                if ( Read<float>( pDef, 0x144 ) <=
                     Read<float>( pDef, 0x140 ) ) {
                    const float age =
                        Read<float>( pDef, 0x140 ) - Read<float>( pDef, 0x144 );
                    if ( batchRemaining == 0 ) {
                        Write<float>( pDef, 0x140, age );
                        Write<float>( pDef, 0x144,
                                      ( invVariance == 0.0f )
                                          ? invInterval
                                          : sampleRandom( pDef, pDef[0x0c],
                                                          invInterval,
                                                          invVariance ) );
                    }

                    const uint32_t sourceOffset =
                        Read<uint32_t>( pEmitter, 0x74 );
                    if ( sourceOffset != 0 && pEmitter[4] == 3 ) {
                        const uint8_t *pSource = pBlob + sourceOffset;
                        Write<uint32_t>( pDef, 0x168,
                                         Read<uint32_t>( pSource, 8 ) );
                        Write<uint32_t>( pDef, 0x16c,
                                         Read<uint32_t>( pSource, 0xc ) );
                    }

                    float *pVelocity = reinterpret_cast<float *>( pVel + 0x10 );
                    const int32_t particleIndex = static_cast<int32_t>(
                        ( pAttr - pAttrPool ) / prto::kAttrStride );
                    float *pPosition = reinterpret_cast<float *>(
                        pPosPool + particleIndex * prto::kPosStride );
                    spawnShape( pDef, pVelocity, pPosition );
                    pVelocity[0] *= genSpeed;
                    pVelocity[1] *= genSpeed;
                    pVelocity[2] *= genSpeed;
                    pVelocity[3] = 0.0f;
                    Write<float>( pVel, 0, 0.0f ); // acceleration = 0
                    Write<float>( pVel, 4, 0.0f );
                    Write<float>( pVel, 8, 0.0f );

                    // Start colour (+0xc..0xf): base (+0x1a0) +/- var (+0x1e4).
                    for ( uint32_t c = 0; c < 4; c++ ) {
                        pAttr[0xc + c] =
                            SpawnRandByte( pDef[0x1a0 + c], pDef[0x1e4 + c] );
                    }

                    // Advance the definition RNG (LCG) into the seed (+0x3c).
                    const int32_t seed =
                        Read<int32_t>( pDef, 0x21c ) * 0x19660d + 0x3c6ef35f;
                    Write<int32_t>( pDef, 0x21c, seed );
                    Write<int32_t>( pAttr, 0x3c, seed );

                    // Rotation velocity (+0x28) and raw size (+0x24).
                    Write<float>(
                        pAttr, 0x28,
                        spawnRandFloat( Read<float>( pDef, 0x1bc ),
                                        Read<float>( pDef, 0x200 ) ) );
                    Write<float>(
                        pAttr, 0x24,
                        spawnRandFloat( Read<float>( pDef, 0x1b8 ),
                                        Read<float>( pDef, 0x1fc ) ) );

                    // Current colour (+0x10..0x13, alpha forced to 0xff).
                    pAttr[0x10] = SpawnRandByte( pDef[0x1a4], pDef[0x1e8] );
                    pAttr[0x11] = SpawnRandByte( pDef[0x1a5], pDef[0x1e9] );
                    pAttr[0x12] = SpawnRandByte( pDef[0x1a6], pDef[0x1ea] );
                    pAttr[0x13] = 0xff;

                    // Alpha seed (+8).
                    pAttr[8] = SpawnRandByte( pDef[0x19c], pDef[0x1e0] );

                    // Size acceleration (+0x2c), scaled by the raw size
                    // squared.
                    const float rawSize = Read<float>( pAttr, 0x24 );
                    Write<float>( pAttr, 0x2c,
                                  spawnRandFloat( Read<float>( pDef, 0x1c0 ),
                                                  Read<float>( pDef, 0x204 ) ) *
                                      rawSize * rawSize );

                    // Velocity timescale (+0x14) and its reciprocal (+0x18);
                    // the integrator scales acceleration by 1/timescale.
                    const float timescale =
                        spawnRandFloat( Read<float>( pDef, 0x1a8 ),
                                        Read<float>( pDef, 0x1ec ) );
                    Write<float>( pAttr, 0x14, timescale );
                    Write<float>( pAttr, 0x18,
                                  ( timescale == 0.0f ) ? 1.0f
                                                        : 1.0f / timescale );

                    // Lifetime / death threshold (+0x20) and unit scale
                    // (+0x30).
                    Write<float>( pAttr, 0x20,
                                  sampleRandom( pDef, pDef[0x0d],
                                                Read<float>( pDef, 0x180 ),
                                                Read<float>( pDef, 0x184 ) ) );
                    Write<float>( pAttr, 0x30, 1.0f );

                    // Final size (+0x24): def scale * raw size * gen size.
                    Write<float>( pAttr, 0x24,
                                  Read<float>( pDef, 0x100 ) * rawSize *
                                      genSize );

                    const ParticleSpawnCallback pSpawnCallback =
                        ReadCallback<ParticleSpawnCallback>( pDef, 0x218 );
                    if ( pSpawnCallback != nullptr ) {
                        pSpawnCallback( pDef, pAttr, pVel, pDef + 0x194,
                                        pDef + 0x1d8 );
                    }

                    Write<float>( pAttr, 0x1c, age );
                    pAttr[3] = 5; // phase: spawn

                    const uint32_t attrOffset =
                        static_cast<uint32_t>( pAttr - pBlob );
                    if ( sourceOffset != 0 && pEmitter[4] == 2 ) {
                        const uint32_t cellsOffset =
                            Read<uint32_t>( pBlob + sourceOffset, 0x1c );
                        const int32_t cellIndex =
                            GetGridCellIndex( pBlob + cellsOffset, pPosition );
                        if ( cellIndex >= 0 ) {
                            const uint32_t cellOffset =
                                cellsOffset + cellIndex * 0x90;
                            Write<uint32_t>( pAttr, 0x38, cellOffset );
                            insertParticleInGridCell( pBlob + cellOffset,
                                                      attrOffset );
                        }
                    }
                    if ( sourceOffset != 0 && pEmitter[4] == 3 ) {
                        linkLatticeParticle( pBlob + sourceOffset, pAttr );
                    }

                    pAttr[0] = 2; // allocation state: active
                    if ( Read<int32_t>( pEmitter, 0x14 ) != 0 ) {
                        Write<int32_t>( pEmitter, 0x14,
                                        Read<int32_t>( pEmitter, 0x14 ) - 1 );
                    }
                }
            }

            pAttr += prto::kAttrStride;
            pVel += prto::kVelStride;
        }
    }

    if ( Read<float>( pDef, 0x144 ) <= Read<float>( pDef, 0x140 ) &&
         Read<float>( pDef, 0x140 ) != Read<float>( pDef, 0x144 ) ) {
        Write<float>( pDef, 0x140, 0.0f );
    }
    pObject[2] = 4;
}

void ParticleFile::spawnShape( uint8_t *pDef,
                               float *pOutVelocity,
                               float *pOutPosition ) {
    // FUN_00459250
    uint8_t *pRng = pDef + 0x21c;
    Write<uint32_t>( pRng, 0, Read<uint32_t>( pRng, 0 ) >> 1 );

    uint32_t velocityMode =
        pDef[0xf] & 0xf; // may be cleared by case 7 variant 2
    uint8_t variant = pDef[0xf] >> 4;

    Eigen::Vector4f dir = Eigen::Vector4f::Zero(); // shape direction (in_EAX)
    Eigen::Vector4f secondary = Eigen::Vector4f::Zero(); // local_d0
    bool transformDir =
        true; // every type but the fixed-direction one transforms

    switch ( pDef[0xe] ) {
    case 1: { // box
        if ( variant == 0 ) {
            const uint32_t s = LCGNext( Read<uint32_t>( pRng, 0 ) );
            Write<uint32_t>( pRng, 0, s );
            dir[0] = ( s & 1 ) ? -0.5f : 0.5f;
            dir[1] = ( s & 2 ) ? -0.5f : 0.5f;
            dir[2] = ( s & 4 ) ? -0.5f : 0.5f;
            if ( velocityMode == 1 ) {
                secondary = dir;
            }
        } else if ( variant == 1 ) {
            const uint32_t s = LCGNext( Read<uint32_t>( pRng, 0 ) );
            Write<uint32_t>( pRng, 0, s );
            const int *edge = kCubeEdges[s % 12];
            const Eigen::Vector4f a( kCubeCorners[edge[0]][0],
                                     kCubeCorners[edge[0]][1],
                                     kCubeCorners[edge[0]][2], 0.0f );
            const Eigen::Vector4f b( kCubeCorners[edge[1]][0],
                                     kCubeCorners[edge[1]][1],
                                     kCubeCorners[edge[1]][2], 0.0f );
            dir = a + ( b - a ) * RandFloat01( pRng );
            if ( velocityMode == 1 ) {
                secondary = a + ( b - a ) * 0.5f; // edge midpoint
            }
        } else if ( variant == 2 ) { // face
            const uint32_t s = LCGNext( Read<uint32_t>( pRng, 0 ) );
            Write<uint32_t>( pRng, 0, s );
            const uint32_t face = s % 6;
            const int axis = face >> 1; // 0=x, 1=y, 2=z
            const float sign = ( face & 1 ) ? -0.5f : 0.5f;
            dir[axis] = sign;
            // The two free axes are filled in ascending index order.
            const int free1 = ( axis == 0 ) ? 1 : 0;
            const int free2 = ( axis == 2 ) ? 1 : 2;
            dir[free1] = RandFloat01( pRng ) - 0.5f;
            dir[free2] = RandFloat01( pRng ) - 0.5f;
            if ( velocityMode == 1 ) {
                secondary = Eigen::Vector4f::Zero();
                secondary[axis] = ( face & 1 ) ? -1.0f : 1.0f; // face normal
            }
        } else { // variant 3: solid box
            for ( int i = 0; i < 3; i++ ) {
                dir[i] = RandFloat01( pRng ) - 0.5f;
            }
            if ( velocityMode == 1 ) {
                Eigen::Vector4f n( dir[0], dir[1], dir[2], 0.0f );
                n.normalize();
                const float ax = std::fabs( n[0] );
                const float ay = std::fabs( n[1] );
                const float az = std::fabs( n[2] );
                secondary = Eigen::Vector4f::Zero();
                if ( ax <= az ) {
                    if ( az <= ay ) { // y dominant
                        secondary[1] = ( dir[1] >= 0.0f ) ? 1.0f : -1.0f;
                    } else { // z dominant
                        secondary[2] = ( dir[2] >= 0.0f ) ? 1.0f : -1.0f;
                    }
                } else if ( ay < ax ) { // x dominant
                    secondary[0] = ( dir[0] >= 0.0f ) ? 1.0f : -1.0f;
                } else { // y dominant
                    secondary[1] = ( dir[1] >= 0.0f ) ? 1.0f : -1.0f;
                }
            }
        }
        break;
    }
    case 2: { // sphere / cone
        if ( variant == 0 || variant == 1 ) {
            variant = 2;
        }
        const float theta =
            ( RandFloat01( pRng ) - 0.5f ) * static_cast<float>( EIGEN_PI );
        const float phi =
            RandFloat01( pRng ) * static_cast<float>( EIGEN_TWO_PI );
        const float cosTheta = std::cos( theta );
        secondary =
            Eigen::Vector4f( std::cos( phi ) * cosTheta, std::sin( theta ),
                             std::sin( phi ) * cosTheta, 0.0f );
        if ( variant == 2 ) {
            dir = secondary * 0.5f;
        } else { // variant 3: solid sphere
            dir = secondary * RandFloat01( pRng ) * 0.5f;
        }
        break;
    }
    case 3: { // circle / cylinder (XZ plane, random height)
        if ( variant == 0 || variant == 1 ) {
            variant = 2;
        }
        const float a =
            RandFloat01( pRng ) * static_cast<float>( EIGEN_TWO_PI );
        secondary = Eigen::Vector4f( std::cos( a ), 0.0f, std::sin( a ), 0.0f );
        if ( variant == 2 ) {
            dir = secondary * 0.5f;
        } else { // variant 3: filled disc
            dir = secondary * RandFloat01( pRng ) * 0.5f;
        }
        dir[1] = RandFloat01( pRng ) - 0.5f;
        break;
    }
    case 7: { // square (XZ plane)
        if ( variant == 3 ) {
            variant = 2;
        }
        if ( variant == 0 ) { // corner
            const uint32_t state = LCGNext( Read<uint32_t>( pRng, 0 ) );
            Write<uint32_t>( pRng, 0, state );
            const uint32_t s = state & 3;
            dir = Eigen::Vector4f( kSquareCorners[s][0], 0.0f,
                                   kSquareCorners[s][1], 0.0f );
            if ( velocityMode == 1 ) {
                secondary = dir;
            }
        } else if ( variant == 1 ) { // edge
            const uint32_t state = LCGNext( Read<uint32_t>( pRng, 0 ) );
            Write<uint32_t>( pRng, 0, state );
            const uint32_t s = state & 3;
            const int *edge = kSquareEdges[s];
            const Eigen::Vector4f a( kSquareCorners[edge[0]][0], 0.0f,
                                     kSquareCorners[edge[0]][1], 0.0f );
            const Eigen::Vector4f b( kSquareCorners[edge[1]][0], 0.0f,
                                     kSquareCorners[edge[1]][1], 0.0f );
            dir = a + ( b - a ) * RandFloat01( pRng );
            if ( velocityMode == 1 ) {
                secondary = Eigen::Vector4f( kSquareEdgeNormals[s][0], 0.0f,
                                             kSquareEdgeNormals[s][1], 0.0f );
            }
        } else { // variant 2: filled square
            dir[0] = RandFloat01( pRng ) - 0.5f;
            dir[1] = 0.0f;
            dir[2] = RandFloat01( pRng ) - 0.5f;
            if ( velocityMode == 1 ) {
                velocityMode = 0;
            }
        }
        break;
    }
    case 8: { // fixed direction (def+0x70)
        dir = Eigen::Vector4f( Read<float>( pDef, 0x70 ),
                               Read<float>( pDef, 0x74 ),
                               Read<float>( pDef, 0x78 ), 0.0f );
        transformDir = false;
        if ( velocityMode == 1 ) {
            secondary = Eigen::Vector4f( RandFloat01( pRng ) - 0.5f,
                                         RandFloat01( pRng ) - 0.5f,
                                         RandFloat01( pRng ) - 0.5f, 0.0f );
        }
        break;
    }
    case 9: { // axis (X)
        if ( variant == 2 || variant == 3 ) {
            variant = 1;
        }
        if ( variant == 0 ) {
            const uint32_t state = LCGNext( Read<uint32_t>( pRng, 0 ) );
            Write<uint32_t>( pRng, 0, state );
            const uint32_t s = state & 1;
            dir[0] = ( s == 0 ) ? 0.5f : -0.5f;
            if ( velocityMode == 1 ) {
                secondary = Eigen::Vector4f::Zero();
                secondary[0] = ( s == 0 ) ? 1.0f : -1.0f;
            }
        } else { // variant 1: random X
            dir[0] = RandFloat01( pRng ) - 0.5f;
            if ( velocityMode == 1 ) {
                secondary = Eigen::Vector4f( 0.0f, RandFloat01( pRng ) - 0.5f,
                                             RandFloat01( pRng ) - 0.5f, 0.0f );
            }
        }
        break;
    }
    case 10: { // ring / disc (XZ plane)
        if ( variant == 0 ) {
            variant = 1;
        } else if ( variant == 3 ) {
            variant = 2;
        }
        const float a =
            RandFloat01( pRng ) * static_cast<float>( EIGEN_TWO_PI );
        const float c = std::cos( a );
        const float s = std::sin( a );
        if ( velocityMode == 1 ) {
            secondary = Eigen::Vector4f( c, 0.0f, s, 0.0f );
        }
        if ( variant == 1 ) { // ring
            dir = Eigen::Vector4f( c * 0.5f, 0.0f, s * 0.5f, 0.0f );
        } else { // variant 2: filled disc
            const float radius = RandFloat01( pRng ) * 0.5f;
            dir = Eigen::Vector4f( radius * c, 0.0f, radius * s, 0.0f );
        }
        break;
    }
    default:
        break;
    }

    Matrix4x4f_t *pMat = (Matrix4x4f_t *)( pDef + 0x40 );

    if ( pOutPosition != nullptr ) {
        const Eigen::Vector4f localPoint =
            transformDir ? Eigen::Vector4f( dir[0], dir[1], dir[2], 1.0f )
                         : Eigen::Vector4f( 0.0f, 0.0f, 0.0f, 1.0f );
        const Eigen::Vector4f spawnPos = TDUMat4x4MulVec( *pMat, localPoint );
        pOutPosition[0] = spawnPos.x();
        pOutPosition[1] = spawnPos.y();
        pOutPosition[2] = spawnPos.z();
        pOutPosition[3] = 0.0f;
    }

    if ( transformDir ) {
        dir = TDUMat4x4MulVec( *pMat, dir );
    }

    Eigen::Vector4f velocity;
    bool applySpread = true;
    if ( velocityMode == 0 ) { // along the emitter's world up
        velocity = TDUMat4x4MulVec( *pMat, kWorldUpVector );
        velocity[3] = 0.0f;
        velocity.normalize();
    } else if ( velocityMode == 1 ) {
        velocity =
            TDUMat4x4MulVec( *pMat, Eigen::Vector4f( secondary[0], secondary[1],
                                                     secondary[2], 0.0f ) );
        velocity[3] = 0.0f;
        velocity.normalize();
    } else if ( velocityMode == 2 ) {
        velocity = kUnitXVector;
    } else {
        velocity = dir;
        applySpread = false;
    }

    const float spreadAngle =
        sampleRandom( pDef, pDef[0x07], Read<float>( pDef, 0x158 ),
                      Read<float>( pDef, 0x15c ) );
    if ( applySpread ) {
        if ( spreadAngle != 0.0f ) {
            const uint32_t u2 = LCGNext( Read<uint32_t>( pDef, 0x21c ) );
            const uint32_t u3 = LCGNext( u2 );
            const uint32_t u4 = LCGNext( u3 );
            Write<uint32_t>( pDef, 0x21c, u4 );
            const Eigen::Vector3f randomVec(
                BitsToFloat( ( u2 & 0x7fffff ) | 0x3f800000 ) - 1.5f,
                BitsToFloat( ( u3 & 0x7fffff ) | 0x3f800000 ) - 1.5f,
                BitsToFloat( ( u4 & 0x7fffff ) | 0x3f800000 ) - 1.5f );
            const Eigen::Vector3f dir3( velocity[0], velocity[1], velocity[2] );

            Eigen::Vector3f axis = randomVec.cross( dir3 );
            const float axisLength = axis.norm();
            if ( axisLength > 0.0f ) {
                axis /= axisLength;
                const float c = std::cos( spreadAngle );
                const float s = std::sin( spreadAngle );
                const Eigen::Vector3f spread =
                    dir3 * c + axis.cross( dir3 ) * s +
                    axis * ( axis.dot( dir3 ) * ( 1.0f - c ) );
                velocity[0] = spread.x();
                velocity[1] = spread.y();
                velocity[2] = spread.z();
                velocity[3] = 0.0f;
            }
        }
    }

    const float speed =
        sampleRandom( pDef, pDef[0x06], Read<float>( pDef, 0x150 ),
                      Read<float>( pDef, 0x154 ) );
    pOutVelocity[0] = velocity[0] * speed;
    pOutVelocity[1] = velocity[1] * speed;
    pOutVelocity[2] = velocity[2] * speed;
    pOutVelocity[3] = 0.0f;
}

float ParticleFile::sampleRandom( uint8_t *pDef,
                                  uint8_t mode,
                                  float base,
                                  float variance ) {
    // FUN_004589b0
    if ( mode == 1 ) {
        // Uniform in [base - variance, base + variance).
        const uint32_t state = LCGNext( Read<uint32_t>( pDef, 0x21c ) );
        Write<uint32_t>( pDef, 0x21c, state );
        const float frac = RandFloat01( state );
        return ( frac + frac - 1.0f ) * variance + base;
    }
    if ( mode == 2 ) {
        const uint32_t state = LCGNext( Read<uint32_t>( pDef, 0x21c ) );
        Write<uint32_t>( pDef, 0x21c, state );
        const float u = RandFloat01( state ) * 0.9f + 0.1f; // DAT_00f416a0 /
                                                            // DAT_00f41afc
        return -std::log( u ) * variance + base;
    }
    if ( mode == 3 ) {
        const uint32_t first = LCGNext( Read<uint32_t>( pDef, 0x21c ) );
        const uint32_t second = LCGNext( first );
        Write<uint32_t>( pDef, 0x21c, second );
        const float radius =
            std::sqrt( std::log( 1.0f - RandFloat01( first ) ) * kMinusTwo );
        const float angle = RandFloat01( second ) * kTwoPI;
        float normal = radius * std::cos( angle ) * 0.15f +
                       0.5f; // DAT_00f41af8 / FLOAT_00f41674
        if ( normal < 0.0f ) {
            normal = 0.0f;
        } else if ( 1.0f < normal ) {
            normal = 1.0f;
        }
        return ( normal * 2.0f - 1.0f ) * variance + base;
    }
    return 0.0f;
}

void ParticleFile::integrateEmitter( uint8_t *pEmitter, float deltaTime ) {
    // FUN_0045c250
    if ( pObject == nullptr ) {
        return;
    }

    int32_t maxParticles = Read<int32_t>( pEmitter, 0x10 );
    if ( maxParticles > 0x800 ) {
        maxParticles = 0x800;
    }

    Write<uint32_t>( pEmitter, 0x54, 0 );
    Write<float>( pEmitter, 0x50, deltaTime );
    Write<uint32_t>( pEmitter, 0x70, 0 );

    const float genDensity = Read<float>( pObject, prto::kGenDensity );
    const float genSpeed = Read<float>( pObject, prto::kGenSpeed );
    const float genSize = Read<float>( pObject, prto::kGenSize );

    const uint32_t defTableOffset = Read<uint32_t>( pEmitter, 0x24 );
    const int32_t defCount = Read<int32_t>( pEmitter, 0x1c );
    if ( defTableOffset != 0 ) {
        uint8_t *pDefTable = pBlob + defTableOffset;
        for ( int32_t i = 0; i < defCount; i++ ) {
            const uint32_t defOffset = Read<uint32_t>( pDefTable, i * 4 );
            if ( defOffset == 0 ) {
                continue;
            }
            uint8_t *pDef = pBlob + defOffset;
            if ( pDef[2] == 0 ) {
                continue;
            }
            Write<uint32_t>( pEmitter, 0x70, defOffset );
            if ( 1 < Read<uint32_t>( pDef, 8 ) ) {
                orientBoundDefinition( pDef );
            }
            const float interval = Read<float>( pDef, 0x170 );
            Write<int32_t>(
                pDef, 0x188,
                static_cast<int32_t>( 1.0f / ( interval * deltaTime ) ) );
            spawnParticles( pEmitter, pDef, maxParticles, genDensity, genSpeed,
                            genSize );
        }
    }

    uint8_t *pAttrPool = poolBase( prto::kAttrBase );
    uint8_t *pVelPool = poolBase( prto::kVelBase );
    uint8_t *pPosPool = poolBase( prto::kPosBase );

    int32_t scratchCount = 0;
    if ( pAttrPool != nullptr && pVelPool != nullptr && pPosPool != nullptr ) {
        float oldestAge = kOldestAgeSentinel;
        for ( int32_t i = 0; i < maxParticles; i++ ) {
            uint8_t *pAttr = pAttrPool + i * prto::kAttrStride;
            if ( pAttr[0] != 2 || pAttr[3] == 3 || pAttr[3] == 4 ) {
                continue;
            }
            uint8_t *pVel = pVelPool + i * prto::kVelStride;
            gScratchAttr[scratchCount] = pAttr;
            gScratchPos[scratchCount] = pPosPool + i * prto::kPosStride;
            gScratchVel[scratchCount] = pVel;
            scratchCount++;

            const float age = Read<float>( pAttr, 0x1c );
            if ( oldestAge < age ) {
                Write<int16_t>( pEmitter, 6, static_cast<int16_t>( i ) );
                oldestAge = age;
            }
            Write<float>( pVel, 0, 0.0f ); // acceleration = 0
            Write<float>( pVel, 4, 0.0f );
            Write<float>( pVel, 8, 0.0f );
            Write<float>( pVel, 0xc, 0.0f );
        }

        if ( scratchCount > 0 && pEmitter[9] != 0 ) {
            const float gx = Read<float>( pEmitter, 0x60 );
            const float gy = Read<float>( pEmitter, 0x64 );
            const float gz = Read<float>( pEmitter, 0x68 );
            if ( std::fabs( gx ) > kGravityThreshold ||
                 std::fabs( gy ) > kGravityThreshold ||
                 std::fabs( gz ) > kGravityThreshold ) {
                for ( int32_t k = 0; k < scratchCount; k++ ) {
                    float *pAccel = reinterpret_cast<float *>( gScratchVel[k] );
                    pAccel[0] += gx;
                    pAccel[1] += gy;
                    pAccel[2] += gz;
                    pAccel[3] = 0.0f;
                }
            }
        }
    }

    applyDefinitionForces( pEmitter, scratchCount );

    if ( pEmitter[9] != 0 ) {
        for ( int32_t k = 0; k < scratchCount; k++ ) {
            uint8_t *pAttr = gScratchAttr[k];
            float *pVel = reinterpret_cast<float *>(
                gScratchVel[k] ); // 0-2 accel, 4-6 velocity
            float *pPos = reinterpret_cast<float *>( gScratchPos[k] );

            const float posStep = Read<float>( pAttr, 0x30 ) * deltaTime;
            const float velX = pVel[4];
            const float velY = pVel[5];
            const float velZ = pVel[6];
            pPos[0] += velX * posStep;
            pPos[1] += velY * posStep;
            pPos[2] += velZ * posStep;
            pPos[3] = 0.0f;

            const float velStep = Read<float>( pAttr, 0x18 ) * deltaTime;
            pVel[4] += pVel[0] * velStep;
            pVel[5] += pVel[1] * velStep;
            pVel[6] += pVel[2] * velStep;
            pVel[7] = 0.0f;
        }
    }

    float bounds[4] = { 0.0f, 0.0f, 0.0f, -1.0f }; // FLOAT_00f41664
    if ( pAttrPool != nullptr && pVelPool != nullptr && pPosPool != nullptr ) {
        for ( int32_t i = 0; i < maxParticles; i++ ) {
            uint8_t *pAttr = pAttrPool + i * prto::kAttrStride;
            if ( pAttr[0] != 2 ) { // not allocated
                continue;
            }
            uint8_t *pVel = pVelPool + i * prto::kVelStride;
            uint8_t *pPos = pPosPool + i * prto::kPosStride;
            float *pAccel = reinterpret_cast<float *>( pVel );
            float *pVelocity = reinterpret_cast<float *>( pVel + 0x10 );
            float *pPosition = reinterpret_cast<float *>( pPos );

            const uint8_t phase = pAttr[3];
            if ( phase == 2 ) { // alive
                const float age = deltaTime + Read<float>( pAttr, 0x1c );
                Write<float>( pAttr, 0x1c, age );
                const float lifetime = Read<float>( pAttr, 0x20 );
                if ( ( lifetime <= age && age != lifetime ) || age < 0.0f ) {
                    pAttr[3] = 3; // die
                } else {
                    emitParticle( bounds, pPosition,
                                  static_cast<float>( pAttr[8] ) *
                                      Read<float>( pAttr, 0x24 ) *
                                      kEmitDistance );
                }
            } else if ( phase == 3 ) { // dying
                if ( ( ~( pEmitter[8] >> 1 ) & 1 ) != 0 ) {
                    pAttr[0] = 1; // mark for re-allocation
                    Write<int32_t>( pEmitter, 0x14,
                                    Read<int32_t>( pEmitter, 0x14 ) + 1 );
                }
                Write<int32_t>( pEmitter, 0x5c,
                                Read<int32_t>( pEmitter, 0x5c ) - 1 );
                pAttr[3] = 4; // recycled
            } else if ( phase ==
                        5 ) { // spawning: sub-frame catch-up integration
                const float velScale = Read<float>( pAttr, 0x18 );
                const float posScale = Read<float>( pAttr, 0x30 );
                const float age = Read<float>( pAttr, 0x1c );

                // Back out one acceleration step, then one velocity step.
                const float backAccel = -velScale * deltaTime;
                pVelocity[0] += backAccel * pAccel[0];
                pVelocity[1] += backAccel * pAccel[1];
                pVelocity[2] += backAccel * pAccel[2];
                pVelocity[3] = 0.0f;

                const float backVel = -posScale * deltaTime;
                pPosition[0] += backVel * pVelocity[0];
                pPosition[1] += backVel * pVelocity[1];
                pPosition[2] += backVel * pVelocity[2];
                pPosition[3] = 0.0f;

                // Advance forward by the accumulated age.
                pPosition[0] += age * pVelocity[0];
                pPosition[1] += age * pVelocity[1];
                pPosition[2] += age * pVelocity[2];
                pPosition[3] = 0.0f;

                const float ageAccel = velScale * age;
                pVelocity[0] += ageAccel * pAccel[0];
                pVelocity[1] += ageAccel * pAccel[1];
                pVelocity[2] += ageAccel * pAccel[2];
                pVelocity[3] = 0.0f;

                Write<int32_t>( pEmitter, 0x5c,
                                Read<int32_t>( pEmitter, 0x5c ) + 1 );
                pAttr[3] = 2; // alive
            }
        }
    }

    Write<float>( pEmitter, 0x84, bounds[0] );
    Write<float>( pEmitter, 0x88, bounds[1] );
    Write<float>( pEmitter, 0x8c, bounds[2] );
    Write<float>( pEmitter, 0x58, bounds[3] );
    Write<uint32_t>( pEmitter, 0x70, 0 );

    const uint32_t linkTableOffset = Read<uint32_t>( pEmitter, 0x3c );
    const int32_t linkCount = Read<int32_t>( pEmitter, 0x38 );
    if ( linkTableOffset == 0 ) {
        return;
    }
    int32_t linkIndex = 0;
    for ( int32_t i = 0; i < linkCount; i++ ) {
        const uint32_t linkOffset =
            Read<uint32_t>( pBlob + linkTableOffset, linkIndex * 4 );
        uint8_t *pLink = pBlob + linkOffset;
        if ( pLink[2] == 0 ) {
            continue;
        }
        Write<uint32_t>( pEmitter, 0x70, linkOffset );
        ageLinkedDefinitions( pEmitter, pLink );
        chainParticles( pLink, 0, maxParticles );
        linkIndex++;
    }
}

void ParticleFile::ageLinkedDefinitions( uint8_t *pEmitter, uint8_t *pLink ) {
    // FUN_0045c250
    uint32_t boundField = 0;
    uint32_t ageField = 0;
    if ( pLink[8] == 1 ) {
        boundField = 8;
        ageField = 0x17c;
    } else if ( pLink[8] == 2 ) {
        boundField = 0x218;
        ageField = 0x21c;
    } else {
        return;
    }

    const float maxAge = Read<float>( pLink, 0xc );
    const float deltaTime = Read<float>( pEmitter, 0x50 );
    const int32_t count = Read<int32_t>( pLink, 0x10 );
    const uint8_t *pTable = pBlob + Read<uint32_t>( pLink, 0x14 );
    for ( int32_t i = 0; i < count; i++ ) {
        uint8_t *pDef = pBlob + Read<uint32_t>( pTable, i * 4 );
        const uint32_t boundParticle = Read<uint32_t>( pDef, boundField );

        bool bRelease = false;
        if ( boundParticle == 1 ) {
            const float age = Read<float>( pDef, ageField );
            if ( maxAge < age ) {
                bRelease = true;
            } else {
                Write<float>( pDef, ageField, age + deltaTime );
            }
        } else if ( boundParticle != 0 ) {
            bRelease = pBlob[boundParticle + 3] != 2;
        }

        if ( bRelease ) {
            pDef[2] = 0;
            Write<uint32_t>( pDef, boundField, 0 );
        }
    }
}

void ParticleFile::chainParticles( uint8_t *pLink,
                                   int32_t start,
                                   int32_t end ) {
    // FUN_0045e3d0
    uint8_t *pAttrPool = poolBase( prto::kAttrBase );
    for ( int32_t i = start; i < end && pAttrPool != nullptr; i++ ) {
        uint8_t *pAttr = pAttrPool + i * prto::kAttrStride;
        const uint8_t phase = pAttr[3];
        if ( pAttr[0] != 2 || phase == 2 || phase == 4 || pLink[9] != 1 ||
             phase == 3 ) {
            continue;
        }
        if ( pLink[0xa] != 0 ) {
            pLink[0xa]--;
            continue;
        }
        pLink[0xa] = pLink[0xb];

        const uint32_t attrOffset = static_cast<uint32_t>( pAttr - pBlob );
        const int32_t count = Read<int32_t>( pLink, 0x10 );
        const uint8_t *pTable = pBlob + Read<uint32_t>( pLink, 0x14 );
        if ( pLink[8] == 1 ) {
            for ( int32_t j = 0; j < count; j++ ) {
                uint8_t *pDef = pBlob + Read<uint32_t>( pTable, j * 4 );
                if ( Read<uint32_t>( pDef, 8 ) != 0 ) {
                    continue;
                }
                Write<uint32_t>( pDef, 8, attrOffset );
                Write<float>( pDef, 0x17c, 0.0f );
                pDef[2] = 1;
                Write<uint32_t>( pDef, 0x18c, 0 );
                orientBoundDefinition( pDef );
                if ( pLink[9] == 2 ) {
                    Write<uint32_t>( pDef, 8, 1 );
                }
                break;
            }
        } else if ( pLink[8] == 2 ) {
            for ( int32_t j = 0; j < count; j++ ) {
                uint8_t *pDef = pBlob + Read<uint32_t>( pTable, j * 4 );
                if ( Read<uint32_t>( pDef, 0x218 ) != 0 ) {
                    continue;
                }
                Write<uint32_t>( pDef, 0x218, attrOffset );
                Write<float>( pDef, 0x21c, 0.0f );
                pDef[2] = 1;
                attachBoundDefinition( pDef );
                if ( pLink[9] == 2 ) {
                    Write<uint32_t>( pDef, 0x218, 1 );
                }
                break;
            }
        }
    }
    pObject[2] = 4;
}

void ParticleFile::orientBoundDefinition( uint8_t *pDef ) {
    // FUN_0045a7b0
    if ( pDef == nullptr ) {
        return;
    }

    const uint32_t attrOffset = Read<uint32_t>( pDef, 8 );
    const int32_t index = Read<int32_t>( pBlob, attrOffset + 4 );
    const uint8_t *pPosition =
        poolBase( prto::kPosBase ) + index * prto::kPosStride;
    std::memcpy( pDef + 0x30, pPosition, 4 * sizeof( float ) );

    const uint8_t alignMode = pDef[5];
    if ( alignMode != 1 ) {
        const uint8_t *pVel =
            poolBase( prto::kVelBase ) + index * prto::kVelStride;
        const uint8_t *pDirection = nullptr;
        if ( alignMode == 2 ) {
            pDirection = pVel + 0x10; // velocity
        } else if ( alignMode == 3 ) {
            pDirection = pVel; // acceleration
        }

        if ( pDirection != nullptr ) {
            const Eigen::Vector4f direction(
                Read<float>( pDirection, 0 ), Read<float>( pDirection, 4 ),
                Read<float>( pDirection, 8 ), Read<float>( pDirection, 0xc ) );
            if ( kOrientThreshold < std::fabs( direction.x() ) ||
                 kOrientThreshold < std::fabs( direction.y() ) ||
                 kOrientThreshold < std::fabs( direction.z() ) ) {
                float x = 0.0f;
                float y = 0.0f;
                float z = 0.0f;
                ExtractEulerAnglesXYZ( BuildBasisAlongZ( direction ), x, y, z );
                Write<float>( pDef, 0x20, x );
                Write<float>( pDef, 0x24, y );
                Write<float>( pDef, 0x28, z );
            }
        }
    }

    rebuildDefinitionMatrix( pDef );
}

void ParticleFile::rebuildDefinitionMatrix( uint8_t *pDef ) {
    // FUN_00458a30
    if ( pObject == nullptr || pObject[0] == 0 || pDef == nullptr ) {
        return;
    }
    Write<Matrix4x4f_t>( pDef, 0x40, BuildDefinitionTransform( pDef ) );
}

void ParticleFile::attachBoundDefinition( uint8_t *pDef ) {
    // FUN_0045e010
    const uint32_t attrOffset = Read<uint32_t>( pDef, 0x218 );
    const int32_t index = Read<int32_t>( pBlob, attrOffset + 4 );
    const uint8_t *pPosition =
        poolBase( prto::kPosBase ) + index * prto::kPosStride;
    Write<float>( pDef, 0x30, Read<float>( pPosition, 0 ) );
    Write<float>( pDef, 0x34, Read<float>( pPosition, 4 ) );
    Write<float>( pDef, 0x38, Read<float>( pPosition, 8 ) );
    Write<float>( pDef, 0x3c, 0.0f );
    rebuildBoundDefinitionMatrices( pDef );
}

void ParticleFile::rebuildBoundDefinitionMatrices( uint8_t *pDef ) {
    // FUN_0045dec0
    if ( pObject == nullptr || pObject[0] == 0 || pDef == nullptr ) {
        return;
    }

    Eigen::Vector3f inverseScale( 1.0f / Read<float>( pDef, 0x10 ),
                                  1.0f / Read<float>( pDef, 0x14 ),
                                  1.0f / Read<float>( pDef, 0x18 ) );
    const float rotationX = Read<float>( pDef, 0x20 );
    const float rotationY = Read<float>( pDef, 0x24 );
    const float rotationZ = Read<float>( pDef, 0x28 );

    Write<Matrix4x4f_t>( pDef, 0x40, BuildDefinitionTransform( pDef ) );

    Matrix4x4f_t inverse = Matrix4x4f_t::Identity();
    inverse( 3, 0 ) -= Read<float>( pDef, 0x30 );
    inverse( 3, 1 ) -= Read<float>( pDef, 0x34 );
    inverse( 3, 2 ) -= Read<float>( pDef, 0x38 );
    // The original swaps the X and Z angles when undoing the rotation.
    MatrixRotateZ( inverse, 0.0f - rotationX );
    MatrixRotateY( inverse, 0.0f - rotationY );
    MatrixRotateX( inverse, 0.0f - rotationZ );
    inverse = inverse * MakeScaleMatrix( inverseScale );
    Write<Matrix4x4f_t>( pDef, 0x80, inverse );
}

void ParticleFile::insertParticleInGridCell( uint8_t *pCell,
                                             uint32_t attrOffset ) {
    // FUN_0045e3a0
    const int32_t capacity = Read<int32_t>( pCell, 0 );
    uint8_t *pTable = pBlob + Read<uint32_t>( pCell, 0x20 );
    for ( int32_t i = 0; i < capacity; i++ ) {
        const uint32_t entry = Read<uint32_t>( pTable, i * 4 );
        if ( entry == attrOffset ) {
            return;
        }
        if ( entry == 0 ) {
            Write<uint32_t>( pTable, i * 4, attrOffset );
            return;
        }
    }
}

void ParticleFile::linkLatticeParticle( uint8_t *pSource, uint8_t *pAttr ) {
    // FUN_0045e100
    const int32_t width = Read<int32_t>( pSource, 0 );
    const int32_t height = Read<int32_t>( pSource, 4 );
    const int32_t column = Read<int32_t>( pSource, 8 );
    const int32_t row = Read<int32_t>( pSource, 0xc );
    if ( column == width && row == height ) {
        return;
    }

    const uint32_t attrOffset = static_cast<uint32_t>( pAttr - pBlob );
    uint8_t *pCells = pBlob + Read<uint32_t>( pSource, 0x10 );
    const int32_t cell = row * width + column;
    Write<uint32_t>( pCells, cell * 4, attrOffset );

    const uint32_t linkOffset = Read<uint32_t>( pSource, 0x14 ) + cell * 0x80;
    pAttr[0xb] = static_cast<uint8_t>( row );
    Write<uint32_t>( pAttr, 0x38, linkOffset );
    pAttr[0xa] = static_cast<uint8_t>( column );

    const uint8_t *pPosPool = poolBase( prto::kPosBase );
    const uint8_t *pPosition =
        pPosPool + Read<int32_t>( pAttr, 4 ) * prto::kPosStride;

    int32_t slot = -1;
    for ( int32_t dx = -1; dx <= 1; dx++ ) {
        for ( int32_t dy = -1; dy <= 1; dy++ ) {
            if ( dx == 0 && dy == 0 ) {
                continue;
            }
            slot++;

            const int32_t neighbourColumn = column + dx;
            const int32_t neighbourRow = row + dy;
            if ( neighbourRow < 0 || height <= neighbourRow ||
                 neighbourColumn < 0 || width <= neighbourColumn ) {
                continue;
            }
            const uint32_t neighbourOffset = Read<uint32_t>(
                pCells, ( neighbourRow * width + neighbourColumn ) * 4 );
            if ( neighbourOffset == 0 ) {
                continue;
            }

            const uint8_t *pNeighbour = pBlob + neighbourOffset;
            const float restLength = ParticleDistance(
                pPosition,
                pPosPool + Read<int32_t>( pNeighbour, 4 ) * prto::kPosStride );
            WriteLatticeLink( pBlob + linkOffset, slot, neighbourOffset,
                              restLength );
            WriteLatticeLink( pBlob + Read<uint32_t>( pNeighbour, 0x38 ),
                              7 - slot, attrOffset, restLength );
        }
    }

    const int32_t nextColumn = column + 1;
    Write<int32_t>( pSource, 8, nextColumn );
    if ( nextColumn == width ) {
        Write<int32_t>( pSource, 8, 0 );
        Write<int32_t>( pSource, 0xc, row + 1 );
    }
}

void ParticleFile::emitDef( uint8_t *pEmitter,
                            uint8_t *pRecord,
                            uint32_t maxParticles,
                            uint32_t colorId,
                            const Matrix4x4f_t &worldToCam,
                            const Matrix4x4f_t &projection,
                            DrawList &out ) {
    // FUN_0045adc0
    if ( pObject == nullptr || Read<float>( pEmitter, 0x58 ) == -1.0f ) {
        return;
    }

    if ( ( pRecord[0xe] != 1 && pRecord[0xe] != 3 ) ||
         gpParticles == nullptr ) {
        return;
    }
    const uint8_t *pMatChunk = pBlob + 0x10 + Read<uint32_t>( pBlob, 0x18 );
    const uint32_t matIndex = Read<uint32_t>( pRecord, 0x40 );
    const uint64_t hashcode =
        Read<uint64_t>( pMatChunk, 0x18 + matIndex * 0x10 );
    Material *pMaterial = gpParticles->resolveParticleMaterial( hashcode );
    if ( pMaterial == nullptr ) {
        return;
    }

    if ( gpRender == nullptr ) {
        return;
    }
    RenderScene *pScene =
        gpRender->getRenderPass<eRenderPass::RP_Near>().pScene;
    if ( pScene == nullptr || pScene->getCamera() == nullptr ) {
        return;
    }

    if ( !out.isInitialized() ) {
        constexpr uint32_t kMaxSprites = 2048;
        DrawStreams streams;
        streams.Diffuse = 1;
        streams.UVMap = 1;
        streams.bIndex16 = 1;
        if ( !out.initialize( kMaxSprites, kMaxSprites * 4, kMaxSprites * 6,
                              true, &streams, "ParticleRList", true ) ) {
            return;
        }
    }

    const Matrix4x4f_t &camToWorld = pScene->getCamera()->getCamToWorld();
    const Eigen::Vector3f right( camToWorld( 0, 0 ), camToWorld( 0, 1 ),
                                 camToWorld( 0, 2 ) );
    const Eigen::Vector3f up( camToWorld( 1, 0 ), camToWorld( 1, 1 ),
                              camToWorld( 1, 2 ) );

    uint8_t *pAttrPool = poolBase( prto::kAttrBase );
    uint8_t *pPosPool = poolBase( prto::kPosBase );
    uint8_t *pVelPool = poolBase( prto::kVelBase );
    if ( pAttrPool == nullptr || pPosPool == nullptr || pVelPool == nullptr ) {
        return;
    }

    const uint32_t spriteCount = maxParticles < 0x800 ? maxParticles : 0x800;
    const uint8_t recordFlags = pRecord[0xb];

    const bool bCoverageBudget = ( recordFlags & 0x10 ) != 0;
    const float coverageBudget = static_cast<float>( pRecord[0xd] );
    float coverage = 0.0f;
    const Matrix4x4f_t clipTransform =
        Read<Matrix4x4f_t>( pRecord, 0xc0 ) * worldToCam * projection;
    const float projectionScale = std::fabs( projection( 0, 0 ) );
    for ( uint32_t i = 0; i < spriteCount; i++ ) {
        uint8_t *pAttr = pAttrPool + i * prto::kAttrStride;
        pAttr[9] = 0;
        if ( pAttr[3] != 2 ) {
            continue;
        }
        if ( !bCoverageBudget ) {
            pAttr[9] = 1;
            continue;
        }
        if ( coverage >= coverageBudget ) {
            continue;
        }
        const float *pPos =
            reinterpret_cast<const float *>( pPosPool + i * prto::kPosStride );
        const Eigen::Vector4f clip = TDUMat4x4MulVec(
            clipTransform, Eigen::Vector4f( pPos[0], pPos[1], pPos[2], 1.0f ) );
        if ( 0.0f < clip.w() ) {
            float screenSize =
                static_cast<float>( pAttr[8] ) * Read<float>( pAttr, 0x24 ) *
                ( 1.0f / clip.w() ) * projectionScale * kEmitDistance;
            if ( 1.0f < screenSize ) {
                screenSize = 1.0f;
            }
            coverage += screenSize * screenSize;
            pAttr[9] = 1;
        }
    }

    int32_t numSprites = 0;
    for ( uint32_t i = 0; i < spriteCount; i++ ) {
        if ( pAttrPool[i * prto::kAttrStride + 9] != 0 ) {
            gSpriteOrder[numSprites++] = i;
        }
    }
    if ( ( recordFlags & 0xf ) != 0 ) {
        if ( ( recordFlags & 3 ) == 0 ) {
            numSprites = 0;
        } else {
            const bool bYoungestFirst = ( recordFlags & 1 ) != 0;
            std::stable_sort(
                gSpriteOrder, gSpriteOrder + numSprites,
                [pAttrPool, bYoungestFirst]( uint32_t a, uint32_t b ) {
                    const float ageA =
                        Read<float>( pAttrPool + a * prto::kAttrStride, 0x1c );
                    const float ageB =
                        Read<float>( pAttrPool + b * prto::kAttrStride, 0x1c );
                    return bYoungestFirst ? ageA < ageB : ageB < ageA;
                } );
        }
    }

    const float aspect = Read<float>( pRecord, 0x74 );
    const bool bTransformUVs =
        ( Read<uint16_t>( pRecord, 0x78 ) & SUV_Enabled ) != 0;
    const uint32_t paletteBlend =
        static_cast<uint32_t>(
            static_cast<int64_t>( Read<float>( pRecord, 0xb8 ) * 255.0f ) ) &
        0xff;
    const bool bCornerTint = Read<uint32_t>( pRecord, 0xbc ) != 0xffffffff;

    out.setIdentityTransform();
    out.setActiveMaterial( pMaterial );

    for ( int32_t s = 0; s < numSprites; s++ ) {
        const uint32_t i = gSpriteOrder[s];
        uint8_t *pAttr = pAttrPool + i * prto::kAttrStride;
        const float *pPos =
            reinterpret_cast<const float *>( pPosPool + i * prto::kPosStride );
        const Eigen::Vector3f center( pPos[0], pPos[1], pPos[2] );

        uint32_t color = Read<uint32_t>( pAttr, 0xc );
        if ( paletteBlend != 0 ) {
            const uint32_t seed = Read<uint32_t>( pAttr, 0x3c );
            const uint32_t slot = seed & 7;
            const uint32_t paletteColor = LerpColorBytes(
                Read<uint32_t>( pRecord, 0x94 + slot * 4 ),
                Read<uint32_t>( pRecord, 0x98 + slot * 4 ),
                static_cast<uint32_t>( static_cast<int32_t>( seed ) >> 3 ) &
                    0xff );
            color = LerpColorBytes( color, paletteColor, paletteBlend );
        }

        uint8_t channels[4];
        for ( uint32_t c = 0; c < 4; c++ ) {
            uint32_t value = ( static_cast<uint32_t>( pAttr[0x10 + c] ) *
                               ( ( color >> ( c * 8 ) ) & 0xff ) ) >>
                             8;
            value *= ( colorId >> ( c * 8 ) ) & 0xff;
            channels[c] =
                static_cast<uint8_t>( ( value >> 8 ) + ( ( value >> 7 ) & 1 ) );
        }
        if ( channels[3] < pRecord[0xf] ) {
            continue;
        }
        const uint32_t spriteColor =
            static_cast<uint32_t>( channels[0] ) |
            ( static_cast<uint32_t>( channels[1] ) << 8 ) |
            ( static_cast<uint32_t>( channels[2] ) << 16 ) |
            ( static_cast<uint32_t>( channels[3] ) << 24 );

        uint32_t cornerColors[4] = { spriteColor, spriteColor, spriteColor,
                                     spriteColor };
        if ( bCornerTint ) {
            for ( uint32_t c = 0; c < 4; c++ ) {
                cornerColors[c] =
                    ScaleColorBytes( spriteColor, pRecord[0xbc + c] );
            }
        }

        // Billboard size: alpha-seed * size / 255; width scaled by the aspect.
        const float size = static_cast<float>( pAttr[8] ) *
                           Read<float>( pAttr, 0x24 ) / 255.0f;
        const Eigen::Vector3f halfWidth = right * ( aspect * size * 0.5f );
        const Eigen::Vector3f halfHeight = up * ( size * 0.5f );

        float texCoords[8] = { 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 1.0f, 1.0f };
        if ( bTransformUVs ) {
            const float *pVelocity = reinterpret_cast<const float *>(
                pVelPool + i * prto::kVelStride + 0x10 );
            const UVTransform transform =
                BuildSpriteUVTransform( pRecord, pAttr, pVelocity, right, up );
            for ( uint32_t corner = 0; corner < 4; corner++ ) {
                const float u = texCoords[corner * 2 + 0];
                const float v = texCoords[corner * 2 + 1];
                texCoords[corner * 2 + 0] =
                    transform.m[0] * u + transform.m[2] * v + transform.m[4];
                texCoords[corner * 2 + 1] =
                    transform.m[1] * u + transform.m[3] * v + transform.m[5];
            }
        }

        if ( out.beginPrimitive( PT_TriangleStrip, 4 ) ) {
            const Eigen::Vector3f topLeft = center - halfWidth + halfHeight;
            const Eigen::Vector3f bottomLeft = center - halfWidth - halfHeight;
            const Eigen::Vector3f topRight = center + halfWidth + halfHeight;
            const Eigen::Vector3f bottomRight = center + halfWidth - halfHeight;
            out.setDiffuse( 0, ColorRGBA( cornerColors[2] ) );
            out.setTexCoords( 0,
                              Eigen::Vector2f( texCoords[0], texCoords[1] ) );
            out.pushVertex( topLeft.x(), topLeft.y(), topLeft.z() );
            out.setDiffuse( 0, ColorRGBA( cornerColors[0] ) );
            out.setTexCoords( 0,
                              Eigen::Vector2f( texCoords[2], texCoords[3] ) );
            out.pushVertex( bottomLeft.x(), bottomLeft.y(), bottomLeft.z() );
            out.setDiffuse( 0, ColorRGBA( cornerColors[3] ) );
            out.setTexCoords( 0,
                              Eigen::Vector2f( texCoords[4], texCoords[5] ) );
            out.pushVertex( topRight.x(), topRight.y(), topRight.z() );
            out.setDiffuse( 0, ColorRGBA( cornerColors[1] ) );
            out.setTexCoords( 0,
                              Eigen::Vector2f( texCoords[6], texCoords[7] ) );
            out.pushVertex( bottomRight.x(), bottomRight.y(), bottomRight.z() );
            out.commitPrimitive();
        }
    }
}
