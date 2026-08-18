#include "shared.h"
#include "entity_aircraft_path.h"

#include "audio/implementation/ed_audio_sample.h"
#include "entity_cinematic.h"
#include "filesystem/binary.h"
#include "game/scenaric/scenaric_bank.h"

static const edAudioSample *
ResolveSoundBlock( const uint8_t *pBlock,
                   ScenaricBank *pBank,
                   std::unique_ptr<edAudioSample> &storage ) {
    if ( pBlock == nullptr ) {
        return nullptr;
    }

    const uint64_t hashcode = *reinterpret_cast<const uint64_t *>( pBlock );
    if ( hashcode == 0 ) {
        return nullptr;
    }

    int32_t wavSize = 0;
    void *pWavData =
        pBank != nullptr ? pBank->getWavData( hashcode, &wavSize ) : nullptr;
    if ( pWavData == nullptr && gpScenaric != nullptr ) {
        pWavData = gpScenaric->findWavData( hashcode, &wavSize );
    }
    if ( pWavData == nullptr || wavSize <= 0 ) {
        OTDU_LOG_WARN(
            "Aircraft path: sound %016llx could not be resolved from the "
            "bank\n",
            (unsigned long long)hashcode );
        return nullptr;
    }

    auto pSample = std::make_unique<edAudioSample>();
    if ( !pSample->loadFromMemory( pWavData, wavSize ) ) {
        return nullptr;
    }

    storage = std::move( pSample );
    return storage.get();
}

EntityAircraftPath::EntityAircraftPath()
    : Entity(),
      pPathPoints{},
      NumPathPoints( 0 ),
      Type( 0 ),
      Floats{},
      pSoundOnGround( nullptr ),
      pSoundInAir( nullptr ),
      pCinematic( nullptr ),
      pSoundOnGroundSample( nullptr ),
      pSoundInAirSample( nullptr ),
      soundOnGroundStorage( nullptr ),
      soundInAirStorage( nullptr ) {
    // FUN_009da130
}

EntityAircraftPath::~EntityAircraftPath() {
    // FUN_00424120
}

Entity *EntityAircraftPath::getPathPointVolume( uint32_t index ) const {
    if ( index >= NumPathPoints || pPathPoints[index].pBlock == nullptr ) {
        return nullptr;
    }
    return *reinterpret_cast<Entity *const *>( pPathPoints[index].pBlock );
}

EntityCinematic *EntityAircraftPath::getCinematic() const {
    if ( pCinematic == nullptr ) {
        return nullptr;
    }

    Entity *pEntity = *reinterpret_cast<Entity *const *>( pCinematic );
    if ( pEntity == nullptr || pEntity->getType() != eEntityType::ET_CINE ) {
        return nullptr;
    }
    return static_cast<EntityCinematic *>( pEntity );
}

void EntityAircraftPath::loadResources( ScenaricBank *param_2 ) {
    // FUN_009c6b00
    for ( uint32_t i = 0; i < NumPathPoints; i++ ) {
        if ( gpScenaric->getVersion() > 0x20 &&
             pPathPoints[i].pBlock != nullptr ) {
            ResolveVolumeBlock( pPathPoints[i].pBlock, param_2 );
        }
    }

    pSoundOnGroundSample =
        ResolveSoundBlock( pSoundOnGround, param_2, soundOnGroundStorage );
    pSoundInAirSample =
        ResolveSoundBlock( pSoundInAir, param_2, soundInAirStorage );

    if ( pCinematic != nullptr ) {
        ResolveEntityRef( pCinematic, param_2 );
    }
}

void EntityAircraftPath::deserialize( BinaryReader *param_2 ) {
    // FUN_009c69e0
    Entity::deserialize( param_2 );

    NumPathPoints =
        DecodeBytecodePairArray( param_2, pPathPoints, kMaxPathPoints );
    if ( NumPathPoints > kMaxPathPoints ) {
        NumPathPoints = kMaxPathPoints;
    }

    Type = *reinterpret_cast<int32_t *>( param_2->pCursor );
    param_2->pCursor += sizeof( int32_t );

    for ( int32_t i = 0; i < 4; ++i ) {
        Floats[i] = *reinterpret_cast<float *>( param_2->pCursor );
        param_2->pCursor += sizeof( float );
    }

    int32_t n0 = *reinterpret_cast<int32_t *>( param_2->pCursor );
    param_2->pCursor += sizeof( int32_t );
    if ( n0 != 0 ) {
        pSoundOnGround = param_2->pCursor;
        param_2->pCursor += 8;
    }

    int32_t n1 = *reinterpret_cast<int32_t *>( param_2->pCursor );
    param_2->pCursor += sizeof( int32_t );
    if ( n1 != 0 ) {
        pSoundInAir = param_2->pCursor;
        param_2->pCursor += 8;
    }

    pCinematic = param_2->pCursor;
    param_2->pCursor += 8;
}
