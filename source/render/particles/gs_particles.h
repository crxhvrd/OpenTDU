#pragma once

#include "shared.h"

#include "game/gs_base.h"

#include "filesystem/bank.h"
#include "render/2db.h"
#include "render/2dm.h"
#include "render/file_collection_2d.h"

#include "particle_instance.h"

#include <string>
#include <vector>

struct Material;

class GSParticles : public GameSystem {
public:
    const char *getName() const override { return "Service : Particles"; }

public:
    GSParticles();
    ~GSParticles();

    bool initialize( TestDriveGameInstance * ) override;
    void tick( float totalTime, float deltaTime ) override;
    void draw( float deltaTime ) override;
    void terminate() override;

#ifdef OTDU_USE_IMGUI
    void drawDebug( float deltaTime ) override;
#endif

    ParticleInstance *activateParticles( eParticleType id, bool bReset );
    ParticleInstance *activateParticles( uint64_t hashcode, bool bReset );
    // OTDU_ADDITION: data asks for names in another case than the bank's file
    // names (physics FX "prt_smokeCol" vs particles.bnk "prt_SmokeCol")
    ParticleInstance *activateParticlesByName( const char *pName, bool bReset );

    void stopAllParticleSystems();
    void resetInstancesInState( eParticleState state );
    Material *resolveParticleMaterial( uint64_t hashcode );

private:
    static constexpr int32_t kMaxParticle2D = 64;
    static constexpr uint32_t kParticleMemorySize = 0x12c000;

private:
    Bank bank;
    std::vector<Particle *> particles;
    uint32_t memoryUsed = 0;

    FileCollection2D collection2D;
    Render2DB render2DBs[kMaxParticle2D];
    Render2DM render2DMs[kMaxParticle2D];
    int32_t num2DB = 0;
    int32_t num2DM = 0;

private:
    Particle *findByID( eParticleType id );
    Particle *findByHashcode( uint64_t hashcode );
    void buildRegistry();
    void loadMaterials();
    Particle *createParticleType( const std::string &name,
                                  const uint8_t *pData,
                                  uint32_t size );
    void simulateParticle( Particle *pParticle, float deltaTime );
    void submitParticle( Particle *pParticle );
};

extern GSParticles *gpParticles;
