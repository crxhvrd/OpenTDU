#include "shared.h"
#include "ed_audio.h"

#include "ed_audio_backend.h"

static constexpr const char *kEdAudioName = "edAudio"; // DAT_00ef1af8

edAudio gEdAudio = {}; // DAT_0100fc80

edAudio::edAudio()
    : edBase( kEdAudioName, 3, 6, 0 ), pBackend( nullptr ), channelCount( 0 ) {
    // FUN_00610ab0
}

edAudio::~edAudio() {
    // FUN_00610df0
    destroy();
}

int32_t edAudio::initialize() {
    // FUN_00610840
    OTDU_UNIMPLEMENTED; // TODO: Memory heap allocation

    pBackend = CreateAudioBackend();
    if ( pBackend == nullptr ) {
        OTDU_LOG_ERROR( "No audio backend available!\n" );
        return 1;
    }

    channelCount = GetPreferredAudioChannelCount();
    if ( !pBackend->initialize( channelCount ) ) {
        delete pBackend;
        pBackend = nullptr;
        channelCount = 0;
        return 1;
    }

    bEnabled = true;
    return 0;
}

void edAudio::destroy() {
    // FUN_00610900
    if ( pBackend != nullptr ) {
        pBackend->shutdown();
        delete pBackend;
        pBackend = nullptr;
    }

    channelCount = 0;
    bEnabled = false;
}

void edAudio::setVolume( float volume ) {
    // FUN_00610dc0
    // TODO: the original writes this straight onto the master output module
    // (GetOutputModule()->Volume, see FUN_00610ea0), which does not exist until
    // the module graph is ported.
    OTDU_UNIMPLEMENTED;
}
