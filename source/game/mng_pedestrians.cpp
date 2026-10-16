#include "shared.h"
#include "mng_pedestrians.h"

#include "ai/gs_ai.h"
#include "core/arg_parser.h"
#include "game/gm_scene.h"
#include "game/scenaric/entity/entity_pedestrian.h"
#include "game/scenaric/gs_scenaric.h"
#include "game/scenaric/scenaric_bank.h"
#include "world/gs_pedestrians.h"
#include "world/gs_world.h"
#include "world/sector_manager.h"
#include "world/streaming_manager.h"

#include <fstream>

MngPedestrians *gpMngPedestrians = nullptr; // DAT_010e7ff8
bool gbPedestriansEnabled = false;

static constexpr const char *kNavigationFolder = "Euro/BNK/OpenTDU/Pedestrians";

static CmdLineArg CmdLineArgPedestrians( "pedestrians", []( const char *pArg ) {
    gbPedestriansEnabled = true;
} );

static constexpr int32_t kAiPedestrianVariant = 1;

static void SetBodyVariants( const int32_t variantId ) {
    AvatarCommonBnk &bank = gpPedestrians->getCharacterBank();
    for ( int32_t i = 0; i < AvatarCommonBnk::kNumBodyTypes; i++ ) {
        for ( int32_t j = 0; j < bank.getNumBody3DD( i ); j++ ) {
            bank.setVariantId( i, j, variantId );
        }
    }
}

static const MngPedestrians::ClipDesc
    kWalkClips[AvatarCommonBnk::kNumBodyTypes][2] = {
        {{ "customers\\male\\walk_loop01.anm", 0.66f },
         { "customers\\male\\walk_loop02.anm", 1.05f }  },
        {{ "customers\\female\\walk_loop01.anm", 0.98f },
         { "customers\\female\\walk_loop02.anm", 1.02f }},
};
static const MngPedestrians::ClipDesc
    kStandClips[AvatarCommonBnk::kNumBodyTypes][2] = {
        {{ "customers\\male\\stand_loop01.anm", 0.0f },
         { "customers\\male\\stand_loop02.anm", 0.0f }  },
        {{ "customers\\female\\stand_loop01.anm", 0.0f },
         { "customers\\female\\stand_loop02.anm", 0.0f }},
};
static constexpr float kAnimationSwitchDelay = 0.25f;

static constexpr const char *kCourtLifeSetName = "HoteE";
static constexpr float kCourtLifeSetCentreX = 10.5f;
static constexpr float kCourtLifeSetCentreZ = 20.6f;

static inline int32_t GetSlotBodyType( const uint32_t slot ) {
    return ( slot & 1 ) == 0 ? 1 : 0;
}

static inline int32_t GetSlotClipVariant( const uint32_t slot ) {
    return ( slot >> 1 ) & 1;
}

static ClipInstance *ResolveClip( const MngPedestrians::ClipDesc &desc ) {
    ClipInstance *pClip =
        gpScenaric->getWorldLifeSetBank().findClipByFilename( desc.pName );
    if ( pClip == nullptr ) {
        OTDU_LOG_WARN( "Pedestrian clip '%s' not found\n", desc.pName );
    }
    return pClip;
}

static void BuildEmptyNavigation( std::vector<uint8_t> &blob ) {
    blob.assign( 0x60, 0 );
    *reinterpret_cast<int32_t *>( blob.data() + 0x40 ) = 1;
    *reinterpret_cast<int32_t *>( blob.data() + 0x44 ) = 0x300;
}

MngPedestrians::MngPedestrians() : Manager() {
    // FUN_006c9c50
    if ( gpMngPedestrians == nullptr ) {
        gpMngPedestrians = this;
    }
}

MngPedestrians::~MngPedestrians() {
    // FUN_006c9c30
    gpMngPedestrians = nullptr;
}

bool MngPedestrians::initialize( TestDriveGameInstance * ) {
    // FUN_006c9ce0
    SetBodyVariants( kAiPedestrianVariant );

    gGSAI.getPedestrianManager().instanciateEntities(
        &gpActiveGMScene->getActorManager() );

    resolveClips();
    return true;
}

void MngPedestrians::tick( float deltaTime, float totalTime ) {
    // FUN_006c9c90
    AiPedestrianManager &pedestrians = gGSAI.getPedestrianManager();

    streamNavigation();

    if ( pedestrians.isAreaRemovalPending() ) {
        pedestrians.removePendingAreas();
        pedestrians.clearAreaRemovalPending();
    }

    if ( pedestrians.isEnabled() ) {
        pedestrians.update( deltaTime );
        pedestrians.updateDisplay();

        updateAnimations( deltaTime );
    }
}

void MngPedestrians::resolveClips() {
    for ( int32_t i = 0; i < AvatarCommonBnk::kNumBodyTypes; i++ ) {
        for ( int32_t j = 0; j < 2; j++ ) {
            pWalkClips[i][j] = ResolveClip( kWalkClips[i][j] );
            pStandClips[i][j] = ResolveClip( kStandClips[i][j] );
        }
    }
    for ( SlotAnimation &slot : slotAnimations ) {
        slot = SlotAnimation();
    }
}

void MngPedestrians::updateAnimations( const float deltaTime ) {
    AiPedestrianManager &pedestrians = gGSAI.getPedestrianManager();
    for ( uint32_t i = 0; i < gAIConfig.PedMaxToDisplay; i++ ) {
        EntityPedestrian *pEntity = static_cast<EntityPedestrian *>(
            pedestrians.getDisplayEntity( i ) );
        const AiPedestrian *pPedestrian = pedestrians.getSlotPedestrian( i );
        if ( pEntity == nullptr || pPedestrian == nullptr ) {
            continue;
        }
        PedestrianAnim *pAnim = pEntity->debugGetAnim();
        if ( pAnim == nullptr || !pAnim->bHasAnim ) {
            continue;
        }

        SlotAnimation &slot = slotAnimations[i];
        const int32_t bodyType = GetSlotBodyType( i );
        const int32_t variant = GetSlotClipVariant( i );

        const bool bMoving = gAIConfig.PedMinSpeed <= pPedestrian->Speed;
        if ( slot.pPlaying == nullptr ) {
            slot.bWalking = bMoving;
        } else if ( bMoving != slot.bWalking ) {
            slot.SwitchTimer += deltaTime;
            if ( slot.SwitchTimer >= kAnimationSwitchDelay ) {
                slot.bWalking = bMoving;
            }
        } else {
            slot.SwitchTimer = 0.0f;
        }

        ClipInstance *pClip = slot.bWalking ? pWalkClips[bodyType][variant]
                                            : pStandClips[bodyType][variant];
        if ( pClip != nullptr && pClip != slot.pPlaying ) {
            pEntity->playAnimationClip( pClip );
            slot.pPlaying = pClip;
            slot.SwitchTimer = 0.0f;
        }

        pAnim->PlaybackRate =
            slot.bWalking ? pPedestrian->Speed /
                                kWalkClips[bodyType][variant].NaturalSpeed
                          : 1.0f;
    }
}

void MngPedestrians::reset() {
    // FUN_006c9be0
}

void MngPedestrians::draw() {
    // FUN_0063f2b0
}

void MngPedestrians::pause() {
    // FUN_006c9bd0
}

void MngPedestrians::terminate() {
    // FUN_006c9c80
    gGSAI.getPedestrianManager().uninstanciateEntities();

    releaseNavigation();
    SetBodyVariants( 0 );
}

void MngPedestrians::streamNavigation() {
    StreamingManager *pStreaming = gpWorld->getStreamingManager();
    if ( pStreaming == nullptr ) {
        return;
    }

    AiPedestrianManager &pedestrians = gGSAI.getPedestrianManager();
    for ( int32_t i = 0; i < kNumSectors; i++ ) {
        Sector &sector = pStreaming->sectorManager.sectors[i];
        StreamedNav &nav = streamedNavs[i];

        const bool bInstalled =
            sector.installedArea.posX >= 0 && sector.installedSector.posX >= 0;
        if ( nav.bRegistered &&
             ( !bInstalled || nav.OriginX != sector.offsetX ||
               nav.OriginZ != sector.offsetY ) ) {
            unregisterNavigation( nav );
        }

        if ( !bInstalled || nav.bRegistered ) {
            continue;
        }

        nav.pSector = &sector;
        nav.OriginX = sector.offsetX;
        nav.OriginZ = sector.offsetY;
        loadNavigation( nav );
        pedestrians.registerArea( nav.Blob.data(), &sector );
        nav.bRegistered = true;
    }
}

void MngPedestrians::unregisterNavigation( StreamedNav &nav ) {
    AiPedestrianManager &pedestrians = gGSAI.getPedestrianManager();
    for ( int32_t i = 0; i < AiPedestrianManager::kNumNavSlots; i++ ) {
        AiPedestrianNavSlot &slot = pedestrians.getNavSlot( i );
        if ( slot.pOwner == nav.pSector ) {
            pedestrians.removeArea( &slot );
            slot.reset();
            break;
        }
    }
    nav.bRegistered = false;
}

void MngPedestrians::releaseNavigation() {
    for ( StreamedNav &nav : streamedNavs ) {
        if ( nav.bRegistered ) {
            unregisterNavigation( nav );
        }
    }
}

void MngPedestrians::loadNavigation( StreamedNav &nav ) {
    char path[256];
    snprintf( path, sizeof( path ), "%s/pednav_%d_%d.pnav", kNavigationFolder,
              nav.OriginX, nav.OriginZ );

    std::ifstream file( path, std::ios::binary | std::ios::ate );
    if ( !file.is_open() ) {
        BuildEmptyNavigation( nav.Blob );
        return;
    }

    const std::streamsize size = file.tellg();
    file.seekg( 0, std::ios::beg );
    nav.Blob.resize( static_cast<size_t>( size ) );
    file.read( reinterpret_cast<char *>( nav.Blob.data() ), size );

    OTDU_LOG_INFO(
        "Pedestrian nav '%s' registered (%u quads)\n", path,
        *reinterpret_cast<const uint32_t *>( nav.Blob.data() + 0x20 ) );
}

static Entity *FindCourtLifeSetTemplate() {
    ScenaricBank &bank = gpScenaric->getWorldLifeSetBank();
    if ( !bank.isLoaded() ) {
        return nullptr;
    }

    for ( int32_t i = 0; i < bank.getEntityCount(); i++ ) {
        Entity *pEntity = bank.getEntity( i );
        if ( pEntity != nullptr && pEntity->getType() == eEntityType::ET_LSET &&
             StringCmpI( pEntity->getInstanceName().c_str(),
                         kCourtLifeSetName ) == 0 ) {
            return pEntity;
        }
    }
    return nullptr;
}

Entity *CreateCourtLifeSet( MngActor &mngActor,
                            const Matrix4x4f_t &courtTransform ) {
    if ( !gbPedestriansEnabled || gpScenaric == nullptr ) {
        return nullptr;
    }

    Entity *pTemplate = FindCourtLifeSetTemplate();
    if ( pTemplate == nullptr ) {
        return nullptr;
    }

    Matrix4x4f_t court = courtTransform;
    for ( int32_t i = 0; i < 3; i++ ) {
        court.row( i ).head<3>().normalize();
    }

    Matrix4x4f_t transform = Matrix4x4f_t::Identity();
    transform.row( 3 ) = Eigen::Vector4f( -kCourtLifeSetCentreX, 0.0f,
                                          -kCourtLifeSetCentreZ, 1.0f );
    MatrixRotateY( transform, kPI * 0.5f );
    transform = transform * court;

    Entity *pInstance = mngActor.instanciateActor( pTemplate );
    if ( pInstance != nullptr ) {
        pInstance->setTransform( transform, true );
    }
    return pInstance;
}
