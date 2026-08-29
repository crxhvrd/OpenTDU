#include "shared.h"
#include "hierarchy_geometry.h"

#include "core/maths_helper.h"

#include "3dd.h"
#include "3dg.h"
#include "geometry_buffer.h"
#include "hiearchy_node.h"
#include "instance.h"
#include "render_api.h"

inline float HalfToFloat( uint16_t half ) {
    const uint32_t sign = uint32_t( half >> 15 ) << 31;
    uint32_t exponent = ( half >> 10 ) & 0x1f;
    uint32_t mantissa = half & 0x3ff;

    if ( exponent == 0 ) {
        if ( mantissa == 0 ) {
            const uint32_t zero = sign;
            float out;
            memcpy( &out, &zero, sizeof( out ) );
            return out;
        }
        // Subnormal: renormalise into a float exponent.
        exponent = 1;
        while ( ( mantissa & 0x400 ) == 0 ) {
            mantissa <<= 1;
            exponent--;
        }
        mantissa &= 0x3ff;
    } else if ( exponent == 0x1f ) {
        const uint32_t special = sign | 0x7f800000u | ( mantissa << 13 );
        float out;
        memcpy( &out, &special, sizeof( out ) );
        return out;
    }

    const uint32_t bits =
        sign | ( ( exponent + ( 127 - 15 ) ) << 23 ) | ( mantissa << 13 );
    float out;
    memcpy( &out, &bits, sizeof( out ) );
    return out;
}

const uint8_t *GetVertexData( const Primitive *pPrimitive ) {
    GeomtryBufferWithHeader *pBuffer = pPrimitive->pVertexBuffer;
    if ( pBuffer == nullptr ) {
        return nullptr;
    }
    if ( pPrimitive->bUploaded && pBuffer->Buffer.pCPUBuffer != nullptr ) {
        return (const uint8_t *)pBuffer->Buffer.pCPUBuffer;
    }
    return (const uint8_t *)pBuffer + kGeometryBufferDataOffset;
}

const uint8_t *GetIndexData( const Primitive *pPrimitive ) {
    GeomtryBufferWithHeader *pBuffer = pPrimitive->pIndexBuffer;
    if ( pBuffer == nullptr ) {
        return nullptr;
    }
    if ( pPrimitive->bUploaded && pBuffer->Buffer.pCPUBuffer != nullptr ) {
        return (const uint8_t *)pBuffer->Buffer.pCPUBuffer;
    }
    return (const uint8_t *)pBuffer + kGeometryBufferDataOffset;
}

bool ReadPosition( const uint8_t *pStream,
                   const eVertexAttributeFormat format,
                   Eigen::Vector3f &outPosition ) {
    switch ( format ) {
    case eVertexAttributeFormat::VAF_POSITION_R32G32B32_SFLOAT:
    case eVertexAttributeFormat::VAF_POSITION_R32G32B32A32_SFLOAT: {
        const float *p = (const float *)pStream;
        outPosition = Eigen::Vector3f( p[0], p[1], p[2] );
        return true;
    }
    case eVertexAttributeFormat::VAF_POSITION_R16G16B16A16_SFLOAT: {
        const uint16_t *p = (const uint16_t *)pStream;
        outPosition = Eigen::Vector3f( HalfToFloat( p[0] ), HalfToFloat( p[1] ),
                                       HalfToFloat( p[2] ) );
        return true;
    }
    default:
        return false;
    }
}

uint32_t GetNumTriangles( const ePrimitiveType type, const uint32_t numIndex ) {
    switch ( type ) {
    case ePrimitiveType::PT_Triangle:
        return numIndex / 3;
    case ePrimitiveType::PT_TriangleStrip:
    case ePrimitiveType::PT_TriangleFan:
        return ( numIndex >= 3 ) ? ( numIndex - 2 ) : 0;
    default:
        return 0;
    }
}

void GatherPrimitive( const Primitive *pPrimitive,
                      const Matrix4x4f_t &modelMatrix,
                      HierarchyTriangleSoup &outSoup ) {
    if ( pPrimitive->NumVertex == 0 || pPrimitive->NumIndex == 0 ) {
        return;
    }

    if ( pPrimitive->NumBones != 0 ) {
        return;
    }

    const uint32_t numTriangles =
        GetNumTriangles( pPrimitive->Type, pPrimitive->NumIndex );
    if ( numTriangles == 0 ) {
        return;
    }

    const PrimtiveVertexAttributes &streams = pPrimitive->VertexStreams;
    if ( streams.Position.Number == 0 ) {
        return;
    }

    const uint8_t *pVertexData = GetVertexData( pPrimitive );
    const uint8_t *pIndexData = GetIndexData( pPrimitive );
    if ( pVertexData == nullptr || pIndexData == nullptr ) {
        return;
    }

    const uint32_t positionStride =
        GetVertexAttributeSize( streams.Position.Format );
    if ( positionStride == 0 ) {
        return;
    }

    const uint8_t *pPositions =
        pVertexData + pPrimitive->VertexAttributeOffsets[0];

    const uint32_t baseVertex = (uint32_t)outSoup.Vertices.size();
    outSoup.Vertices.reserve( baseVertex + pPrimitive->NumVertex );
    for ( uint32_t i = 0; i < pPrimitive->NumVertex; i++ ) {
        Eigen::Vector3f localPosition;
        if ( !ReadPosition( pPositions + i * positionStride,
                            streams.Position.Format, localPosition ) ) {
            outSoup.Vertices.resize( baseVertex );
            return;
        }
        outSoup.Vertices.push_back(
            TDUMat4x4MulVec( modelMatrix, localPosition ) );
    }

    const bool bIs16BitIndex = ( pPrimitive->IndexType == 1 );
    const uint16_t *pIndices16 = (const uint16_t *)pIndexData;
    const uint32_t *pIndices32 = (const uint32_t *)pIndexData;

    const uint32_t firstIndex = pPrimitive->IndexOffset;
    const bool bIsStrip =
        ( pPrimitive->Type == ePrimitiveType::PT_TriangleStrip );
    const bool bIsFan = ( pPrimitive->Type == ePrimitiveType::PT_TriangleFan );

    auto readIndex = [&]( uint32_t at ) -> uint32_t {
        return bIs16BitIndex ? (uint32_t)pIndices16[at] : pIndices32[at];
    };

    outSoup.Indices.reserve( outSoup.Indices.size() + numTriangles * 3 );
    for ( uint32_t t = 0; t < numTriangles; t++ ) {
        uint32_t i0, i1, i2;
        if ( bIsFan ) {
            i0 = readIndex( firstIndex );
            i1 = readIndex( firstIndex + t + 1 );
            i2 = readIndex( firstIndex + t + 2 );
        } else if ( bIsStrip ) {
            i0 = readIndex( firstIndex + t );
            i1 = readIndex( firstIndex + t + 1 );
            i2 = readIndex( firstIndex + t + 2 );
            if ( t & 1 ) {
                const uint32_t swap = i1;
                i1 = i2;
                i2 = swap;
            }
        } else {
            i0 = readIndex( firstIndex + t * 3 + 0 );
            i1 = readIndex( firstIndex + t * 3 + 1 );
            i2 = readIndex( firstIndex + t * 3 + 2 );
        }

        if ( i0 >= pPrimitive->NumVertex || i1 >= pPrimitive->NumVertex ||
             i2 >= pPrimitive->NumVertex ) {
            continue;
        }
        if ( i0 == i1 || i1 == i2 || i0 == i2 ) {
            continue;
        }

        outSoup.Indices.push_back( baseVertex + i0 );
        outSoup.Indices.push_back( baseVertex + i1 );
        outSoup.Indices.push_back( baseVertex + i2 );
    }
}

const uint64_t kReflectPlaneHash = GetIdentifier64bit( "D_REFLECT" );
const uint64_t kOceanLevelHash = GetIdentifier64bit( "D_OCEAN" );

bool ShouldSkipInstance( Instance *pInstance ) {
    if ( pInstance == nullptr || pInstance->getNumLODs() == 0 ) {
        return true;
    }
    if ( pInstance->isFree() ) {
        return true;
    }
    if ( ( pInstance->getFlags() >> 2 & 1 ) != 0 ) {
        return true;
    }
    const uint64_t hashcode = pInstance->getHashcode();
    return hashcode == kReflectPlaneHash || hashcode == kOceanLevelHash;
}

void GatherInstance( Instance *pInstance, HierarchyTriangleSoup &outSoup ) {
    if ( ShouldSkipInstance( pInstance ) ) {
        return;
    }

    InstanceLOD &lod = pInstance->getLOD( 0 );
    if ( lod.pHashcode == nullptr ) {
        return;
    }

    RenderFile::Section *pSection = lod.pHashcode->pEntry;
    if ( pSection == nullptr || pSection->Type == kHeightmapMagic ) {
        return;
    }

    RenderObject *pObject =
        (RenderObject *)( (uint8_t *)pSection + sizeof( *pSection ) );
    RenderFile::Section *pPrimitiveSection = pObject->PrimitiveSection;
    if ( pPrimitiveSection == nullptr ) {
        return;
    }

    const Matrix4x4f_t &modelMatrix = pInstance->getModelMatrix();

    uint8_t *pSectionIt = (uint8_t *)pPrimitiveSection;
    for ( uint32_t i = 0; i < pObject->NumPrimitives; i++ ) {
        GatherPrimitive( (const Primitive *)( pSectionIt + 0x10 ), modelMatrix,
                         outSoup );
        pSectionIt += ( (RenderFile::Section *)pSectionIt )->Size;
    }
}

bool GatherHierarchyTriangles( HiearchyNode *pRoot,
                               HierarchyTriangleSoup &outSoup ) {
    if ( pRoot == nullptr ) {
        return false;
    }

    HiearchyNode *pNode = pRoot;
    for ( uint32_t remaining = (uint32_t)pRoot->NumChildren + 1u;
          remaining != 0 && pNode != nullptr; remaining-- ) {
        GatherInstance( pNode->pInstance, outSoup );
        pNode = pNode->pNext;
    }

    return outSoup.Indices.size() >= 3 && outSoup.Vertices.size() >= 3;
}
