#include "shared.h"
#include "mng_music.h"

#include "audio/gs_audio.h"
#include "audio/implementation/ed_audio_sample.h"
#include "config/gs_config.h"
#include "core/ed_xmb.h"
#include "filesystem/gs_file.h"

MngMusic *gpMngMusic = nullptr; // DAT_010e7fd8

MngMusic::MngMusic()
    : pAudioClient( nullptr ),
      playFlags( 0 ),
      pMusicData( nullptr ),
      musicDataSize( 0 ),
      pInstallGuard( std::make_shared<bool>( true ) ),
      pSource( nullptr ),
      bPlaying( false ),
      currentVolume( -1.0f ), // FLOAT_00f41664
      state( MMS_None ),
      loadingMutex( "MusicMutex" ) {
    // FUN_006d7210
    musicFilename[0] = '\0';
    gpMngMusic = this;
}

MngMusic::~MngMusic() {
    if ( gpMngMusic == this ) {
        gpMngMusic = nullptr;
    }
}

bool MngMusic::initialize( TestDriveGameInstance * ) {
    // FUN_006d73e0
    if ( pInstallGuard == nullptr ) {
        pInstallGuard = std::make_shared<bool>( true );
    }

    if ( gpAudio != nullptr ) {
        pAudioClient = gpAudio->createClient( "ClientMenuMusic" );
    }

    if ( pAudioClient == nullptr ) {
        OTDU_LOG_WARN( "MngMusic: no 'ClientMenuMusic' audio client, front end "
                       "music will stay silent\n" );
    }

    return true;
}

void MngMusic::terminate() {
    // FUN_006d7480
    const bool bStillInstalling = ( state == MMS_Loading );
    if ( bStillInstalling ) {
        OTDU_LOG_WARN( "MngMusic: '%s' is still installing, leaving its buffer "
                       "behind\n",
                       musicFilename );
    }

    stop();

    if ( bStillInstalling ) {
        pMusicData = nullptr;
        musicDataSize = 0;
    }

    pInstallGuard.reset();
    pAudioClient = nullptr;
}

void MngMusic::tick( float, float ) {
    // FUN_006d71a0
    if ( state != MMS_Ready ) {
        return;
    }

    if ( !bPlaying ) {
        if ( ( playFlags & 1 ) != 0 && pSource != nullptr ) {
            pSource->play();
            pSource->setVolume( currentVolume );
            bPlaying = true;
        }
    } else if ( ( playFlags & 1 ) == 0 && pSource != nullptr ) {
        pSource->stop( 0.0f );
        bPlaying = false;
    }
}

void MngMusic::stop() {
    // FUN_006d7400
    if ( musicFilename[0] == '\0' ) {
        return;
    }

    musicFilename[0] = '\0';
    currentVolume = -1.0f; // FLOAT_00f41664

    if ( pSource != nullptr ) {
        pSource->stop( 0.0f );
        pSource->setSamples( nullptr, nullptr );

        if ( pAudioClient != nullptr &&
             pAudioClient->getStereoPipeline() != nullptr ) {
            pAudioClient->getStereoPipeline()->removeSource( pSource.get() );
        }

        pSource.reset();
    }

    pChannelSamples[0].reset();
    pChannelSamples[1].reset();

    if ( pMusicData != nullptr && state == MMS_Ready ) {
        TestDrive::Free( pMusicData );
        pMusicData = nullptr;
        musicDataSize = 0;
    }

    state = MMS_None;
}

void MngMusic::loadMusic( const char *pFilename, float volume ) {
    // FUN_006d74e0
    ScopedMutexLock lock( &loadingMutex );

    if ( state == MMS_None ) {
        state = MMS_Loading;
    } else if ( state != MMS_Ready ) {
        return;
    }

    if ( strcmp( musicFilename, pFilename ) == 0 ) {
        // Same track: just retune the volume if it changed.
        OTDU_LOG_DEBUG( "MngMusic::LoadMusic: music already loaded\n" );
        if ( currentVolume != volume && pSource != nullptr ) {
            pSource->setVolume( volume );
            currentVolume = volume;
        }
        return;
    }

    // Different track: drop the current one and install the new file.
    if ( musicFilename[0] != '\0' ) {
        bPlaying = false;
        stop();
        state = MMS_Loading;
    }
    pSource.reset();
    currentVolume = volume;

    char path[512];
    snprintf( path, sizeof( path ), "%sSound/Musics/%s",
              gpConfig->getResRootPath(), pFilename );

    bool bInstalling = false;
    if ( gpFile != nullptr ) {
        if ( pMusicData != nullptr ) {
            TestDrive::Free( pMusicData );
            pMusicData = nullptr;
        }
        musicDataSize = 0;

        std::weak_ptr<bool> guard = pInstallGuard;
        bInstalling = gpFile->loadBank(
            path, &pMusicData, &musicDataSize, this,
            [this, guard]( int32_t type, int32_t bSuccess ) { 
                if ( guard.expired() ) {
                    return;
                }
                if ( type == 4 && bSuccess ) {
                    onMusicInstalled();
                }
            } );
    }

    if ( bInstalling ) {
        strncpy( musicFilename, pFilename, sizeof( musicFilename ) - 1 );
        musicFilename[sizeof( musicFilename ) - 1] = '\0';
        return;
    }

    OTDU_LOG_WARN( "MngMusic::LoadMusic: can't install '%s'\n", path );
    state = MMS_None;
}

void MngMusic::onMusicInstalled() {
    // FUN_006d7270
    if ( pMusicData == nullptr ) {
        OTDU_LOG_WARN(
            "MngMusic::InstallMusicAsyncCLBK: can't find music '%s'\n",
            musicFilename );
        state = MMS_None;
        return;
    }

    const edXMBNode root( pMusicData );
    const edXMBNode channels = root.findChild( "Channels" );
    const edXMBNode buffers = root.findChild( "Buffers" );

    for ( int32_t i = 0; i < 2; i++ ) {
        pChannelSamples[i].reset();

        const edXMBNode channel = channels.getElement( i );
        const edXMBNode data =
            buffers.getElement( channel.readInt( "BufferIndex", i ) )
                .findChild( "Data" );

        const void *pSampleData = data.getValue();
        if ( pSampleData == nullptr || !data.isArray() ) {
            continue;
        }

        auto sample = std::make_unique<edAudioSample>();
        if ( sample->loadFromMemory( pSampleData, data.getArrayCount() ) ) {
            pChannelSamples[i] = std::move( sample );
        }
    }

    if ( pChannelSamples[0] == nullptr || pChannelSamples[1] == nullptr ) {
        OTDU_LOG_WARN( "MngMusic::InstallMusicAsyncCLBK: '%s' has no usable "
                       "stereo pair\n",
                       musicFilename );
        state = MMS_None;
        return;
    }

    PipelineStereo *pPipeline = ( pAudioClient != nullptr )
                                    ? pAudioClient->getStereoPipeline()
                                    : nullptr;
    if ( pPipeline == nullptr ) {
        state = MMS_None;
        return;
    }

    pSource = std::make_unique<AudioStereoSource>();
    pSource->getLeftChannel().debugSetDescription( "MngMusic" );
    pSource->getRightChannel().debugSetDescription( "MngMusic" );
    pSource->setSamples( pChannelSamples[0].get(), pChannelSamples[1].get() );
    pPipeline->addSource( pSource.get() );

    pSource->setLoopState( true );
    pSource->setVolume( currentVolume );
    pSource->setPitch( 1.0f ); // FLOAT_00f4165c

    OTDU_LOG_INFO( "Music '%s' installed (%d frames at %dHz, volume %.2f)\n",
                   musicFilename, pChannelSamples[0]->getFrameCount(),
                   pChannelSamples[0]->getSampleRate(), currentVolume );

    state = MMS_Ready;
}
