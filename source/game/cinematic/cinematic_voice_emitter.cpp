#include "shared.h"
#include "cinematic_voice_emitter.h"

#include "cin_res_collection.h"
#include "gs_cinematics.h"
#include "ai/ai_pool_traffic.h"
#include "audio/gs_audio.h"
#include "audio/implementation/audio_dispatcher_playback.h"
#include "audio/implementation/ed_audio_sample.h"
#include "config/gs_config.h"
#include "filesystem/gs_file.h"
#include "game/avatar/avatar_clothes.h"
#include "game/avatar/avatar_part.h"
#include "game/scenaric/entity/entity_avatar.h"
#include "game/scenaric/entity/entity_extra_challenge.h"
#include "game/scenaric/gs_scenaric.h"
#include "player_data/gs_playerdata.h"

static constexpr float kDefaultVoiceVolume = 1.0f; // FLOAT_00f4165c
static constexpr const char *kFallbackVoiceLanguage = "US";
static constexpr const char *kCarRentalVoiceName =
    "V_M_CarEscort_CarRental_004";

static bool ResolveAvatarVoiceGender( const char *pVoiceName,
                                      CinResCollection *pOwner,
                                      int32_t *pOutGender ) {
    // FUN_007decb0
    const char *pSeparator = strchr( pVoiceName, '_' );
    if ( pSeparator == nullptr || pOwner == nullptr ) {
        return false;
    }

    pSeparator = strchr( pSeparator + 1, '_' );
    if ( pSeparator == nullptr ||
         strncmp( pSeparator + 1, "Avatar", 6 ) != 0 ) {
        return false;
    }

    Entity *pAvatar =
        static_cast<Entity *>( pOwner->findActorEntity( "Avatar" ) );
    if ( pAvatar == nullptr || pAvatar->getEntityIndex() != 1 ) {
        return false;
    }

    // FUN_007f73d0
    PooledAvatarEntry *pEntry =
        static_cast<EntityAvatar *>( pAvatar )->getPooledEntry();
    *pOutGender =
        ( pEntry != nullptr ) ? pEntry->Body.getAvatarPart()->Gender : 0;
    return true;
}

static void RandomizeArrestVoiceLine( char *pVoiceName ) {
    // FUN_007deb70
    char *pNumber = strrchr( pVoiceName, '_' );
    if ( pNumber != nullptr && pNumber[1] != '\0' && pNumber[2] != '\0' &&
         pNumber[3] != '\0' ) {
        pNumber[3] = static_cast<char>(
            '0' + RandomRange( gpScenaric->getRandomGenerator(), 1, 8 ) );
    }
}

static bool ApplyAvatarVoiceGender( char *pVoiceName,
                                    CinResCollection *pOwner ) {
    // FUN_007ded30
    int32_t gender = 0;
    if ( !ResolveAvatarVoiceGender( pVoiceName, pOwner, &gender ) ) {
        return false;
    }

    char *pGender = strchr( pVoiceName, '_' );
    if ( pGender == nullptr ) {
        return true;
    }
    pGender++;
    *pGender = ( gender != 0 ) ? 'F' : 'M';

    if ( strncmp( pGender + 1, "_Avatar_Arrest", 14 ) == 0 ) {
        RandomizeArrestVoiceLine( pVoiceName );
    }
    return true;
}

static void BuildExtraChallengeVoiceName( char *pOutName,
                                          const char *pVoiceName,
                                          const Entity *pChallenge ) {
    // FUN_007de960
    const uint32_t voiceType =
        static_cast<const EntityExtraChallenge *>( pChallenge )
            ->getAvatarVoiceType();

    const char *pPrefix = nullptr;
    switch ( pChallenge->getType() ) {
    case eEntityType::ET_SHOG: {
        static constexpr const char *kPrefixes[] = { nullptr,
                                                     "V_F_Shopping_Trucker_",
                                                     "V_F_Shopping_Seductive_",
                                                     "V_F_Shopping_Stuckup_",
                                                     "V_F_Shopping_Foreign_",
                                                     "V_F_Shopping_Hurry_" };
        if ( voiceType == 0 ) {
            strcpy( pOutName, pVoiceName );
            return;
        }
        if ( voiceType < sizeof( kPrefixes ) / sizeof( kPrefixes[0] ) ) {
            pPrefix = kPrefixes[voiceType];
        }
        break;
    }
    case eEntityType::ET_HIHI:
        if ( voiceType == 0 ) {
            strcpy( pOutName, pVoiceName );
            return;
        }
        if ( voiceType == 1 ) {
            pPrefix = "V_M_HitchHiker_Idiot_";
        } else if ( voiceType == 2 ) {
            pPrefix = "V_M_HitchHiker_Hippy_";
        }
        break;
    case eEntityType::ET_TRAN:
        if ( strcmp( pVoiceName, kCarRentalVoiceName ) == 0 ) {
            strcpy( pOutName, pVoiceName );
            return;
        }
        if ( voiceType == 0 ) {
            pPrefix = "V_M_Errands_Character1_";
        } else if ( voiceType == 1 ) {
            pPrefix = "V_M_Errands_Character2_";
        } else if ( voiceType == 2 ) {
            pPrefix = "V_M_Errands_Character3_";
        }
        break;
    case eEntityType::ET_CVOY:
        if ( strcmp( pVoiceName, kCarRentalVoiceName ) == 0 ) {
            strcpy( pOutName, pVoiceName );
            return;
        }
        if ( voiceType == 0 ) {
            strcpy( pOutName, "V_M_CarEscort_CarDealer_001" );
            return;
        }
        if ( voiceType == 1 ) {
            strcpy( pOutName, "V_M_CarEscort_CarRental_002" );
            return;
        }
        break;
    default:
        break;
    }

    int32_t length = 0;
    if ( pPrefix != nullptr ) {
        length = sprintf( pOutName, "%s", pPrefix );
    }
    const char *pLineNumber = strrchr( pVoiceName, '_' );
    pLineNumber = ( pLineNumber != nullptr ) ? pLineNumber + 1 : pVoiceName;
    strcpy( pOutName + length, pLineNumber );
}

CinematicVoiceEmitter::CinematicVoiceEmitter()
    : State( CVES_None ),
      pInstance( nullptr ),
      pSample( nullptr ),
      Volume( kDefaultVoiceVolume ),
      pOwnerAvatar( nullptr ),
      Name{} {}

CinematicVoiceEmitter::~CinematicVoiceEmitter() { uninstall(); }

void CinematicVoiceEmitter::install( char *pVoiceName,
                                     const float volume,
                                     CinResCollection *pOwner ) {
    // FUN_007dee40
    pInstance = nullptr;
    if ( pVoiceName == nullptr ) {
        return;
    }

    strncpy( Name, pVoiceName, kNameLength - 1 );
    Name[kNameLength - 1] = '\0';
    Volume = volume;

    const bool bAvatarVoice = ApplyAvatarVoiceGender( pVoiceName, pOwner );
    const char *pFileName = pVoiceName;

    char challengeVoiceName[0x104] = {};
    Entity *pChallenge = gpScenaric->getActiveExtraChallenge();
    if ( pChallenge != nullptr && !bAvatarVoice ) {
        BuildExtraChallengeVoiceName( challengeVoiceName, pVoiceName,
                                      pChallenge );
        pFileName = challengeVoiceName;
    }

    char path[0x104] = {};
    snprintf( path, sizeof( path ), "%s%s%s/%s.wav", gpConfig->getResRootPath(),
              "Sound/Voices/", gpPlayerData->getLanguage(), pFileName );

    State = CVES_Loading;
    if ( !loadSample( path ) ) {
        snprintf( path, sizeof( path ), "%s%s%s/%s.wav",
                  gpConfig->getResRootPath(), "Sound/Voices/",
                  kFallbackVoiceLanguage, pVoiceName );
        if ( !loadSample( path ) ) {
            OTDU_LOG_WARN( "Missing cinematic voice '%s'\n", pFileName );
        }
    }

    if ( pSample != nullptr ) {
        createInstance();
    }
}

bool CinematicVoiceEmitter::loadSample( const char *pPath ) {
    void *pWavData = nullptr;
    uint32_t wavSize = 0;
    if ( !gpFile->loadFile( pPath, &pWavData, &wavSize ) ) {
        return false;
    }

    std::unique_ptr<edAudioSample> pLoadedSample =
        std::make_unique<edAudioSample>();
    const bool bLoaded =
        pLoadedSample->loadFromMemory( pWavData, int32_t( wavSize ) );
    TestDrive::Free( pWavData );
    if ( !bLoaded ) {
        return false;
    }

    pSample = std::move( pLoadedSample );
    return true;
}

void CinematicVoiceEmitter::createInstance() {
    // FUN_007deb00
    State = CVES_Ready;

    AudioClient *pClient =
        ( gpCinematics != nullptr ) ? gpCinematics->getVoiceClient() : nullptr;
    pInstance =
        ( pClient != nullptr ) ? pClient->acquireSoundInstance() : nullptr;
    if ( pInstance == nullptr ) {
        return;
    }

    pInstance->debugSetDescription( "Voice Instance" );
    pInstance->setSample( pSample.get() );
    pInstance->setVolume( Volume );
    pInstance->setDopplerFactor( 0.0f );
    OTDU_UNIMPLEMENTED_ONCE; // TODO: AUDIO (FUN_00613800, emitter listens to
                             // its instance: Stopped/Ended -> uninstall)
}

void CinematicVoiceEmitter::play() {
    // FUN_007debc0
    static const Eigen::Vector3f kVoicePosition( 0.0f, 5.0f, 0.0f );
    if ( pInstance == nullptr ) {
        return;
    }

    pInstance->setPosition(
        kVoicePosition,
        AudioDispatcherPlaybackInstanceValues::ADPCS_ListenerRelative );
    pInstance->playSubtitled( Name, 0.0f );
}

void CinematicVoiceEmitter::playFrom( EntityAvatar *pAvatar ) {
    // FUN_007de880
    pOwnerAvatar = pAvatar;
    if ( pInstance == nullptr ) {
        return;
    }

    pInstance->setSample( pSample.get() );
    pInstance->debugSetDescription( "Voice Instance" );
    pInstance->playSubtitled( Name, 0.0f );
}

void CinematicVoiceEmitter::setPosition( const Eigen::Vector3f &position ) {
    // FUN_007de870
    if ( pInstance != nullptr ) {
        pInstance->setPosition(
            position, AudioDispatcherPlaybackInstanceValues::ADPCS_World );
    }
}

void CinematicVoiceEmitter::stop() {
    // FUN_007de860
    if ( pInstance != nullptr ) {
        pInstance->stop( 0.0f );
    }
}

void CinematicVoiceEmitter::uninstall() {
    // FUN_007de8e0
    if ( State == CVES_None ) {
        return;
    }

    if ( pInstance != nullptr ) {
        pInstance->stop( 0.0f );
        pInstance->setSample( nullptr );
        pInstance->release();
        pInstance = nullptr;
    }
    pSample.reset();
    State = CVES_None;
}
