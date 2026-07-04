#pragma once

#include "shared.h"

#include "tdu_detail.h"

#include "core/maths_helper.h"

// Temporary diagnostics for the "placed emitters don't render" investigation.
// Set to 0 to strip. Uses OTDU_LOG_ALWAYS so it is visible in Release builds.
#define OTDU_PARTICLES_DIAG 1

struct ParticleInstance;
class Sector;

// A single placed particle emitter streamed within a sector. Populated from the
// sector's .PMI placement resource (0x60 bytes in the original, offsets noted).
struct ParticleEmitterPlacement {
    uint64_t hashcode = 0;                 // 0x00 particle-system CRC64 name
    Matrix4x4f_t transform =               // 0x10 world transform (row 3 = pos)
        Matrix4x4f_t::Identity();
    uint32_t color = 0xffffffff;           // 0x50 RGBA tint
    ParticleInstance *pInstance = nullptr; // 0x54 activated instance, or null
    uint32_t visibilityIndex = 0;          // 0x58 per-sector occlusion index
    float scale = 1.0f;                    // 0x5c emitter size (GenSize)
};

// One streamed sector's placed particle emitters (0xb60 bytes in the original, a
// pooled fixed array of up to 30 placements addressed by sector slot).
struct ParticleSectorGroup {
    Sector *pSector = nullptr;                 // 0x00 (null = empty slot)
    ParticleEmitterPlacement records[30] = {}; // 0x10
    int32_t numRecords = 0;                    // 0xb50
#if OTDU_PARTICLES_DIAG
    bool bDiagLogged = false;
#endif
};

// Streamed placed-particle-emitter world detail (hash "part"). Unlike the other
// details it owns no bank/materials of its own: it activates GSParticles systems
// for the emitter placements streamed with each sector (EURO .PMI resources) and
// fades them by camera distance.
class Particles : public TDUDetail {
public:
    static constexpr int32_t kNumSectorSlots = 7; // SectorManager::sectors[7]
    static constexpr int32_t kMaxRecordsPerGroup = 30;

public:
    Particles();
    ~Particles() override;

    bool initialize() override;
    void tick( float deltaTime ) override;
    void draw() override;
    void terminate() override;

    // Per-sector streaming hooks, called from SectorBank::instanciate /
    // Sector::reset (mirrors RoadPatch's per-sector install/uninstall).
    void installSector( Sector *pSector, void *pPMIResource );
    void uninstallSector( Sector *pSector );

private:
    int32_t findSectorSlot( const Sector *pSector ) const;
    void updateGroup( ParticleSectorGroup &group, float visibilityDistSquared );
    void releaseGroup( ParticleSectorGroup &group );
    static void releaseEmitter( ParticleEmitterPlacement &placement );

private:
    ParticleSectorGroup groups[kNumSectorSlots]; // field @0x3c0
    float visibilityDistance;                    // field @0x53c0
};

extern Particles *gpParticlesDetail;
