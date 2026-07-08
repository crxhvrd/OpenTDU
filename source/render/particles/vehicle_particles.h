#pragma once

#include "shared.h"

#include <array>

struct ParticleInstance;
class VehicleBase;

class VehicleParticles {
public:
    VehicleParticles();
    ~VehicleParticles();

    void update( VehicleBase *param_2 );
    void reset();
    void setState( bool param_2 );

private:
    static constexpr const uint32_t kNumInstancePerType = 4;

private:
    std::array<ParticleInstance *, kNumInstancePerType> burnParticles;
    std::array<ParticleInstance *, kNumInstancePerType> smokeParticles;
    std::array<ParticleInstance *, kNumInstancePerType> dustParticles;
    std::array<ParticleInstance *, kNumInstancePerType> gravelParticles;
    std::array<ParticleInstance *, kNumInstancePerType> grassParticles;
    std::array<ParticleInstance *, kNumInstancePerType> startExhaustParticles;

private:
    void stopAll( bool bImmediate );
};
