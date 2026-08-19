#include "shared.h"
#include "sector_geometry.h"

#include "core/maths_helper.h"
#include "game/scenaric/entity/base_entity.h"
#include "render/2dm.h"
#include "render/camera/camera_base.h"
#include "render/instance.h"
#include "render/material.h"
#include "render/mng_env_map.h"
#include "render/render_buckets.h"
#include "render/render_scene.h"
#include "world/gs_mng_resources.h"

static constexpr float kObjectNodeInstanceCullRatio = 3.0f; // DAT_00f416b4

SectorGeometry::SectorGeometry()
    : num2DB( 0 ),
      pRenderNode( nullptr ),
      pNetworkString( "NETWORK" ),
      pBlockString( "BLOCK" ),
      entityGrid() {
    // FUN_006fd960
}

void SectorGeometry::rebaseRenderInstances(
    const Eigen::Vector4f &streamOrigin ) {
    for ( size_t i = 0; i < renderInstances.size(); i++ ) {
        Matrix4x4f_t m = renderInstances[i]->getModelMatrix();
        m( 3, 0 ) = renderInstanceAbsPos[i].x() - streamOrigin.x();
        m( 3, 2 ) = renderInstanceAbsPos[i].z() - streamOrigin.z();
        renderInstances[i]->setModelMatrix( m );
    }
}

void SectorGeometry::rebaseHeightmapInstance(
    const Eigen::Vector4f &streamOrigin ) {
    if ( pHeightmapInstance == nullptr ) {
        return;
    }
    Matrix4x4f_t m = pHeightmapInstance->getModelMatrix();
    m( 3, 0 ) = heightmapAbsPos.x() - streamOrigin.x();
    m( 3, 2 ) = heightmapAbsPos.z() - streamOrigin.z();
    pHeightmapInstance->setModelMatrix( m );

    for ( Instance *pEnvInstance : heightmapEnvInstances ) {
        if ( pEnvInstance != nullptr ) {
            pEnvInstance->setModelMatrix( m );
        }
    }
}

void SectorGeometry::instanciateHeightmapEnvInstances(
    InstanceEntry *pInstanceEntry, uint64_t hashcode, eTerrainTier tier ) {
    // FUN_00703410
    if ( pHeightmapInstance == nullptr || pInstanceEntry == nullptr ) {
        return;
    }

    releaseHeightmapEnvInstances();

    const Matrix4x4f_t matrix = pHeightmapInstance->getModelMatrix();
    for ( uint32_t i = 0; i < kNumHeightmapEnvInstances; i++ ) {
        if ( i == 1 && tier == eTerrainTier::TT_World ) {
            continue;
        }

        Instance *pInstance =
            collection.instantiate( pInstanceEntry, hashcode );
        if ( pInstance == nullptr ) {
            continue;
        }

        Matrix4x4f_t instanceMatrix = matrix;
        pInstance->setModelMatrix( instanceMatrix );
        pInstance->calculateBiggestScale();
        pInstance->setHeightmapTileMask( nullptr );
        pInstance->setHeightmapLODCache( heightmapEnvLODCaches[i].data() );
        pInstance->getFlagsWrite() &= ~3u;

        heightmapEnvInstances[i] = pInstance;
    }

    if ( heightmapEnvInstances[1] != nullptr ) {
        pHeightmapEnvMaterial = InstallHeightmapEnvMaterial(
            heightmapEnvInstances[1], heightmapEnvMaterialBank );
    }
}

void SectorGeometry::releaseHeightmapEnvInstances() {
    for ( Instance *&pEnvInstance : heightmapEnvInstances ) {
        if ( pEnvInstance != nullptr ) {
            collection.uninstantiate( pEnvInstance );
            pEnvInstance = nullptr;
        }
    }

    if ( pHeightmapEnvMaterial != nullptr ) {
        if ( gpMngResources != nullptr ) {
            gpMngResources->getTerrainResources().releaseMaterial(
                pHeightmapEnvMaterial );
        }
        pHeightmapEnvMaterial = nullptr;
    }
    heightmapEnvMaterialBank.clear();
}

Instance *SectorGeometry::getHeightmapInstance( uint32_t index ) const {
    // Heightmap::pInstanceHeightmap[index] (Heightmap+0x240 + index * 4).
    if ( index == 0 ) {
        return pHeightmapInstance;
    }
    if ( index > kNumHeightmapEnvInstances ) {
        return nullptr;
    }
    return heightmapEnvInstances[index - 1];
}

void SectorGeometry::submitHeightmapInstance( RenderScene *pScene,
                                              uint32_t index ) {
    // FUN_0042f170
    if ( pScene == nullptr ) {
        return;
    }

    Instance *pInstance = getHeightmapInstance( index );
    if ( pInstance != nullptr ) {
        pScene->enqueueDynamicInstance( pInstance );
    }
}

void SectorGeometry::rebaseImpostors( const Eigen::Vector4f &streamOrigin ) {
    // FUN_006fed80/FUN_007000c0
    const float uvOriginX = impostorUVAbsOrigin.x() - streamOrigin.x();
    const float uvOriginZ = impostorUVAbsOrigin.z() - streamOrigin.z();
    for ( ImpostorGrid &grid : impostorGrids ) {
        for ( MassInstanceImpostor &mi : grid.instances ) {
            const float x = mi.AbsPos.x() - streamOrigin.x();
            const float z = mi.AbsPos.z() - streamOrigin.z();
            if ( mi.pInstance != nullptr ) {
                Matrix4x4f_t m = mi.BuildMatrix;
                m( 3, 0 ) = x;
                m( 3, 2 ) = z;
                mi.pInstance->setModelMatrix( m );
            }
            mi.SpherePos.x() = mi.SphereAbsPos.x() - streamOrigin.x();
            mi.SpherePos.y() = mi.SphereAbsPos.y() - streamOrigin.y();
            mi.SpherePos.z() = mi.SphereAbsPos.z() - streamOrigin.z();
        }

        for ( Material *pMaterial : grid.uvOriginMaterials ) {
            WriteImpostorUVOrigin( pMaterial, uvOriginX, uvOriginZ, false );
        }
    }
}

void SectorGeometry::WriteImpostorUVOrigin( Material *pMaterial,
                                            float originX,
                                            float originZ,
                                            bool bInit ) {
    if ( pMaterial == nullptr ) {
        return;
    }
    MaterialShaderParameterArray *pArray =
        static_cast<MaterialShaderParameterArray *>(
            pMaterial->getParameterByIndex( 0 ) );
    if ( pArray == nullptr ) {
        return;
    }
    float *pSlot = (float *)pArray->getParameterDataByIndex( 0 );
    if ( pSlot == nullptr ) {
        return;
    }
    pSlot[0] = originX;
    pSlot[2] = originZ;
    if ( bInit ) {
        pSlot[3] = 768.0f;
    }
}

void SectorGeometry::instanciateObjectNodes(
    float offsetX, float offsetZ, const Eigen::Vector4f &streamOrigin ) {
    // FUN_006fefe0
    const char *kNames[2] = { pNetworkString, pBlockString };
    for ( int32_t i = 0; i < 2; i++ ) {
        if ( pObjectNodes[i] != nullptr ) {
            continue;
        }

        HiearchyNode *pNode = impostorRenderSet.collection.instantiate(
            GetIdentifier64bit( kNames[i] ) );
        if ( pNode == nullptr ) {
            continue;
        }

        const Eigen::Vector4f local = pNode->TransformMatrix.row( 3 );
        objectNodeAbsPos[i] = Eigen::Vector4f( local.x() + offsetX, local.y(),
                                               local.z() + offsetZ, local.w() );
        bObjectNodeVisible[i] = true;
        pObjectNodes[i] = pNode;
    }

    rebaseObjectNodes( streamOrigin );
}

void SectorGeometry::rebaseObjectNodes( const Eigen::Vector4f &streamOrigin ) {
    // FUN_006fefe0 (inlined)
    for ( int32_t i = 0; i < 2; i++ ) {
        if ( pObjectNodes[i] == nullptr ) {
            continue;
        }
        Matrix4x4f_t m = pObjectNodes[i]->TransformMatrix;
        m( 3, 0 ) = objectNodeAbsPos[i].x() - streamOrigin.x();
        m( 3, 2 ) = objectNodeAbsPos[i].z() - streamOrigin.z();
        pObjectNodes[i]->TransformMatrix = m;
        pObjectNodes[i]->updateAndPropagateTransformMatrices( false );
    }
}

void SectorGeometry::submitObjectNodes( RenderScene *pScene ) {
    // FUN_006fe580 (inlined)
    if ( pScene == nullptr ) {
        return;
    }
    for ( int32_t i = 0; i < 2; i++ ) {
        if ( pObjectNodes[i] != nullptr && bObjectNodeVisible[i] ) {
            pScene->enqueueDynamicHiearchy( pObjectNodes[i] );
        }
    }
}

void SectorGeometry::submitObjectNodeInstances( RenderScene *pScene ) {
    // FUN_006fde80
    if ( pScene == nullptr ) {
        return;
    }

    Camera *pCamera = pScene->getCamera();
    if ( pCamera == nullptr ) {
        return;
    }

    const Eigen::Vector4f cameraPosition =
        pCamera->getCamToWorld().row( 3 ).transpose();

    for ( HiearchyNode *pObjectNode : pObjectNodes ) {
        for ( HiearchyNode *pNode = pObjectNode; pNode != nullptr;
              pNode = pNode->pNext ) {
            Instance *pInstance = pNode->pInstance;
            if ( pInstance == nullptr || pInstance->getNumLODs() == 0 ) {
                continue;
            }

            Render3DDHashEntry *pHashEntry = pInstance->getLOD( 0 ).pHashcode;
            if ( pHashEntry == nullptr || pHashEntry->pEntry == nullptr ) {
                continue;
            }

            // Skip the section header to reach the object payload.
            const RenderObject *pObject =
                (const RenderObject *)( (const uint8_t *)pHashEntry->pEntry +
                                        sizeof( RenderFile::Section ) );

            Eigen::Vector4f sphereCenter = Eigen::Vector4f::Zero();
            sphereCenter.head<3>() = pObject->BoundingSphere.head<3>();
            const Eigen::Vector4f worldCenter =
                Mat4x4fTransform( pInstance->getModelMatrix(), sphereCenter );

            const Eigen::Vector3f delta =
                worldCenter.head<3>() - cameraPosition.head<3>();
            const float radius = pObject->BoundingSphere.w();
            const float cullRatio =
                gbDebugEnvMapDenseReflection
                    ? kObjectNodeInstanceCullRatio * gDebugEnvMapObjectCullScale
                    : kObjectNodeInstanceCullRatio;
            if ( delta.squaredNorm() < cullRatio * radius * radius ) {
                pScene->enqueueDynamicInstance( pInstance );
            }
        }
    }

    for ( Instance *pImpostorInstance : envImpostorInstances ) {
        pScene->enqueueDynamicInstance( pImpostorInstance );
    }
}

void SectorGeometry::uninstanciateObjectNodes() {
    for ( int32_t i = 0; i < 2; i++ ) {
        if ( pObjectNodes[i] != nullptr ) {
            impostorRenderSet.collection.uninstantiate( pObjectNodes[i] );
            pObjectNodes[i] = nullptr;
        }
    }

    detail2Materials.clear();
}

static void WriteDetail2ProjectionOrigin( MaterialParameter *pParam,
                                          float originX,
                                          float originZ ) {
    MaterialShaderParameterArray *pArray =
        static_cast<MaterialShaderParameterArray *>( pParam );
    float *pSlot = (float *)pArray->getParameterDataByIndex( 0 );
    if ( pSlot != nullptr ) {
        pSlot[0] = originX;
        pSlot[1] = 0.0f;
        pSlot[2] = originZ;
        pSlot[3] = 0.0f;
    }
}

static void PatchImpostorGridShadowMaterials(
    ImpostorGrid &grid,
    const AssetPtr<RenderFile::Section> &pShadowSection,
    float originX,
    float originZ ) {
    // FUN_006ffe10
    if ( !grid.bBuilt ) {
        return;
    }

    for ( Material *pMaterial : grid.uvOriginMaterials ) {
        if ( pMaterial == nullptr ) {
            continue;
        }
        MaterialParameter *pParam = pMaterial->getParameterByIndex( 0 );
        if ( pParam == nullptr ) {
            continue;
        }
        MaterialLayer *pLayer = pParam->getLayer( 0 );
        if ( pLayer != nullptr && pLayer->NumTextures > 2 ) {
            MaterialLayerTexture &tex = pLayer->pLayerTextures[2];
            tex.pTextureSection = pShadowSection;
            tex.SamplerAddress[0] = MTM_Mirror;
            tex.SamplerAddress[1] = MTM_Mirror;
        }
        WriteDetail2ProjectionOrigin( pParam, originX, originZ );
    }
}

void SectorGeometry::rebaseDetail2Materials(
    const Eigen::Vector4f &streamOrigin ) {
    // FUN_007000c0
    const float originX = detail2AbsOrigin.x() - streamOrigin.x();
    const float originZ = detail2AbsOrigin.z() - streamOrigin.z();
    for ( Material *pMaterial : detail2Materials ) {
        if ( pMaterial == nullptr ) {
            continue;
        }
        MaterialParameter *pParam = pMaterial->getParameterByIndex( 0 );
        if ( pParam != nullptr ) {
            WriteDetail2ProjectionOrigin( pParam, originX, originZ );
        }
    }
}

void SectorGeometry::patchObjectsGroupMaterials(
    const Eigen::Vector4f &worldCorner, const Eigen::Vector4f &streamOrigin ) {
    // FUN_006fe120
    if ( !impostorRenderSet.bValid || pHeightmapInstance == nullptr ) {
        return;
    }

    // Sector grid corner in absolute (for rebasing) and render-relative frames.
    detail2AbsOrigin = worldCorner;
    detail2Materials.clear();
    const float gridOriginX = worldCorner.x() - streamOrigin.x();
    const float gridOriginZ = worldCorner.z() - streamOrigin.z();

    static constexpr uint64_t kShadowHash = 0xc8ad93c95455291bull;
    static constexpr uint64_t kDetail2Hash = 0xb0f1234b0f123473ull;
    static constexpr uint64_t kVegShaderHash = 0x73078865ca37336dull;
    static constexpr uint64_t kWaterShaderHash = 0x0123210123210123ull;

    AssetPtr<RenderFile::Section> pTerrainShadow = {};
    Material *pTerrainMaterial = render2DM.getMaterialAtIndex( 0 );
    if ( pTerrainMaterial != nullptr ) {
        MaterialParameter *pTerrainParam =
            pTerrainMaterial->getParameterByIndex( 0 );
        if ( pTerrainParam != nullptr && pTerrainParam->NumLayers >= 5 ) {
            MaterialLayer *pTerrainLayer1 = pTerrainParam->getLayer( 1 );
            if ( pTerrainLayer1 != nullptr ) {
                pTerrainShadow =
                    pTerrainLayer1->pLayerTextures[0].pTextureSection;
            }
        }
    }

    pStaticShadowSection = pTerrainShadow;

    // Stage id the SHADOW slot is keyed by.
    const uint64_t kShadowStageId = GetIdentifier64bit( "SHADOW" );

    Render2DM &objects2DM = impostorRenderSet.render2DM;
    const uint32_t numMaterials = objects2DM.getNumMaterials();
    for ( uint32_t i = 0; i < numMaterials; i++ ) {
        Material *pMaterial = objects2DM.getMaterialAtIndex( i );
        if ( pMaterial == nullptr ) {
            continue;
        }
        MaterialParameter *pParam = pMaterial->getParameterByIndex( 0 );
        if ( pParam == nullptr ) {
            continue;
        }

        const uint64_t guid = pParam->Hashcode;
        if ( guid == kShadowHash ) {
            MaterialLayer *pLayer = pParam->getLayer( 0 );
            if ( pLayer != nullptr ) {
                for ( uint32_t t = 0; t < pLayer->NumTextures; t++ ) {
                    MaterialLayerTexture &tex = pLayer->pLayerTextures[t];
                    if ( tex.TextureTypeHashcode == kShadowStageId ) {
                        tex.pTextureSection = pTerrainShadow;
                        tex.SamplerAddress[0] = MTM_Mirror;
                        tex.SamplerAddress[1] = MTM_Mirror;
                        break;
                    }
                }
            }
        } else if ( guid == kDetail2Hash ) {
            if ( (int32_t)detail2Materials.size() >= kMaxDetail2Materials ) {
                continue;
            }
            MaterialLayer *pLayer = pParam->getLayer( 0 );
            if ( pLayer != nullptr && pLayer->NumTextures > 2 ) {
                MaterialLayerTexture &tex = pLayer->pLayerTextures[2];
                tex.pTextureSection = pTerrainShadow;
                tex.SamplerAddress[0] = MTM_Mirror;
                tex.SamplerAddress[1] = MTM_Mirror;
            }
            WriteDetail2ProjectionOrigin( pParam, gridOriginX, gridOriginZ );
            detail2Materials.push_back( pMaterial );
        } else if ( ( guid == kVegShaderHash || guid == kWaterShaderHash ) &&
                    pMaterial->BlendOP != BO_Disabled ) {
            pMaterial->OT = RB_Water;
        }
    }

    // Mass instance grid (the .PMI dynamic group) keeps private material
    // copies, patch those too.
    PatchImpostorGridShadowMaterials( impostorGrids[1], pTerrainShadow,
                                      gridOriginX, gridOriginZ );
}

static void EnqueueRoadResource( SectorGeometry::RoadResourceList list[2],
                                 int32_t bufferToggle,
                                 const SectorGeometry::RoadResourceEntry &e ) {
    SectorGeometry::RoadResourceList &buffer = list[1 - bufferToggle];
    if ( buffer.count <
         static_cast<int32_t>( SectorGeometry::kMaxRoadResourceSlots ) ) {
        buffer.slots[buffer.count] = e;
        buffer.count++;
    }
}

void SectorGeometry::registerRoadType4( void *pResource,
                                        uint32_t size,
                                        void *pCtxA,
                                        void *pCtxB ) {
    // FUN_008415a0
    EnqueueRoadResource( roadRegistry.type4, roadRegistry.bufferToggle,
                         { pResource, size, pCtxA, pCtxB, true } );
}

void SectorGeometry::registerRoadType1b( void *pResource,
                                         uint32_t size,
                                         void *pCtxA,
                                         void *pCtxB ) {
    // FUN_00841600
    EnqueueRoadResource( roadRegistry.type1b, roadRegistry.bufferToggle,
                         { pResource, size, pCtxA, pCtxB, true } );
}

void SectorGeometry::registerRoadNetwork( void *pResource, uint32_t size ) {
    // FUN_00841550
    EnqueueRoadResource( roadRegistry.network, roadRegistry.bufferToggle,
                         { pResource, size, nullptr, nullptr, true } );
}

void SectorGeometry::registerRoadBlock( void *pBlock ) {
    // FUN_00841770
    EnqueueRoadResource( roadRegistry.block, roadRegistry.bufferToggle,
                         { pBlock, 0, nullptr, nullptr, true } );
}

int32_t SectorGeometry::gatherEntities( const Eigen::Vector4f &refPos,
                                        int32_t cellRadius,
                                        int32_t entityType,
                                        Entity **pOut,
                                        int32_t maxCount ) const {
    // FUN_006fd760
    if ( !entityGrid.bValid || entityGrid.cellSize == 0.0f ) {
        return 0;
    }

    const float inv = 1.0f / entityGrid.cellSize;
    const int32_t centerX =
        static_cast<int32_t>( ( refPos.x() - entityGrid.originX ) * inv );
    const int32_t centerZ =
        static_cast<int32_t>( ( refPos.z() - entityGrid.originZ ) * inv );

    int32_t count = 0;
    for ( int32_t gz = centerZ - cellRadius; gz <= centerZ + cellRadius;
          gz++ ) {
        for ( int32_t gx = centerX - cellRadius; gx <= centerX + cellRadius;
              gx++ ) {
            // Sweep window clamped to the grid (the populator bins into a
            // stride x stride grid; see SectorEntityGrid).
            if ( gx < 0 || gx >= entityGrid.stride || gz < 0 ||
                 gz >= entityGrid.stride ) {
                continue;
            }

            const int32_t cellIndex = gz * entityGrid.stride + gx;
            if ( cellIndex < 0 || cellIndex >= static_cast<int32_t>(
                                                   entityGrid.cells.size() ) ) {
                continue;
            }

            for ( Entity *pEntity : entityGrid.cells[cellIndex] ) {
                if ( pEntity == nullptr ) {
                    continue;
                }
                if ( entityType != -1 &&
                     static_cast<int32_t>( pEntity->getType() ) !=
                         entityType ) {
                    continue;
                }
                pOut[count++] = pEntity;
                if ( count == maxCount ) {
                    return count;
                }
            }
        }
    }
    return count;
}
