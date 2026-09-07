#include "shared.h"
#include "scenaric_bank.h"

#include "system/system_shared.h"
#include "game/scenaric/gs_scenaric.h"
#include "game/scenaric/mng_actor.h"
#include "game/scenaric/entity/entity_car_player.h"
#include "render/serialized_skinned_model.h"
#include "render/instance.h"
#include "render/animation/skeleton.h"
#include "render/animation/clip.h"
#include "render/animation/animation.h"
#include "filesystem/gs_file.h"
#include "physics/gs_physics.h"
#include "game/gm_world.h"
#include "world/gs_world_streaming.h"
#include "audio/implementation/audio_dispatcher_playback.h"
#include "audio/implementation/ed_audio_stream_entry.h"

static inline void ReleaseTemplateResources( BinaryArray &container ) {
    // FUN_007db1a0 (inlined)
    for ( int32_t i = container.getCount() - 1; i >= 0; i-- ) {
        Entity *pTemplate = static_cast<Entity *>( container.getElement( i ) );
        if ( pTemplate != nullptr ) {
            pTemplate->unloadResources();
        }
    }
}

static inline bool ContainerOwns( BinaryArray &container, Entity *pEntity ) {
    // FUN_007db1a0 (inlined)
    for ( int32_t i = 0; i < container.getCount(); i++ ) {
        if ( static_cast<Entity *>( container.getElement( i ) ) == pEntity ) {
            return true;
        }
    }
    return false;
}

ScenaricBank::ScenaricBank()
    : filename( "" ),
      hashcode( 0 ),
      parentHashcode( 0 ),
      id( -1 ),
      state( BBS_Unused ),
      pBuffer( nullptr ),
      loadedSize( 0 ),
      bank(),
      pMngActor( nullptr ),
      pPhysicsFactoryDynamic( nullptr ),
      pPhysicsFactoryStatic( nullptr ),
      collection2D(),
      collection3D(),
      bIsSpotBank( false ),
      bExternalBuffer( false ),
      bNoCopy( false ) {
    // FUN_007da1b0
}

ScenaricBank::~ScenaricBank() {}

void ScenaricBank::instanciate() {
    // FUN_007db5e0
    constructResourceLists();
    pushResources2D();
    pushResources3D();

    if ( pMngActor != nullptr ) {
        activate( pMngActor );
    }

    state = BBS_Loaded;
}

void ScenaricBank::activate( MngActor *pTargetMngActor ) {
    // FUN_007db5e0
    if ( bActivated ) {
        return;
    }
    bActivated = true;

    prepareTemplates( zoneVolumes );
    prepareTemplates( zones );
    prepareTemplates( entities );

    // The InstantiateEntity loops (only when an actor manager owns this bank).
    if ( pTargetMngActor != nullptr ) {
        pMngActor = pTargetMngActor;
        if ( !bIsSpotBank ) {
            instanciateContainer( zones );
            instanciateContainer( zoneVolumes );
        }
        instanciateContainer( entities );
    }

    if ( bIsSpotBank ) {
        instanciateSceneries();
    }
}

void ScenaricBank::prepareTemplates( BinaryArray &container ) {
    // FUN_00425950
    for ( int32_t i = 0; i < container.getCount(); i++ ) {
        Entity *pTemplate = static_cast<Entity *>( container.getElement( i ) );
        if ( pTemplate != nullptr ) {
            pTemplate->loadResources( this );
            pTemplate->initialize();
        }
    }
}

Entity *ScenaricBank::getEntityByHashcode( uint64_t hashcode ) const {
    // FUN_00425b90
    if ( hashcode == 0 ) {
        return nullptr;
    }

    const int32_t count = entities.getCount();
    for ( int32_t i = 0; i < count; i++ ) {
        if ( entities.getHashcode( i ) == hashcode ) {
            return static_cast<Entity *>( entities.getElement( i ) );
        }
    }
    return nullptr;
}

Entity *ScenaricBank::getVolumeByHashcode( uint64_t hashcode ) const {
    if ( hashcode == 0 ) {
        return nullptr;
    }

    const int32_t count = zoneVolumes.getCount();
    for ( int32_t i = 0; i < count; i++ ) {
        if ( zoneVolumes.getHashcode( i ) == hashcode ) {
            return static_cast<Entity *>( zoneVolumes.getElement( i ) );
        }
    }
    return nullptr;
}

Path *ScenaricBank::getPathByHashcode( uint64_t hashcode ) const {
    // FUN_004291e0
    if ( hashcode == 0 ) {
        return nullptr;
    }

    const int32_t count = paths.getCount();
    for ( int32_t i = 0; i < count; i++ ) {
        if ( paths.getHashcode( i ) == hashcode ) {
            return static_cast<Path *>( paths.getElement( i ) );
        }
    }
    return nullptr;
}

SoundBinary *ScenaricBank::getSoundByHashcode( uint64_t hashcode ) const {
    // FUN_00428e80 (inlined)
    if ( hashcode == 0 ) {
        return nullptr;
    }

    const int32_t count = sounds.getCount();
    for ( int32_t i = 0; i < count; i++ ) {
        if ( sounds.getHashcode( i ) == hashcode ) {
            return static_cast<SoundBinary *>( sounds.getElement( i ) );
        }
    }
    return nullptr;
}

void ScenaricBank::setBankTransform( const Matrix4x4f_t &transform ) {
    bankTransform = transform;
    bTransform = true;
}

void ScenaricBank::instanciateContainer( BinaryArray &container ) {
    // FUN_007db5e0
    for ( int32_t i = 0; i < container.getCount(); i++ ) {
        Entity *pTemplate = static_cast<Entity *>( container.getElement( i ) );
        if ( pTemplate == nullptr ) {
            continue;
        }

        Entity *pInstance = nullptr;
        if ( !bNoCopy ) {
            pInstance = pMngActor->instanciateActor( pTemplate );
        } else {
            pTemplate->initializeInstance( pMngActor, nullptr );
            pInstance = pTemplate;
        }

        if ( bTransform && pInstance != nullptr ) {
            Matrix4x4f_t worldMatrix =
                pInstance->getModelMatrix() * bankTransform;
            pInstance->setTransform( worldMatrix, true );
        }
    }
}

void ScenaricBank::unload() {
    // FUN_007d9d00
    for ( HiearchyNode *pNode : sceneryNodes ) {
        if ( pNode != nullptr ) {
            collection3D.uninstantiate( pNode );
        }
    }
    sceneryNodes.clear();

    releaseResourceLists();
    bActivated = false;
    pSpotSector = nullptr;
    pSector = nullptr;

    if ( !bExternalBuffer && pBuffer != nullptr ) {
        TestDrive::Free( pBuffer );
    }
    pBuffer = nullptr;

    parentHashcode = 0;
    hashcode = 0;
    filename.clear();
    state = BBS_Unused;
}

bool ScenaricBank::stepStreaming() {
    // FUN_007dbc90
    const uint64_t previousHashcode = hashcode;

    switch ( state ) {
    case BBS_Request:
        if ( !streamFromFile() ) {
            OTDU_LOG_ERROR( "Bank load request failed! (streamFromFile() "
                            "returned false)\n" );
            reset();
        }
        break;
    case BBS_Loading:
        loadContent();
        state = BBS_TmpInit;
        break;
    case BBS_TmpInit:
        instanciate();
        break;
    case BBS_Cancel:
    case BBS_LoadFailed:
        unload();
        break;
    case BBS_Term:
        destroy();
        break;
    case BBS_Flushing:
        terminate();
        unload();
        break;
    default:
        break;
    }

    return hashcode != previousHashcode;
}

void ScenaricBank::reset() {
    // FUN_007d9710
    parentHashcode = 0;
    hashcode = 0;
    state = BBS_Unused;
    filename.clear();
}

namespace {
    // Builds one render-object list from a file-reference table. Each file-ref
    // entry holds a bank-entry index whose data is the render-file blob (a host
    // pointer via getEntry); Render*::initialize relocates the blob in place.
    template <typename T>
    void buildResourceList( BankInstance &bank,
                            const uint32_t *pStructs,
                            const uint64_t *pHashcodes,
                            std::vector<BankRenderResource<T>> &out,
                            const char *label = "" ) {
        if ( pStructs == nullptr ) {
            return;
        }
        const uint32_t count = pStructs[0];
        out.reserve( count );
        for ( uint32_t i = 0; i < count; ++i ) {
            const int32_t entryIdx = static_cast<int32_t>( pStructs[1 + i] );
            ParsedBankEntry entry;
            entry.bResolveFilename = false;
            if ( bank.getEntry( entryIdx, entry ) != 0 ) {
                continue;
            }

            auto pResource = std::make_unique<T>();
            if ( !pResource->initialize( entry.pData ) ) {
                continue;
            }
            out.push_back( { pHashcodes[i], std::move( pResource ) } );
        }
    }

    template <typename T>
    T *findResource( std::vector<BankRenderResource<T>> &list,
                     uint64_t hashcode ) {
        for ( auto &entry : list ) {
            if ( entry.hashcode == hashcode ) {
                return entry.object.get();
            }
        }
        return nullptr;
    }
} // namespace

void ScenaricBank::constructResourceLists() {
    // FUN_007db5e0
    buildResourceList( bank, fileRefs[FR_3DD].pStructs,
                       fileRefs[FR_3DD].pHashcodes, lists3DD, "3DD" );
    buildResourceList( bank, fileRefs[FR_3DG].pStructs,
                       fileRefs[FR_3DG].pHashcodes, lists3DG, "3DG" );
    buildResourceList( bank, fileRefs[FR_2DM].pStructs,
                       fileRefs[FR_2DM].pHashcodes, lists2DM );
    buildResourceList( bank, fileRefs[FR_2DB].pStructs,
                       fileRefs[FR_2DB].pHashcodes, lists2DB );
    buildResourceList( bank, fileRefs[FR_XSB].pStructs,
                       fileRefs[FR_XSB].pHashcodes, listsSkeleton );
    buildResourceList( bank, fileRefs[FR_Anm].pStructs,
                       fileRefs[FR_Anm].pHashcodes, listsClip );

    // UVA tracks additionally need the blob length.
    const uint32_t *pUvaStructs = fileRefs[FR_Uva].pStructs;
    const uint64_t *pUvaHashes = fileRefs[FR_Uva].pHashcodes;
    if ( pUvaStructs != nullptr ) {
        const uint32_t count = pUvaStructs[0];
        listsUVA.reserve( count );
        for ( uint32_t i = 0; i < count; ++i ) {
            ParsedBankEntry entry;
            entry.bResolveFilename = false;
            if ( bank.getEntry( static_cast<int32_t>( pUvaStructs[1 + i] ),
                                entry ) != 0 ) {
                continue;
            }

            auto pResource = std::make_unique<RenderUVA>();
            if ( pResource->initialize( entry.pData,
                                        static_cast<size_t>( entry.Size ) ) ) {
                listsUVA.push_back( { pUvaHashes[i], std::move( pResource ) } );
            }
        }
    }
}

void ScenaricBank::releaseResourceLists() {
    for ( auto &r : lists3DD ) {
        collection3D.unregister3DD( r.object.get() );
    }
    for ( auto &r : lists3DG ) {
        collection3D.unregister3DG( r.object.get() );
    }
    for ( auto &r : lists2DM ) {
        collection2D.unregister2DM( r.object.get() );
    }
    for ( auto &r : lists2DB ) {
        collection2D.unregister2DB( r.object.get() );
    }
    for ( auto &r : listsUVA ) {
        collection2D.unregisterUVA( r.object.get() );
    }
    lists3DD.clear();
    lists3DG.clear();
    lists2DM.clear();
    lists2DB.clear();
    listsUVA.clear();
    listsSkeleton.clear();
    listsClip.clear();
}

void *ScenaricBank::getCinData( uint64_t hashcode, int32_t *pOutSize ) {
    return getFileRefData( FR_Cin, hashcode, pOutSize );
}

void *ScenaricBank::getWavData( uint64_t hashcode, int32_t *pOutSize ) {
    return getFileRefData( FR_Wav, hashcode, pOutSize );
}

void *ScenaricBank::getFileRefData( int32_t table,
                                    uint64_t hashcode,
                                    int32_t *pOutSize ) {
    const uint32_t *pStructs = fileRefs[table].pStructs;
    const uint64_t *pHashcodes = fileRefs[table].pHashcodes;
    if ( pStructs == nullptr || pHashcodes == nullptr || hashcode == 0 ) {
        return nullptr;
    }

    const uint32_t count = pStructs[0];
    for ( uint32_t i = 0; i < count; i++ ) {
        if ( pHashcodes[i] != hashcode ) {
            continue;
        }

        ParsedBankEntry entry;
        entry.bResolveFilename = false;
        if ( bank.getEntry( static_cast<int32_t>( pStructs[1 + i] ), entry ) !=
             0 ) {
            return nullptr;
        }
        if ( pOutSize != nullptr ) {
            *pOutSize = entry.Size;
        }
        return entry.pData;
    }

    return nullptr;
}

Render3DD *ScenaricBank::getRender3DD( uint64_t hashcode ) {
    return findResource( lists3DD, hashcode );
}

Render3DG *ScenaricBank::getRender3DG( uint64_t hashcode ) {
    return findResource( lists3DG, hashcode );
}

Render2DM *ScenaricBank::getRender2DM( uint64_t hashcode ) {
    return findResource( lists2DM, hashcode );
}

Render2DB *ScenaricBank::getRender2DB( uint64_t hashcode ) {
    return findResource( lists2DB, hashcode );
}

SkeletonInstance *ScenaricBank::getSkeleton( uint64_t hashcode ) {
    return findResource( listsSkeleton, hashcode );
}

ClipInstance *ScenaricBank::getClip( uint64_t hashcode ) {
    return findResource( listsClip, hashcode );
}

namespace {
    int countChannelMatches( SkeletonInstance *pSkel, ClipInstance *pClip ) {
        const uint8_t *pCh = pClip->getControllers();
        const uint32_t numCh = pClip->getNumControllers();
        int matches = 0;
        for ( uint32_t i = 0; i < numCh && pCh != nullptr; i++ ) {
            const uint32_t boneID =
                *reinterpret_cast<const uint32_t *>( pCh + 8 );
            const uint32_t size =
                *reinterpret_cast<const uint32_t *>( pCh + 4 );
            if ( pSkel->findNode( boneID ) >= 0 ) {
                matches++;
            }
            if ( size == 0 ) {
                break;
            }
            pCh += size;
        }
        return matches;
    }
} // namespace

ClipInstance *ScenaricBank::debugScoreClipsForSkeleton( SkeletonInstance *pSkel,
                                                        int *pBestMatches ) {
    ClipInstance *pBest = nullptr;
    int best = 0;
    if ( pSkel != nullptr ) {
        OTDU_LOG_ALWAYS(
            "[ANIM-CLIP] '%s': scoring %zu clips vs skeleton nodes=%d\n",
            filename.c_str(), listsClip.size(), pSkel->getNumNodes() );
        for ( auto &c : listsClip ) {
            const int m = countChannelMatches( pSkel, c.object.get() );
            OTDU_LOG_ALWAYS(
                "[ANIM-CLIP]   clip hash=0x%llx ctrls=%u matches=%d\n",
                static_cast<unsigned long long>( c.hashcode ),
                c.object->getNumControllers(), m );
            if ( m > best ) {
                best = m;
                pBest = c.object.get();
            }
        }
    }
    if ( pBestMatches != nullptr ) {
        *pBestMatches = best;
    }
    return pBest;
}

ClipInstance *ScenaricBank::debugCompatibleClip( SkeletonInstance *pSkel,
                                                 int index,
                                                 int *pCompatibleCount ) {
    std::vector<ClipInstance *> compatible;
    if ( pSkel != nullptr ) {
        for ( auto &c : listsClip ) {
            if ( countChannelMatches( pSkel, c.object.get() ) > 0 ) {
                compatible.push_back( c.object.get() );
            }
        }
    }
    const int count = static_cast<int>( compatible.size() );
    if ( pCompatibleCount != nullptr ) {
        *pCompatibleCount = count;
    }
    if ( count == 0 ) {
        return nullptr;
    }
    const int i =
        ( ( index % count ) + count ) % count; // wrap (handles negative)
    return compatible[i];
}

// TODO: REMOVE ME
ClipInstance *ScenaricBank::debugPickCalmestClip( SkeletonInstance *pSkel,
                                                  float minMatchFraction ) {
    if ( pSkel == nullptr ) {
        return nullptr;
    }
    const int numNodes = pSkel->getNumNodes();
    const int minMatches =
        ( numNodes > 0 && minMatchFraction > 0.0f )
            ? static_cast<int>( numNodes * minMatchFraction ) + 1
            : 1;
    ClipInstance *pBest = nullptr;
    float leastMotion = -1.0f;
    for ( auto &c : listsClip ) {
        ClipInstance *pClip = c.object.get();
        if ( pClip == nullptr ||
             countChannelMatches( pSkel, pClip ) < minMatches ) {
            continue;
        }

        Animation anim;
        anim.initialize( pSkel, pClip );
        const int32_t numNodes = anim.getNumNodes();
        const Matrix4x4f_t *pPalette = anim.getPalette();
        if ( pPalette == nullptr || numNodes <= 0 ) {
            continue;
        }
        std::vector<Eigen::Vector3f> startTranslations( numNodes );
        for ( int32_t i = 0; i < numNodes; i++ ) {
            startTranslations[i] = Eigen::Vector3f(
                pPalette[i]( 3, 0 ), pPalette[i]( 3, 1 ), pPalette[i]( 3, 2 ) );
        }

        anim.update( pClip->getTotalDuration() * 0.5f );
        pPalette = anim.getPalette();
        float motion = 0.0f;
        for ( int32_t i = 0; i < numNodes; i++ ) {
            const Eigen::Vector3f mid( pPalette[i]( 3, 0 ), pPalette[i]( 3, 1 ),
                                       pPalette[i]( 3, 2 ) );
            motion += ( mid - startTranslations[i] ).norm();
        }

        if ( leastMotion < 0.0f || motion < leastMotion ) {
            leastMotion = motion;
            pBest = pClip;
        }
    }
    return pBest;
}

void ScenaricBank::pushResources2D() {
    // FUN_007d9660
    for ( auto &r : lists2DB ) {
        collection2D.register2DB( r.object.get() );
    }
    for ( auto &r : listsUVA ) {
        collection2D.registerUVA( r.object.get() );
    }
    for ( auto &r : lists2DM ) {
        collection2D.register2DM( r.object.get() );
    }
}

void ScenaricBank::pushResources3D() {
    // FUN_007d9c20
    const uint64_t dgRef =
        pActiveScenery != nullptr
            ? ( ( static_cast<uint64_t>( pActiveScenery->field_0x54 ) << 32 ) |
                pActiveScenery->field_0x50 )
            : 0ull;
    int registeredCount = 0;
    for ( auto &r : lists3DG ) {
        bool bRegister = true;
        if ( bankVersion >= 0x11 ) {
            bRegister = false;
            if ( pActiveScenery != nullptr ) {
                if ( r.hashcode == dgRef ) {
                    bRegister = true;
                }
            }
        }
        if ( bRegister ) {
            collection3D.register3DG( r.object.get() );
            registeredCount++;
        }
    }
    for ( auto &r : lists3DD ) {
        collection3D.register3DD( r.object.get() );
    }

    for ( auto &r3DD : lists3DD ) {
        for ( auto &r2DM : lists2DM ) {
            r3DD.object->bindMaterialsBank( r2DM.object.get() );
        }
    }
}

void ScenaricBank::instanciateSceneries() {
    // FUN_004288e0
    if ( pActiveScenery == nullptr || pActiveScenery->pName == nullptr ) {
        return;
    }

    const uint64_t searchHash = GetIdentifier64bit( pActiveScenery->pName );
    for ( auto &r : lists3DD ) {
        const uint32_t n = r.object->getNumHiearchy();
        for ( uint32_t i = 0; i < n; i++ ) {
            const uint64_t h = r.object->getHiearchyHashcode( i );
        }
    }

    HiearchyNode *pNode = collection3D.instantiate( searchHash );
    if ( pNode == nullptr ) {
        for ( auto &r : lists3DD ) {
            if ( r.object->getNumHiearchy() > 0 ) {
                RenderFile::Section *pSec = r.object->getHiearchyEntry( 0 );
                if ( pSec != nullptr ) {
                    pNode = collection3D.instantiate( r.object.get(), pSec );
                    if ( pNode != nullptr ) {
                        break;
                    }
                }
            }
        }
    }

    if ( pNode == nullptr ) {
        return;
    }

    pActiveScenery->pHierarchyNode = pNode;
    pActiveScenery->pBank = this;
    pActiveScenery->InitialMatrix = pNode->TransformMatrix;

    if ( !bIsSpotBank && gpActiveGM != nullptr ) {
        gpActiveGM->getActorManager().setParentScenery( pActiveScenery );
    }

    const Eigen::Vector4f streamOrigin =
        ( gpWorldStreaming != nullptr && gpWorldStreaming->isEnabled() )
            ? gpWorldStreaming->getPosition()
            : Eigen::Vector4f::Zero();

    if ( bTransform ) {
        pActiveScenery->InitialMatrix = bankTransform;
        pActiveScenery->shiftObjects( streamOrigin );
    }
    pActiveScenery->shiftObjects( streamOrigin );

    sceneryNodes.push_back( pNode );
}

Entity *ScenaricBank::getFirst( eEntityType type ) {
    // FUN_007d9e30
    for ( int32_t i = 0; i < entities.getCount(); i++ ) {
        Entity *pEntity = static_cast<Entity *>( entities.getElement( i ) );
        if ( pEntity != nullptr && pEntity->getType() == type ) {
            return pEntity;
        }
    }
    return nullptr;
}

Entity *ScenaricBank::findEntity( eEntityType type,
                                  const char *pInstanceName ) {
    // FUN_007dab40
    for ( int32_t i = 0; i < entities.getCount(); i++ ) {
        Entity *pEntity = static_cast<Entity *>( entities.getElement( i ) );
        if ( pEntity == nullptr || pEntity->getType() != type ) {
            continue;
        }

        if ( StringCmpI( pEntity->getInstanceName().c_str(), pInstanceName ) ==
             0 ) {
            return pEntity;
        }
    }
    return nullptr;
}

void ScenaricBank::forceLoad() {
    // FUN_007dbd50
    if ( state == BBS_Request || ( bExternalBuffer && state == BBS_TmpInit ) ) {
        while ( stepStreaming(), state != BBS_Max ) {
            if ( state == BBS_LoadFailed ) {
                return;
            }
            if ( state == BBS_Unused ) {
                return;
            }
            if ( state == BBS_Loaded ) {
                return;
            }
        }
    }
}

void ScenaricBank::destroy() {
    // FUN_007db1a0
    state = BBS_Flushing;
    bTransform = false;

    if ( pMngActor != nullptr ) {
        if ( !bNoCopy ) {
            // Uninstanciate every actor that was instanced from one of this
            // bank's templates
            for ( int32_t i = 0; i < pMngActor->getActorCount(); ) {
                Entity *pActor = pMngActor->getActor( i );
                Entity *pTemplate =
                    pActor != nullptr ? pActor->getParent() : nullptr;
                if ( pTemplate != nullptr &&
                     ( ContainerOwns( zones, pTemplate ) ||
                       ContainerOwns( zoneVolumes, pTemplate ) ||
                       ContainerOwns( entities, pTemplate ) ) ) {
                    MngActor::Uninstanciate( pMngActor, pActor );
                    continue;
                }
                i++;
            }
        } else {
            // In-place instanciation (spot exterior banks)
            for ( int32_t i = 0; i < zones.getCount(); i++ ) {
                Entity *pTemplate =
                    static_cast<Entity *>( zones.getElement( i ) );
                if ( pTemplate != nullptr ) {
                    pTemplate->destroyInstance();
                }
            }
            for ( int32_t i = 0; i < zoneVolumes.getCount(); i++ ) {
                Entity *pTemplate =
                    static_cast<Entity *>( zoneVolumes.getElement( i ) );
                if ( pTemplate != nullptr ) {
                    pTemplate->destroyInstance();
                }
            }

            for ( int32_t i = 0; i < pMngActor->getActorCount(); ) {
                Entity *pActor = pMngActor->getActor( i );
                if ( pActor != nullptr && ContainerOwns( entities, pActor ) ) {
                    if ( pActor->getType() == eEntityType::ET_CARP ) {
                        static_cast<EntityCarPlayer *>( pActor )
                            ->unloadResources();
                    }
                    pMngActor->removeActorFromList( pActor );
                    continue;
                }
                i++;
            }
        }
    }

    for ( int32_t i = sceneries.getCount() - 1; i >= 0; i-- ) {
        SceneryBinary *pScenery =
            static_cast<SceneryBinary *>( sceneries.getElement( i ) );
        if ( pScenery == nullptr || pScenery->pHierarchyNode == nullptr ) {
            continue;
        }

        if ( gpActiveGM != nullptr ) {
            gpActiveGM->getActorManager().setParentScenery( nullptr );
        }

        FileCollection3D *pOwnerCollection =
            pScenery->pBank != nullptr ? pScenery->pBank->getCollection3D()
                                       : &collection3D;
        pOwnerCollection->uninstantiate( pScenery->pHierarchyNode );

        sceneryNodes.erase( std::remove( sceneryNodes.begin(),
                                         sceneryNodes.end(),
                                         pScenery->pHierarchyNode ),
                            sceneryNodes.end() );
        pScenery->pHierarchyNode = nullptr;
    }

    ReleaseTemplateResources( entities );
    ReleaseTemplateResources( zones );
    ReleaseTemplateResources( zoneVolumes );

    for ( int32_t i = sounds.getCount() - 1; i >= 0; i-- ) {
        SoundBinary *pSound =
            static_cast<SoundBinary *>( sounds.getElement( i ) );
        if ( pSound != nullptr && pSound->pSoundInstance != nullptr ) {
            pSound->pSoundInstance->stop( 0.0f );
        }
    }

    pActiveScenery = nullptr;
}

void ScenaricBank::installStaticPhysics( void *pBlob, uint32_t blobSize ) {
    // FUN_007daf50
    if ( gpPhysics == nullptr ) {
        return;
    }

    const uint64_t factoryHashcode =
        ( pSpotSector != nullptr ) ? pSpotSector->Hashcode : hashcode;

    if ( bTransform ) {
        const Matrix4x4f_t transform = bankTransform;
        pPhysicsFactoryStatic = gpPhysics->installStaticCollision(
            pBlob, blobSize, factoryHashcode, &transform );
    } else {
        pPhysicsFactoryStatic = gpPhysics->installStaticCollision(
            pBlob, blobSize, factoryHashcode, nullptr );
    }
}

void ScenaricBank::terminate() {
    // FUN_007d9fd0
    if ( gpPhysics != nullptr ) {
        gpPhysics->cancelPendingPropFactory( &pPhysicsFactoryDynamic );
    }
    if ( pPhysicsFactoryDynamic != nullptr ) {
        gpPhysics->removeAndDestroyFactory( pPhysicsFactoryDynamic );
        pPhysicsFactoryDynamic = nullptr;
    }
    if ( pPhysicsFactoryStatic != nullptr ) {
        gpPhysics->removeAndDestroyFactory( pPhysicsFactoryStatic );
        pPhysicsFactoryStatic = nullptr;
    }

    if ( state != BBS_TmpInit ) {
        for ( int32_t i = 0; i < FR_Count; i++ ) {
            fileRefs[i] = BankFileRefArray{};
        }
    }
}

void ScenaricBank::load( void *pBuffer,
                         uint64_t hashcode,
                         uint64_t parentHashcode,
                         MngActor *pMngActor,
                         bool param_6 ) {
    // FUN_007dba50
    if ( state == BBS_Loaded ) {
        destroy();
        unload();
    }

    this->pMngActor = pMngActor;
    this->bNoCopy = param_6;
    this->hashcode = hashcode;
    this->id = static_cast<int32_t>( hashcode );
    this->parentHashcode = parentHashcode;
    this->bExternalBuffer = true;
    this->pBuffer = pBuffer;

    loadContent();
    state = BBS_TmpInit;
}

void ScenaricBank::loadFromFile( const char *pBankPath,
                                 MngActor *pMngActor,
                                 bool param_6 ) {
    // FUN_007dbae0
    // The original aborts on a path that would overflow its 512-byte buffer.
    if ( strlen( pBankPath ) >= 512 ) {
        filename.clear();
        return;
    }
    filename = pBankPath;

    if ( state == BBS_Loaded ) {
        destroy();
        unload();
    }

    this->pMngActor = pMngActor;
    this->bNoCopy = param_6;
    this->hashcode = 0;
    this->id = 0;
    this->parentHashcode = 0;
    this->bExternalBuffer = false;
    this->state = BBS_Request;
}

bool ScenaricBank::streamFromFile() {
    // FUN_007dbc50
    const bool bLoaded =
        gpFile->loadFile( filename.c_str(), &pBuffer, &loadedSize );
    state = bLoaded ? BBS_Loading : BBS_LoadFailed;
    return bLoaded;
}

void ScenaricBank::loadContent() {
    // FUN_007dafe0
    uint32_t bufferSize = loadedSize;
    if ( bufferSize == 0 ) {
        const BankHeader *pBankHeader = reinterpret_cast<const BankHeader *>(
            static_cast<uint8_t *>( pBuffer ) + 8 );
        bufferSize = static_cast<uint32_t>( pBankHeader->Size );
    }
    bank.load( static_cast<const uint8_t *>( pBuffer ), bufferSize, true );

    const int32_t numEntries = bank.getNumElements();

    ParsedBankEntry entry;
    entry.bResolveFilename = false;

    for ( int32_t i = 0; i < numEntries; i++ ) {
        bank.getEntry( i, entry );

        if ( entry.Type == 0 ) {
            if ( entry.SubType == 0xb ) {
                installStaticPhysics( entry.pData, entry.Size );
            } else if ( !bIsSpotBank && entry.SubType == 0xc &&
                        gpPhysics != nullptr ) {
                gpPhysics->queuePropCollisionFactory( entry.pData, entry.Size,
                                                      &pPhysicsFactoryDynamic );
            }
            continue;
        }
        if ( entry.Type != 8 ) {
            continue;
        }

        BinaryReader reader( entry.pData );
        switch ( entry.SubType ) {
        case 1: {
            // FUN_007d9e90
            const uint8_t *p = reader.pCursor;
            const uint32_t tag = *reinterpret_cast<const uint32_t *>( p );
            // Funny magic number :-)
            //          |
            //          v
            if ( tag == 0xB16B00B5 ) {
                const uint8_t *v = p + 8;
                const uint32_t version =
                    ( uint32_t( v[0] ) << 24 ) | ( uint32_t( v[1] ) << 16 ) |
                    ( uint32_t( v[2] ) << 8 ) | uint32_t( v[3] );
                bankVersion = version;
                if ( gpScenaric != nullptr ) {
                    gpScenaric->setAssetsVersion( version );
                }
            }
            break;
        }
        case 2:
            entities.deserialize( reader, this );
            break;
        case 5:
            zoneVolumes.deserialize( reader, this );
            zones.deserialize( reader, this );
            break;
        case 6:
            deserializeFileReferences( reader );
            break;
        case 7:
            sceneries.deserialize( reader, this );
            {
                const int32_t n = sceneries.getCount();
                pActiveScenery = ( n > 0 ) ? static_cast<SceneryBinary *>(
                                                 sceneries.getElement( n - 1 ) )
                                           : nullptr;
            }
            break;
        case 8:
            paths.deserialize( reader, this );
            break;
        case 9:
            sounds.deserialize( reader, this );
            break;
        default:
            break;
        }
    }

    entry.Clear();
}

void ScenaricBank::deserializeFileReferences( BinaryReader &reader ) {
    // FUN_007d98d0
    uint8_t *pBlockStart = reader.pCursor;
    reader.pCursor += 8; // leading tag (4) + block size (4)
    reader.saveStack[reader.depth] = pBlockStart;
    reader.depth++;

    const uint32_t tag = *reinterpret_cast<const uint32_t *>( pBlockStart );
    if ( tag == 0x8A58C0DE ) {
        const uint32_t version = bankVersion;

        int32_t tableCount = FR_Wav + 1;
        if ( version > 3 ) {
            tableCount = FR_Uva + 1;
        }
        if ( version > 6 ) {
            tableCount = FR_Sce + 1;
        }

        for ( int32_t i = 0; i < tableCount; i++ ) {
            uint32_t *pArray = reinterpret_cast<uint32_t *>( reader.pCursor );
            const uint32_t numElements = pArray[0];

            reader.pCursor =
                ( numElements != 0 )
                    ? reinterpret_cast<uint8_t *>( pArray + 1 + numElements )
                    : reinterpret_cast<uint8_t *>( pArray + 1 );

            if ( static_cast<int32_t>( numElements ) < 1 ) {
                fileRefs[i].pStructs = nullptr;
                fileRefs[i].pHashcodes = nullptr;
            } else {
                fileRefs[i].pStructs = pArray;
                fileRefs[i].pHashcodes =
                    reinterpret_cast<uint64_t *>( reader.pCursor );
                reader.pCursor += numElements * sizeof( uint64_t );
            }
        }
    }

    // Pop back to the parent block: resume right after this block's payload.
    reader.depth--;
    uint32_t *pParent =
        reinterpret_cast<uint32_t *>( reader.saveStack[reader.depth] );
    reader.pCursor = reinterpret_cast<uint8_t *>( pParent ) + pParent[1];
}
