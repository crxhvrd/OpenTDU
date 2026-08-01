#pragma once

#include "core/ed_base.h"
#include "core/thread.h"

class edAudioBackend;
class edAudioBuffer;
class edAudioMixer;
class edAudioObject;

struct edAudioListener {
    Eigen::Vector3f Position;
    Eigen::Vector3f Front;
    Eigen::Vector3f Top;
    Eigen::Vector3f LeftToRight;
    Eigen::Vector3f Speed;
    float DopplerFactor; // SetOutputDopplerFactor in Audio.ini
    float SpatialFactor; // SetOutputSpatialFactor in Audio.ini

    edAudioListener()
        : Position( Eigen::Vector3f::Zero() ),
          Front( 0.0f, 0.0f, 1.0f ),
          Top( 0.0f, 1.0f, 0.0f ),
          LeftToRight( 1.0f, 0.0f, 0.0f ),
          Speed( Eigen::Vector3f::Zero() ),
          DopplerFactor( 1.0f ),
          SpatialFactor( 0.0f ) {}

    inline void copyFrom( const edAudioListener &other ) { /* FUN_00402c10 */
        *this = other;
    }

    inline Eigen::Vector3f
    toListenerSpace( const Eigen::Vector3f &world ) const {
        const Eigen::Vector3f relative = world - Position;
        return Eigen::Vector3f( relative.dot( LeftToRight ),
                                relative.dot( Top ), relative.dot( Front ) );
    }
};

// Low level audio module
class edAudio : public edBase {
public:
    inline edAudioBackend *getBackend() const { return pBackend; }
    inline int32_t getChannelCount() const { return channelCount; }
    inline edAudioObject *getOutputModule() const { /* FUN_00610ea0 */
        return pOutputModule;
    }
    inline float getFrameProcessTime() const { return FrameProcessTime; }
    inline float getProcessorUsage() const { return ProcessorUsage; }
    inline edAudioListener &getListener() { /* FUN_006107e0 */
        return Listener;
    }
    inline const edAudioListener &getListener() const { return Listener; }
    inline const Eigen::Vector3f &getSpeakerDirection( int32_t channel ) const {
        // FUN_006107d0
        const int32_t clamped =
            ( channel < 0 )
                ? 0
                : ( channel >= kMaxSpeakers ? kMaxSpeakers - 1 : channel );
        return SpeakerDirections[clamped];
    }

public:
    edAudio();
    ~edAudio();

    int32_t initialize() override;
    void destroy() override;

    void setVolume( float volume );
    void audioThreadMain();

private:
    int32_t processOneBlock();

private:
    static constexpr int32_t kMaxSpeakers = 6;

public:
    edAudioBackend *pBackend;
    edAudioObject *pOutputModule;
    std::vector<float> mixBlock;
    int32_t channelCount;
    Eigen::Vector3f SpeakerDirections[kMaxSpeakers];
    edAudioListener Listener;
    TestDriveThread audioThread;
    std::atomic<bool> bAudioThreadMustLoop;
    float FrameProcessTime;
    float ProcessorUsage;
    float ThreadSleepTime;
    std::vector<float> interleavedBlock;
};

extern edAudio gEdAudio;
