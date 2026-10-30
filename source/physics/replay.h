#pragma once

#include "shared.h"

class FlashPlayer;
class PhysicsVehicle;
struct FlashMessage;
struct ReplayDriving;

extern uint32_t gRPLConfig[5];        // DAT_010d6f38
extern int32_t gReplayModeDebugIndex; // DAT_010e7c18

enum class eReplayState : int32_t {
    RS_Off = 0,
    RS_Recording = 1,
    RS_StartPlayback = 2,
    RS_WaitForRecording = 3,
    RS_Stop = 4,
    RS_Playback = 5,
};

// Instant replay recorder: three ring buffers sharing one allocation, each
// written every N physics steps (snapshots, poses and driving inputs).
class Replay {
public:
    inline eReplayState getState() const { return state; }
    inline void setState( const eReplayState newState ) { state = newState; }
    inline bool isPlayingBack() const {
        return state == eReplayState::RS_Playback;
    }
    inline bool isInitialized() const { return bInitialized; }
    inline int32_t getSlowDownFactor() const { return slowDownFactor; }
    inline int32_t getSpeedUpFactor() const { return speedUpFactor; }
    inline int32_t getNumFrames() const { return numFrames; }
    inline int32_t getNumRecordedFrames() const { return numRecordedFrames; }
    inline void requestStop() { bStopRequested = 1; }
    inline bool isExitRequested() const { return bExitRequested != 0; }
    inline void setRemotePlayer( FlashPlayer *pPlayer ) {
        pRemotePlayer = pPlayer;
    }

    inline bool isCursorBeforeLastPose() const {
        if ( firstFrame <= lastPoseFrame ) {
            return cursor < lastPoseFrame;
        }
        return cursor < lastPoseFrame || firstFrame <= cursor;
    }

    inline float getTimeScale() const {
        if ( state == eReplayState::RS_Playback ) {
            if ( slowDownFactor > 1 ) {
                return 1.0f / static_cast<float>( slowDownFactor );
            }
            if ( speedUpFactor > 1 ) {
                return static_cast<float>( speedUpFactor );
            }
        }
        return 1.0f;
    }

public:
    Replay();

    void initialize( const uint32_t size );
    void release();
    void reset();
    void setupRings( const int32_t numVehicles );
    void resetSession();

    void record( const bool bStep );
    bool playback( const bool bPaused );
    void advanceSlowDownSubStep();
    void tick();
    bool onFlashMessage( const FlashMessage &message );

    static void PackDriving( const ReplayDriving &driving, uint8_t *pRecord );
    static void UnpackDriving( const uint8_t *pRecord, ReplayDriving &driving );

private:
    void requestPlayback();
    void rewind();
    void exitPlayback();
    void restoreCamera();
    void applyCameraSwitchRequest();
    void updatePendingFlags();
    void applyPoseRecords( PhysicsVehicle &vehicle, const int32_t index );
    void updateRemoteVariables( PhysicsVehicle &vehicle );
    void getCameraLabel( char *pLabel ) const;

    template <typename Fn> void forEachRecordedVehicle( Fn &&fn );

private:
    uint32_t bufferSize;          // +0x00
    uint8_t *pBuffer;             // +0x04
    uint8_t *pSnapshotRing;       // +0x08
    uint8_t *pPoseRing;           // +0x0c
    uint8_t *pInputRing;          // +0x10
    int32_t snapshotRecordSize;   // +0x14
    int32_t poseRecordSize;       // +0x18
    int32_t inputRecordSize;      // +0x1c
    int32_t inputRingSize;        // +0x20
    int32_t poseRingSize;         // +0x24
    int32_t snapshotRingSize;     // +0x28
    int32_t snapshotFrequency;    // +0x2c
    int32_t poseFrequency;        // +0x30
    int32_t inputFrequency;       // +0x34
    int32_t numSnapshotRecords;   // +0x38
    int32_t numPoseRecords;       // +0x3c
    int32_t numInputRecords;      // +0x40
    int32_t numVehicles;          // +0x44
    int32_t numFrames;            // +0x48
    int32_t cursor;               // +0x4c
    int32_t numRecordedFrames;    // +0x50
    int32_t firstFrame;           // +0x54
    int32_t writeFrame;           // +0x58
    int32_t field_0x5c;           // +0x5c
    int32_t lastPoseFrame;        // +0x60
    int32_t lastSnapshotFrame;    // +0x64
    int32_t lastInputFrame;       // +0x68
    eReplayState state;           // +0x6c
    int32_t field_0x70;           // +0x70
    int32_t slowDownDisplayValue; // +0x74
    int32_t slowDownFactor;       // +0x78
    int32_t speedUpFactor;        // +0x7c
    int32_t field_0x80;           // +0x80
    int32_t field_0x84;           // +0x84
    int32_t slowDownSubStep;      // +0x88
    int32_t savedCameraMode;      // +0x8c
    int32_t field_0x90;           // +0x90
    int32_t field_0x94;           // +0x94
    uint8_t bPlaybackStarted;     // +0x98
    uint8_t bExitRequested;       // +0x99
    uint8_t bPaused;              // +0x9a
    uint8_t bStopRequested;       // +0x9b
    uint8_t bInitialized;         // +0x9c
    uint8_t bReplayRequestHidden; // +0x9d
    uint8_t bRewound;             // +0x9e
    uint8_t bRestoreCamera;       // +0x9f
    uint8_t bForceCameraSwitch;   // +0xa0
    int32_t replayExitRequest;    // +0xa4
    FlashPlayer *pRemotePlayer;   // +0xa8
};

extern Replay gReplay; // 0x00fa9770
