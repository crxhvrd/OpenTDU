#include "shared.h"
#include "entity_life_set.h"

#include "core/random/bcpl.h"
#include "filesystem/binary.h"
#include "game/game_mechanism.h"
#include "game/scenaric/entity/entity_cinematic.h"
#include "game/scenaric/mng_actor.h"
#include "game/scenaric/mng_life_set.h"
#include "game/scenaric/scenaric_bank.h"
#include "game/scenaric/gs_scenaric.h"
#include "world/gs_world_streaming.h"

void EntityLifeSet::LifeCinematic::deserialize( BinaryReader *pReader ) {
    // FUN_007dddd0
    bFocusOn = ReadStreamU32( pReader ) != 0;
    bRandomTimeStart = ReadStreamU32( pReader ) != 0;
    bAlignToTerrain = ReadStreamU32( pReader ) != 0;
    Orientation = ReadStreamU32( pReader );

    NumPositions = ReadStreamU32( pReader );
    for ( uint32_t i = 0; i < NumPositions; i++ ) {
        pPositions[i].pPosMatrix =
            reinterpret_cast<SerializedTransform *>( pReader->pCursor );
        pReader->pCursor += sizeof( SerializedTransform );
    }

    CinematicResource.pBinRefCinematic =
        reinterpret_cast<EntityCinematic *>( pReader->pCursor );
    pReader->pCursor += 8;

    if ( gpScenaric->getVersion() < 0x22 ) {
        AvatarStyleIndex = 2;
    } else {
        AvatarStyleIndex = static_cast<int32_t>( ReadStreamU32( pReader ) );
    }
}

void EntityLifeSet::LifeSetCinematic::prepare( ScenaricBank *pBank ) {
    // FUN_007de0e0
    for ( uint32_t i = 0; i < NumLifeCinematics; i++ ) {
        SerializedCinematic &ref = pLifeCinematics[i].CinematicResource;

        const uint32_t *pHashcodeBlob =
            reinterpret_cast<const uint32_t *>( ref.pBinRefCinematic );
        Entity *pResolved = nullptr;
        if ( pHashcodeBlob != nullptr ) {
            const uint64_t hashcode =
                static_cast<uint64_t>( pHashcodeBlob[0] ) |
                ( static_cast<uint64_t>( pHashcodeBlob[1] ) << 32 );
            if ( hashcode != 0 ) {
                if ( pBank != nullptr ) {
                    pResolved = pBank->getEntityByHashcode( hashcode );
                }
                if ( pResolved == nullptr && gpScenaric != nullptr ) {
                    pResolved = gpScenaric->findEntityByHashcode( hashcode );
                }
            }
        }
        ref.pBinRefCinematic = static_cast<EntityCinematic *>( pResolved );
    }
}

EntityLifeSet::LifeSetBank::LifeSetBank() : NumLifeSetCinematics( 0 ) {
    // FUN_007ddf40
}

void EntityLifeSet::LifeSetBank::deserialize( BinaryReader *pReader ) {
    // FUN_00427a90
    NumLifeSetCinematics = ReadStreamU32( pReader );
    for ( uint32_t i = 0; i < NumLifeSetCinematics; i++ ) {
        LifeSetCinematic &exclusive = pLifeSetCinematics[i];
        const uint32_t numLifeCinematics = ReadStreamU32( pReader );
        for ( uint32_t j = 0; j < numLifeCinematics; j++ ) {
            exclusive.pLifeCinematics[j].deserialize( pReader );
        }
        exclusive.NumLifeCinematics = numLifeCinematics;
    }
}

void EntityLifeSet::LifeSetBank::prepare( ScenaricBank *pBank ) {
    // FUN_007de200
    for ( uint32_t i = 0; i < NumLifeSetCinematics; i++ ) {
        pLifeSetCinematics[i].prepare( pBank );
    }
}

EntityLifeSet::EntityLifeSet()
    : Entity(),
      bLoaded( false ),
      bPlaying( false ),
      cinematicIndexRange{ 0xffffffff, 0xffffffff },
      bank(),
      flags( 0 ),
      numCinematicsPlaying( 0 ) {
    // FUN_007ddfe0
    for ( EntityCinematic *&pCinematic : pCurrentlyPlayingCinematics ) {
        pCinematic = nullptr;
    }
}

EntityLifeSet::~EntityLifeSet() {}

void EntityLifeSet::initialize() {
    // FUN_007de1e0
    Entity::initialize();
}

void EntityLifeSet::loadResources( ScenaricBank *pBank ) {
    // FUN_007de4c0
    if ( pMechanism != nullptr ) {
        pMechanism->loadResources( pBank );
    }
    resolveSoundReference();
    bank.prepare( pBank );
}

void EntityLifeSet::tick( PlayerGameMech *, float, float ) {
    // FUN_007de810
    if ( bLoaded && !bPlaying ) {
        if ( gpMngLifeSet != nullptr ) {
            gpMngLifeSet->addLifeSet( this );
        }
        bPlaying = true;
    }
}

void EntityLifeSet::unknownFunc() {
    // FUN_007de7a0
    if ( gpMngLifeSet != nullptr ) {
        if ( !bLoaded ) {
            gpMngLifeSet->removeLifeSet( this );
        } else if ( gpMngLifeSet->getActiveLifeSet() != nullptr ) {
            gpMngLifeSet->unbindActiveLifeSet();
            Entity::unknownFunc();
            return;
        }
    }
    Entity::unknownFunc();
}

void EntityLifeSet::deserialize( BinaryReader *pReader ) {
    // FUN_007de060
    Entity::deserialize( pReader );

    bLoaded = ReadStreamU32( pReader ) != 0;
    cinematicIndexRange[0] = ReadStreamU32( pReader );
    cinematicIndexRange[1] = ReadStreamU32( pReader );
    bank.deserialize( pReader );
    flags = ReadStreamU32( pReader );
}

void EntityLifeSet::instanciate( MngActor *pMngActor, Entity *param_1 ) {
    // FUN_007ddee0
    Entity::instanciate( pMngActor, param_1 );

    EntityLifeSet *pInstance = static_cast<EntityLifeSet *>( param_1 );
    pInstance->cinematicIndexRange[0] = cinematicIndexRange[0];
    pInstance->cinematicIndexRange[1] = cinematicIndexRange[1];
    pInstance->bank = bank;
    pInstance->flags = flags;
}

void EntityLifeSet::deinstanciate() {
    // FUN_007ddfc0
    Entity::deinstanciate();
}

void EntityLifeSet::unknownFunc2( void *param_2 ) {
    // FUN_007ddc90
    flags = *static_cast<uint32_t *>( param_2 );
}

uint32_t EntityLifeSet::stopPlaying() {
    // FUN_007de240
    const uint32_t stopped = numCinematicsPlaying;
    for ( uint32_t i = 0; i < numCinematicsPlaying; i++ ) {
        EntityCinematic *pCinematic = pCurrentlyPlayingCinematics[i];
        pCinematic->stop( true );
        MngActor::Uninstanciate( nullptr, pCinematic );
    }
    numCinematicsPlaying = 0;
    return stopped;
}

bool EntityLifeSet::startPlayingLifeCinematic( LifeCinematic *pLifeCinematic,
                                               MngLifeSet *pMngLifeSet ) {
    // FUN_007de2b0
    Entity *pTemplate = static_cast<Entity *>(
        pLifeCinematic->CinematicResource.pBinRefCinematic );
    if ( pTemplate == nullptr ) {
        return false;
    }

    const Matrix4x4f_t &model = getModelMatrix();
    const Eigen::Vector4f shift = gpWorldStreaming->isEnabled()
                                      ? gpWorldStreaming->getPosition()
                                      : Eigen::Vector4f::Zero();
    float anchorX = model( 3, 0 ) - shift.x();
    float anchorY = model( 3, 1 ) - shift.y();
    float anchorZ = model( 3, 2 ) - shift.z();
    if ( pLifeCinematic->NumPositions != 0 ) {
        uint32_t index = RandomBelow( gpScenaric->getRandomGenerator(),
                                      pLifeCinematic->NumPositions );
        if ( index >= pLifeCinematic->NumPositions ) {
            index = pLifeCinematic->NumPositions - 1;
        }
        const SerializedTransform *pSpawn =
            pLifeCinematic->pPositions[index].pPosMatrix;
        if ( pSpawn != nullptr ) {
            anchorX += pSpawn->PositionX;
            anchorY += pSpawn->PositionY;
            anchorZ += pSpawn->PositionZ;
        }
    }

    Entity *pInstance =
        EntityFactory::DoAction( nullptr, pTemplate->getType(), nullptr,
                                 eInstantiationFlag::IF_CreateEntity )
            .pEntity;
    if ( pInstance == nullptr ) {
        return false;
    }
    pTemplate->instanciate( nullptr, pInstance );
    pInstance->initializeInstance( nullptr, pTemplate );
    pInstance->initialize();

    EntityCinematic *pCinematic = static_cast<EntityCinematic *>( pInstance );
    pCinematic->setPlaybackAnchor(
        Eigen::Vector3f( anchorX, anchorY, anchorZ ) );
    pCinematic->setLoop( true );
    if ( pLifeCinematic->bRandomTimeStart ) {
        pCinematic->setStartTime(
            RandomUnit( gpScenaric->getRandomGenerator() ) );
    }
    // TODO: CINEMATICS — set the pedestrian style (AvatarStyleIndex)
    pCinematic->start( true );

    pCurrentlyPlayingCinematics[numCinematicsPlaying] = pCinematic;
    numCinematicsPlaying++;
    return true;
}

uint32_t EntityLifeSet::startPlaying( MngLifeSet *pMngLifeSet,
                                      uint32_t budget ) {
    // FUN_007de510
    const uint32_t minPlayed = cinematicIndexRange[0];
    const uint32_t maxPlayed = cinematicIndexRange[1];

    uint32_t target = minPlayed + RandomBelow( gpScenaric->getRandomGenerator(),
                                               ( maxPlayed - minPlayed ) + 1 );
    if ( static_cast<int32_t>( target ) < static_cast<int32_t>( minPlayed ) ) {
        target = minPlayed;
    }
    if ( static_cast<int32_t>( maxPlayed ) < static_cast<int32_t>( target ) ) {
        target = maxPlayed;
    }

    if ( budget < target ) {
        const uint32_t granted =
            pMngLifeSet->updateStreaming( target - budget );
        if ( granted + budget <= target ) {
            target = granted + budget;
        }
    }

    if ( target == 0 ) {
        return 0;
    }

    LifeCinematic *candidates[10];
    uint32_t numCandidates = 0;
    for ( uint32_t group = 0; group < bank.NumLifeSetCinematics; group++ ) {
        LifeSetCinematic &exclusive = bank.pLifeSetCinematics[group];
        if ( exclusive.NumLifeCinematics != 0 ) {
            uint32_t index = RandomBelow( gpScenaric->getRandomGenerator(),
                                          exclusive.NumLifeCinematics );
            if ( static_cast<int32_t>( index ) < 0 ) {
                index = 0;
            }
            if ( exclusive.NumLifeCinematics - 1 < index ) {
                index = exclusive.NumLifeCinematics - 1;
            }
            candidates[numCandidates++] = &exclusive.pLifeCinematics[index];
        }
    }

    uint32_t playedCount = 0;
    uint32_t deferredCount = 0;
    bool handled[10] = {};
    for ( uint32_t i = 0; i < numCandidates; i++ ) {
        LifeCinematic *pCandidate = candidates[i];
        const uint32_t orientationBit = 1u << ( flags & 0x1f );
        if ( ( pCandidate->Orientation & orientationBit ) == 0 ) {
            handled[i] = true;
        } else if ( !pCandidate->bFocusOn ) {
            deferredCount++;
            handled[i] = false;
        } else {
            startPlayingLifeCinematic( pCandidate, pMngLifeSet );
            playedCount++;
            handled[i] = true;
            if ( playedCount == target ) {
                return playedCount;
            }
        }
    }

    while ( playedCount < target && deferredCount != 0 ) {
        const uint32_t lastIndex = deferredCount - 1;
        uint32_t pick =
            RandomBelow( gpScenaric->getRandomGenerator(), deferredCount );
        if ( static_cast<int32_t>( pick ) < 0 ) {
            pick = 0;
        }
        if ( static_cast<int32_t>( lastIndex ) <
             static_cast<int32_t>( pick ) ) {
            pick = lastIndex;
        }

        uint32_t seen = 0;
        uint32_t slot = 0;
        while ( seen < pick || ( seen == pick && handled[slot] ) ) {
            if ( !handled[slot] ) {
                seen++;
            }
            slot++;
        }

        deferredCount--;
        handled[slot] = true;
        if ( startPlayingLifeCinematic( candidates[slot], pMngLifeSet ) ) {
            playedCount++;
        }
    }

    return playedCount;
}
