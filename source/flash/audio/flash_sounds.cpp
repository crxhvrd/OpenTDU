#include "shared.h"
#include "flash_sounds.h"

#include "audio/implementation/ed_audio.h"
#include "audio/implementation/ed_audio_object.h"
#include "audio/implementation/ed_audio_stream_entry.h"
#include "config/gs_config.h"
#include "game/game_mechanism.h"
#include "game/gm_scene.h"
#include "game/mng_hud.h"
#include "game/mng_music.h"
#include "game/scenaric/gs_scenaric.h"
#include "render/camera/camera_gps.h"
#include "render/camera/gs_camera.h"
#include "tdu_instance.h"

FlashSounds::FlashSounds() : Bank(), pMemoryPool( nullptr ) {
    // FUN_0099ddc0
}

FlashSounds::~FlashSounds() {
    // FUN_0099dd20
    terminate();
}

bool FlashSounds::initialize( void *pMemory ) {
    pMemoryPool = pMemory;
    bInitialized = true;
    return true;
}

void FlashSounds::terminate() {
    // FUN_0099dfe0
    if ( !bInitialized ) {
        return;
    }

    for ( std::unique_ptr<edAudioStreamEntry> &voice : voices ) {
        if ( voice != nullptr ) {
            voice->stop();
        }
    }

    if ( pipeline.getBuffer() != nullptr ) {
        pipeline.getBuffer()->unlinkAllEntries();
    }

    for ( std::unique_ptr<edAudioStreamEntry> &voice : voices ) {
        voice.reset();
    }

    samples.clear();

    if ( pMemoryPool != nullptr ) {
        TestDrive::FreeAligned( pMemoryPool );
        pMemoryPool = nullptr;
    }

    gpsVoice.terminate();
    pipeline.destroy();
    pAudioClient = nullptr;

    bInitialized = false;
}

void FlashSounds::loadFrontEndSounds() {
    // FUN_0099e9b0
    if ( isLoaded() ) {
        return;
    }

    if ( gpAudio == nullptr || gEdAudio.getOutputModule() == nullptr ) {
        OTDU_LOG_WARN( "No audio output, front end sounds stay silent\n" );
        return;
    }

    if ( pAudioClient == nullptr ) {
        pAudioClient = gpAudio->createClient( "ClientMenuSFX" );
        OTDU_ASSERT( pAudioClient != nullptr );
    }

    edAudioObject *pPipelineParent = ( pAudioClient != nullptr )
                                         ? pAudioClient->getRoot()
                                         : gEdAudio.getOutputModule();

    if ( pipeline.getBuffer() == nullptr &&
         !pipeline.create( pPipelineParent ) ) {
        return;
    }

    for ( int32_t i = 0; i < kNumVoices; i++ ) {
        if ( voices[i] != nullptr ) {
            continue;
        }

        voices[i] = std::make_unique<edAudioStreamEntry>();

        char description[32];
        snprintf( description, sizeof( description ), "FlashSounds %d", i );
        voices[i]->debugSetDescription( description );

        pipeline.getBuffer()->addNewEntry( voices[i].get() );
    }

    const char *pRootPath =
        ( gpConfig != nullptr ) ? gpConfig->getResRootPath() : "";

    char bankPath[512];
    snprintf( bankPath, sizeof( bankPath ), "%sFrontEnd/sounds/fe_snd.bnk",
              pRootPath );

    if ( !loadBootBank( bankPath, nullptr ) ) {
        OTDU_LOG_ERROR( "Failed to load front end sound bank '%s'!\n",
                        bankPath );
        return;
    }

    parseSoundList();

    OTDU_LOG_INFO( "Front end sounds loaded ('%s', %d entries, %zu messages, "
                   "%d music tracks)\n",
                   bankPath, getNumElements(), soundList.size(),
                   numMusicEntries );
}

void FlashSounds::parseSoundList() {
    ParsedBankEntry entry = {};
    if ( bank.getEntry( const_cast<char *>( "sounds.ini" ), entry ) < 0 ||
         entry.pData == nullptr || entry.Size <= 0 ) {
        OTDU_LOG_WARN( "No sounds.ini in the front end bank, sounds will play "
                       "unattenuated\n" );
        return;
    }

    const char *pText = static_cast<const char *>( entry.pData );
    const char *pEnd = pText + entry.Size;

    while ( pText < pEnd ) {
        const char *pLineEnd = pText;
        while ( pLineEnd < pEnd && *pLineEnd != '\n' && *pLineEnd != '\r' ) {
            pLineEnd++;
        }

        const std::string line( pText, size_t( pLineEnd - pText ) );
        pText = ( pLineEnd < pEnd ) ? pLineEnd + 1 : pEnd;

        if ( line.find( "//" ) == 0 ) {
            continue;
        }

        // AddFESound  "message"    "file.wav"  volume
        // SetMusic    "FRONTENDID" "file.wav"  volume  waitCine
        const bool bMusicLine = line.find( "SetMusic" ) != std::string::npos;
        const size_t keywordStart =
            bMusicLine ? line.find( "SetMusic" ) : line.find( "AddFESound" );
        if ( keywordStart == std::string::npos ) {
            continue;
        }

        std::string token[4];
        int32_t tokenCount = 0;
        size_t cursor = keywordStart + ( bMusicLine ? 8 : 10 );

        while ( tokenCount < 4 && cursor < line.size() ) {
            while ( cursor < line.size() &&
                    ( line[cursor] == ' ' || line[cursor] == '\t' ) ) {
                cursor++;
            }
            if ( cursor >= line.size() ) {
                break;
            }

            if ( line[cursor] == '"' ) {
                const size_t closing = line.find( '"', cursor + 1 );
                if ( closing == std::string::npos ) {
                    break;
                }
                token[tokenCount++] =
                    line.substr( cursor + 1, closing - cursor - 1 );
                cursor = closing + 1;
            } else {
                size_t wordEnd = cursor;
                while ( wordEnd < line.size() && line[wordEnd] != ' ' &&
                        line[wordEnd] != '\t' ) {
                    wordEnd++;
                }
                token[tokenCount++] = line.substr( cursor, wordEnd - cursor );
                cursor = wordEnd;
            }
        }

        if ( tokenCount < 2 || token[0].empty() || token[1].empty() ) {
            continue;
        }

        if ( bMusicLine ) {
            if ( numMusicEntries >= kMaxMusicEntries ) {
                OTDU_LOG_WARN( "sounds.ini declares more than %d SetMusic "
                               "entries; '%s' was dropped\n",
                               kMaxMusicEntries, token[0].c_str() );
                continue;
            }

            FrontEndMusic &music = musicList[numMusicEntries++];
            strncpy( music.Key, token[0].c_str(), sizeof( music.Key ) - 1 );
            strncpy( music.FileName, token[1].c_str(),
                     sizeof( music.FileName ) - 1 );
            music.Volume =
                ( tokenCount > 2 ) ? float( atof( token[2].c_str() ) ) : 1.0f;
            music.bWaitCine =
                ( tokenCount > 3 ) && atoi( token[3].c_str() ) != 0;
            continue;
        }

        FrontEndSound sound;
        sound.FileName = token[1];
        sound.Volume =
            ( tokenCount > 2 ) ? float( atof( token[2].c_str() ) ) : 1.0f;

        soundList[token[0]] = sound;
    }
}

const FlashSounds::FrontEndSound *
FlashSounds::findSound( const char *pFlashMessage ) const {
    auto it = soundList.find( std::string( pFlashMessage ) );
    return ( it != soundList.end() ) ? &it->second : nullptr;
}

const FlashSounds::FrontEndMusic *
FlashSounds::findMusic( const char *pKey ) const {
    // FUN_0099d280
    if ( pKey == nullptr ) {
        return nullptr;
    }

    for ( int32_t i = 0; i < kMaxMusicEntries; i++ ) {
        if ( strcmp( musicList[i].Key, pKey ) == 0 ) {
            return &musicList[i];
        }
    }

    return nullptr;
}

const char *FlashSounds::ResolveGameModeMusicKey() {
    // FUN_0099e410 (inlined)
    if ( gpTestDriveInstance == nullptr ) {
        return nullptr;
    }

    switch ( gpTestDriveInstance->getActiveGameMode() ) {
    case GM_Map: {
        // The space view plays the credits music.
        static constexpr int32_t kGPSCameraMode = 3;
        const CameraGPS *pCameraGPS =
            static_cast<const CameraGPS *>( gpCamera->findCameraWithMode(
                kGPSCameraMode, gpCamera->getBoundCarState() ) );
        if ( pCameraGPS != nullptr &&
             pCameraGPS->getZoom() == CameraGPS::GZL_Space ) {
            return "CREDITS";
        }
        return "MAP";
    }

    case GM_Home:
        return "HOME";

    case GM_Shop: {
        if ( gpScenaric == nullptr ||
             gpScenaric->getCurrentSpotIndex() == -1 ) {
            return nullptr;
        }

        SpotMap *pSpot = gpScenaric->getSpotList().getWorldSpotChecked(
            gpScenaric->getCurrentSpotIndex() );
        if ( pSpot == nullptr ) {
            return nullptr;
        }

        switch ( pSpot->Type ) {
        case 1:
        case 2:
            return "CARDEAL";
        case 4:
            return "TUNING";
        case 8:
            switch ( pSpot->SubType ) {
            case 0:
                return "CARWASH";
            case 1:
            case 9:
                return "CARPAINT";
            case 2:
            case 3:
                return "CARRENT";
            case 4:
                return "REALTOR";
            default:
                return nullptr;
            }
        default:
            return nullptr;
        }
    }

    case GM_Mission:
        if ( gpMngHud == nullptr || gpMngHud->getGameMech() == nullptr ||
             gpMngHud->getGameMech()->getCurrentState() !=
                 GameMechanism::MS_Ranking ) {
            return nullptr;
        }
        return "RANKING";

    case GM_GameModeEdit:
        return "GMEDITOR";
    case GM_InstantChallenge:
        return "MAP";
    case GM_Garage:
        return "GARAGE";
    case GM_Living:
        // TODO: "ETRADE"
        return "LIVING";
    case GM_Goodies:
        return "GOODIES";
    case GM_DriveIn:
        return "DRIVEIN";
    case GM_Dressing:
        return "DRESSING";
    case GM_AfterMarket:
        return "TUNING";
    case GM_Estates:
        return "REALTOR";
    case GM_Club:
        return "CLUB";
    case GM_Lobby:
        return "LOBBY";
    case GM_Login:
        return "LOGIN";
    case GM_OutroDemo:
        return "OUT_DEMO";
    case GM_Jail:
        return "JAIL";
    case GM_IntroDemo:
        return "IN_DEMO";
    default:
        return nullptr;
    }
}

bool FlashSounds::playFrontEndMusic( bool bFromCine ) {
    // FUN_0099e410
    if ( gpTestDriveInstance == nullptr ) {
        return false;
    }

    if ( gpActiveGameMode != nullptr && gpActiveGameMode->hasRequestedExit() ) {
        OTDU_LOG_DEBUG( "FlashSounds::playFrontEndMusic: leaving this game "
                        "mode, no need to play music now\n" );
        return false;
    }

    const FrontEndMusic *pMusic = nullptr;
    if ( gpActiveGMScene != nullptr &&
         gpActiveGMScene->getMusicDesc()[0] != '\0' ) {
        pMusic = findMusic( gpActiveGMScene->getMusicDesc() );
        if ( pMusic == nullptr ) {
            OTDU_LOG_WARN( "The music you have referenced in your .bnk is not "
                           "referenced in sounds.ini : %s\n",
                           gpActiveGMScene->getMusicDesc() );
        }
    }

    if ( pMusic == nullptr ) {
        pMusic = findMusic( ResolveGameModeMusicKey() );
    }

    if ( pMusic == nullptr || gpMngMusic == nullptr ) {
        OTDU_LOG_DEBUG(
            "FlashSounds::playFrontEndMusic: aborted, no music set\n" );
        return true;
    }

    if ( !bFromCine && pMusic->bWaitCine ) {
        OTDU_LOG_DEBUG( "FlashSounds::playFrontEndMusic: wait cine is true and "
                        "fromcine is false for file: %s\n",
                        pMusic->FileName );
        return false;
    }

    OTDU_LOG_INFO( "Playing music: %s\n", pMusic->FileName );

    gpMngMusic->loadMusic( pMusic->FileName, pMusic->Volume );
    gpMngMusic->setPlayFlags( 1 );
    return true;
}

void FlashSounds::stopFrontEndMusic() {
    // FUN_0099d720
    OTDU_LOG_DEBUG( "Stoping music\n" );
    if ( gpMngMusic != nullptr ) {
        gpMngMusic->setPlayFlags( 0 );
    }
}

const edAudioSample *FlashSounds::getSample( const char *pName ) {
    if ( pName == nullptr || pName[0] == '\0' || !isLoaded() ) {
        return nullptr;
    }

    const std::string key( pName );

    auto it = samples.find( key );
    if ( it != samples.end() ) {
        return it->second.get();
    }

    ParsedBankEntry entry = {};
    if ( bank.getEntry( const_cast<char *>( pName ), entry ) < 0 ||
         entry.pData == nullptr || entry.Size <= 0 ) {
        samples.emplace( key, nullptr );
        return nullptr;
    }

    auto sample = std::make_unique<edAudioSample>();
    if ( !sample->loadFromMemory( entry.pData, entry.Size ) ) {
        samples.emplace( key, nullptr );
        return nullptr;
    }

    const edAudioSample *pSample = sample.get();
    samples.emplace( key, std::move( sample ) );

    return pSample;
}

int32_t FlashSounds::getFreeVoice() {
    for ( int32_t i = 0; i < kNumVoices; i++ ) {
        if ( voices[i] != nullptr && voices[i]->isStopped() ) {
            return i;
        }
    }

    return -1;
}

bool FlashSounds::queuePendingSound( const char *pName, const bool bLoop ) {
    // FUN_0099d230
    for ( PendingSound &pending : pendingSounds ) {
        if ( pending.Name[0] == '\0' ) {
            strncpy( pending.Name, pName, sizeof( pending.Name ) - 1 );
            pending.Name[sizeof( pending.Name ) - 1] = '\0';
            pending.bLoop = bLoop;
            return true;
        }
    }

    return false;
}

void FlashSounds::flushPendingSound() {
    // FUN_0099daf0
    PendingSound &pending = pendingSounds[0];
    if ( pending.Name[0] != '\0' ) {
        playFrontEndSound( pending.Name, pending.bLoop ? 1 : 0, false );
        pending.Name[0] = '\0';
        pending.bLoop = false;
    }
}

bool FlashSounds::playFrontEndSound( const char *param_2,
                                     int32_t param_3,
                                     bool param_4 ) {
    // FUN_0099d780
    if ( param_2 == nullptr ) {
        return false;
    }

    if ( param_4 ) {
        return queuePendingSound( param_2, param_3 == 1 );
    }

    std::string message( param_2 );
    std::string group;
    if ( const size_t separator = message.find( ':' );
         separator != std::string::npos ) {
        group = message.substr( separator + 1 );
        message.resize( separator );
    }

    const FrontEndSound *pSound = findSound( message.c_str() );
    if ( pSound == nullptr ) {
        OTDU_LOG_WARN( "Can't find sample for flash message '%s'\n",
                       message.c_str() );
        return false;
    }

    const edAudioSample *pSample = getSample( pSound->FileName.c_str() );
    if ( pSample == nullptr ) {
        OTDU_LOG_WARN( "Front end sound '%s' -> '%s' is not in the bank\n",
                       message.c_str(), pSound->FileName.c_str() );
        return false;
    }

    const int32_t voiceIndex = getFreeVoice();
    if ( voiceIndex < 0 ) {
        return false;
    }

    edAudioStreamEntry *pVoice = voices[voiceIndex].get();

    if ( !pVoice->setSample( pSample ) ) {
        return false;
    }

    pVoice->setLoopCount( ( param_3 == 1 ) ? -1 : 0 );

    pVoice->Volume = pSound->Volume;
    pVoice->Pitch = 1.0f;
    snprintf( voiceGroups[voiceIndex], kMaxGroupNameLength, "%s",
              group.c_str() );

    pVoice->play();

    return true;
}

#ifdef OTDU_USE_IMGUI
const char *FlashSounds::getVoiceGroup( int32_t voiceIndex ) const {
    if ( voiceIndex < 0 || voiceIndex >= kNumVoices ) {
        return "";
    }
    return voiceGroups[voiceIndex];
}

bool FlashSounds::isVoicePlaying( int32_t voiceIndex ) const {
    if ( voiceIndex < 0 || voiceIndex >= kNumVoices ) {
        return false;
    }
    const edAudioStreamEntry *pVoice = voices[voiceIndex].get();
    return pVoice != nullptr && !pVoice->isStopped();
}
#endif

bool FlashSounds::stopGroup( const char *pGroupName, float fadeTime ) {
    // FUN_0099d200
    if ( pGroupName == nullptr ) {
        return false;
    }

    bool bStoppedAny = false;
    for ( int32_t i = 0; i < kNumVoices; i++ ) {
        if ( strcmp( voiceGroups[i], pGroupName ) != 0 ) {
            continue;
        }

        edAudioStreamEntry *pVoice = voices[i].get();
        if ( pVoice != nullptr && !pVoice->isStopped() ) {
            float fade = fadeTime;
            if ( fadeTime < 0.0f ) {
                fade = pVoice->getDuration() - pVoice->getPosition();
                if ( fade < 0.0f ) {
                    fade = 0.0f;
                }
            }
            pVoice->stop( fade );
        }

        voiceGroups[i][0] = '\0';
        bStoppedAny = true;
    }

    return bStoppedAny;
}

void FlashSounds::stopAllGroups() {
    // FUN_0099cfc0
    for ( int32_t i = 0; i < kNumVoices; i++ ) {
        if ( voiceGroups[i][0] == '\0' ) {
            continue;
        }

        edAudioStreamEntry *pVoice = voices[i].get();
        if ( pVoice != nullptr && !pVoice->isStopped() ) {
            pVoice->stop( 0.0f );
        }
        voiceGroups[i][0] = '\0';
    }
}
