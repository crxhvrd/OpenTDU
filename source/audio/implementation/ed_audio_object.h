#pragma once

#include "core/mutex.h"

// Entries a single module may gather inputs from in one block.
static constexpr int32_t kMaxAudioModuleInputs = 128;
// Width of the spatial bus.
static constexpr int32_t kSpatialChannelCount = 5;

// One node of the DSP graph.
struct edAudioDSPNode {
    float Volume;
    float Pitch;
    uint32_t FrameCount;
    float *pWorkingBuffer;
};

struct edAudioProcessInputs {
    int32_t Count;
    float **ppBuffers;
};

TestDriveMutex &GetAudioGraphMutex();

class edAudioObject {
public:
    inline int32_t getInputChannelCount() const { return InputChannelCount; }
    inline int32_t getOutputChannelCount() const { return OutputChannelCount; }
    inline const char *getDescription() const { return pDescription; }
    inline int32_t getEntryCount() const { return EntryCount; }
    inline edAudioObject *getParent() const { return pParent; }
    inline edAudioObject *getLastEntry() const { return pLastEntry; }
    inline edAudioObject *getPreviousEntry() const { return pPreviousEntry; }

public:
    edAudioObject( int32_t inputCount, int32_t outputCount );
    virtual ~edAudioObject();

    virtual void onEntryAttached( edAudioObject *pParent ) {}
    virtual void onEntryDetached( edAudioObject *pParent ) {}
    virtual bool prepare( edAudioDSPNode &node ) { return true; }
    virtual void internalProcess( edAudioDSPNode &node,
                                  const edAudioProcessInputs &inputs ) {}

    float *process( const edAudioDSPNode &node );
    void addNewEntry( edAudioObject *pEntry );
    void unlinkAllEntries();
    void unlinkEntry( edAudioObject *pEntry );
    void debugSetDescription( const char *pInDescription );

public:
    int32_t InputChannelCount;
    int32_t OutputChannelCount;
    float Volume;
    float Pitch;
    bool bBypassed;
    bool bMuted;

protected:
    void gatherEntries( edAudioDSPNode &node, edAudioProcessInputs &inputs );

protected:
    char pDescription[32];
    edAudioObject *pParent;
    edAudioObject *pNextEntry;
    edAudioObject *pPreviousEntry;
    int32_t EntryCount;
    edAudioObject *pLastEntry;
};

class edAudioBuffer : public edAudioObject {
public:
    explicit edAudioBuffer( int32_t channelCount );
    ~edAudioBuffer() override;

    void onEntryAttached( edAudioObject *pParent ) override;
    void onEntryDetached( edAudioObject *pParent ) override;

    void dynamicAudioMemoryAllocate();
    void dynamicAudioMemoryFree();

    bool prepare( edAudioDSPNode &node ) override;

private:
    float *pBuffer;
};

class edAudioSpatializer : public edAudioObject {
public:
    static constexpr int32_t kMaxMatrixChannels = 8;

public:
    edAudioSpatializer( int32_t inputCount, int32_t outputCount );
    ~edAudioSpatializer() override;

    void
    setGain( float gain, int32_t sourceChannel, int32_t destinationChannel );
    float getGain( int32_t sourceChannel, int32_t destinationChannel ) const;
    void fillMatrix( float value );

    void
    pan( const Eigen::Vector3f &direction, float focus, int32_t sourceChannel );

    void internalProcess( edAudioDSPNode &node,
                          const edAudioProcessInputs &inputs ) override;

private:
    float Matrix[kMaxMatrixChannels * kMaxMatrixChannels];
};

class edAudioChannelAdapter : public edAudioSpatializer {
public:
    edAudioChannelAdapter( int32_t inputCount, int32_t outputCount );
};

class edAudioCluster : public edAudioSpatializer {
public:
    inline edAudioBuffer *getBuffer() { return &Buffer; }
    inline const Eigen::Vector3f &getPosition() const { return Position; }
    inline void setPosition( const Eigen::Vector3f &position ) {
        Position = position;
    }

public:
    edAudioCluster();
    ~edAudioCluster() override;

private:
    edAudioBuffer Buffer;
    Eigen::Vector3f Position;
};

// Sums its entries into the working buffer, channel by channel.
class edAudioMixer : public edAudioObject {
public:
    explicit edAudioMixer( int32_t channelCount );
    ~edAudioMixer() override;

    void internalProcess( edAudioDSPNode &node,
                          const edAudioProcessInputs &inputs ) override;
};
