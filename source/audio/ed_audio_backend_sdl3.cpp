#include "shared.h"
#include "ed_audio_backend.h"

#if defined( OTDU_USE_SDL )
#include <SDL3/SDL.h>
#endif

// Speaker layouts the original engine knows about. FUN_00616fd0 switches on the
// DirectSound speaker config and ends up with 1, 2, 6 or 8 output channels
// (quad is reachable too); anything else falls back to stereo.
static constexpr int32_t kSupportedChannelCounts[] = { 1, 2, 4, 6, 8 };

int32_t GetPreferredAudioChannelCount() {
    // Equivalent of DirectSound caps query done in FUN_00616fd0.
#if defined( OTDU_USE_SDL )
    SDL_AudioSpec deviceSpec = {};
    if ( SDL_GetAudioDeviceFormat( SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK,
                                   &deviceSpec, nullptr ) ) {
        for ( int32_t supportedCount : kSupportedChannelCounts ) {
            if ( deviceSpec.channels == supportedCount ) {
                return supportedCount;
            }
        }

        OTDU_LOG_WARN( "Unsupported speaker layout (%i channels); falling back "
                       "to stereo\n",
                       deviceSpec.channels );
    } else {
        OTDU_LOG_WARN( "Could not query audio device format ('%s'); falling "
                       "back to stereo\n",
                       SDL_GetError() );
    }
#endif

    return 2;
}

#if defined( OTDU_USE_SDL )
namespace {
    class edAudioBackendSDL3 : public edAudioBackend {
    public:
        edAudioBackendSDL3() : pStream( nullptr ) {}

        ~edAudioBackendSDL3() override { shutdown(); }

        bool initialize( int32_t numChannels ) override {
            OTDU_ASSERT( pStream == nullptr );

            SDL_AudioSpec sourceSpec = {};
            // The engine mixes in float and the original only converted down to
            // 16 bit because DirectSound required it, so we keep the mix bus
            // format and let SDL convert for the device.
            sourceSpec.format = SDL_AUDIO_F32;
            sourceSpec.channels = numChannels;
            sourceSpec.freq = kAudioOutputSampleRate;
            pStream =
                SDL_OpenAudioDeviceStream( SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK,
                                           &sourceSpec, nullptr, nullptr );
            if ( pStream == nullptr ) {
                OTDU_LOG_ERROR( "Failed to open audio output device: '%s'!\n",
                                SDL_GetError() );
                return false;
            }

            channelCount = numChannels;

            if ( !SDL_ResumeAudioStreamDevice( pStream ) ) {
                OTDU_LOG_ERROR( "Failed to start audio output device: '%s'!\n",
                                SDL_GetError() );
                shutdown();
                return false;
            }

            OTDU_LOG_INFO( "Audio output opened (%i channels, %iHz, %i frames "
                           "per block)\n",
                           channelCount, kAudioOutputSampleRate,
                           kAudioFramesPerBlock );
            return true;
        }

        void shutdown() override {
            if ( pStream != nullptr ) {
                SDL_DestroyAudioStream( pStream );
                pStream = nullptr;
            }

            channelCount = 0;
        }

        void submit( const float *pInterleavedFrames,
                     int32_t frameCount ) override {
            if ( pStream == nullptr || pInterleavedFrames == nullptr ||
                 frameCount <= 0 ) {
                return;
            }

            const int32_t sizeInBytes = frameCount * getFrameSizeInBytes();
            if ( !SDL_PutAudioStreamData( pStream, pInterleavedFrames,
                                          sizeInBytes ) ) {
                OTDU_LOG_ERROR( "Failed to submit audio samples: '%s'!\n",
                                SDL_GetError() );
            }
        }

        int32_t getQueuedFrames() const override {
            if ( pStream == nullptr ) {
                return 0;
            }

            const int32_t queuedBytes = SDL_GetAudioStreamQueued( pStream );
            if ( queuedBytes <= 0 ) {
                return 0;
            }

            return queuedBytes / getFrameSizeInBytes();
        }

    private:
        inline int32_t getFrameSizeInBytes() const {
            return channelCount * static_cast<int32_t>( sizeof( float ) );
        }

    private:
        SDL_AudioStream *pStream;
    };
} // namespace

edAudioBackend *CreateAudioBackend() { return new edAudioBackendSDL3(); }
#else
edAudioBackend *CreateAudioBackend() { return nullptr; }
#endif
