#include "shared.h"
#include "radio_stream_player.h"

#include "audio/gs_audio.h"
#include "audio/implementation/ed_audio_stream_entry.h"
#include "audio/radio/radio_mp3_reader.h"

bool RadioStreamPlayer::IsDecodableFile( const char *pFilename ) {
    // FUN_007c8060
    FileDirectAccess file;
    file.bNeedName = false;

    if ( !file.openFile( pFilename, true, false ) ) {
        return false;
    }

    RadioMp3Reader reader( &file );
    const bool bDecodable = reader.probe();

    file.close();
    return bDecodable;
}

RadioStreamPlayer::RadioStreamPlayer( AudioClient *pInAudioClient,
                                      edAudioStreamEntryListener *pListener )
    : pAudioClient( pInAudioClient ), File(), pEntry( nullptr ) {
    // FUN_004039b0 (inlined)
    PipelineStereo *pPipeline = ( pAudioClient != nullptr )
                                    ? pAudioClient->getStereoPipeline()
                                    : nullptr;
    if ( pPipeline == nullptr ) {
        return;
    }

    pEntry = std::make_unique<edAudioStreamEntry>();
    pPipeline->addStereoEntry( pEntry.get() );

    pEntry->addListener( pListener );
}

RadioStreamPlayer::~RadioStreamPlayer() {
    // FUN_007c8010
    stop();

    // FUN_006136b0
    if ( pEntry != nullptr && pAudioClient != nullptr &&
         pAudioClient->getStereoPipeline() != nullptr ) {
        pAudioClient->getStereoPipeline()->removeStereoEntry( pEntry.get() );
    }
    pEntry.reset();
}

bool RadioStreamPlayer::play( const char *pFilename,
                              const bool bResolveFilename ) {
    // FUN_007c7fa0
    stop();

    File.bNeedName = bResolveFilename;
    if ( !File.openFile( pFilename, true, false ) ) {
        return false;
    }

    if ( pEntry == nullptr ) {
        return false;
    }

    std::unique_ptr<RadioMp3Reader> pReader =
        std::make_unique<RadioMp3Reader>( &File );
    if ( !pReader->probe() ) {
        OTDU_LOG_WARN( "Radio track '%s' is not decodable\n", pFilename );
        File.close();
        return false;
    }

    if ( !pEntry->setReader( std::move( pReader ) ) ) {
        File.close();
        return false;
    }
    pEntry->setLoopCount( 0 );

    // FUN_00613f20
    pEntry->play( 0.0f );
    return true;
}

void RadioStreamPlayer::stop() {
    if ( pEntry != nullptr ) {
        // FUN_006144b0
        pEntry->stop( 0.0f );
        // FUN_00614790
        pEntry->setReader( nullptr );
    }

    // FUN_006086b0
    File.close();
}

void RadioStreamPlayer::setVolume( const float volume ) {
    // FUN_00613640
    if ( pEntry != nullptr ) {
        pEntry->Volume = volume;
    }
}
