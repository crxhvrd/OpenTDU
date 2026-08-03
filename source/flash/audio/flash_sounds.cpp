#include "shared.h"
#include "flash_sounds.h"

#include "audio/implementation/ed_audio.h"
#include "audio/implementation/ed_audio_object.h"
#include "audio/implementation/ed_audio_stream_entry.h"
#include "config/gs_config.h"

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

    if ( pipeline.getBuffer() == nullptr &&
         !pipeline.create( gEdAudio.getOutputModule() ) ) {
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

    OTDU_LOG_INFO( "Front end sounds loaded ('%s', %d entries, %zu messages)\n",
                   bankPath, getNumElements(), soundList.size() );
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

        if ( line.find( "AddFESound" ) == std::string::npos ||
             line.find( "//" ) == 0 ) {
            continue;
        }

        // AddFESound  "message"  "file.wav"  volume
        std::string token[3];
        int32_t tokenCount = 0;
        size_t cursor = line.find( "AddFESound" ) + 10;

        while ( tokenCount < 3 && cursor < line.size() ) {
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

bool FlashSounds::playFrontEndSound( const char *param_2,
                                     int32_t param_3,
                                     bool param_4 ) {
    // FUN_0099d780
    if ( param_2 == nullptr ) {
        return false;
    }

    if ( param_4 ) {
        OTDU_UNIMPLEMENTED;
        return false;
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
