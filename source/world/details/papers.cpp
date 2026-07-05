#include "shared.h"
#include "papers.h"

#include "game/avatar/gs_cloth_system.h"

#include "render/2dm.h"
#include "render/camera/camera_game.h"
#include "render/camera/gs_camera.h"
#include "render/particles/gs_particles.h"

#include "world/weather/gs_weather.h"

#include <cstdlib>

// DEBUG: Force papers spawning without GSAmbiance (unimplemented)
bool gDebugForcePapers = false;

static constexpr const char *kPaperNames[Papers::kNumPapers] = { // DAT_00f7f3b8
    "J_01", "J_02", "J_04", "J_05", "J_06", "J_07", "J_08", "J_09" };

Papers::Papers()
    : TDUDetail(),
      render2DBs{ Render2DB(), Render2DB(), Render2DB(), Render2DB(),
                  Render2DB(), Render2DB(), Render2DB(), Render2DB() },
      render2DM(),
      bTestAmbiance( true ),
      papers{},
      numPapersToSpawn( 2 ),
      paperWindFactor( 0.06f ), // DAT_00f41e08
      paperSpawnTime( 0.9f ),   // DAT_00f416a0
      paperNextSpawnTime( 0.0f ),
      paperMinDistance( 20.0f ), // DAT_00f03ee0
      paperMedDistance( 50.0f ), // DAT_00f41848
      paperMaxDistance( 90.0f ), // FLOAT_00f4180c
      leftMinDistance( 20.0f ),  // DAT_00f03ee0
      leafMaxDistance( 60.0f ),  // FLOAT_00f41744
      pLeafParticle( nullptr ) {
    // FUN_008a69b0
    for ( int32_t i = 0; i < kNumPapers; i++ ) {
        papers[i].lifetime = -1.0f; // FLOAT_00f41664
        papers[i].pName = nullptr;
        papers[i].pCloth = nullptr;
    }
    pName = "Papers";
    hash = 0x70617072; // "papr"
}

Papers::~Papers() {
    // FUN_00414740
}

bool Papers::initialize() {
    // FUN_008a74a0
    loadBank( "Papers.bnk" );
    if ( !bank.isLoaded() ) {
        return true;
    }

    load2DResources( render2DBs, &render2DM, 1, kNumPapers );

    // The original also resolves each sheet's cloth material chunk here
    // (FUN_008a7f90 into DAT_00f7f3cc) so the GSClothSystem can texture it;
    // stubbed until that subsystem is ported.
    for ( int32_t i = 0; i < kNumPapers; i++ ) {
        papers[i].pName = kPaperNames[i];
    }

    paperNextSpawnTime = 0.0f;
    bInitialized = true;
    return true;
}

void Papers::tick( float deltaTime ) {
    // FUN_008a7480
    updatePapers( deltaTime );
    updateLeaf();
}

void Papers::draw() {
    // FUN_008a63c0
    // NOP
}

void Papers::terminate() {
    // FUN_008a6410
    bInitialized = false;
    releasePapers();
    unload2DResources();
    if ( bank.isLoaded() ) {
        bank.unload();
    }
}

void Papers::setEnabled( bool bEnable ) {
    // FUN_008a63d0
    bEnabled = bEnable;
    if ( !bEnable && pLeafParticle != nullptr ) {
        // Release the flying-leaf particle instance back to GSParticles.
        pLeafParticle->State = eParticleState::PS_Dead;
        pLeafParticle->stop();
        pLeafParticle = nullptr;
    }
}

void Papers::updatePapers( float deltaTime ) {
    // FUN_008a7070
    if ( gpCamera == nullptr ) {
        return;
    }

    if ( gpWeather != nullptr && gpWeather->isConfigDirty() ) {
        gpWeather->updateActiveConfig();
    }

    OTDU_UNIMPLEMENTED; // TODO: REPLAY (will use frame dt in the meantime)
    const float scaledDt = deltaTime;

    paperNextSpawnTime -= scaledDt;
    int32_t spawnBudget = 0;
    if ( paperNextSpawnTime <= 0.0f ) {
        if ( !bTestAmbiance || isAmbianceRegionActive( 6 ) ) {
            spawnBudget = numPapersToSpawn;
        }
        paperNextSpawnTime = paperSpawnTime;
    }

    const Eigen::Vector4f testWind( 4.0f, 0.0f, 1.0f, 0.0f );

    for ( int32_t i = 0; i < kNumPapers; i++ ) {
        Paper &paper = papers[i];

        if ( paper.pCloth != nullptr ) {
            ClothInstance *pCloth =
                static_cast<ClothInstance *>( paper.pCloth );
            if ( pCloth->State == 0 ) {
                paper.lifetime = -1.0f;
                paper.pCloth = nullptr;
            } else {
                paper.lifetime -= scaledDt;
                if ( paper.lifetime < 0.0f ) {
                    pCloth->State = 0;
                    pCloth->bVisible = 0;
                    pCloth->pMaterial = nullptr;
                    paper.pCloth = nullptr;
                } else {
                    pCloth->Acceleration = testWind;
                }
            }
        } else if ( paper.lifetime < 0.0f && spawnBudget > 0 &&
                    gpClothSystem != nullptr ) {
            ClothInstance *pCloth = gpClothSystem->allocateCloth();
            paper.pCloth = pCloth;
            if ( pCloth != nullptr ) {
                Eigen::Vector4f spawnPos;
                computeSpawnPosition( paperMinDistance, paperMaxDistance,
                                      paperMedDistance, spawnPos );

                Matrix4x4f_t transform = Matrix4x4f_t::Identity();
                transform.row( 3 ) = spawnPos;

                Material *pMat = render2DM.getMaterialAtIndex( 0 );
                pCloth->setup( 8, 8, 1.5f, 2.0f, transform, pMat, 4 );
                pCloth->SolverIterations = 2; // FUN_008a7070 spawn override
                OTDU_UNIMPLEMENTED;           // TODO: PHYSICS

                paper.lifetime = 0.5f; // FLOAT_00f41674
                spawnBudget--;
            }
        }
    }
}

void Papers::updateLeaf() {
    // FUN_008a6e50
    const bool bAmbianceActive =
        !bTestAmbiance || isAmbianceRegionActive( 3 ) ||
        isAmbianceRegionActive( 5 ) || isAmbianceRegionActive( 4 ) ||
        isAmbianceRegionActive( 0 );

    if ( bAmbianceActive ) {
        if ( pLeafParticle == nullptr ||
             pLeafParticle->State != eParticleState::PS_Alive ) {
            // Activate the FlyingLeaf particle. The original also binds a
            // per-emitter geometry callback (FUN_008a64d0) to the activated
            // instance; deferred with the ed particle engine.
            pLeafParticle = ( gpParticles != nullptr )
                                ? gpParticles->activateParticles(
                                      eParticleType::PT_Leaf2, true )
                                : nullptr;
        }
        if ( pLeafParticle != nullptr ) {
            // Reposition the leaf ahead of the camera each frame (FUN_008a7700
            // + matrix translate); deferred with computeSpawnPosition.
        }
    } else if ( pLeafParticle != nullptr ) {
        // Deactivate the leaf particle when leaving the region.
        pLeafParticle->State = eParticleState::PS_Killed;
        pLeafParticle->stop();
        pLeafParticle = nullptr;
    }
}

void Papers::releasePapers() {
    // FUN_008a6410
    for ( int32_t i = 0; i < kNumPapers; i++ ) {
        Paper &paper = papers[i];
        paper.lifetime = -1.0f; // FLOAT_00f41664
        if ( paper.pCloth != nullptr ) {
            ClothInstance *pCloth =
                static_cast<ClothInstance *>( paper.pCloth );
            pCloth->State = 0;
            pCloth->bVisible = 0;
            pCloth->pMaterial = nullptr;
            paper.pCloth = nullptr;
        }
    }
}

bool Papers::isAmbianceRegionActive( int32_t regionId ) {
    // FUN_00901c10
    if ( gDebugForcePapers ) {
        return true;
    }
    (void)regionId;
    OTDU_UNIMPLEMENTED; // TODO: WORLD
    return false;
}

void Papers::computeSpawnPosition( float minDistance,
                                   float maxDistance,
                                   float mediumDistance,
                                   Eigen::Vector4f &outPos ) {
    // FUN_008a7700
    auto rand01 = []() { return ( rand() & 0x7fff ) / 32768.0f; };

    CameraGame *pCam = gpCamera->getDefaultCamera();
    const Matrix4x4f_t &camToWorld = pCam->getCamToWorld();
    const Eigen::Vector3f camPos = pCam->getWorldPositionOffset();
    const Eigen::Vector3f right = camToWorld.row( 0 ).head<3>();
    const Eigen::Vector3f up = camToWorld.row( 1 ).head<3>();
    const Eigen::Vector3f forward = -camToWorld.row( 2 ).head<3>();

    const float distance =
        minDistance + rand01() * ( maxDistance - minDistance );
    const float lateral = ( rand01() - 0.5f ) * mediumDistance;
    const float height = 2.0f + rand01() * 8.0f;

    const Eigen::Vector3f pos =
        camPos + forward * distance + right * lateral + up * height;
    outPos = Eigen::Vector4f( pos.x(), pos.y(), pos.z(), 1.0f );
}
