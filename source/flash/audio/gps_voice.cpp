#include "shared.h"
#include "gps_voice.h"

#include "audio/implementation/ed_audio_sample.h"
#include "audio/implementation/ed_audio_stream_entry.h"
#include "config/gs_config.h"
#include "filesystem/gs_file.h"
#include "player_data/gs_playerdata.h"

static constexpr uint32_t kMaxNumSamples = 0x11;
static constexpr int32_t kVoiceSampleEntryType = 10;
static constexpr int32_t kVoiceSampleEntrySubType = 6;

static constexpr const char *kSampleNames[kMaxNumSamples] =
    {
        "V_F_GPS_Voice_001.wav", "V_F_GPS_Voice_002.wav",
        "V_F_GPS_Voice_003.wav", "V_F_GPS_Voice_004.wav",
        "V_F_GPS_Voice_005.wav", "V_F_GPS_Voice_006.wav",
        "V_F_GPS_Voice_007.wav", "V_F_GPS_Voice_008.wav",
        "V_F_GPS_Voice_009.wav", "V_F_GPS_Voice_010.wav",
        "V_F_GPS_Voice_011.wav", "V_F_GPS_Voice_012.wav",
        "V_F_GPS_Voice_013.wav", "V_F_GPS_Voice_014.wav",
        "V_F_GPS_Voice_015.wav", "V_F_GPS_Voice_016.wav",
        "V_F_GPS_Voice_017.wav" }; // PTR_s_V_F_GPS_Voice_001_wav_00f71e68

GPSVoice::GPSVoice()
    : Bank(),
      pMemoryPool( nullptr ),
      audioState( eAudioState::GVAS_Unknown ),
      bEnabled( true ) {}

GPSVoice::~GPSVoice() { terminate(); }

bool GPSVoice::initialize() {
    // FUN_0099e8c0
    if ( audioState != eAudioState::GVAS_Unknown || pMemoryPool == nullptr ) {
        return true;
    }

    const char *pPathRoot = gpConfig->getResRootPath();
    const char *pActiveLanguage = gpPlayerData->getLanguage();

    std::string bankPath = pPathRoot;
    bankPath += "Sound/Voices/";
    bankPath += pActiveLanguage;
    bankPath += "/GPS.bnk";

    if ( !bLoaded ) {
        bUnknownBoolean = false;
        if ( !loadBankWithMemory( gpFile, bankPath.c_str(), pMemoryPool,
                                  kMemoryPoolSize, true ) ) {
            audioState = eAudioState::GVAS_Unknown;
            return true;
        }
    }

    audioState = eAudioState::GVAS_WaitingForParsing;
    return true;
}

void GPSVoice::initializeFromDatabase() {
    // FUN_009ad8a0
    terminate();
    initialize();
}

void GPSVoice::loadContent() {
    // FUN_0099de30
    const int32_t numElements = getNumElements();
    if ( numElements > int32_t( kMaxNumSamples ) ) {
        audioState = eAudioState::GVAS_Unknown;
        return;
    }

    ParsedBankEntry entry;
    for ( int32_t i = 0; i < numElements; i++ ) {
        if ( getEntry( i, entry ) != 0 || entry.Type != kVoiceSampleEntryType ||
             entry.SubType != kVoiceSampleEntrySubType ) {
            continue;
        }

        const size_t separator = entry.Filename.find_last_of( '\\' );
        const std::string fileName =
            ( separator == std::string::npos )
                ? entry.Filename
                : entry.Filename.substr( separator + 1 );
        if ( fileName.empty() ) {
            break;
        }

        const uint64_t fileHashcode = GetIdentifier64bit( fileName.c_str() );
        int32_t sampleIndex = -1;
        for ( int32_t j = 0; j < int32_t( kMaxNumSamples ); j++ ) {
            if ( GetIdentifier64bit( kSampleNames[j] ) == fileHashcode ) {
                sampleIndex = j;
                break;
            }
        }
        if ( sampleIndex == -1 ) {
            break;
        }

        auto pSample = std::make_unique<edAudioSample>();
        if ( pSample->loadFromMemory( entry.pData, entry.Size ) ) {
            audioSamples[sampleIndex].pContent = std::move( pSample );
        }
    }

    if ( pAudioClient == nullptr && gpAudio != nullptr ) {
        pAudioClient = gpAudio->createClient( "ClientGPS" );
    }

    if ( pAudioClient != nullptr && pipeline.getBuffer() == nullptr ) {
        pipeline.create( pAudioClient->getRoot() );
    }

    if ( pipeline.getBuffer() != nullptr && pSoundInstance == nullptr ) {
        pSoundInstance = std::make_unique<edAudioStreamEntry>();
        pSoundInstance->debugSetDescription( "GPSVoice" );
        pipeline.getBuffer()->addNewEntry( pSoundInstance.get() );
    }

    audioState = eAudioState::GVAS_Ready;
}

bool GPSVoice::play( const int32_t index ) {
    // FUN_0099d430
    if ( !bEnabled ) {
        return true;
    }
    if ( audioState != eAudioState::GVAS_Ready ) {
        return false;
    }
    if ( pSoundInstance == nullptr ) {
        return false;
    }

    const Sample *pSample = ( index != -1 ) ? &audioSamples[index] : nullptr;
    if ( pSoundInstance->isStopped() && pSample != nullptr &&
         pSample->pContent != nullptr ) {
        pSoundInstance->setSample( pSample->pContent.get() );
        pSoundInstance->debugSetDescription( kSampleNames[index] );
        pSoundInstance->setLoopCount( 0 );
        pSoundInstance->Pitch = 1.0f;
        pSoundInstance->Volume = 1.0f;
        pSoundInstance->play( 0.0f );
        return true;
    }

    OTDU_LOG_WARN(
        "Can't play GPS sound (state was:%s) (asked:%d) (sample:%s)\n",
        pSoundInstance->isStopped() ? "stopped" : "playing", index,
        ( pSample != nullptr && pSample->pContent != nullptr ) ? "present"
                                                               : "missing" );
    return false;
}

void GPSVoice::terminate() {
    // FUN_0099d3d0
    if ( pSoundInstance != nullptr ) {
        pSoundInstance->stop();
        if ( pipeline.getBuffer() != nullptr ) {
            pipeline.getBuffer()->unlinkAllEntries();
        }
        pSoundInstance.reset();
    }
    pipeline.destroy();

    for ( Sample &sample : audioSamples ) {
        sample.pContent.reset();
    }

    unload();

    audioState = eAudioState::GVAS_Unknown;
}
