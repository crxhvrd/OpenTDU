#include "shared.h"
#include "particle_instance.h"

#include <cstdlib>
#include <cstring>

ParticleInstance *Particle::activateParticles( bool bReset ) {
    // FUN_0080d4a0
    ParticleInstance *pStopping = nullptr;
    ParticleInstance *pResult = nullptr;
    for ( ParticleInstance *pInstance : Instances ) {
        if ( pInstance->State == eParticleState::PS_Dead ) {
            pResult = pInstance;
            break;
        }
        if ( pInstance->State == eParticleState::PS_Killed ) {
            pStopping = pInstance;
        }
        pResult = pStopping;
    }

    if ( pResult != nullptr ) {
        pResult->Scale = 1.0f; // FLOAT_00f4165c
        pResult->Density = 1.0f;
        pResult->Speed = 1.0f;
        pResult->Color = Eigen::Vector4f( 1.0f, 1.0f, 1.0f, 1.0f );
        pResult->activate( bReset );
        pResult->State = eParticleState::PS_Alive;
    }
    return pResult;
}

void ParticleInstance::activate( bool bReset ) {
    // FUN_0080c970
    if ( pParticleInstanciated->SimuType ==
         eParticleSimulationType::PST_Sparkle ) {
        ActivateSparkleInstance( this, bReset );
        return;
    }

    Alpha = 1.0f; // FLOAT_00f4165c

    if ( pPrimaryEmitter != nullptr ) {
        pPrimaryEmitter[0x227] &= 0xfd;
        std::memset( pPrimaryEmitter + 0x220, 0, 4 );
    }

    // Clear the stop-requested flag (bit 0x4) on every emitter.
    uint8_t *pObject = file.getObject();
    if ( pObject != nullptr ) {
        uint8_t *pEmitters = file.arrayBase( prtf::kEmitterBase );
        const int32_t count =
            static_cast<int32_t>( file.arrayCount( prtf::kEmitterCount ) );
        if ( pEmitters != nullptr && count > 0 ) {
            for ( int32_t i = 0; i < count; i++ ) {
                pEmitters[i * prtf::kEmitterStride + 8] &= 0xfb;
            }
        }
    }

    if ( bReset ) {
        // The original advances the CRT RNG once before reseeding.
        rand();
        file.reseed( static_cast<uint32_t>( rand() & 0x7fff ) );
    }
}

void ParticleInstance::stop() {
    // FUN_0080c940
    if ( pParticleInstanciated->SimuType ==
         eParticleSimulationType::PST_Sparkle ) {
        return; // FUN_0080aa40
    }

    uint8_t *pObject = file.getObject();
    if ( pObject != nullptr ) {
        uint8_t *pEmitters = file.arrayBase( prtf::kEmitterBase );
        const int32_t count =
            static_cast<int32_t>( file.arrayCount( prtf::kEmitterCount ) );
        if ( pEmitters != nullptr && count > 0 ) {
            for ( int32_t i = 0; i < count; i++ ) {
                pEmitters[i * prtf::kEmitterStride + 8] |= 0x04;
            }
        }
    }
    StopTimer = 0.0f;
}
