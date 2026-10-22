#include "shared.h"
#include "spot_stream.h"

#include "config/gs_config.h"
#include "core/maths_helper.h"
#include "filesystem/gs_file.h"
#include "game/gs_intro_pool.h"
#include "game/scenaric/gs_scenaric.h"
#include "game/scenaric/scenaric_bank.h"
#include "game/scenaric/spots.h"
#include "game/scenaric/entity/entity_cinematic.h"
#include "game/cinematic/mng_cinematics.h"
#include "player_data/gs_playerdata.h"
#include "player_data/player_data_interface.h"
#include "player_data/saved_garage.h"
#include "render/2dm.h"
#include "render/material.h"
#include "render/render_scene.h"
#include "sector_manager.h"
#include "tdu_instance.h"

// Each spot streams into its own 0x380000 (3.5 MiB) slice of the shared intro
// pool, handed out round-robin as the three Spots are constructed (matches the
// original CHAR_00h_010e7f9d counter in FUN_006fcc00).
static constexpr size_t kSpotPoolSize = 0x380000;
static int32_t gSpotPoolSlice = 0;

// Material parameter GUID of the spot ground shader
static constexpr uint64_t kSpotGroundMaterialGUID = 0xb0f1234b0f123473ull;

Spot::Spot() : Bank(), StreamedResource(), StreamingCallback() {
    // FUN_006fcc00
    status = SS_None;
    pSCBank = nullptr;
    pSpotSector = nullptr;
    pSector = nullptr;
    numGroundMaterial = 0;

    // Slice of the shared intro pool (the original derives the offset from a
    // per-spot counter). gpIntroPool may be null very early during bring-up.
    if ( gpIntroPool != nullptr && gpIntroPool->pIntroPool != nullptr ) {
        pSpotPool = static_cast<uint8_t *>( gpIntroPool->pIntroPool ) +
                    gSpotPoolSlice * kSpotPoolSize;
    } else {
        pSpotPool = nullptr;
    }
    gSpotPoolSlice++;
}

Spot::~Spot() { unload(); }

bool SpotStream::isInLoadedSpot( const void *pAddress ) const {
    const uint8_t *pByte = static_cast<const uint8_t *>( pAddress );
    for ( const SpotCell &cell : cells ) {
        const uint8_t *pBegin =
            static_cast<const uint8_t *>( cell.spot.getBankFile() );
        if ( pBegin != nullptr && pByte >= pBegin &&
             pByte < pBegin + cell.spot.getBankFileSize() ) {
            return true;
        }
    }
    return false;
}

void Spot::unload() {
    // FUN_006fcb60
    if ( isLoaded() ) {
        Bank::unload();
    }
    status = SS_None;
}

void Spot::loadContent() {
    // NOP
}

void Spot::instanciate() {
    // FUN_006fd220
    if ( gpScenaric != nullptr ) {
        pSCBank = gpScenaric->getNewSpotBank();
        if ( pSCBank != nullptr && pSpotSector != nullptr ) {
            Spots &spots = gpScenaric->getSpotList();
            SpotMap *pWorld = spots.getSpotMap( pSpotSector->Hashcode );
            if ( pWorld != nullptr ) {
                Matrix4x4f_t transform = Matrix4x4f_t::Identity();
                MatrixRotateX( transform, pSpotSector->EulerX );
                MatrixRotateY( transform, pSpotSector->EulerY );
                MatrixRotateZ( transform, pSpotSector->EulerZ );
                transform.row( 3 ) =
                    Eigen::Vector4f( pWorld->X, pWorld->Y, pWorld->Z, 1.0f );
                pSCBank->setBankTransform( transform );
            }

            if ( pSector != nullptr && pSector->state == 1 ) {
                pSCBank->setSpotBank( true );
                pSCBank->bindSpot( pSpotSector, pSector );

                MngActor &mngActor = pSector->geometry.mngActor;
                const int32_t numActorsBefore = mngActor.getActorCount();

                pSCBank->load( pBankFile, 0, 0, &mngActor, /*bNoCopy=*/true );
                pSCBank->forceLoad();
                gpScenaric->registerBank( pSCBank );

                for ( int32_t i = numActorsBefore; i < mngActor.getActorCount();
                      i++ ) {
                    Entity *pActor = mngActor.getActor( i );
                    if ( pActor != nullptr ) {
                        pActor->setSpot( this );
                    }
                }

                if ( pWorld != nullptr &&
                     pWorld->Type == eSpotType::ST_House ) {
                    launchHouseGateCine( pWorld );
                }

                setupGroundMaterials();
            }
        }
    }

    status = SS_Loaded;
    markAsInstanciated();
}

void Spot::launchHouseGateCine( SpotMap *pWorld ) {
    // FUN_007dbbb0
    if ( pSCBank == nullptr || pWorld == nullptr ||
         gpMngCinematics == nullptr ) {
        return;
    }

    IPlayerData *pSave =
        ( gpPlayerData != nullptr ) ? gpPlayerData->getLoadedSave() : nullptr;
    Garage *pGarage = ( pSave != nullptr ) ? pSave->getGarage() : nullptr;
    if ( pGarage == nullptr ) {
        return;
    }

    const bool bOwned = pGarage->isHouseOwned( pWorld->Hashcode );
    const char *pCineName = bOwned ? "C_OPEN" : "C_CLOSE";

    EntityCinematic *pCine = static_cast<EntityCinematic *>(
        pSCBank->findEntity( eEntityType::ET_CINE, pCineName ) );
    if ( pCine == nullptr || ( pCine->getFlags() & 0x4u ) != 0 ) {
        return;
    }

    pCine->setLoop( false );
    pCine->launchSpotCine( pWorld, 0.0f );

    OTDU_LOG_DEBUG( "House gate cinematic '%s' launched for spot 0x%016llx\n",
                    pCineName, (unsigned long long)pWorld->Hashcode );
}

void Spot::setupGroundMaterials() {
    // FUN_006fcd20
    numGroundMaterial = 0;

    if ( pSCBank == nullptr || pSector == nullptr ) {
        return;
    }

    RenderFile::Section *pShadowSection =
        ( pSector->state == 1 ) ? pSector->heightmap.pShadowTextureSection
                                : nullptr;

    const Eigen::Vector4f shiftPos = gpWorldStreaming->isEnabled()
                                         ? gpWorldStreaming->getPosition()
                                         : Eigen::Vector4f::Zero();

    const float projectionOrigin[4] = {
        static_cast<float>( pSector->offsetX ) - shiftPos.x(),
        0.0f - shiftPos.y(),
        static_cast<float>( pSector->offsetY ) - shiftPos.z(),
        1.0f - shiftPos.w() // FLOAT_00f4165c
    };

    for ( Render2DM *pMaterialBank : pSCBank->getCollection2D()->Render2DMs ) {
        if ( pMaterialBank == nullptr ) {
            continue;
        }

        const uint32_t numMaterials = pMaterialBank->getNumMaterials();
        for ( uint32_t i = 0; i < numMaterials; i++ ) {
            Material *pMaterial = pMaterialBank->getMaterialAtIndex( i );
            if ( pMaterial == nullptr ) {
                continue;
            }

            MaterialParameter *pParam = pMaterial->getParameterByIndex( 0 );
            if ( pParam == nullptr ||
                 pParam->Hashcode != kSpotGroundMaterialGUID ||
                 numGroundMaterial >= kMaxNumGroundMaterial ) {
                continue;
            }

            MaterialLayer *pLayer = pParam->getLayer( 0 );
            if ( pLayer != nullptr ) {
                pLayer->pLayerTextures[2].pTextureSection = pShadowSection;
                pLayer->pLayerTextures[2].SamplerAddress[0] = MTM_Mirror;
                pLayer->pLayerTextures[2].SamplerAddress[1] = MTM_Mirror;
            }

            MaterialShaderParameterArray *pArray =
                static_cast<MaterialShaderParameterArray *>( pParam );
            float *pSlot = (float *)pArray->getParameterDataByIndex( 0 );
            if ( pSlot != nullptr ) {
                pSlot[0] = projectionOrigin[0];
                pSlot[1] = projectionOrigin[1];
                pSlot[2] = projectionOrigin[2];
                pSlot[3] = projectionOrigin[3];
            }

            pMaterials[numGroundMaterial] = pMaterial;
            numGroundMaterial++;
        }
    }
}

bool Spot::onWorldStreamed( Eigen::Vector4f *param_1,
                            Eigen::Vector4f *param_2,
                            Eigen::Vector4f *param_3 ) {
    // FUN_006fd020
    if ( pSCBank != nullptr && pSCBank->getActiveScenery() != nullptr ) {
        pSCBank->getActiveScenery()->shiftObjects( *param_3 );
    }

    for ( int32_t i = 0; i < numGroundMaterial; i++ ) {
        Material *pMaterial = pMaterials[i];
        if ( pMaterial == nullptr ) {
            continue;
        }
        MaterialParameter *pParam = pMaterial->getParameterByIndex( 0 );
        if ( pParam == nullptr ) {
            continue;
        }
        MaterialShaderParameterArray *pArray =
            static_cast<MaterialShaderParameterArray *>( pParam );
        float *pSlot = (float *)pArray->getParameterDataByIndex( 0 );
        if ( pSlot != nullptr ) {
            pSlot[0] -= param_1->x();
            pSlot[1] -= param_1->y();
            pSlot[2] -= param_1->z();
            pSlot[3] -= param_1->w();
        }
    }
    return true;
}

bool Spot::loadFromSource( const char *pSourceName ) {
    // FUN_006fd160
    std::string bankPath = gpConfig->getResRootPath();
    bankPath += "Level/";
    bankPath += gpConfig->pLevelName;
    bankPath += "/Spots/";
    bankPath += pSourceName;
    bankPath += ".bnk";

    if ( !isLoaded() ) {
        bool bInstalled = loadBankWithMemory( gpFile, bankPath.c_str(),
                                              pSpotPool, kSpotPoolSize, false );
        if ( !bInstalled ) {
            status = SS_None;
            return false;
        }
    }

    status = SS_Loading;
    return true;
}

void Spot::destroy() {
    // FUN_006fd0d0
    if ( status != SS_Loaded ) {
        return;
    }

    if ( pSCBank != nullptr && gpScenaric != nullptr ) {
        gpScenaric->unloadBank( pSCBank );
        pSCBank = nullptr;
    }

    numGroundMaterial = 0;
    pSpotSector = nullptr;
    pSector = nullptr;

    if ( isLoaded() ) {
        unload();
    }

    status = SS_None;
}

void SpotStream::arm() {
    // FUN_006fc0f0
    if ( !bInitialized ) {
        bStream = false;
        bDraw = false;
        for ( SpotCell &cell : cells ) {
            cell.flags = 0;
            cell.pSector = nullptr;
            cell.pSpotSector = nullptr;
        }
        bInitialized = true;
    }
}

void SpotStream::updateRendering() {
    // FUN_006fc060
    if ( bNoPendingUpdate ) {
        bDraw = false;
    }
    // NOTE: Debug feature leftover omitted.
}

void SpotStream::tick() {
    // FUN_006fc680
    bNoPendingUpdate = true;
}

void SpotStream::waitForPendingLoads() { gpFile->flushPendingAsync(); }

void SpotStream::unload() {
    // FUN_006fc8b0
    if ( !bInitialized ) {
        return;
    }

    // Wait until no cell is mid-load, then tear them all down.
    for ( ;; ) {
        bool bAnyLoading = false;
        for ( SpotCell &cell : cells ) {
            if ( cell.spot.status == SS_Loading ) {
                bAnyLoading = true;
                break;
            }
        }
        if ( !bAnyLoading ) {
            break;
        }
        waitForPendingLoads();
    }

    for ( SpotCell &cell : cells ) {
        cell.spot.destroy();
        cell.flags = 0;
        cell.pSector = nullptr;
        cell.pSpotSector = nullptr;
    }
    bInitialized = false;
}

static int32_t InsertNearest( SpotStream::Candidate *pOut,
                              int32_t count,
                              int32_t maxCount,
                              float distSq,
                              SpotSector *pSpot,
                              void *pCategory ) {
    if ( count >= maxCount && distSq >= pOut[maxCount - 1].distSq ) {
        return count;
    }

    int32_t insertAt = count < maxCount ? count : maxCount - 1;
    while ( insertAt > 0 && pOut[insertAt - 1].distSq > distSq ) {
        if ( insertAt < maxCount ) {
            pOut[insertAt] = pOut[insertAt - 1];
        }
        insertAt--;
    }
    pOut[insertAt] = { pCategory, distSq, pSpot };
    return count < maxCount ? count + 1 : maxCount;
}

int32_t SpotStream::gatherNearby( const Eigen::Vector4f &position,
                                  Candidate *pOut,
                                  int32_t maxCount ) {
    // FUN_006fc390
    for ( int32_t i = 0; i < maxCount; i++ ) {
        pOut[i] = {};
    }

    if ( gpScenaric == nullptr ) {
        return 0;
    }

    Spots &spots = gpScenaric->getSpotList();
    int32_t count = 0;

    for ( int32_t list = 0; list < Spots::kNumSectorLists; list++ ) {
        const SpotsList &category = spots.getSectorList( list );
        SpotSector *pSectors = category.pSpotsMapOrSector.pSector;
        if ( pSectors == nullptr ) {
            continue;
        }

        for ( uint32_t n = 0; n < category.NumSpots; n++ ) {
            SpotSector *pSpot = &pSectors[n];

            const int32_t worldIdx = spots.getSpotMapIndex( pSpot->Hashcode );
            if ( worldIdx < 0 ||
                 worldIdx >= (int32_t)spots.getNumWorldSpots() ) {
                continue;
            }

            SpotMap *pWorld = spots.getWorldSpot( worldIdx );
            if ( pWorld == nullptr || pWorld->Type == ST_Challenge ) {
                continue;
            }

            const float dx = position.x() - pWorld->X;
            const float dy = position.y() - pWorld->Y;
            const float dz = position.z() - pWorld->Z;
            const float dw = position.w(); // spot has no w; ref is 0
            const float distSq = dx * dx + dy * dy + dz * dz + dw * dw;

            count = InsertNearest( pOut, count, maxCount, distSq, pSpot,
                                   const_cast<SpotsList *>( &category ) );
        }
    }

    return count;
}

int32_t SpotStream::gatherLoaded( const Eigen::Vector4f &position,
                                  Candidate *pOut,
                                  int32_t maxCount ) {
    // FUN_006fc140
    for ( int32_t i = 0; i < maxCount; i++ ) {
        pOut[i] = {};
    }

    if ( gpScenaric == nullptr ) {
        return 0;
    }

    Spots &spots = gpScenaric->getSpotList();
    int32_t count = 0;

    for ( SpotCell &cell : cells ) {
        SpotSector *pSpot = cell.pSpotSector;
        if ( pSpot == nullptr ) {
            continue;
        }

        const int32_t worldIdx = spots.getSpotMapIndex( pSpot->Hashcode );
        if ( worldIdx < 0 || worldIdx >= (int32_t)spots.getNumWorldSpots() ) {
            continue;
        }

        SpotMap *pWorld = spots.getWorldSpot( worldIdx );
        if ( pWorld == nullptr || pWorld->Type == ST_Challenge ) {
            continue;
        }

        const float dx = position.x() - pWorld->X;
        const float dy = position.y() - pWorld->Y;
        const float dz = position.z() - pWorld->Z;
        const float dw = position.w();
        const float distSq = dx * dx + dy * dy + dz * dz + dw * dw;

        count = InsertNearest( pOut, count, maxCount, distSq, pSpot, nullptr );
    }

    return count;
}

bool SpotStream::setsDiffer( const Candidate *pA,
                             int32_t numA,
                             const Candidate *pB,
                             int32_t numB ) {
    // FUN_006fbee0
    if ( numA != numB ) {
        return true;
    }
    for ( int32_t i = 0; i < numA; i++ ) {
        bool bFound = false;
        for ( int32_t j = 0; j < numB; j++ ) {
            if ( pA[i].pSpot == pB[j].pSpot ) {
                bFound = true;
                break;
            }
        }
        if ( !bFound ) {
            return true;
        }
    }
    return false;
}

void SpotStream::unloadStale( const Candidate *pNearby, int32_t numNearby ) {
    // FUN_006fc750
    for ( SpotCell &cell : cells ) {
        if ( cell.pSpotSector == nullptr ) {
            continue;
        }
        bool bStillNearby = false;
        for ( int32_t i = 0; i < numNearby; i++ ) {
            if ( pNearby[i].pSpot == cell.pSpotSector ) {
                bStillNearby = true;
                break;
            }
        }
        if ( !bStillNearby ) {
            cell.spot.destroy();
            cell.pSpotSector = nullptr;
            cell.pSector = nullptr;
            cell.flags = 0;
        }
    }
}

bool SpotStream::loadIntoCell( const Candidate &nearby, SpotCell &cell ) {
    // FUN_006fc970
    if ( gpScenaric == nullptr || nearby.pSpot == nullptr ) {
        return false;
    }

    Spots &spots = gpScenaric->getSpotList();
    const int32_t worldIdx = spots.getSpotMapIndex( nearby.pSpot->Hashcode );
    if ( worldIdx < 0 || worldIdx >= (int32_t)spots.getNumWorldSpots() ) {
        return false;
    }
    SpotMap *pWorld = spots.getWorldSpot( worldIdx );
    if ( pWorld == nullptr ) {
        return false;
    }

    char sourceName[9] = {};
    std::memcpy( sourceName, &pWorld->SourceHashcode, 8 );
    sourceName[8] = '\0';
    cell.spot.pSpotSector = nearby.pSpot;
    cell.spot.loadFromSource( sourceName );

    if ( cell.spot.status != SS_None ) {
        SpotsList *pCategory = static_cast<SpotsList *>( nearby.pCategory );
        Sector *pOwningSector =
            ( pCategory != nullptr ) ? pCategory->pSector : nullptr;

        cell.pSector = pOwningSector;
        cell.pSpotSector = nearby.pSpot;
        cell.spot.pSector = pOwningSector;
        cell.spot.pSpotSector = nearby.pSpot;

        cell.spot.instanciate();
        return true;
    }
    return false;
}

void SpotStream::updateCenter( const Eigen::Vector4f &position ) {
    // FUN_006fca00

    // NOTE: the original gates the whole pass on bStream. arm()/resume() clear
    // bStream, and the point at which ongoing streaming re-enables it is not
    // visible in the streaming path that has been ported so far; this gate may
    // need to be revisited once it can be checked at runtime (with assets).
    if ( !bStream ) {
        return;
    }

    // Do not re-evaluate while a cell is still loading.
    for ( SpotCell &cell : cells ) {
        if ( cell.spot.status == SS_Loading ) {
            return;
        }
    }

    Candidate nearby[kNumCells];
    Candidate loaded[kNumCells];
    const int32_t numNearby = gatherNearby( position, nearby, kNumCells );
    const int32_t numLoaded = gatherLoaded( position, loaded, kNumCells );

    if ( !setsDiffer( nearby, numNearby, loaded, numLoaded ) ) {
        return;
    }

    unloadStale( nearby, numNearby );

    for ( int32_t i = 0; i < numNearby; i++ ) {
        bool bAlreadyLoaded = false;
        for ( int32_t j = 0; j < numLoaded; j++ ) {
            if ( loaded[j].pSpot == nearby[i].pSpot ) {
                bAlreadyLoaded = true;
                break;
            }
        }
        if ( bAlreadyLoaded ) {
            continue;
        }

        SpotCell *pFreeCell = nullptr;
        for ( SpotCell &cell : cells ) {
            if ( cell.pSpotSector == nullptr ) {
                pFreeCell = &cell;
                break;
            }
        }
        if ( pFreeCell != nullptr ) {
            loadIntoCell( nearby[i], *pFreeCell );
        }
    }
}

void SpotStream::unloadSpotsForSector( Sector *pSector ) {
    // FUN_006fc800
    if ( !bInitialized ) {
        return;
    }

    for ( ;; ) {
        bool bAnyLoading = false;
        for ( SpotCell &cell : cells ) {
            if ( cell.spot.status == SS_Loading && cell.pSector == pSector ) {
                bAnyLoading = true;
                break;
            }
        }
        if ( !bAnyLoading ) {
            break;
        }
        waitForPendingLoads();
    }

    for ( SpotCell &cell : cells ) {
        if ( cell.pSector == pSector ) {
            cell.spot.destroy();
            cell.pSpotSector = nullptr;
            cell.pSector = nullptr;
            cell.flags = 0;
        }
    }
}
