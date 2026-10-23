#include "shared.h"
#include "mng_dbg_scnrc.h"

#include "entity/entity_volume.h"
#include "mng_actor.h"
#include "render/camera/camera_game.h"
#include "render/camera/gs_camera.h"
#include "render/gs_render.h"
#include "render/text_renderer.h"
#include "world/gs_world_streaming.h"
#include "world/mng_zone.h"

MngDebugScenaric *gpMngDbgScnrc = nullptr; // DAT_010e7f3c

static bool gDisplayVolumes = false;               // DAT_010e7f35
static bool BOOL_010e7f36 = false;                 // DAT_010e7f36
bool gGameMechsDebug = false;                      // DAT_010e7f37
static bool gGMECPTChanges = false;                // DAT_010e7f38
static bool gDisplayActorsPMIMemoryNeeded = false; // DAT_010e7f39

static int32_t gMaxNeededSizeFound = 0; // DAT_010e7fcc

static constexpr float kVolumeDisplayDistanceSq = 10000.0f; // DAT_00f416c4
static constexpr int32_t kMaxAutorisedPMISize = 0xc8000;

static MngActor *gpMngActor = nullptr; // DAT_006deb10

static void FUN_0070f370() {
    // FUN_0070f370
    BOOL_010e7f36 = !BOOL_010e7f36;
}

MngDebugScenaric::MngDebugScenaric() : Manager(), drawList() {
    // FUN_007bc4b2 (inlined)
    gDisplayVolumes = false;
    BOOL_010e7f36 = false;
    gGMECPTChanges = false;

    gpMngActor = nullptr;
    gpMngDbgScnrc = this;
}

MngDebugScenaric::~MngDebugScenaric() {
    // FUN_007bc359 (inlined)
    gpMngDbgScnrc = nullptr;
}

void MngDebugScenaric::tick( float, float ) {
    // FUN_00710120
    if ( gDisplayVolumes && gpMngZone != nullptr ) {
        if ( !drawList.isInitialized() ) {
            drawList.initialize( 250, 1000, 0, true, nullptr, "DbgScenaric",
                                 false );
        }

        drawList.reset();

        const Eigen::Vector4f &cameraPosition =
            *gpCamera->getActiveCamera()->getWorldPositionRW();
        for ( int32_t i = 0; i < gpMngZone->getNumVolumes(); i++ ) {
            const EntityVolume *pVolume =
                static_cast<const EntityVolume *>( gpMngZone->getVolume( i ) );

            const Eigen::Vector4f volumePosition =
                pVolume->getModelMatrix().row( 3 ).transpose();
            if ( ( volumePosition - cameraPosition ).squaredNorm() >=
                 kVolumeDisplayDistanceSq ) {
                continue;
            }

            Matrix4x4f_t volumeMatrix =
                pVolume->getInverseTransform().inverse();
            if ( gpWorldStreaming->isEnabled() ) {
                volumeMatrix.row( 3 ) -=
                    gpWorldStreaming->getPosition().transpose();
            }
        }
    }

    if ( gDisplayActorsPMIMemoryNeeded ) {
        char buffer[128];
        snprintf( buffer, sizeof( buffer ),
                  "MaxNeededSizeFound: %d    /    MaxAutorisedSize: %d",
                  gMaxNeededSizeFound, kMaxAutorisedPMISize );
        DrawDebugText( 200, 200, buffer );
    }
}

bool MngDebugScenaric::initialize( TestDriveGameInstance * ) {
    // FUN_0070f3c0
    return true;
}

void MngDebugScenaric::draw() {
    // FUN_007100d0
    if ( drawList.isInitialized() ) {
        gpRender->getRenderPass<eRenderPass::RP_Near>()
            .pScene->enqueueDynamicDrawList( &drawList );
    }
}

void MngDebugScenaric::terminate() {
    // FUN_0070f380
    if ( drawList.isInitialized() ) {
        drawList.destroy();
    }
}
