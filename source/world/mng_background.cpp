#include "shared.h"
#include "mng_background.h"

#include "core/maths_helper.h"

#include "render/2db.h"
#include "render/2dm.h"
#include "render/3dd.h"
#include "render/3dg.h"
#include "render/geometry_buffer.h"
#include "render/gs_render.h"
#include "render/hiearchy_node.h"
#include "render/instance.h"
#include "render/material.h"
#include "render/render_scene.h"
#include "render/render_context.h"

#include "render/camera/camera_game.h"
#include "render/camera/gs_camera.h"

#include "world/gs_mng_resources.h"
#include "world/weather/gs_weather.h"
#include "world/weather/weather_config.h"
#include "world/weather/weather_desc.h"

MngBackground *gpMngBackground = nullptr; // DAT_010e8010

bool gbBackgroundCloudsEnabled = true;
bool gbBackgroundDomeEnabled = true;
bool gbBackgroundDomeWireframe = false;
static eFillMode gDomeOriginalFillMode = FM_Default;

// Fix for the dome mesh uv seams issue from the original game.
bool gbBackgroundDomeSkyFixes = true;

static constexpr float kUpperDomeElevation = 13.0f;

static inline bool IsUsablePointer( const void *pPointer ) {
    const uintptr_t address = (uintptr_t)pPointer;
    if ( address < 0x10000u ) {
        return false;
    }
    const uint32_t low = (uint32_t)address;
    return low != 0xcdcdcdcdu && low != 0xddddddddu && low != 0xfeeefeeeu &&
           low != 0xbaadf00du && low != 0xffffffffu;
}

static void PatchDomeUVSeams( Instance *pInstance ) {
    if ( pInstance == nullptr || pInstance->getNumLODs() == 0 ) {
        return;
    }

    InstanceLOD &lod = pInstance->getLOD( 0 );
    if ( !IsUsablePointer( lod.pHashcode ) ||
         !IsUsablePointer( lod.pHashcode->pEntry ) ) {
        return;
    }

    RenderObject *pObject =
        (RenderObject *)( (uint8_t *)lod.pHashcode->pEntry + 0x10 );
    RenderFile::Section *pPrimitiveSection = pObject->PrimitiveSection;
    if ( !IsUsablePointer( pPrimitiveSection ) ||
         pObject->NumPrimitives > 64u ) {
        return;
    }

    for ( uint32_t primIdx = 0; primIdx < pObject->NumPrimitives; primIdx++ ) {
        Primitive *pPrimitive =
            (Primitive *)( (uint8_t *)pPrimitiveSection + 0x10 );
        const PrimtiveVertexAttributes &streams = pPrimitive->VertexStreams;

        const bool bFormatOK =
            streams.Position.Format == VAF_POSITION_R32G32B32_SFLOAT &&
            streams.UV.Format == VAF_TEXCOORD_R32G32_SFLOAT &&
            streams.Position.Number != 0 && streams.UV.Number != 0;

        GeomtryBufferWithHeader *pVB = pPrimitive->pVertexBuffer;
        uint8_t *pData = IsUsablePointer( pVB )
                             ? (uint8_t *)pVB + kGeometryBufferDataOffset
                             : nullptr;
        const uint32_t dataSize =
            ( pVB != nullptr ) ? pVB->Buffer.ByteSize : 0u;
        const uint32_t numVertex = pPrimitive->NumVertex;

        const uint32_t uvAttrIdx =
            streams.Position.Number + streams.Normal.Number +
            streams.Diffuse.Number + streams.Specular.Number;
        const uint32_t posOffset = pPrimitive->VertexAttributeOffsets[0];
        const uint32_t uvOffset =
            ( uvAttrIdx < PrimtiveVertexAttributes::kMaxNumAttributes )
                ? pPrimitive->VertexAttributeOffsets[uvAttrIdx]
                : 0xffffffffu;

        const bool bInRange =
            pData != nullptr && uvOffset != 0xffffffffu && numVertex != 0 &&
            (uint64_t)posOffset + (uint64_t)numVertex * 12u <= dataSize &&
            (uint64_t)uvOffset + (uint64_t)numVertex * 8u <= dataSize;

        if ( !bFormatOK || !bInRange ) {
            OTDU_LOG_WARN( "Dome UV weld skipped for primitive %u (posFmt=%u "
                           "uvFmt=%u inRange=%d)\n",
                           primIdx, (uint32_t)streams.Position.Format,
                           (uint32_t)streams.UV.Format, bInRange ? 1 : 0 );
        } else {
            const float *pPos = (const float *)( pData + posOffset );
            float *pUV = (float *)( pData + uvOffset );

            // Cluster by DIRECTION, not world position
            std::vector<float> dir( (size_t)numVertex * 3 );
            for ( uint32_t v = 0; v < numVertex; v++ ) {
                const float px = pPos[v * 3 + 0];
                const float py = pPos[v * 3 + 1];
                const float pz = pPos[v * 3 + 2];
                const float len = std::sqrt( px * px + py * py + pz * pz );
                const float inv = ( len > 0.0001f ) ? ( 1.0f / len ) : 0.0f;
                dir[v * 3 + 0] = px * inv;
                dir[v * 3 + 1] = py * inv;
                dir[v * 3 + 2] = pz * inv;
            }

            // ~0.05 deg: tight enough not to merge neighbouring vertices (the
            // dense band steps ~1.9 deg in azimuth).
            const float minDot = 0.99999962f;

            std::vector<int32_t> clusterOf( numVertex, -1 );
            int32_t numWelded = 0, numWrapSkipped = 0;
            float maxDelta = 0.0f;

            for ( uint32_t i = 0; i < numVertex; i++ ) {
                if ( clusterOf[i] != -1 ) {
                    continue;
                }
                clusterOf[i] = (int32_t)i;

                std::vector<uint32_t> members;
                members.push_back( i );
                for ( uint32_t j = i + 1; j < numVertex; j++ ) {
                    if ( clusterOf[j] != -1 ) {
                        continue;
                    }
                    const float dot = dir[i * 3 + 0] * dir[j * 3 + 0] +
                                      dir[i * 3 + 1] * dir[j * 3 + 1] +
                                      dir[i * 3 + 2] * dir[j * 3 + 2];
                    if ( dot >= minDot ) {
                        clusterOf[j] = (int32_t)i;
                        members.push_back( j );
                    }
                }
                if ( members.size() < 2 ) {
                    continue;
                }

                float spread = 0.0f;
                bool bWrap = false;
                for ( size_t a = 0; a < members.size(); a++ ) {
                    for ( size_t b = a + 1; b < members.size(); b++ ) {
                        const float du = std::fabs( pUV[members[a] * 2 + 0] -
                                                    pUV[members[b] * 2 + 0] );
                        const float dv = std::fabs( pUV[members[a] * 2 + 1] -
                                                    pUV[members[b] * 2 + 1] );
                        if ( ( du > 0.999f && dv < 0.001f ) ||
                             ( dv > 0.999f && du < 0.001f ) ) {
                            bWrap = true;
                        }
                        spread = Max( spread, std::sqrt( du * du + dv * dv ) );
                    }
                }
                if ( bWrap ) {
                    numWrapSkipped++;
                    continue;
                }
                if ( spread <= 0.0001f ) {
                    continue;
                }

                // Prefer members that are not pinned to a texture border.
                auto onBorder = []( const float coord ) {
                    return coord <= 0.000001f || coord >= 0.999999f;
                };
                double sumU = 0.0, sumV = 0.0;
                int32_t numInterior = 0;
                for ( uint32_t member : members ) {
                    if ( !onBorder( pUV[member * 2 + 0] ) &&
                         !onBorder( pUV[member * 2 + 1] ) ) {
                        sumU += pUV[member * 2 + 0];
                        sumV += pUV[member * 2 + 1];
                        numInterior++;
                    }
                }
                if ( numInterior == 0 ) {
                    for ( uint32_t member : members ) {
                        sumU += pUV[member * 2 + 0];
                        sumV += pUV[member * 2 + 1];
                    }
                    numInterior = (int32_t)members.size();
                }

                const float weldedU = (float)( sumU / numInterior );
                const float weldedV = (float)( sumV / numInterior );
                for ( uint32_t member : members ) {
                    pUV[member * 2 + 0] = weldedU;
                    pUV[member * 2 + 1] = weldedV;
                }
                numWelded++;
                maxDelta = Max( maxDelta, spread );
            }

            OTDU_LOG_DEBUG(
                "Dome UV weld: primitive %u, %u verts -> %d seam "
                "cluster(s) welded (%d wrap cluster(s) left alone), "
                "largest UV step removed %.5f\n",
                primIdx, numVertex, numWelded, numWrapSkipped, maxDelta );

            {
                static constexpr int32_t kNumBins = 48;
                double binU[kNumBins] = {}, binV[kNumBins] = {};
                int32_t binN[kNumBins] = {};
                float lowElev = 90.0f, highElev = kUpperDomeElevation;

                for ( uint32_t v = 0; v < numVertex; v++ ) {
                    const float px = pPos[v * 3 + 0];
                    const float py = pPos[v * 3 + 1];
                    const float pz = pPos[v * 3 + 2];
                    const float horiz = std::sqrt( px * px + pz * pz );
                    const float elev =
                        ( horiz > 0.0001f )
                            ? ( std::atan2( py, horiz ) * 57.29578f )
                            : 90.0f;
                    if ( elev < kUpperDomeElevation ) {
                        continue;
                    }
                    lowElev = Min( lowElev, elev );
                    highElev = Max( highElev, elev );
                }

                const float span = Max( 0.001f, highElev - lowElev );
                for ( uint32_t v = 0; v < numVertex; v++ ) {
                    const float px = pPos[v * 3 + 0];
                    const float py = pPos[v * 3 + 1];
                    const float pz = pPos[v * 3 + 2];
                    const float horiz = std::sqrt( px * px + pz * pz );
                    const float elev =
                        ( horiz > 0.0001f )
                            ? ( std::atan2( py, horiz ) * 57.29578f )
                            : 90.0f;
                    if ( elev < kUpperDomeElevation ) {
                        continue;
                    }
                    int32_t b = (int32_t)( ( elev - lowElev ) / span *
                                           ( kNumBins - 1 ) );
                    b = Max( 0, Min( kNumBins - 1, b ) );
                    binU[b] += pUV[v * 2 + 0];
                    binV[b] += pUV[v * 2 + 1];
                    binN[b]++;
                }

                // Fill empty bins from the nearest populated one so the
                // interpolation below never reads a hole.
                for ( int32_t b = 0; b < kNumBins; b++ ) {
                    if ( binN[b] != 0 ) {
                        continue;
                    }
                    for ( int32_t d = 1; d < kNumBins; d++ ) {
                        const int32_t lo = b - d, hi = b + d;
                        if ( lo >= 0 && binN[lo] != 0 ) {
                            binU[b] = binU[lo] / binN[lo];
                            binV[b] = binV[lo] / binN[lo];
                            binN[b] = 1;
                            break;
                        }
                        if ( hi < kNumBins && binN[hi] != 0 ) {
                            binU[b] = binU[hi] / binN[hi];
                            binV[b] = binV[hi] / binN[hi];
                            binN[b] = 1;
                            break;
                        }
                    }
                }
                for ( int32_t b = 0; b < kNumBins; b++ ) {
                    if ( binN[b] > 1 ) {
                        binU[b] /= binN[b];
                        binV[b] /= binN[b];
                        binN[b] = 1;
                    }
                }

                int32_t numFlattened = 0;
                for ( uint32_t v = 0; v < numVertex; v++ ) {
                    const float px = pPos[v * 3 + 0];
                    const float py = pPos[v * 3 + 1];
                    const float pz = pPos[v * 3 + 2];
                    const float horiz = std::sqrt( px * px + pz * pz );
                    const float elev =
                        ( horiz > 0.0001f )
                            ? ( std::atan2( py, horiz ) * 57.29578f )
                            : 90.0f;
                    if ( elev < kUpperDomeElevation ) {
                        continue;
                    }
                    const float t =
                        ( elev - lowElev ) / span * ( kNumBins - 1 );
                    const int32_t b0 =
                        Max( 0, Min( kNumBins - 1, (int32_t)t ) );
                    const int32_t b1 = Min( kNumBins - 1, b0 + 1 );
                    const float f = Max( 0.0f, Min( 1.0f, t - (float)b0 ) );
                    pUV[v * 2 + 0] =
                        (float)( binU[b0] * ( 1.0 - f ) + binU[b1] * f );
                    pUV[v * 2 + 1] =
                        (float)( binV[b0] * ( 1.0 - f ) + binV[b1] * f );
                    numFlattened++;
                }
                OTDU_LOG_DEBUG( "Dome upper-UV flattened: %d vertices above "
                                "%.1f deg (elev %.2f..%.2f)\n",
                                numFlattened, kUpperDomeElevation, lowElev,
                                highElev );
            }
        }

        if ( pPrimitiveSection->Size == 0 ) {
            break;
        }
        pPrimitiveSection =
            (RenderFile::Section *)( (uint8_t *)pPrimitiveSection +
                                     pPrimitiveSection->Size );
        if ( !IsUsablePointer( pPrimitiveSection ) ) {
            break;
        }
    }
}

static float ParseNameNumericSuffix( const uint64_t hashcode ) {
    // FUN_006bef10
    const TestDriveString8 name( hashcode );

    // Node "hashcodes" are 8 chars packed into a uint64_t (TestDriveString8);
    // parse the decimal suffix of the name as a float (e.g. "NUAGE12" -> 12.0).
    int32_t charIndex = 7;
    while ( charIndex >= 0 && name.AsChar[charIndex] == '\0' ) {
        charIndex--;
    }

    float value = 0.0f;
    float placeValue = 1.0f;
    while ( charIndex >= 0 && name.AsChar[charIndex] >= '0' &&
            name.AsChar[charIndex] <= '9' ) {
        value += (float)( name.AsChar[charIndex] - '0' ) * placeValue;
        placeValue *= 10.0f; // FLOAT_00f4166c
        charIndex--;
    }

    return value;
}

void MngBackground::Cloud::reset() {
    // FUN_006bf4d0
    pHierRoot = nullptr;
    pHierNear = nullptr;
    pHierFar = nullptr;
    Color = Eigen::Vector4f::Ones(); // FLOAT_00f4165c

    RootAngle = 0.0f;
    NearAngle = 0.0f;
    FarAngle = 0.0f;
    NearSubAngles.fill( 0.0f );
    FarSubAngles.fill( 0.0f );

    RootSpeed = 0.0f;
    NearSpeed = 0.0f;
    FarSpeed = 0.0f;
    NearSubSpeeds.fill( 0.0f );
    FarSubSpeeds.fill( 0.0f );
}

bool MngBackground::Cloud::instanciate( const char *pName,
                                        const Eigen::Vector4f &color ) {
    // FUN_006bfee0
    char acNodeName[76];
    int32_t nameLength = 0;
    for ( ; pName[nameLength] != '\0' && nameLength <= 0x3c; nameLength++ ) {
        acNodeName[nameLength] = pName[nameLength];
    }
    acNodeName[nameLength] = '\0';

    FileCollection3D &collection3D =
        gpMngResources->getSkyDomeResources().getCollection3D();

    pHierRoot = collection3D.instantiate( GetIdentifier64bit( acNodeName ) );
    if ( pHierRoot == nullptr ) {
        return false;
    }
    pHierRoot->updateAndPropagateTransformMatrices( false );

    acNodeName[nameLength] = '_';
    acNodeName[nameLength + 1] = 'N';
    acNodeName[nameLength + 2] = '\0';
    pHierNear = pHierRoot->findChild( GetIdentifier64bit( acNodeName ) );

    acNodeName[nameLength + 1] = 'F';
    pHierFar = pHierRoot->findChild( GetIdentifier64bit( acNodeName ) );

    Color = color;

    int32_t numNodes = pHierRoot->NumChildren + 1;
    HiearchyNode *pNode = pHierRoot;
    for ( ; numNodes != 0 && pNode != nullptr;
          numNodes--, pNode = pNode->pNext ) {
        Instance *pInstance = pNode->pInstance;
        if ( pInstance == nullptr ) {
            continue;
        }

        pInstance->getFlagsWrite() |= 0x40;

        Material *pMaterial = pInstance->getMaterial( 0 );
        if ( pMaterial != nullptr ) {
            pMaterial->DepthWrite = 2;
            pMaterial->DepthTest = 1;
            pMaterial->AlphaTest = (eComparisonFunction)1;
            pMaterial->SrcBlend = (eBlendMode)5;
            pMaterial->DstBlend = (eBlendMode)6;
            pMaterial->BlendOP = (eBlendOP)1;
            pMaterial->AlphaSource = 0;
            pMaterial->OT = 0x12;

            MaterialParameter *pParameter = pMaterial->getParameterByIndex( 0 );
            if ( pParameter != nullptr ) {
                pParameter->Flags.pCustomFlags[0] = 0;
                pParameter->Flags.pCustomFlags[1] = 0;
            }

            gpRenderContext->retrieveShadersForMaterial( pMaterial, 0 );
        }
    }

    RootAngle = 0.0f;
    NearAngle = 0.0f;
    FarAngle = 0.0f;
    NearSubAngles.fill( 0.0f );
    FarSubAngles.fill( 0.0f );

    if ( pHierRoot->pInstance != nullptr ) {
        RootSpeed =
            ParseNameNumericSuffix( pHierRoot->pInstance->getHashcode() );
        RootInitialPosition = pHierRoot->pInstance->getModelMatrix().row( 3 );
    }

    if ( pHierNear != nullptr ) {
        if ( pHierNear->pInstance != nullptr ) {
            NearSpeed =
                ParseNameNumericSuffix( pHierNear->pInstance->getHashcode() );
            NearInitialPosition =
                pHierNear->pInstance->getModelMatrix().row( 3 );
        }

        const uint32_t numSubNodes =
            Min( (uint32_t)pHierNear->NumChildren, kMaxNumSubNodes );
        HiearchyNode *pChild = pHierNear->pNext;
        for ( uint32_t subNodeIdx = 0; subNodeIdx < numSubNodes;
              subNodeIdx++, pChild = pChild->pNext ) {
            NearSubSpeeds[subNodeIdx] =
                ParseNameNumericSuffix( pChild->pInstance->getHashcode() );
            NearSubInitialPositions[subNodeIdx] =
                pChild->pInstance->getModelMatrix().row( 3 );
        }
    }

    if ( pHierFar != nullptr ) {
        if ( pHierFar->pInstance != nullptr ) {
            FarSpeed =
                ParseNameNumericSuffix( pHierFar->pInstance->getHashcode() );
            FarInitialPosition = pHierFar->pInstance->getModelMatrix().row( 3 );
        }

        const uint32_t numSubNodes =
            Min( (uint32_t)pHierFar->NumChildren, kMaxNumSubNodes );
        HiearchyNode *pChild = pHierFar->pNext;
        for ( uint32_t subNodeIdx = 0; subNodeIdx < numSubNodes;
              subNodeIdx++, pChild = pChild->pNext ) {
            FarSubSpeeds[subNodeIdx] =
                ParseNameNumericSuffix( pChild->pInstance->getHashcode() );
            FarSubInitialPositions[subNodeIdx] =
                pChild->pInstance->getModelMatrix().row( 3 );
        }
    }

    RootPosition = RootInitialPosition;
    NearPosition = NearInitialPosition;
    FarPosition = FarInitialPosition;
    NearSubPositions = NearSubInitialPositions;
    FarSubPositions = FarSubInitialPositions;
    return true;
}

void MngBackground::Cloud::scroll( float angleDelta ) {
    // FUN_006bf1b0
    if ( pHierRoot == nullptr ) {
        return;
    }

    auto advance = [angleDelta]( float &angle, const float speed ) {
        angle += speed * angleDelta;
        if ( angle >= (float)EIGEN_TWO_PI ) {
            angle -= (float)EIGEN_TWO_PI;
        }
    };

    if ( pHierRoot->pInstance != nullptr ) {
        advance( RootAngle, RootSpeed );
    }

    if ( pHierNear != nullptr ) {
        if ( pHierNear->pInstance != nullptr ) {
            advance( NearAngle, NearSpeed );
        }
        const uint32_t numSubNodes =
            Min( (uint32_t)pHierNear->NumChildren, kMaxNumSubNodes );
        HiearchyNode *pChild = pHierNear->pNext;
        for ( uint32_t subNodeIdx = 0; subNodeIdx < numSubNodes;
              subNodeIdx++, pChild = pChild->pNext ) {
            if ( pChild->pInstance != nullptr ) {
                advance( NearSubAngles[subNodeIdx], NearSubSpeeds[subNodeIdx] );
            }
        }
    }

    if ( pHierFar != nullptr ) {
        if ( pHierFar->pInstance != nullptr ) {
            advance( FarAngle, FarSpeed );
        }
        const uint32_t numSubNodes =
            Min( (uint32_t)pHierFar->NumChildren, kMaxNumSubNodes );
        HiearchyNode *pChild = pHierFar->pNext;
        for ( uint32_t subNodeIdx = 0; subNodeIdx < numSubNodes;
              subNodeIdx++, pChild = pChild->pNext ) {
            if ( pChild->pInstance != nullptr ) {
                advance( FarSubAngles[subNodeIdx], FarSubSpeeds[subNodeIdx] );
            }
        }
    }
}

void MngBackground::Cloud::apply() {
    // FUN_006bf000
    if ( pHierRoot == nullptr ) {
        return;
    }

    auto setTransform = []( Instance *pInstance, const float angle,
                            const Eigen::Vector4f &position ) {
        if ( pInstance == nullptr ) {
            return;
        }
        Matrix4x4f_t transform = MakeRotationMatY( angle );
        transform.row( 3 ) = position;
        pInstance->setModelMatrix( transform );
    };

    setTransform( pHierRoot->pInstance, RootAngle, RootPosition );

    if ( pHierNear != nullptr ) {
        setTransform( pHierNear->pInstance, NearAngle, NearPosition );

        const uint32_t numSubNodes =
            Min( (uint32_t)pHierNear->NumChildren, kMaxNumSubNodes );
        HiearchyNode *pChild = pHierNear->pNext;
        for ( uint32_t subNodeIdx = 0; subNodeIdx < numSubNodes;
              subNodeIdx++, pChild = pChild->pNext ) {
            setTransform( pChild->pInstance, NearSubAngles[subNodeIdx],
                          NearSubPositions[subNodeIdx] );
        }
    }

    if ( pHierFar != nullptr ) {
        setTransform( pHierFar->pInstance, FarAngle, FarPosition );

        const uint32_t numSubNodes =
            Min( (uint32_t)pHierFar->NumChildren, kMaxNumSubNodes );
        HiearchyNode *pChild = pHierFar->pNext;
        for ( uint32_t subNodeIdx = 0; subNodeIdx < numSubNodes;
              subNodeIdx++, pChild = pChild->pNext ) {
            setTransform( pChild->pInstance, FarSubAngles[subNodeIdx],
                          FarSubPositions[subNodeIdx] );
        }
    }
}

void MngBackground::Cloud::rebase( const Eigen::Vector4f &streamCenter ) {
    // FUN_006bf770
    if ( pHierRoot == nullptr ) {
        return;
    }

    if ( pHierRoot->pInstance != nullptr ) {
        RootPosition = RootInitialPosition - streamCenter;
    }

    if ( pHierNear != nullptr ) {
        if ( pHierNear->pInstance != nullptr ) {
            NearPosition = NearInitialPosition - streamCenter;
        }
        const uint32_t numSubNodes =
            Min( (uint32_t)pHierNear->NumChildren, kMaxNumSubNodes );
        HiearchyNode *pChild = pHierNear->pNext;
        for ( uint32_t subNodeIdx = 0; subNodeIdx < numSubNodes;
              subNodeIdx++, pChild = pChild->pNext ) {
            if ( pChild->pInstance != nullptr ) {
                NearSubPositions[subNodeIdx] =
                    NearSubInitialPositions[subNodeIdx] - streamCenter;
            }
        }
    }

    if ( pHierFar != nullptr ) {
        if ( pHierFar->pInstance != nullptr ) {
            FarPosition = FarInitialPosition - streamCenter;
        }
        const uint32_t numSubNodes =
            Min( (uint32_t)pHierFar->NumChildren, kMaxNumSubNodes );
        HiearchyNode *pChild = pHierFar->pNext;
        for ( uint32_t subNodeIdx = 0; subNodeIdx < numSubNodes;
              subNodeIdx++, pChild = pChild->pNext ) {
            if ( pChild->pInstance != nullptr ) {
                FarSubPositions[subNodeIdx] =
                    FarSubInitialPositions[subNodeIdx] - streamCenter;
            }
        }
    }
}

void MngBackground::CloudGroup::reset() {
    // FUN_004321c0
    for ( Cloud &cloud : Clouds ) {
        cloud.reset();
    }
    NumClouds = 0;
    pWeatherConfig = nullptr;
}

void MngBackground::CloudGroup::instanciate( const WeatherConfig *pConfig ) {
    // FUN_006c02e0
    const std::vector<std::string> &cloudNames = pConfig->getCloudNames();
    const std::vector<Eigen::Vector4f> &cloudColors = pConfig->getCloudColors();

    for ( size_t cloudIdx = 0;
          cloudIdx < cloudNames.size() && NumClouds < kMaxNumClouds;
          cloudIdx++ ) {
        const Eigen::Vector4f color = ( cloudIdx < cloudColors.size() )
                                          ? cloudColors[cloudIdx]
                                          : Eigen::Vector4f::Ones();
        if ( Clouds[NumClouds].instanciate( cloudNames[cloudIdx].c_str(),
                                            color ) ) {
            NumClouds++;
        }
    }

    pWeatherConfig = pConfig;
}

void MngBackground::CloudGroup::unInstanciate() {
    // FUN_006bee00
    FileCollection3D &collection3D =
        gpMngResources->getSkyDomeResources().getCollection3D();

    for ( uint32_t cloudIdx = 0; cloudIdx < NumClouds; cloudIdx++ ) {
        Cloud &cloud = Clouds[cloudIdx];
        if ( cloud.pHierRoot != nullptr ) {
            collection3D.uninstantiate( cloud.pHierRoot );
        }
        cloud.pHierRoot = nullptr;
        cloud.pHierNear = nullptr;
        cloud.pHierFar = nullptr;
        cloud.bDrawNear = true;
        cloud.bDrawFar = true;
    }

    NumClouds = 0;
}

void MngBackground::CloudGroup::setFade( float fade ) {
    // FUN_006bf9c0
    for ( uint32_t cloudIdx = 0; cloudIdx < NumClouds; cloudIdx++ ) {
        Cloud &cloud = Clouds[cloudIdx];
        cloud.Color.w() = fade;

        if ( cloud.pHierRoot == nullptr ) {
            continue;
        }

        const ColorRGBA packedColor = PackFloatColor( cloud.Color );

        int32_t numNodes = cloud.pHierRoot->NumChildren + 1;
        HiearchyNode *pNode = cloud.pHierRoot;
        for ( ; numNodes != 0 && pNode != nullptr;
              numNodes--, pNode = pNode->pNext ) {
            if ( pNode->pInstance != nullptr ) {
                pNode->pInstance->setColor( packedColor );
            }
        }
    }
}

void MngBackground::CloudGroup::rebase( const Eigen::Vector4f &streamCenter ) {
    // FUN_006bfa50
    for ( uint32_t cloudIdx = 0; cloudIdx < NumClouds; cloudIdx++ ) {
        Clouds[cloudIdx].rebase( streamCenter );
    }
}

MngBackground::MngBackground()
    : Manager(),
      WorldListener(),
      StreamingCallback(),
      cloudsSpeedFactor( 0.005f ), // DAT_00f41bb0
      bInitialized( false ),
      nearPlane( 2.0f ),  // DAT_00f416b0
      farPlane( 1.0e9f ), // DAT_00f419ac
      pDomeMaterial( nullptr ),
      pHiearchyNodeDome( nullptr ),
      bEnabled( true ),
      cloudsGlobalFade( 1.0f ), // FLOAT_00f4165c
      bDomeOverride( false ),
      domeOverrideHashcode( 0 ) {
    // FUN_006bfb60
    for ( CloudGroup &group : CloudGroups ) {
        group.reset();
    }
    CloudGroupRandom.reset();
    pCloudGroupSorted[0] = &CloudGroups[0];
    pCloudGroupSorted[1] = &CloudGroups[1];

    RenderPass &backPass = gpRender->getRenderPass<eRenderPass::RP_Back>();
    backPass.pScene->getFrustumWrite().setPlanes( nearPlane, farPlane );

    backPass.pViewport->ClearColor.R = 0xff;
    backPass.pViewport->ClearColor.G = 0x00;
    backPass.pViewport->ClearColor.B = 0x00;
    backPass.pViewport->ClearColor.A = 0xff;

    gpMngBackground = this;
}

MngBackground::~MngBackground() {
    // FUN_006bfb00
    gpMngBackground = nullptr;
}

bool MngBackground::initialize( TestDriveGameInstance * ) {
    // FUN_006c0af0
    SkyDomeResources &skyDomeResources = gpMngResources->getSkyDomeResources();
    if ( skyDomeResources.isLoaded() ) {
        pHiearchyNodeDome = skyDomeResources.getCollection3D().instantiate(
            GetIdentifier64bit( "DOME" ) );
        if ( pHiearchyNodeDome != nullptr ) {
            Instance *pDomeInstance = pHiearchyNodeDome->pInstance;
            pDomeInstance->getFlagsWrite() &= 0xfffffffc;

            pDomeMaterial = pDomeInstance->getMaterial( 0 );
            if ( pDomeMaterial != nullptr ) {
                pDomeMaterial->DepthWrite = 2;
                gDomeOriginalFillMode = pDomeMaterial->FillMode;
            }

            if ( gbBackgroundDomeSkyFixes ) {
                PatchDomeUVSeams( pDomeInstance );
            }
        }
    }

    // FUN_006c0290
    if ( CloudGroupRandom.NumClouds < kMaxNumClouds &&
         CloudGroupRandom.Clouds[CloudGroupRandom.NumClouds].instanciate(
             "RANDOM", Eigen::Vector4f::Ones() ) ) {
        CloudGroupRandom.NumClouds++;
    }

    gpWeather->registerListener( this );

    // The original re-registers its streaming callback to make sure it only
    // occupies a single slot.
    gpWorldStreaming->unregisterCallback( this );
    gpWorldStreaming->registerCallback( this );
    return true;
}

void MngBackground::terminate() {
    // FUN_006bf660
    gpWorldStreaming->unregisterCallback( this );
    gpWeather->unregisterListener( this );

    if ( pHiearchyNodeDome != nullptr ) {
        gpMngResources->getSkyDomeResources().getCollection3D().uninstantiate(
            pHiearchyNodeDome );
        pHiearchyNodeDome = nullptr;
    }

    CloudGroupRandom.unInstanciate();
    for ( CloudGroup &group : CloudGroups ) {
        group.unInstanciate();
    }
}

void MngBackground::reset() {
    // FUN_006c09b0
    updateBitmap( gpWeather->getCurrentDayPeriod() );
}

void MngBackground::onTimeOfDayChange( int32_t param_1 ) {
    // FUN_006c0be0
    updateBitmap( param_1 );
}

void MngBackground::updateBitmap( int32_t dayPeriod ) {
    WeatherDesc *pWeatherDesc = gpWeather->getCurrentWeatherDesc();
    if ( pWeatherDesc == nullptr ) {
        return;
    }

    const int32_t numPeriods = pWeatherDesc->getNumDayPeriods();
    const bool bHDR = gpRender->isHDREnabled();

    auto getConfig = [&]( int32_t period ) -> WeatherConfig * {
        if ( period < 0 || period >= numPeriods ) {
            return nullptr;
        }
        const int32_t configIndex =
            bHDR ? pWeatherDesc->getHDRConfigIndex( period )
                 : pWeatherDesc->getSDRConfigIndex( period );
        if ( configIndex < 0 ||
             configIndex >= gpWeather->getNumRegisteredWeatherConfig() ) {
            return nullptr;
        }
        return gpWeather->getWeatherConfigByIndex( configIndex );
    };

    WeatherConfig *pCurrentConfig = nullptr;
    WeatherConfig *pNextConfig = nullptr;
    if ( numPeriods == 1 ) {
        pCurrentConfig = getConfig( 0 );
        pNextConfig = pCurrentConfig;
    } else {
        pCurrentConfig = getConfig( dayPeriod );
        const int32_t nextPeriod =
            ( dayPeriod == numPeriods - 1 ) ? 0 : dayPeriod + 1;
        pNextConfig = getConfig( nextPeriod );
    }

    if ( pCurrentConfig == nullptr || pNextConfig == nullptr ) {
        return;
    }

    updateMaterialTextures(
        GetIdentifier64bit( pCurrentConfig->getDome2DBFilename().c_str() ),
        GetIdentifier64bit( pNextConfig->getDome2DBFilename().c_str() ) );
    updateClouds( pCurrentConfig, pNextConfig );
}

void MngBackground::updateMaterialTextures( uint64_t currentDomeHashcode,
                                            uint64_t nextDomeHashcode ) {
    // FUN_006bee70
    if ( pDomeMaterial == nullptr ) {
        return;
    }

    if ( bDomeOverride ) {
        currentDomeHashcode = domeOverrideHashcode;
        nextDomeHashcode = domeOverrideHashcode;
    }

    MaterialParameter *pParameter = pDomeMaterial->getParameterByIndex( 0 );
    MaterialLayer *pLayer = pParameter->getLayer( 0 );
    pLayer->pLayerTextures[0].Hashcode = currentDomeHashcode;
    pLayer->pLayerTextures[1].Hashcode = nextDomeHashcode;
    for ( int32_t textureIdx = 0; textureIdx < 2; textureIdx++ ) {
        MaterialLayerTexture &texture = pLayer->pLayerTextures[textureIdx];
        texture.MinMagFilter = (eTextureFiltering)2;
        texture.MipFilter = (eTextureFiltering)2;
        texture.SamplerAddress[0] = (eMaterialTileMode)2;
        texture.SamplerAddress[1] = (eMaterialTileMode)2;
    }

    SkyDomeResources &skyDomeResources = gpMngResources->getSkyDomeResources();
    skyDomeResources.getCollection2D().bindBitmapReferences(
        &skyDomeResources.getSkyDome2DM() );
}

void MngBackground::setDomeOverride( bool bEnabled, uint64_t domeHashcode ) {
    bDomeOverride = bEnabled;
    domeOverrideHashcode = domeHashcode;

    // Re-apply the dome textures immediately so the change is visible without
    // waiting for the next time-of-day period transition.
    updateBitmap( gpWeather->getCurrentDayPeriod() );
}

void MngBackground::updateClouds( const WeatherConfig *pCurrentConfig,
                                  const WeatherConfig *pNextConfig ) {
    // FUN_006c0340
    std::swap( pCloudGroupSorted[0], pCloudGroupSorted[1] );

    const WeatherConfig *pConfigs[2] = { pCurrentConfig, pNextConfig };
    for ( int32_t groupIdx = 0; groupIdx < 2; groupIdx++ ) {
        CloudGroup *pGroup = pCloudGroupSorted[groupIdx];

        if ( pGroup->NumClouds != 0 &&
             pGroup->pWeatherConfig != pConfigs[groupIdx] ) {
            pGroup->unInstanciate();
        }
        if ( pGroup->NumClouds != 0 ) {
            continue;
        }

        pGroup->instanciate( pConfigs[groupIdx] );

        const Eigen::Vector4f streamCenter =
            gpWorldStreaming->isEnabled() ? gpWorldStreaming->getPosition()
                                          : Eigen::Vector4f::Zero();
        pGroup->rebase( streamCenter );

        // Advance every node by a random phase so clouds don't start in sync.
        const float randomAngle = (float)( rand() & 0x7fff ) *
                                  ( 1.0f / 32767.0f ) *
                                  (float)EIGEN_TWO_PI; // DAT_00f41714
        for ( uint32_t cloudIdx = 0; cloudIdx < pGroup->NumClouds;
              cloudIdx++ ) {
            pGroup->Clouds[cloudIdx].scroll( randomAngle );
        }
    }
}

void MngBackground::tick( float deltaTime, float ) {
    // FUN_006c0440
    if ( !bEnabled ) {
        return;
    }

    gpWeather->updateActiveConfig();

    SkyDomeResources &skyDomeResources = gpMngResources->getSkyDomeResources();

    // Dome materials take the interpolated emissive color of the working
    // weather config.
    Render2DM &dome2DM = skyDomeResources.getSkyDome2DM();
    for ( uint32_t materialIdx = 0; materialIdx < dome2DM.getNumMaterials();
          materialIdx++ ) {
        Material *pMaterial = dome2DM.getMaterialAtIndex( materialIdx );
        if ( pMaterial != nullptr ) {
            pMaterial->EmissiveColor =
                gpWeather->getActiveWeatherConfig().getDomeEmissive();
        }
    }

    // Cloud materials emissive (alpha forced to 1.0).
    Render2DM &clouds2DM = skyDomeResources.getCloud2DM();
    for ( uint32_t materialIdx = 0; materialIdx < clouds2DM.getNumMaterials();
          materialIdx++ ) {
        Material *pMaterial = clouds2DM.getMaterialAtIndex( materialIdx );
        if ( pMaterial != nullptr ) {
            gpWeather->updateActiveConfig();
            const float emissive =
                gpWeather->getActiveWeatherConfig().getCloudsEmissive();
            pMaterial->EmissiveColor =
                Eigen::Vector4f( emissive, emissive, emissive, 1.0f );
        }
    }

    // Scale the dome and recenter it on the camera (the original reads the
    // active camera shifted position; activeCamera is copied from
    // defaultCamera each frame).
    if ( pHiearchyNodeDome != nullptr ) {
        Eigen::Vector3f domeScale( 30.0f, 10.0f,
                                   30.0f ); // DAT_00fabf50 / DAT_00fabf4c
        const Eigen::Vector3f cameraPosition =
            gpCamera->getDefaultCamera()->getWorldPositionOffset();

        pHiearchyNodeDome->TransformMatrix = MakeScaleMatrix( domeScale );
        pHiearchyNodeDome->TransformMatrix.row( 3 ) += Eigen::Vector4f(
            cameraPosition.x(), cameraPosition.y(), cameraPosition.z(), 0.0f );
        pHiearchyNodeDome->updateAndPropagateTransformMatrices( false );
    }

    // Blend factor between the current and next period dome 2DBs (last
    // shader parameter of the dome material).
    const float interpolationCoeff =
        gpWeather->getDayPeriodInterpolationCoeff();
    if ( pDomeMaterial != nullptr ) {
        MaterialShaderParameterArray *pParameters =
            (MaterialShaderParameterArray *)pDomeMaterial->getParameterByIndex(
                0 );
        if ( pParameters->NumParameters != 0 ) {
            float *pBlendFactor = (float *)pParameters->getParameterDataByIndex(
                pParameters->NumParameters - 1 );
            *pBlendFactor = 1.0f - interpolationCoeff;
        }
    }

    // Crossfade the two sorted groups and scroll every cloud.
    const float angleDelta = cloudsSpeedFactor * deltaTime;
    for ( int32_t groupIdx = 0; groupIdx < 2; groupIdx++ ) {
        const float fade =
            ( groupIdx == 0 ) ? 1.0f - interpolationCoeff : interpolationCoeff;
        CloudGroup *pGroup = pCloudGroupSorted[groupIdx];
        if ( pGroup == nullptr ) {
            continue;
        }

        pGroup->setFade( cloudsGlobalFade * fade );
        for ( uint32_t cloudIdx = 0; cloudIdx < pGroup->NumClouds;
              cloudIdx++ ) {
            pGroup->Clouds[cloudIdx].scroll( angleDelta );
        }
        for ( uint32_t cloudIdx = 0; cloudIdx < pGroup->NumClouds;
              cloudIdx++ ) {
            pGroup->Clouds[cloudIdx].apply();
        }
    }

    for ( uint32_t cloudIdx = 0; cloudIdx < CloudGroupRandom.NumClouds;
          cloudIdx++ ) {
        CloudGroupRandom.Clouds[cloudIdx].scroll( angleDelta );
    }
    for ( uint32_t cloudIdx = 0; cloudIdx < CloudGroupRandom.NumClouds;
          cloudIdx++ ) {
        CloudGroupRandom.Clouds[cloudIdx].apply();
    }
}

void MngBackground::draw() {
    // FUN_006bfe40
    if ( !bEnabled ) {
        return;
    }

    RenderScene *pBackScene =
        gpRender->getRenderPass<eRenderPass::RP_Back>().pScene;
    RenderScene *pNearScene =
        gpRender->getRenderPass<eRenderPass::RP_NearNoFog>().pScene;

    if ( gbBackgroundCloudsEnabled ) {
        for ( CloudGroup &group : CloudGroups ) {
            drawCloudLayer( pBackScene, pNearScene, group );
        }
        drawCloudLayer( pBackScene, pNearScene, CloudGroupRandom );
    }

    if ( pDomeMaterial != nullptr ) {
        pDomeMaterial->FillMode =
            gbBackgroundDomeWireframe ? FM_Wireframe : gDomeOriginalFillMode;
    }

    if ( pHiearchyNodeDome != nullptr && gbBackgroundDomeEnabled ) {
        pBackScene->enqueueDynamicHiearchy( pHiearchyNodeDome );
    }
}

void MngBackground::drawCloudLayer( RenderScene *pFarScene,
                                    RenderScene *pNearScene,
                                    CloudGroup &group ) {
    // FUN_006bfd70
    for ( uint32_t cloudIdx = 0; cloudIdx < group.NumClouds; cloudIdx++ ) {
        Cloud &cloud = group.Clouds[cloudIdx];
        if ( cloud.pHierNear != nullptr && pNearScene != nullptr &&
             cloud.bDrawNear ) {
            pNearScene->enqueueDynamicHiearchy( cloud.pHierNear );
        }
        if ( cloud.pHierFar != nullptr && pFarScene != nullptr &&
             cloud.bDrawFar ) {
            pFarScene->enqueueDynamicHiearchy( cloud.pHierFar );
        }
    }
}

bool MngBackground::onWorldStreamed( Eigen::Vector4f *param_1,
                                     Eigen::Vector4f *param_2,
                                     Eigen::Vector4f *param_3 ) {
    // FUN_006bfa90
    for ( CloudGroup &group : CloudGroups ) {
        group.rebase( *param_3 );
    }
    CloudGroupRandom.rebase( *param_3 );
    return true;
}

void MngBackground::drawReflection( RenderScene *pFarScene ) {
    // FUN_006e35d0 (inlined)
    if ( pHiearchyNodeDome != nullptr ) {
        pFarScene->enqueueDynamicHiearchy( pHiearchyNodeDome );
    }

    for ( CloudGroup &group : CloudGroups ) {
        if ( group.NumClouds != 0 ) {
            drawCloudLayer( pFarScene, nullptr, group );
        }
    }
    if ( CloudGroupRandom.NumClouds != 0 ) {
        drawCloudLayer( pFarScene, nullptr, CloudGroupRandom );
    }
}
