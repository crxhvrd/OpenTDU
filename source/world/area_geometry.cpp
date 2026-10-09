#include "shared.h"
#include "area_geometry.h"

#include "render/instance.h"
#include "render/material.h"
#include "render/render_context.h"
#include "render/render_scene.h"
#include "gs_mng_resources.h"
#include "sector_manager.h"
#include "gs_world_streaming.h"

// Area bank entry group (ParsedBankEntry::Type): the terrain render set is
// group 4, same as sectors.
static constexpr int32_t kAreaTerrainGroup = 4;
static constexpr int32_t kAreaObjectsGroup = 5;

void AreaGeometry::rebaseRenderInstances(
    const Eigen::Vector4f &streamOrigin ) {
    for ( size_t i = 0; i < renderInstances.size(); i++ ) {
        Matrix4x4f_t m = renderInstances[i]->getModelMatrix();
        m( 3, 0 ) = renderInstanceAbsPos[i].x() - streamOrigin.x();
        m( 3, 2 ) = renderInstanceAbsPos[i].z() - streamOrigin.z();
        renderInstances[i]->setModelMatrix( m );
    }
}

void AreaGeometry::rebaseHeightmapInstance(
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

void AreaGeometry::instanciateObjectNodes(
    int32_t areaCoordX,
    int32_t areaCoordY,
    float offsetX,
    float offsetZ,
    const Eigen::Vector4f &streamOrigin ) {
    // FUN_00706890
    ObjectsRenderSet &set = objectsRenderSet;
    if ( !set.bValid ) {
        return;
    }

    static const char *const kNodeNames[ObjectsRenderSet::kNumObjectNodes] = {
        "NETWORK", "BLOCK" };

    for ( uint32_t n = 0; n < ObjectsRenderSet::kNumObjectNodes; n++ ) {
        if ( set.pObjectNodes[n] != nullptr ) {
            continue;
        }
        HiearchyNode *pNode =
            set.collection3D.instantiate( GetIdentifier64bit( kNodeNames[n] ) );
        if ( pNode == nullptr ) {
            continue;
        }

        for ( uint32_t j = 0; j < ObjectsRenderSet::kSubGridDim; j++ ) {
            for ( uint32_t i = 0; i < ObjectsRenderSet::kSubGridDim; i++ ) {
                char name[32];
                snprintf( name, sizeof( name ), "Sector-%d-%d-%d-%d",
                          areaCoordX, areaCoordY, (int32_t)i, (int32_t)j );
                HiearchyNode *pSub =
                    pNode->findChild( GetIdentifier64bit( name ) );
                set.subInstances[n][j * ObjectsRenderSet::kSubGridDim + i] =
                    ( pSub != nullptr ) ? pSub->pInstance : nullptr;
            }
        }

        const Eigen::Vector4f local = pNode->TransformMatrix.row( 3 );
        set.objectNodeAbsPos[n] = Eigen::Vector4f(
            local.x() + offsetX, local.y(), local.z() + offsetZ, local.w() );
        set.pObjectNodes[n] = pNode;
    }

    rebaseObjectNodes( streamOrigin );
}

void AreaGeometry::rebaseObjectNodes( const Eigen::Vector4f &streamOrigin ) {
    // FUN_00706890
    ObjectsRenderSet &set = objectsRenderSet;
    for ( uint32_t n = 0; n < ObjectsRenderSet::kNumObjectNodes; n++ ) {
        if ( set.pObjectNodes[n] == nullptr ) {
            continue;
        }
        Matrix4x4f_t m = set.pObjectNodes[n]->TransformMatrix;
        m( 3, 0 ) = set.objectNodeAbsPos[n].x() - streamOrigin.x();
        m( 3, 2 ) = set.objectNodeAbsPos[n].z() - streamOrigin.z();
        set.pObjectNodes[n]->TransformMatrix = m;
        set.pObjectNodes[n]->updateAndPropagateTransformMatrices( false );
    }
}

void AreaGeometry::submitObjectNodes( RenderScene *pScene,
                                      int32_t tilesPerSectorX,
                                      int32_t tilesPerSectorZ,
                                      const AreaGeometry *pEastArea,
                                      uint32_t &numSubmitted ) {
    // FUN_00707080
    if ( pScene == nullptr || tilesPerSectorX <= 0 || tilesPerSectorZ <= 0 ) {
        return;
    }

    auto isTileCovered = []( const AreaGeometry &geometry, int32_t col,
                             int32_t row ) -> bool {
        const int32_t tile = PMITileIndex( col, row );
        return HeightmapTileHidden(
            HeightmapResolveTileLOD( geometry.heightmapLODCache[tile],
                                     geometry.heightmapTileMask[tile] ) );
    };

    ObjectsRenderSet &set = objectsRenderSet;
    for ( uint32_t n = 0; n < ObjectsRenderSet::kNumObjectNodes; n++ ) {
        if ( set.pObjectNodes[n] == nullptr ) {
            continue;
        }
        for ( uint32_t j = 0; j < ObjectsRenderSet::kSubGridDim; j++ ) {
            for ( uint32_t i = 0; i < ObjectsRenderSet::kSubGridDim; i++ ) {
                if ( numSubmitted >= ObjectsRenderSet::kMaxSubInstances ) {
                    break;
                }
                Instance *pSub =
                    set.subInstances[n][j * ObjectsRenderSet::kSubGridDim + i];
                if ( pSub == nullptr ) {
                    continue;
                }

                const int32_t baseCol =
                    (int32_t)i * tilesPerSectorX + (int32_t)( j & 1 );
                const int32_t baseRow = (int32_t)j * tilesPerSectorZ;

                bool bAllCovered = true;
                for ( int32_t dz = 0; dz < tilesPerSectorZ && bAllCovered;
                      dz++ ) {
                    for ( int32_t dx = 0; dx < tilesPerSectorX && bAllCovered;
                          dx++ ) {
                        const int32_t col = baseCol + dx;
                        const int32_t row = baseRow + dz;
                        if ( row >= kHeightmapNumTile ) {
                            continue;
                        }
                        if ( col < kHeightmapNumTile ) {
                            if ( !isTileCovered( *this, col, row ) ) {
                                bAllCovered = false;
                            }
                        } else if ( pEastArea != nullptr ) {
                            if ( !isTileCovered( *pEastArea,
                                                 col - kHeightmapNumTile,
                                                 row ) ) {
                                bAllCovered = false;
                            }
                        }
                    }
                }
                if ( bAllCovered ) {
                    continue;
                }

                pScene->enqueueDynamicInstance( pSub );
                numSubmitted++;
            }
        }
    }
}

void AreaGeometry::reset() {
    if ( pRenderNode != nullptr ) {
        collection.uninstantiate( pRenderNode );
        pRenderNode = nullptr;
    }
    for ( Instance *pInstance : renderInstances ) {
        collection.uninstantiate( pInstance );
    }
    renderInstances.clear();
    renderInstanceAbsPos.clear();

    releaseHeightmapEnvInstances();
    if ( pHeightmapInstance != nullptr ) {
        collection.uninstantiate( pHeightmapInstance );
        pHeightmapInstance = nullptr;
    }
    heightmapTileMask.fill( 0 );
    heightmapWorkGrid.fill( 0 );

    objectsRenderSet.reset();
}

void AreaGeometry::ObjectsRenderSet::reset() {
    // FUN_007061a0 + FUN_00706210
    for ( HiearchyNode *&pNode : pObjectNodes ) {
        if ( pNode != nullptr ) {
            collection3D.uninstantiate( pNode );
            pNode = nullptr;
        }
    }
    for ( auto &nodeRow : subInstances ) {
        for ( Instance *&pSub : nodeRow ) {
            pSub = nullptr;
        }
    }
    collection = FileCollection2D();
    collection3D = FileCollection3D();
    num2DB = 0;
    numUVA = 0;
    bValid = false;
}

static bool BuildAreaObjectsRenderSet( Bank &bank,
                                       AreaGeometry::ObjectsRenderSet &set ) {
    // FUN_00706560 + FUN_007064f0 + FUN_00706ae0
    set.reset();
    bool bHas2DM = false;
    bool bHas3DD = false;
    bool bHas3DG = false;

    const int32_t numEntries = bank.getNumElements();
    for ( int32_t i = 0; i < numEntries; i++ ) {
        ParsedBankEntry entry;
        if ( bank.getEntry( i, entry ) != 0 ) {
            break;
        }
        if ( entry.Type != kAreaObjectsGroup ) {
            continue;
        }

        switch ( entry.SubType ) {
        case AT_3DD: // geometry (FUN_007064f0)
            bHas3DD = set.render3DD.initialize( entry.pData );
            break;
        case AT_3DG: // geometry group (FUN_007064f0)
            bHas3DG = set.render3DG.initialize( entry.pData );
            break;
        case AT_2DB: // bitmaps
            if ( set.num2DB < AreaGeometry::ObjectsRenderSet::kMaxNum2DB ) {
                auto &slot = set.set2DB[set.num2DB];
                slot.pResourceMemory = entry.pData;
                if ( slot.Resource.initialize( entry.pData ) ) {
                    set.num2DB++;
                }
            }
            break;
        case AT_UVA: // UV-animation tracks (waterfall / beach-foam scroll)
            if ( set.numUVA < AreaGeometry::ObjectsRenderSet::kMaxNumUVA ) {
                if ( set.setUVA[set.numUVA].initialize( entry.pData,
                                                        entry.Size ) ) {
                    set.numUVA++;
                }
            }
            break;
        case AT_2DM: // materials
            bHas2DM = set.render2DM.initialize( entry.pData );
            break;
        default:
            break;
        }
    }

    for ( uint32_t b = 0; b < set.num2DB; b++ ) {
        set.collection.register2DB( &set.set2DB[b].Resource );
    }
    for ( uint32_t u = 0; u < set.numUVA; u++ ) {
        set.collection.registerUVA( &set.setUVA[u] );
    }
    if ( bHas2DM ) {
        set.collection.register2DM( &set.render2DM );
    }

    if ( bHas3DG ) {
        set.collection3D.register3DG( &set.render3DG );
    }
    if ( bHas3DD ) {
        set.collection3D.register3DD( &set.render3DD );
        if ( bHas2DM ) {
            set.render3DD.bindMaterialsBank( &set.render2DM );
        }
    }

    set.bValid = bHas3DD && bHas2DM;
    return set.bValid;
}

void AreaBank::loadContent() { instanciate(); }

void AreaBank::instanciate() {
    // FUN_009a6d50
    if ( pOwner == nullptr ) {
        return;
    }

    InstanciateTerrainGeometry( *this, pOwner->geometry, pOwner->heightmap,
                                (float)pOwner->offsetX, (float)pOwner->offsetY,
                                eTerrainTier::TT_Area );

    if ( BuildAreaObjectsRenderSet( *this,
                                    pOwner->geometry.objectsRenderSet ) ) {
        const Eigen::Vector4f streamOrigin =
            gpWorldStreaming->isEnabled() ? gpWorldStreaming->getPosition()
                                          : Eigen::Vector4f::Zero();
        pOwner->geometry.instanciateObjectNodes(
            pOwner->coords.posX, pOwner->coords.posY, (float)pOwner->offsetX,
            (float)pOwner->offsetY, streamOrigin );
    }

    // Mark the area as loaded; submit (#7) and the reshuffle (#4/#5) gate on
    // state == 1.
    pOwner->state = 1;

    markAsInstanciated();
}

static uint8_t gTerrainAlphaRef = 0xb4; // BYTE_00f99c9c

void RefreshTerrainMaterialAlphaTest( Material *pTerrainMat ) {
    // FUN_00703390 (head)
    if ( pTerrainMat == nullptr ) {
        return;
    }

    pTerrainMat->AlphaRef = gTerrainAlphaRef;
    pTerrainMat->AlphaTest = ( pTerrainMat->AlphaRef == 0 )
                                 ? (eComparisonFunction)1
                                 : (eComparisonFunction)0;
    pTerrainMat->OT = pTerrainMat->getOTNumber();
}

void SetupTerrainMaterial(
    Material *pTerrainMat,
    SectorHeightmap &heightmap,
    eTerrainTier tier,
    const std::function<Render2DB *( uint64_t hashcode )> &resolveBitmap ) {
    // FUN_00703ab0.
    if ( pTerrainMat == nullptr ) {
        return;
    }

    pTerrainMat->AlphaRef = gTerrainAlphaRef;
    pTerrainMat->SrcBlend = eBlendMode::BM_One;
    pTerrainMat->DstBlend = eBlendMode::BM_Zero;
    pTerrainMat->BlendOP = eBlendOP::BO_Disabled;
    pTerrainMat->AlphaTest = ( tier == eTerrainTier::TT_Sector )
                                 ? (eComparisonFunction)1
                                 : (eComparisonFunction)0;
    pTerrainMat->OT = pTerrainMat->getOTNumber();

    MaterialParameter *pParam = pTerrainMat->getParameterByIndex( 0 );
    if ( pParam == nullptr ) {
        return;
    }

    auto resolveLayerBitmap = [&]( uint32_t layerIndex ) -> Render2DB * {
        MaterialLayer *pLayer = pParam->getLayer( layerIndex );
        if ( pLayer == nullptr || pLayer->NumTextures == 0 ) {
            return nullptr;
        }
        return resolveBitmap( pLayer->pLayerTextures[0].Hashcode );
    };

    if ( pParam->NumLayers < 5 ) {
        MaterialLayer *pLayer1 = pParam->getLayer( 1 );
        if ( pLayer1 != nullptr ) {
            pLayer1->NumTextures = 0;
            pLayer1->pLayerTextures[0].Hashcode = 0;
            pLayer1->pLayerTextures[0].pTextureSection = nullptr;
        }
        pParam->NumLayers = 1;

        // If the surviving layer 0 is the world map ("HAWAI-C"), raise the
        // custom permutation bit fillParameterFlags forwards for terrain.
        MaterialLayer *pLayer0 = pParam->getLayer( 0 );
        if ( pLayer0 != nullptr && pLayer0->pLayerTextures[0].Hashcode ==
                                       GetIdentifier64bit( "HAWAI-C" ) ) {
            pParam->Flags.pCustomFlags[0] = 1;
        }
    } else {
        // Sector terrain (>= 5 layers): the shadow / AO map rides layer 1.
        heightmap.pShadowBitmap = resolveLayerBitmap( 1 );

        MaterialLayer *pShadowLayer = pParam->getLayer( 1 );
        if ( pShadowLayer != nullptr ) {
            heightmap.pShadowTextureSection =
                pShadowLayer->pLayerTextures[0].pTextureSection;
        }
    }

    if ( pParam->Type == MPT_Terrain ) {
        heightmap.pColorBitmap = resolveLayerBitmap( 0 );
        heightmap.pTypeBitmap = resolveLayerBitmap( 3 );
    }

    gpRenderContext->retrieveShadersForMaterial( pTerrainMat, 0 );
}

void AreaGeometry::instanciateHeightmapEnvInstances(
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
        pInstance->setHeightmapLODCoef(
            pHeightmapInstance->getHeightmapLODCoef() );
        pInstance->getFlagsWrite() &= ~3u;

        heightmapEnvInstances[i] = pInstance;
    }

    if ( heightmapEnvInstances[1] != nullptr ) {
        pHeightmapEnvMaterial = InstallHeightmapEnvMaterial(
            heightmapEnvInstances[1], heightmapEnvMaterialBank );
    }
}

void AreaGeometry::releaseHeightmapEnvInstances() {
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

Instance *AreaGeometry::getHeightmapInstance( uint32_t index ) const {
    if ( index == 0 ) {
        return pHeightmapInstance;
    }
    if ( index > kNumHeightmapEnvInstances ) {
        return nullptr;
    }
    return heightmapEnvInstances[index - 1];
}

void AreaGeometry::submitHeightmapInstance( RenderScene *pScene,
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

Material *InstallHeightmapEnvMaterial( Instance *pInstance,
                                       std::vector<uint8_t> &outBankStorage ) {
    // FUN_00703240
    outBankStorage.clear();
    if ( pInstance == nullptr || gpMngResources == nullptr ) {
        return nullptr;
    }

    const uint32_t bankSize = pInstance->getMaterialBankSize();
    if ( bankSize <= sizeof( RenderFile::Section ) ) {
        return nullptr;
    }

    outBankStorage.resize( bankSize );
    RenderFile::Section *pCopyBank =
        pInstance->copyMaterialBank( outBankStorage.data() );
    if ( pCopyBank == nullptr ) {
        outBankStorage.clear();
        return nullptr;
    }

    Render3DDHashEntry *pEntries = (Render3DDHashEntry *)( pCopyBank + 1 );
    RenderFile::Section *pSourceSection = pEntries[0].pEntry;
    if ( pSourceSection == nullptr ) {
        outBankStorage.clear();
        return nullptr;
    }

    Material *pSource = (Material *)( pSourceSection + 1 );
    MaterialParameter *pSourceParam = pSource->getParameterByIndex( 0 );
    if ( pSourceParam == nullptr ||
         pSourceParam->Type != eMaterialParameterType::MPT_Terrain ) {
        outBankStorage.clear();
        return nullptr;
    }

    TerrainResources &terrainResources = gpMngResources->getTerrainResources();
    Material *pCopy = terrainResources.acquireMaterial();
    if ( pCopy == nullptr ) {
        outBankStorage.clear();
        return nullptr;
    }

    MaterialParameter *pCopyParam = pCopy->getParameterByIndex( 0 );
    if ( pCopyParam == nullptr ) {
        terrainResources.releaseMaterial( pCopy );
        outBankStorage.clear();
        return nullptr;
    }

    pCopyParam->NumLayers = pSourceParam->NumLayers;
    MaterialTerrainParameter *pSourceTerrain =
        (MaterialTerrainParameter *)pSourceParam;
    MaterialTerrainParameter *pCopyTerrain =
        (MaterialTerrainParameter *)pCopyParam;
    for ( uint32_t i = 0; i < pSourceParam->NumLayers; i++ ) {
        pCopyTerrain->AlphaBlendModeTest[i] =
            pSourceTerrain->AlphaBlendModeTest[i];

        MaterialLayer *pSourceLayer = pSourceParam->getLayer( i );
        MaterialLayer *pCopyLayer = pCopyParam->getLayer( i );
        if ( pSourceLayer != nullptr && pCopyLayer != nullptr ) {
            *pCopyLayer = *pSourceLayer;
        }
    }

    const Eigen::Vector4f specularColor = pCopy->SpecularColor;
    const Eigen::Vector4f emissiveColor = pCopy->EmissiveColor;
    *pCopy = *pSource;
    pCopy->SpecularColor = specularColor;
    pCopy->EmissiveColor = emissiveColor;

    pCopy->pOTNode = nullptr;

    pCopy->BlendOP = eBlendOP::BO_Add;
    pCopy->SrcBlend = eBlendMode::BM_SrcAlpha;
    pCopy->DstBlend = eBlendMode::BM_InvSrcAlpha;
    pCopy->AlphaRef = 1;

    pCopyParam->Flags = pSourceParam->Flags;
    pCopyParam->Flags.pCustomFlags[1] = 1;

    gpRenderContext->retrieveShadersForMaterial( pCopy, 0 );
    pCopy->OT = pCopy->getOTNumber();

    pEntries[0].pEntry = (RenderFile::Section *)pCopy - 1;
    pInstance->setMaterialBank( pCopyBank );

    return pCopy;
}

void InstanciateTerrainGeometry( Bank &bank,
                                 AreaGeometry &geometry,
                                 SectorHeightmap &heightmap,
                                 float offsetX,
                                 float offsetZ,
                                 eTerrainTier tier ) {
    // FUN_00703d10
    geometry.num2DB = 0;

    bool bHas3DD = false;
    bool bHas3DG = false;
    bool bHas2DM = false;

    const int32_t numEntries = bank.getNumElements();
    for ( int32_t i = 0; i < numEntries; i++ ) {
        ParsedBankEntry entry;
        if ( bank.getEntry( i, entry ) != 0 ) {
            break;
        }

        if ( entry.Type != kAreaTerrainGroup ) {
            continue;
        }

        switch ( entry.SubType ) {
        case AT_3DD: // terrain
            bHas3DD = geometry.render3DD.initialize( entry.pData );
            break;
        case AT_3DG:
            bHas3DG = geometry.render3DG.initialize( entry.pData );
            break;
        case AT_2DM: // materials
            bHas2DM = geometry.render2DM.initialize( entry.pData );
            break;
        case AT_2DB: // bitmaps
            if ( geometry.num2DB < AreaGeometry::kMaxNum2DB ) {
                auto &slot = geometry.set2DB[geometry.num2DB];
                slot.pResourceMemory = entry.pData;
                if ( slot.Resource.initialize( entry.pData ) ) {
                    geometry.num2DB++;
                }
            }
            break;
        default:
            break;
        }
    }

    if ( bHas3DG ) {
        geometry.collection.register3DG( &geometry.render3DG );
    }
    if ( bHas3DD ) {
        heightmap.buildRenderDescriptor();
        geometry.collection.register3DD( &geometry.render3DD );
        if ( bHas2DM ) {
            geometry.render3DD.bindMaterialsBank( &geometry.render2DM );
            for ( uint32_t b = 0; b < geometry.num2DB; b++ ) {
                geometry.render2DM.bindBitmapReference(
                    &geometry.set2DB[b].Resource );
            }
            if ( geometry.pGroundBitmapCollection != nullptr ) {
                geometry.pGroundBitmapCollection->bindBitmapReferences(
                    &geometry.render2DM );
            }

            SetupTerrainMaterial(
                geometry.render2DM.getMaterialAtIndex( 0 ), heightmap, tier,
                [&]( uint64_t hashcode ) -> Render2DB * {
                    for ( uint32_t b = 0; b < geometry.num2DB; b++ ) {
                        if ( geometry.set2DB[b].Resource.getBitmapHashcode() ==
                             hashcode ) {
                            return &geometry.set2DB[b].Resource;
                        }
                    }
                    return nullptr;
                } );
        }

        for ( uint32_t i = 0;; i++ ) {
            RenderFile::Section *pInstSection =
                geometry.render3DD.getInstanceEntry( i );
            if ( pInstSection == nullptr ) {
                break;
            }

            uint64_t hashcode =
                geometry.render3DD.findInstanceHashcode( pInstSection );
            Instance *pInstance = geometry.collection.instantiate(
                (InstanceEntry *)( pInstSection + 1 ), hashcode );
            if ( pInstance != nullptr ) {
                uint32_t objMagic = 0;
                if ( pInstance->getNumLODs() > 0 &&
                     pInstance->getLOD( 0 ).pHashcode != nullptr ) {
                    uint32_t *pObj =
                        (uint32_t *)pInstance->getLOD( 0 ).pHashcode->pEntry;
                    if ( pObj != nullptr ) {
                        objMagic = *pObj;
                    }
                }
                Matrix4x4f_t m;
                const bool bIsHeightmap = ( objMagic == kHeightmapMagic );
                if ( bIsHeightmap ) {
                    const float quadScale = heightmap.getQuadScale();
                    m = Matrix4x4f_t::Identity();
                    m.row( 0 ) *= quadScale;
                    m.row( 2 ) *= quadScale;
                    m( 3, 0 ) = heightmap.worldOriginX;
                    m( 3, 2 ) = heightmap.worldOriginZ;
                } else {
                    // Props keep their disk InstanceEntry placement, shifted to
                    // the tier's absolute world origin.
                    m = pInstance->getModelMatrix();
                    m( 3, 0 ) += offsetX;
                    m( 3, 2 ) += offsetZ;
                }
                pInstance->setModelMatrix( m );

                const Eigen::Vector4f absPos( m( 3, 0 ), m( 3, 1 ), m( 3, 2 ),
                                              1.0f );
                if ( bIsHeightmap ) {
                    // The matrix was rebuilt with the quad scale, so recompute
                    // biggestScale (the frustum-cull radius multiplier);
                    // otherwise the large level/area patch is culled as if it
                    // were unit-scaled.
                    pInstance->calculateBiggestScale();
                    geometry.pHeightmapInstance = pInstance;
                    geometry.heightmapAbsPos = absPos;
                    pInstance->setHeightmapTileMask(
                        geometry.heightmapTileMask.data() );
                    pInstance->setHeightmapLODCache(
                        geometry.heightmapLODCache.data() );
                    pInstance->setHeightmapWorkGrid(
                        geometry.heightmapWorkGrid.data() );
                    pInstance->setHeightmapLODCoef(
                        ( heightmap.getQuadScale() > 100.0f ) ? 1.0e-6f
                                                              : 6.0e-6f );
                    geometry.instanciateHeightmapEnvInstances(
                        (InstanceEntry *)( pInstSection + 1 ), hashcode, tier );
                } else {
                    geometry.renderInstances.push_back( pInstance );
                    geometry.renderInstanceAbsPos.push_back( absPos );
                }
            }
        }

        const Eigen::Vector4f streamOrigin =
            gpWorldStreaming->isEnabled() ? gpWorldStreaming->getPosition()
                                          : Eigen::Vector4f::Zero();
        geometry.rebaseRenderInstances( streamOrigin );
        geometry.rebaseHeightmapInstance( streamOrigin );
    }
}
