#pragma once

#include "shared.h"

#include "tdu_detail.h"

#include "core/maths_helper.h"

struct ParticleInstance;
class Sector;

// A single placed particle emitter streamed within a sector. Populated from the
// sector's .PMI placement resource (0x60 bytes in the original, offsets noted).
struct ParticleEmitterPlacement {
    uint64_t hashcode = 0;
    Matrix4x4f_t transform = Matrix4x4f_t::Identity();
    uint32_t color = 0xffffffff;
    ParticleInstance *pInstance = nullptr;
    uint32_t visibilityIndex = 0;
    float scale = 1.0f;
};

struct ParticleSectorGroup {
    Sector *pSector = nullptr;
    ParticleEmitterPlacement records[30] = {};
    int32_t numRecords = 0;
};

class Particles : public TDUDetail {
public:
    static constexpr int32_t kNumSectorSlots = 7;
    static constexpr int32_t kMaxRecordsPerGroup = 30;

public:
    Particles();
    ~Particles() override;

    bool initialize() override;
    void tick( float deltaTime ) override;
    void draw() override;
    void terminate() override;

    void installSector( Sector *pSector, void *pPMIResource );
    void uninstallSector( Sector *pSector );

private:
    int32_t findSectorSlot( const Sector *pSector ) const;
    void updateGroup( ParticleSectorGroup &group, float visibilityDistSquared );
    void releaseGroup( ParticleSectorGroup &group );
    static void releaseEmitter( ParticleEmitterPlacement &placement );

private:
    ParticleSectorGroup groups[kNumSectorSlots];
    float visibilityDistance;
};

extern Particles *gpParticlesDetail;
