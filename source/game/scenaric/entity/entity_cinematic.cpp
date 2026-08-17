#include "shared.h"
#include "entity_cinematic.h"

#include <cctype>

#include "core/maths_helper.h"
#include "filesystem/bank.h"
#include "filesystem/binary.h"
#include "gs_timer.h"
#include "game/game_mechanism.h"
#include "game/gm_scene.h"
#include "game/cinematic/mng_cinematics.h"
#include "game/scenaric/gs_scenaric.h"
#include "game/scenaric/entity/entity_avatar.h"
#include "game/scenaric/entity/entity_pedestrian.h"
#include "game/scenaric/mng_actor.h"
#include "game/scenaric/scenaric_bank.h"
#include "game/scenaric/spots.h"
#include "render/animation/clip.h"
#include "render/animation/skeleton.h"
#include "world/gs_world_streaming.h"

namespace {
    void ResolveCinematicEntry( BankInstance &bank,
                                const std::string &name,
                                ParsedBankEntry &outEntry ) {
        bank.getEntry( const_cast<char *>( name.c_str() ), outEntry );
        if ( outEntry.pData != nullptr ) {
            return;
        }

        std::string suffix = name;
        if ( suffix.size() > 2 && suffix[0] == 'C' && suffix[1] == '_' ) {
            suffix = suffix.substr( 2 );
        }
        for ( char &c : suffix ) {
            c = static_cast<char>(
                ::toupper( static_cast<unsigned char>( c ) ) );
        }

        const int32_t count = bank.getNumElements();
        for ( int32_t i = 0; i < count; i++ ) {
            ParsedBankEntry entry;
            bank.getEntry( i, entry );
            if ( entry.pData == nullptr || entry.Size < 4 ||
                 *reinterpret_cast<const uint32_t *>( entry.pData ) !=
                     0x214e4943u /* "CIN!" */ ) {
                continue;
            }
            std::string base = entry.Filename;
            const size_t slash = base.find_last_of( "\\/" );
            if ( slash != std::string::npos ) {
                base = base.substr( slash + 1 );
            }
            const size_t dot = base.find_last_of( '.' );
            if ( dot != std::string::npos ) {
                base = base.substr( 0, dot );
            }
            for ( char &c : base ) {
                c = static_cast<char>(
                    ::toupper( static_cast<unsigned char>( c ) ) );
            }
            if ( base.size() >= suffix.size() &&
                 base.compare( base.size() - suffix.size(), suffix.size(),
                               suffix ) == 0 ) {
                outEntry = entry; // matched; pData points into the bank buffer
                return;
            }
        }
    }

    void PlayBestAmbientClip( Entity *pEntity, ScenaricBank *pBank ) {
        if ( pEntity == nullptr || pBank == nullptr ||
             pEntity->getType() != eEntityType::ET_PTDR ) {
            return;
        }
        EntityPedestrian *pPed = static_cast<EntityPedestrian *>( pEntity );
        void *pSkeletonBlob = pPed->debugGetCharSkeleton();
        if ( pSkeletonBlob == nullptr ) {
            return;
        }
        SkeletonInstance skeleton;
        skeleton.wrapBlob( pSkeletonBlob );
        ClipInstance *pClip = pBank->debugPickCalmestClip( &skeleton, 0.3f );
        if ( pClip != nullptr ) {
            pPed->playAnimationClip( pClip );
        }
    }
} // namespace

EntityCinematic::EntityCinematic()
    : Entity(),
      cinematicFlags( 0 ),
      cinematicPlaybackFlags( 0 ),
      cinematicPlaybackFlags2( 3 ),
      StartTime( 0.0f ),
      Time( 0.0f ),
      CineTotalTime( 0.0f ),
      FadeTime( 0.0f ),
      pSCBank( nullptr ),
      pSpotWorld( nullptr ),
      pCinData( nullptr ),
      cinDataSize( 0 ),
      bPaused( false ),
      playbackSpeed( 1.0f ),
      bLoop( false ),
      bHideFrontEnd( false ),
      bOnFinalFrame( false ),
      bSkipped( false ),
      bDontSwitchCamera( false ),
      bInitialized( false ),
      bForceStop( false ),
      bStopGuard( false ),
      bStackActive( false ),
      activeGameMode( GM_FreeRide ) {
    // FUN_007e80d0
}

EntityCinematic::~EntityCinematic() {
    // FUN_00425ae0
}

void EntityCinematic::initialize() {
    // FUN_007e9530
    pCinData = nullptr;
    cinDataSize = 0;
    cinematicBuffer.clear();
    bInitialized = true;

    if ( pSCBank == nullptr || cutsceneName.empty() ) {
        return;
    }

    ParsedBankEntry entry;
    ResolveCinematicEntry( pSCBank->getBank(), cutsceneName, entry );
    if ( entry.pData == nullptr ) {
        return;
    }
    pCinData = entry.pData;
    cinDataSize = static_cast<uint32_t>( entry.Size );
    entry.Clear();

    const uint32_t bufferSize = ExtractBufferSize( pCinData, cinDataSize );
    if ( bufferSize != 0 ) {
        cinematicBuffer.assign( bufferSize, 0 );
    }
}

void EntityCinematic::loadResources( ScenaricBank *param_2 ) {
    // FUN_007e8e80
    if ( pMechanism != nullptr ) {
        pMechanism->loadResources( param_2 );
    }
    resolveSoundReference();
    pSCBank = param_2;
}

void EntityCinematic::initializeInstance( void *param_2, void *param_3 ) {
    // FUN_007e79c0
    // NOP
}

void EntityCinematic::unloadResources() {
    // FUN_007e8dd0
    cinematicPlayer.reset();
    cinematicBuffer.clear();
    pCinData = nullptr;
    cinDataSize = 0;
    bInitialized = false;
    if ( pMechanism != nullptr ) {
        pMechanism->terminate();
        delete pMechanism;
        pMechanism = nullptr;
    }
}

void EntityCinematic::tick() {
    // FUN_007e9320
    Entity::tick();

    const float dt =
        ( gpTimer != nullptr ) ? gpTimer->GameDeltaTime : ( 1.0f / 60.0f );

    if ( isPlaying() ) {
        const float totalTime = cinematicPlayer.getTotalTime();

        const float advance = playbackSpeed * dt;
        if ( Time + advance > 0.0f && !bPaused ) {
            Time += advance;
        }

        const bool reachedEnd = totalTime < Time;
        if ( !reachedEnd ) {
            cinematicPlayer.tick( Time, dt );
        } else if ( !bLoop ) {
            stop( true );
            return;
        } else {
            Time = 0.0f;
        }
    }

    if ( ( cinematicFlags & 0x4u ) == 0 ) {
        if ( ( cinematicFlags & 0x1u ) != 0 ) {
            begin();
        }
    } else if ( ( cinematicFlags & 0x2u ) != 0 ) {
        end();
    }
}

void EntityCinematic::seek( float time ) {
    Time = time;
    if ( Time < 0.0f ) {
        Time = 0.0f;
    }
    const float totalTime = cinematicPlayer.getTotalTime();
    if ( totalTime > 0.0f && Time > totalTime ) {
        Time = totalTime;
    }
    cinematicPlayer.tick( Time, 0.0f );
}

void EntityCinematic::setPlaybackAnchor( const Eigen::Vector3f &pos ) {
    cinematicPlayer.setBaseTranslation( pos );
}

void EntityCinematic::deserialize( BinaryReader *pReader ) {
    // FUN_007e8400
    Entity::deserialize( pReader );

    cinematicPlaybackFlags = ReadStreamU32( pReader );
    if ( gpScenaric->getVersion() > 0xc ) {
        cinematicPlaybackFlags2 = ReadStreamU32( pReader );
    }

    OTDU_UNIMPLEMENTED; // TODO:
    cutsceneName = name;
}

void EntityCinematic::reset() {
    // FUN_007e9720
    bPaused = false;
    if ( ( cinematicFlags & 0x4u ) != 0 ) {
        Time = 0.0f;
    }
    start( true );
}

void EntityCinematic::instanciate( MngActor *pMngActor, Entity *param_1 ) {
    // FUN_007e8200
    Entity::instanciate( pMngActor, param_1 );

    EntityCinematic *pInstance = static_cast<EntityCinematic *>( param_1 );
    pInstance->cinematicFlags = cinematicFlags;
    pInstance->cinematicPlaybackFlags = cinematicPlaybackFlags;
    pInstance->cinematicPlaybackFlags2 = cinematicPlaybackFlags2;
    pInstance->StartTime = StartTime;
    pInstance->CineTotalTime = CineTotalTime;
    pInstance->pSCBank = pSCBank;
    pInstance->cutsceneName = cutsceneName;
    pInstance->pSpotWorld = pSpotWorld;
    pInstance->bLoop = bLoop;
    pInstance->bHideFrontEnd = bHideFrontEnd;
    pInstance->bSkipped = bSkipped;
    pInstance->playbackSpeed = playbackSpeed;

    OTDU_UNIMPLEMENTED;
}

void EntityCinematic::start( bool bBeginNow ) {
    // FUN_007e91f0
    if ( gpMngCinematics == nullptr || !gpMngCinematics->isValid() ||
         pSCBank == nullptr || !pSCBank->isLoaded() ) {
        stop( true );
        return;
    }

    // Already ending (bit 2) or already playing (bit 0): nothing to do.
    if ( ( cinematicFlags & 0x4u ) != 0 || ( cinematicFlags & 0x1u ) != 0 ) {
        return;
    }

    OTDU_UNIMPLEMENTED; // TODO: INPUT

    activeGameMode = GM_FreeRide;
    bSkipped = false;
    cinematicFlags |= 0x1u;

    OTDU_UNIMPLEMENTED; // TODO: GAMEMODE

    if ( !bBeginNow || begin() ) {
        if ( gpMngCinematics == nullptr ) {
            stop( true );
            return;
        }
        gpMngCinematics->addPlayingCinematic( this );
    }
}

void EntityCinematic::stop( bool bEndNow ) {
    // FUN_007e8d10
    const uint32_t flags = cinematicFlags;
    if ( ( ( flags & 0x4u ) == 0 && !bStopGuard ) || bForceStop ) {
        if ( ( flags & 0x1u ) != 0 ) {
            cinematicFlags = flags & ~0x1u;
            OTDU_UNIMPLEMENTED; // TODO: GAMEMODE
        }
        if ( bStackActive ) {
            OTDU_UNIMPLEMENTED;
            bStackActive = false;
        }
    } else {
        cinematicFlags = flags | 0x2u;
        OTDU_UNIMPLEMENTED; // TODO: GAMEMODE
        if ( bEndNow ) {
            end();
            return;
        }
    }
}

bool EntityCinematic::calculateSpotMatrix( SpotMap *pSpot,
                                           Matrix4x4f_t &outMatrix ) {
    // FUN_007e7dd0
    outMatrix = Matrix4x4f_t::Identity();

    SpotSector *pSector =
        gpScenaric->getSpotList().getSpotSector( pSpot->Hashcode );
    if ( pSector == nullptr ) {
        return false;
    }

    // TODO: confirm the full XYZ-euler against more spot assets.
    const Eigen::Quaternionf rotation(
        Eigen::AngleAxisf( pSector->Angle, Eigen::Vector3f::UnitY() ) );
    outMatrix = BuildRotationMatrix( rotation );

    Eigen::Vector4f shift = gpWorldStreaming->isEnabled()
                                ? gpWorldStreaming->getPosition()
                                : Eigen::Vector4f::Zero();
    outMatrix( 3, 0 ) = pSpot->X - shift.x();
    outMatrix( 3, 1 ) = pSpot->Y - shift.y();
    outMatrix( 3, 2 ) = pSpot->Z - shift.z();
    outMatrix( 3, 3 ) = 1.0f;
    return true;
}

bool EntityCinematic::launchSpotCine( SpotMap *pSpot, float startTime ) {
    // FUN_007e96c0
    Matrix4x4f_t mat;
    calculateSpotMatrix( pSpot, mat );
    pSpotWorld = pSpot;
    mat( 3, 1 ) += startTime;

    cinematicPlayer.setBaseTranslation(
        Eigen::Vector3f( mat( 3, 0 ), mat( 3, 1 ), mat( 3, 2 ) ) );

    start( true );
    return true;
}

bool EntityCinematic::begin() {
    // FUN_007e8f90
    if ( pCinData == nullptr || cinematicBuffer.empty() ||
         pSCBank == nullptr ) {
        stop( true );
        return false;
    }

    if ( !cinematicPlayer.createInterface( pCinData, cinDataSize,
                                           cinematicBuffer.data(),
                                           &pSCBank->getBank() ) ) {
        return false;
    }

    CinResCollection::bQuiet = true;

    if ( gpActiveGMScene != nullptr ) {
        const int32_t numActors = cinematicPlayer.getActorCount();
        for ( int32_t i = 0; i < numActors; i++ ) {
            Entity *pTemplate =
                static_cast<Entity *>( cinematicPlayer.getActorEntity( i ) );
            if ( pTemplate == nullptr ) {
                continue;
            }
            if ( Entity::IsAvatarEntityType( pTemplate ) ) {
                gpActiveGMScene->getActorManager().registerActor( pTemplate );
                continue;
            }
            Entity *pInstance =
                gpActiveGMScene->getActorManager().instanciateActor(
                    pTemplate );
            if ( pInstance != nullptr ) {
                cinematicPlayer.setActorEntity( i, pInstance );
                PlayBestAmbientClip( pInstance, pSCBank );
            } else {
                cinematicPlayer.setActorEntity( i, nullptr );
            }
        }
    }

    OTDU_UNIMPLEMENTED;

    CineTotalTime = cinematicPlayer.getTotalTime();
    Time = StartTime;
    cinematicFlags = ( cinematicFlags & 0xfffffffcu ) | 4u;
    return true;
}

void EntityCinematic::end() {
    // FUN_007e8840
    if ( gpActiveGMScene != nullptr ) {
        MngActor &sceneActors = gpActiveGMScene->getActorManager();
        const int32_t count = cinematicPlayer.getActorCount();
        for ( int32_t i = 0; i < count; i++ ) {
            Entity *pActor =
                static_cast<Entity *>( cinematicPlayer.getActorEntity( i ) );
            if ( pActor != nullptr ) {
                if ( Entity::IsAvatarEntityType( pActor ) ) {
                    sceneActors.removeActorFromList( pActor );
                    if ( gpScenaric != nullptr ) {
                        gpScenaric->getAvatarManager().releaseAvatar(
                            static_cast<EntityAvatar *>( pActor ) );
                    }
                } else {
                    MngActor::Uninstanciate( &sceneActors, pActor );
                }
                cinematicPlayer.setActorEntity( i, nullptr );
            }
        }
    }
    cinematicFlags = ( cinematicFlags & 0xfffffff8u ) | 0x10u;
    Time = 0.0f;
    bStopGuard = false;
    if ( gpMngCinematics != nullptr ) {
        gpMngCinematics->removeCinematic( this );
    }
}
