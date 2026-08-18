#include "shared.h"
#include "mng_cinematics.h"

#include "game/scenaric/entity/entity_cinematic.h"
#include "world/gs_world.h"
#include "world/sector_manager.h"
#include "world/streaming_manager.h"

MngCinematics *gpMngCinematics = nullptr; // DAT_010e7fd0

static constexpr float kNoGroundHeight = -99999.99f;     // DAT_00f418fc
static constexpr float kHeightmapMissHeight = 100000.0f; // DAT_00f418e0
static constexpr float kProbeLift = 1.0f;                // FLOAT_00f4165c

static Eigen::Vector3f GetWorldShiftOrigin() { return Eigen::Vector3f::Zero(); }

static float SampleWorldGroundHeight( const Eigen::Vector4f &probe ) {
    // FUN_00843dd0
    if ( gpWorld == nullptr ) {
        return kNoGroundHeight;
    }

    StreamingManager *pStreaming = gpWorld->getStreamingManager();
    if ( pStreaming == nullptr ) {
        return kNoGroundHeight;
    }

    Eigen::Vector4f local = probe;
    local.head<3>() -= GetWorldShiftOrigin();

    Sector *pSector = pStreaming->sectorManager.getSectorAtPosition( local );
    if ( pSector == nullptr || pSector->state != 1 ||
         pSector->geometry.pHeightmapInstance == nullptr ) {
        return kNoGroundHeight;
    }

    const float height =
        SampleGroundHeight( pSector->geometry.pHeightmapInstance,
                            pSector->heightmap, local.x(), local.z() );
    return height >= kHeightmapMissHeight ? kNoGroundHeight : height;
}

void ProjectActorOnGround( Entity *pActor,
                           bool bRebaseOnWorldShift,
                           bool bQueryPhysics ) {
    // FUN_007e4c50
    if ( pActor == nullptr ) {
        return;
    }

    Matrix4x4f_t matrix = pActor->getModelMatrix();

    if ( bRebaseOnWorldShift ) {
        matrix.row( 3 ).head<3>() += GetWorldShiftOrigin();
    }

    matrix( 3, 1 ) += kProbeLift;
    const Eigen::Vector4f probe( matrix( 3, 0 ), matrix( 3, 1 ), matrix( 3, 2 ),
                                 matrix( 3, 3 ) );

    float groundHeight = SampleWorldGroundHeight( probe );

    if ( bQueryPhysics ) {
        OTDU_UNIMPLEMENTED_ONCE; // TODO: PHYSICS
    }

    if ( groundHeight <= kNoGroundHeight ) {
        return;
    }

    matrix.row( 3 ).head<3>() -= GetWorldShiftOrigin();
    matrix.row( 3 ) << matrix( 3, 0 ), groundHeight, matrix( 3, 2 ), 1.0f;

    pActor->setTransform( matrix, false );
}

MngCinematics::MngCinematics()
    : Manager(),
      StreamingCallback(),
      pActiveCinematic( nullptr ),
      bIsValid( false ),
      numCinematics( 0 ) {
    // FUN_006de670
    pCinematics.fill( nullptr );
    gpMngCinematics = this;
}

MngCinematics::~MngCinematics() {
    // FUN_006de620
    if ( gpMngCinematics == this ) {
        gpMngCinematics = nullptr;
    }
}

bool MngCinematics::initialize( TestDriveGameInstance * ) {
    // FUN_006de400
    bIsValid = true;
    return true;
}

void MngCinematics::tick( float, float ) {
    // FUN_006de720
    bool bVar6 = gpActiveGameMode->isMessageBoxVisible();

    EntityCinematic *pSVar2 = pActiveCinematic;
    if ( ( ( ( pSVar2 != nullptr ) &&
             ( ( pSVar2->getFlags() >> 2 & 1 ) != 0 ) ) &&
           ( pSVar2->isLooping() == false ) ) &&
         ( bVar6 == '\0' ) ) {
        OTDU_UNIMPLEMENTED;
    }

    for ( EntityCinematic *pCinematic : pCinematics ) {
        if ( pCinematic != nullptr ) {
            pCinematic->tick();
        }
    }
}

void MngCinematics::pause() {
    // FUN_006de6e0
    bIsValid = false;
    for ( EntityCinematic *pCinematic : pCinematics ) {
        if ( pCinematic != nullptr ) {
            OTDU_UNIMPLEMENTED; // TODO: SCENARIC
            pCinematic->stop( true );
        }
    }
}

void MngCinematics::addPlayingCinematic( EntityCinematic *pCinematic ) {
    // FUN_006de530
    for ( EntityCinematic *&pSlot : pCinematics ) {
        if ( pSlot == nullptr ) {
            pSlot = pCinematic;
            if ( pCinematic->hasFocus() ) {
                pActiveCinematic = pCinematic;
                numCinematics++;
            }
            return;
        }
    }
}

void MngCinematics::removeCinematic( EntityCinematic *pCinematic ) {
    // FUN_006de4d0
    for ( EntityCinematic *&pSlot : pCinematics ) {
        if ( pSlot == pCinematic ) {
            if ( pCinematic->hasFocus() ) {
                pActiveCinematic = nullptr;
                numCinematics--;
            }
            pSlot = nullptr;
            OTDU_UNIMPLEMENTED; // TODO: SCENARIC
            return;
        }
    }
}

bool MngCinematics::onWorldStreamed( Eigen::Vector4f *param_1,
                                     Eigen::Vector4f *param_2,
                                     Eigen::Vector4f *param_3 ) {
    // FUN_006de580
    OTDU_UNIMPLEMENTED;
    return false;
}
