#include "shared.h"
#include "gps_voice.h"

#include "audio/implementation/ed_audio_stream_entry.h"
#include "config/gs_config.h"
#include "filesystem/gs_file.h"
#include "player_data/gs_playerdata.h"

static constexpr uint32_t kMaxNumSamples = 0x11;

GPSVoice::GPSVoice()
    : Bank(),
      pMemoryPool( nullptr ),
      audioState( eAudioState::GVAS_Unknown ),
      bEnabled( true ) {
    audioSamples.fill( { nullptr, nullptr } );
}

GPSVoice::~GPSVoice() {}

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
        bool bVar2 = loadBank( gpFile, bankPath.c_str() );
        if ( bVar2 ) {
            audioState = eAudioState::GVAS_WaitingForParsing;
        } else {
            audioState = eAudioState::GVAS_Unknown;
        }
    }

    return true;
}

void GPSVoice::initializeFromDatabase() {
    // FUN_009ad8a0
    terminate();
    initialize();
}

bool GPSVoice::play( const int32_t index ) {
    // FUN_0099d430
    static const char *kSampleNames[kMaxNumSamples] = {
        "V_F_GPS_Voice_001.wav", "V_F_GPS_Voice_002.wav",
        "V_F_GPS_Voice_003.wav", "V_F_GPS_Voice_004.wav",
        "V_F_GPS_Voice_005.wav", "V_F_GPS_Voice_006.wav",
        "V_F_GPS_Voice_007.wav", "V_F_GPS_Voice_008.wav",
        "V_F_GPS_Voice_009.wav", "V_F_GPS_Voice_010.wav",
        "V_F_GPS_Voice_011.wav", "V_F_GPS_Voice_012.wav",
        "V_F_GPS_Voice_013.wav", "V_F_GPS_Voice_014.wav",
        "V_F_GPS_Voice_015.wav", "V_F_GPS_Voice_016.wav",
        "V_F_GPS_Voice_017.wav" }; // PTR_s_V_F_GPS_Voice_001_wav_00f71e68

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
        pSoundInstance->setSample(
            static_cast<const edAudioSample *>( pSample->pContent ) );
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
    for ( Sample &sample : audioSamples ) {
        sample.pContent = nullptr;
    }

    if ( pSoundInstance != nullptr ) {
        pSoundInstance->stop();
        pSoundInstance = nullptr;
    }

    unload();

    audioState = eAudioState::GVAS_Unknown;
}
