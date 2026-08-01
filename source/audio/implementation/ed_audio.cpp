#include "shared.h"
#include "ed_audio.h"

#include "core/time.h"
#include "ed_audio_backend.h"
#include "ed_audio_memory.h"
#include "ed_audio_object.h"

static constexpr const char *kEdAudioName = "edAudio"; // DAT_00ef1af8

// Target period of one mix block, in seconds.
static constexpr float kBlockPeriod =
    float( kAudioFramesPerBlock ) / float( kAudioOutputSampleRate );
static constexpr float kAudioSampleScale = 1.0f / 32768.0f;

static TestDriveThread::ThreadReturnType_t MainAudioThread( void *in_param_1 ) {
    edAudio *pThis = (edAudio *)in_param_1;
    pThis->audioThreadMain();
    return (TestDriveThread::ThreadReturnType_t)0;
}

edAudio gEdAudio = {}; // DAT_0100fc80

edAudio::edAudio()
    : edBase( kEdAudioName, 3, 6, 0 ),
      pBackend( nullptr ),
      pOutputModule( nullptr ),
      channelCount( 0 ),
      bAudioThreadMustLoop( false ),
      FrameProcessTime( 0.0f ),
      ProcessorUsage( 0.0f ),
      ThreadSleepTime( 0.0f ) {
    // FUN_00610ab0
    static constexpr float kDiagonal = 0.70710677f; // DAT_00f41af4

    SpeakerDirections[0] = { -kDiagonal, 0.0f, kDiagonal };  // front left
    SpeakerDirections[1] = { kDiagonal, 0.0f, kDiagonal };   // front right
    SpeakerDirections[2] = { -kDiagonal, 0.0f, -kDiagonal }; // rear left
    SpeakerDirections[3] = { kDiagonal, 0.0f, -kDiagonal };  // rear right
    SpeakerDirections[4] = { 0.0f, 0.0f, 1.0f };             // centre
    SpeakerDirections[5] = { 0.0f, 0.0f, 0.0f };             // LFE
}

edAudio::~edAudio() {
    // FUN_00610df0
    destroy();
}

int32_t edAudio::initialize() {
    // FUN_00610840
    if ( edAudioHeap::GetFreeBytes() == 0 ) {
        return 4;
    }

    pBackend = CreateAudioBackend();
    if ( pBackend == nullptr ) {
        OTDU_LOG_ERROR( "No audio backend available!\n" );
        return 1;
    }

    channelCount = GetPreferredAudioChannelCount();
    if ( !pBackend->initialize( channelCount ) ) {
        destroy();
        return 1;
    }

    pOutputModule = new edAudioObject( channelCount, channelCount );

    pOutputModule->debugSetDescription( "Output" );

    mixBlock.assign( channelCount * kAudioFramesPerBlock, 0.0f );

    interleavedBlock.resize( channelCount * kAudioFramesPerBlock );

    bAudioThreadMustLoop = true;
    audioThread.initialize( (TestDriveThread::StartRoutine_t)&MainAudioThread,
                            this, 0, 0 );

    bEnabled = true;
    return 0;
}

void edAudio::destroy() {
    // FUN_00610900
    bAudioThreadMustLoop = false;
    audioThread.terminate();

    if ( pOutputModule != nullptr ) {
        pOutputModule->unlinkAllEntries();
    }
    delete pOutputModule;
    pOutputModule = nullptr;

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
    if ( pOutputModule != nullptr ) {
        pOutputModule->Volume = volume;
    }
}

void edAudio::audioThreadMain() {
    // FUN_006109a0
    while ( bAudioThreadMustLoop ) {
        const float blockStart = GetTimePrecise();

        // Draining until the ring is genuinely full restores the intended
        // kAudioRingBlockCount blocks of slack, and is also what the original
        // does - DirectSound hands it the whole play-to-write cursor gap.
        while ( processOneBlock() > 0 ) {
        }

        FrameProcessTime = GetTimePrecise() - blockStart;
        ThreadSleepTime = Max( 0.0f, kBlockPeriod - FrameProcessTime );

        if ( ThreadSleepTime > 0.0f ) {
            std::this_thread::sleep_for(
                std::chrono::duration<float>( ThreadSleepTime ) );
        }

        const float totalTime = GetTimePrecise() - blockStart;
        if ( totalTime > 0.0f ) {
            ProcessorUsage = ( FrameProcessTime / totalTime ) * 100.0f * 0.1f +
                             ProcessorUsage * 0.9f;
        }
    }
}

int32_t edAudio::processOneBlock() {
    // FUN_00616590
    const int32_t queuedFrames = pBackend->getQueuedFrames();
    const int32_t ringCapacity = kAudioFramesPerBlock * kAudioRingBlockCount;
    int32_t frameCount = ringCapacity - queuedFrames;
    if ( frameCount <= 0 ) {
        return 0;
    }

    frameCount = Min( frameCount, kAudioFramesPerBlock );

    const size_t blockSampleCount =
        static_cast<size_t>( channelCount ) * static_cast<size_t>( frameCount );
    std::fill( mixBlock.begin(), mixBlock.begin() + blockSampleCount, 0.0f );

    edAudioDSPNode settings = {};
    settings.Volume = 1.0f;
    settings.Pitch = 1.0f;
    settings.FrameCount = static_cast<uint32_t>( frameCount );
    settings.pWorkingBuffer = mixBlock.data();
    {
        ScopedMutexLock lock( &GetAudioGraphMutex() );

        const float *pMixed = pOutputModule->process( settings );

        if ( pMixed == nullptr ) {
            return 0;
        }

        for ( int32_t frame = 0; frame < frameCount; frame++ ) {
            for ( int32_t channel = 0; channel < channelCount; channel++ ) {
                const float sample =
                    pMixed[channel * frameCount + frame] * kAudioSampleScale;
                interleavedBlock[frame * channelCount + channel] =
                    Clamp( sample, -1.0f, 1.0f );
            }
        }
    }

    pBackend->submit( interleavedBlock.data(), frameCount );

    return frameCount;
}
