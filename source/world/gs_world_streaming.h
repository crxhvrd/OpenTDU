#pragma once

#include "game/gs_base.h"

class StreamingCallback {
public:
    StreamingCallback();
    virtual ~StreamingCallback();

    virtual bool onWorldStreamed( Eigen::Vector4f *param_1,
                                  Eigen::Vector4f *param_2,
                                  Eigen::Vector4f *param_3 ) = 0;
};

class PhysicsStreamingCallback {
public:
    virtual ~PhysicsStreamingCallback() = default;

    virtual bool onWorldStreamed( Eigen::Vector4f *param_1,
                                  Eigen::Vector4f *param_2,
                                  Eigen::Vector4f *param_3 ) = 0;
};

class GSWorldStreaming : public GameSystem {
public:
    inline const Eigen::Vector4f &getPosition() const { return position; }
    inline const Eigen::Vector4f &getLastPosition() const {
        return prevPosition;
    }
    inline const Eigen::Vector4f &getOffset() const { return nextSectorOffset; }
    inline bool isEnabled() const { return bEnabled; }
    inline bool isStreamingActive() const { return bStreamingActive; }

    inline void setStreamCenter( Eigen::Vector4f *param_1 ) {
        pStreamCenter = param_1;
    }

    inline void setStreamCenterPosition( const Eigen::Vector4f &param_1 ) {
        streamCenter = param_1;
    }

    inline void setPosition( const Eigen::Vector4f &param_1 ) {
        position = param_1;
    }

    inline void setPhysicsCallback( PhysicsStreamingCallback *param_1 ) {
        pPhysicsCallback = param_1;
    }

    const char *getName() const override { return "World Streaming"; }

public:
    GSWorldStreaming();
    ~GSWorldStreaming();

    bool initialize( TestDriveGameInstance * ) override;
    void terminate() override;
    void tick( float totalTime, float deltaTime ) override;
    void updateStreaming();

    void resetStreamCenter();

    void registerCallback( StreamingCallback *param_1 );
    void unregisterCallback( StreamingCallback *param_1 );

private:
    static constexpr uint32_t kMaxNumCallbacks = 32;

private:
    Eigen::Vector4f position;
    Eigen::Vector4f prevPosition;
    Eigen::Vector4f nextSectorOffset;

    Eigen::Vector4f streamCenter;
    Eigen::Vector4f *pStreamCenter;

    std::array<StreamingCallback *, kMaxNumCallbacks> callbacks;
    PhysicsStreamingCallback *pPhysicsCallback;
    float size;
    uint8_t bEnabled : 1;
    uint8_t bStreamingActive : 1;
    uint8_t bHasJustFinishedStreaming : 1;
};

extern Eigen::Vector4f DAT_00fac350;
extern GSWorldStreaming *gpWorldStreaming;