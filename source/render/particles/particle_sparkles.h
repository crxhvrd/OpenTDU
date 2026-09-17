#pragma once

struct Particle;
struct ParticleInstance;

// One spark ribbon (0x60 bytes in the original pool).
struct Spark {
    Eigen::Vector4f Position;
    Eigen::Vector4f PreviousPosition;
    Eigen::Vector4f OlderPosition;
    Eigen::Vector4f Velocity;
    Eigen::Vector4f GroundPlane;
    float Lifetime;
    float Life;
    float PreviousLife;
    float Width;
};
static constexpr int32_t kNumSparksPerInstance = 400; // 0x2580 / 0x60

void InitializeSparkles( Particle *pParticle,
                         uint32_t &memoryUsed,
                         uint32_t memorySize );
void SimulateSparkles( Particle *pParticle, float deltaTime );
void ActivateSparkleInstance( ParticleInstance *pInstance, bool bReset );
