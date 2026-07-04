#include "shared.h"
#include "particles.h"

#include "core/maths_helper.h"

#include "render/camera/camera_game.h"
#include "render/camera/gs_camera.h"
#include "render/particles/gs_particles.h"

#include "world/gs_world.h"
#include "world/sector_manager.h"
#include "world/streaming_manager.h"

Particles *gpParticlesDetail = nullptr;

constexpr uint32_t kPMIMagic = 0x494d502e;      // ".PMI"
constexpr float kScaleFactor = 1.0f / 16384.0f; // DAT_00f41ec0
constexpr float kRotationFactor =
    2.0f * 3.14159265358979f / 65536.0f;             // DAT_00f41ec4
constexpr float kPositionFactor = 1.0f / 64.0f;      // DAT_00f41ebc
constexpr float kPositionOffset = 128.0f;            // DAT_00f4176c
constexpr float kCellMetricFactor = 1.0f / 65536.0f; // DAT_00f41678

constexpr float kVisibilityDistance = 300.0f; // DAT_00f4183c
constexpr float kFadeStartFactor = 0.8f;      // DAT_00f416d0

Particles::Particles()
    : TDUDetail(), groups(), visibilityDistance( kVisibilityDistance ) {
    // FUN_008a4ff0
    pName = "Particles";
    hash = 0x70617274; // "part"
    gpParticlesDetail = this;
}

Particles::~Particles() {
    // FUN_004147a0
    gpParticlesDetail = nullptr;
}

bool Particles::initialize() {
    // FUN_008a4f00 - the detail owns no assets; it only drives GSParticles for
    // the per-sector emitter placements streamed in installSector().
    bInitialized = true;
    return true;
}

void Particles::tick( float ) {
    // FUN_008a5280
    const float visibilityDistSquared = visibilityDistance * visibilityDistance;
    for ( int32_t i = 0; i < kNumSectorSlots; i++ ) {
        updateGroup( groups[i], visibilityDistSquared );
    }
}

void Particles::draw() {
    // FUN_008a4ef0 (TestDrive::NOP) - the placements are rendered by GSParticles.
}

void Particles::terminate() {
    // FUN_008a4fb0
    for ( int32_t i = 0; i < kNumSectorSlots; i++ ) {
        releaseGroup( groups[i] );
    }
    bInitialized = false;
}

int32_t Particles::findSectorSlot( const Sector *pSector ) const {
    // Mirrors FUN_008a54a0 / FUN_008a4f60: index of the sector in the streaming
    // sector array (groups[i] tracks sectors[i]).
    if ( gpWorld == nullptr ) {
        return -1;
    }
    StreamingManager *pStreaming = gpWorld->getStreamingManager();
    if ( pStreaming == nullptr ) {
        return -1;
    }
    for ( int32_t i = 0; i < kNumSectorSlots; i++ ) {
        if ( &pStreaming->sectorManager.sectors[i] == pSector ) {
            return i;
        }
    }
    return -1;
}

void Particles::releaseEmitter( ParticleEmitterPlacement &placement ) {
    // The out-of-range / teardown path shared by updateGroup and releaseGroup.
    if ( placement.pInstance != nullptr ) {
        placement.pInstance->State = eParticleState::PS_Dead;
        placement.pInstance->stop();
        placement.pInstance = nullptr;
    }
}

void Particles::releaseGroup( ParticleSectorGroup &group ) {
    // FUN_008a4f10
    if ( group.pSector == nullptr ) {
        return;
    }
    const int32_t numRecords = group.numRecords;
    group.pSector = nullptr;
    group.numRecords = 0;
    for ( int32_t i = 0; i < numRecords; i++ ) {
        releaseEmitter( group.records[i] );
    }
}

void Particles::updateGroup( ParticleSectorGroup &group,
                             float visibilityDistSquared ) {
    // FUN_008a5060
    if ( group.pSector == nullptr || group.pSector->state != 1 ||
         gpCamera == nullptr ) {
        return;
    }

    const Matrix4x4f_t &camMat = gpCamera->getDefaultCamera()->getCamToWorld();
    const Eigen::Vector4f camPos = camMat.row( 3 );
    const float fadeStart = visibilityDistSquared * kFadeStartFactor;

#if OTDU_PARTICLES_DIAG
    const bool bLogNow = !group.bDiagLogged;
    if ( bLogNow ) {
        OTDU_LOG_ALWAYS(
            "[PART-DIAG] updateGroup active pSector=%p state=%d records=%d "
            "camPos=(%.1f, %.1f, %.1f) visDist2=%.0f gpParticles=%p\n",
            (void *)group.pSector, group.pSector->state, group.numRecords,
            camPos.x(), camPos.y(), camPos.z(), visibilityDistSquared,
            (void *)gpParticles );
    }
#endif

    const auto &lodCache = group.pSector->geometry.heightmapLODCache;

    for ( int32_t i = 0; i < group.numRecords; i++ ) {
        ParticleEmitterPlacement &rec = group.records[i];

        // Per-tile heightmap visibility cull (FUN_008a5060): the original skips
        // emitters whose heightmap clipmap tile is frustum / far-plane-footprint
        // culled this frame (LOD byte == 0xff). RenderContext::drawHeightmap
        // computes that per-tile LOD each frame and caches it on the sector
        // geometry (the level-0 18x18 grid), indexed by the emitter's authored
        // .PMI tile index (record.visibilityIndex). 0xfe (below-sea flat) and the
        // real LOD levels 0..4 all count as visible.
        const uint8_t tileLod = ( rec.visibilityIndex < lodCache.size() )
                                    ? lodCache[rec.visibilityIndex]
                                    : 0;
        if ( tileLod == 0xff ) {
#if OTDU_PARTICLES_DIAG
            if ( bLogNow ) {
                OTDU_LOG_ALWAYS( "[PART-DIAG]   rec[%d] LODCULL visIdx=%u\n", i,
                                 rec.visibilityIndex );
            }
#endif
            releaseEmitter( rec );
            continue;
        }

        const Eigen::Vector4f recPos = rec.transform.row( 3 );
        const Eigen::Vector4f delta = recPos - camPos;
        const float dist2 = delta.x() * delta.x() + delta.y() * delta.y() +
                            delta.z() * delta.z();
        if ( dist2 > visibilityDistSquared ) {
#if OTDU_PARTICLES_DIAG
            if ( bLogNow ) {
                OTDU_LOG_ALWAYS( "[PART-DIAG]   rec[%d] OUT dist2=%.0f\n", i,
                                 dist2 );
            }
#endif
            releaseEmitter( rec );
            continue;
        }

        if ( rec.pInstance == nullptr ||
             rec.pInstance->State != eParticleState::PS_Alive ) {
            rec.pInstance = ( gpParticles != nullptr )
                                ? gpParticles->activateParticles( rec.hashcode,
                                                                  false )
                                : nullptr;
        }

#if OTDU_PARTICLES_DIAG
        if ( bLogNow ) {
            OTDU_LOG_ALWAYS( "[PART-DIAG]   rec[%d] IN dist2=%.0f visIdx=%u "
                             "lod=0x%02x hash=%016llx activated=%p\n",
                             i, dist2, rec.visibilityIndex, tileLod,
                             (unsigned long long)rec.hashcode,
                             (void *)rec.pInstance );
        }
#endif

        if ( rec.pInstance != nullptr ) {
            rec.pInstance->Matrix = rec.transform;
            rec.pInstance->Scale = rec.scale;

            // Distance fade for default (non-sparkle) systems only.
            const Particle *pInfos = rec.pInstance->pParticleInstanciated;
            if ( pInfos != nullptr &&
                 pInfos->SimuType == eParticleSimulationType::PST_Default ) {
                float alpha = 1.0f;
                if ( dist2 > fadeStart ) {
                    alpha = 1.0f - ( dist2 - fadeStart ) /
                                       ( visibilityDistSquared - fadeStart );
                }
                rec.pInstance->Alpha = alpha;
            }
        }
    }

#if OTDU_PARTICLES_DIAG
    if ( bLogNow ) {
        group.bDiagLogged = true;
    }
#endif
}

void Particles::installSector( Sector *pSector, void *pPMIResource ) {
    // FUN_008a54a0 + FUN_008a52c0. The placement blob is the same .PMI resource
    // (asset group 5, type 0x18) consumed by RoadPatch::installSectorProps; each
    // subsystem keeps the entries whose hashcode it can resolve.
    if ( !bInitialized || pPMIResource == nullptr ) {
#if OTDU_PARTICLES_DIAG
        OTDU_LOG_ALWAYS( "[PART-DIAG] installSector SKIP: bInitialized=%d res=%p\n",
                         (int)bInitialized, pPMIResource );
#endif
        return;
    }
    const int32_t slotIndex = findSectorSlot( pSector );
    if ( slotIndex < 0 ) {
#if OTDU_PARTICLES_DIAG
        OTDU_LOG_ALWAYS(
            "[PART-DIAG] installSector SKIP: sector %p not in slot array\n",
            (void *)pSector );
#endif
        return;
    }

    const uint16_t *pHeader = static_cast<const uint16_t *>( pPMIResource );
    if ( pHeader[0] != 0 || pHeader[1] != 6 ||
         *reinterpret_cast<const uint32_t *>( pHeader + 6 ) != kPMIMagic ) {
#if OTDU_PARTICLES_DIAG
        OTDU_LOG_ALWAYS( "[PART-DIAG] installSector SKIP: bad header "
                         "hdr0=%u hdr1=%u magic=%08x\n",
                         pHeader[0], pHeader[1],
                         *reinterpret_cast<const uint32_t *>( pHeader + 6 ) );
#endif
        return;
    }

    const float offsetX = static_cast<float>( pSector->offsetX );
    const float offsetZ = static_cast<float>( pSector->offsetY );
    const float originY = *reinterpret_cast<const float *>( pHeader + 0x12 );
    const float cellSizeY =
        ( *reinterpret_cast<const float *>( pHeader + 0x14 ) - originY ) *
        kCellMetricFactor;

    ParticleSectorGroup &group = groups[slotIndex];
    group.numRecords = 0;
#if OTDU_PARTICLES_DIAG
    group.bDiagLogged = false;
#endif

    const uint32_t numTypes =
        *reinterpret_cast<const uint32_t *>( pHeader + 0x10 );
    const uint16_t *pType = pHeader + 0xe8;

    for ( uint32_t t = 0; t < numTypes; t++ ) {
        const uint64_t hashcode =
            static_cast<uint64_t>(
                *reinterpret_cast<const uint32_t *>( pType + 8 ) ) |
            ( static_cast<uint64_t>(
                  *reinterpret_cast<const uint32_t *>( pType + 0xa ) )
              << 32 );

        const uint16_t instanceCount = pType[0xc];
        const uint32_t instanceStride =
            Align( static_cast<uint32_t>( pType[0xd] ) + 0x20, 16 );
        const uint16_t *pInstance = pType + 0x10;

        for ( uint16_t n = 0; n < instanceCount; n++ ) {
            if ( group.numRecords < kMaxRecordsPerGroup ) {
                const float scaleX = pInstance[3] * kScaleFactor;
                const float scaleY = pInstance[4] * kScaleFactor;
                const float scaleZ = pInstance[5] * kScaleFactor;
                const float rotX = pInstance[6] * kRotationFactor;
                const float rotY = pInstance[7] * kRotationFactor;
                const float rotZ = pInstance[8] * kRotationFactor;
                const float posX =
                    pInstance[0] * kPositionFactor - kPositionOffset;
                const float posY = pInstance[1] * cellSizeY + originY;
                const float posZ =
                    pInstance[2] * kPositionFactor - kPositionOffset;

                Eigen::Vector3f scale( scaleX, scaleY, scaleZ );
                Matrix4x4f_t transform = MakeScaleMatrix( scale );
                MatrixRotateX( transform, rotX );
                MatrixRotateZ( transform, rotZ );
                MatrixRotateY( transform, rotY );
                transform.row( 3 ) = Eigen::Vector4f( posX + offsetX, posY,
                                                      posZ + offsetZ, 1.0f );

                ParticleEmitterPlacement &rec = group.records[group.numRecords];
                rec.hashcode = hashcode;
                rec.transform = transform;
                const uint8_t colorByte =
                    static_cast<uint8_t>( pInstance[10] & 0xff );
                rec.color = 0xff000000u | ( colorByte << 16 ) |
                            ( colorByte << 8 ) | colorByte;
                rec.visibilityIndex = pInstance[9];
                rec.scale = scaleX;
                rec.pInstance = nullptr;
                group.numRecords++;
            }

            pInstance = reinterpret_cast<const uint16_t *>(
                reinterpret_cast<const uint8_t *>( pInstance ) +
                instanceStride );
        }
    }

    group.pSector = pSector;

#if OTDU_PARTICLES_DIAG
    OTDU_LOG_ALWAYS( "[PART-DIAG] installSector slot=%d parsed %d records "
                     "(sectorOffset=%d,%d)\n",
                     slotIndex, group.numRecords, pSector->offsetX,
                     pSector->offsetY );
    for ( int32_t d = 0; d < group.numRecords; d++ ) {
        const ParticleEmitterPlacement &r = group.records[d];
        OTDU_LOG_ALWAYS( "[PART-DIAG]   rec[%d] hash=%016llx "
                         "pos=(%.1f, %.1f, %.1f) scale=%.3f\n",
                         d, (unsigned long long)r.hashcode, r.transform( 3, 0 ),
                         r.transform( 3, 1 ), r.transform( 3, 2 ), r.scale );
    }
#endif
}

void Particles::uninstallSector( Sector *pSector ) {
    // FUN_008a4f60
    const int32_t slotIndex = findSectorSlot( pSector );
    if ( slotIndex >= 0 ) {
        releaseGroup( groups[slotIndex] );
    }
}
