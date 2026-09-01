#include "shared.h"
#include "ed_audio_object.h"

#include "ed_audio.h"
#include "ed_audio_memory.h"

TestDriveMutex &GetAudioGraphMutex() {
    // FUN_00610820
    static TestDriveMutex gGraphMutex = {};
    return gGraphMutex;
}

static constexpr const char *kDefaultDescription =
    "No description set"; // DAT_00ef1b78

// 1/sqrt(2) (stereo to mono)
static constexpr float kHalfPower = 0.70710677f; // DAT_3f34fdf4

edAudioObject::edAudioObject( int32_t inputCount, int32_t outputCount )
    : InputChannelCount( inputCount ),
      OutputChannelCount( outputCount ),
      Volume( 1.0f ),
      Pitch( 1.0f ),
      bBypassed( false ),
      bMuted( false ),
      pParent( nullptr ),
      pNextEntry( nullptr ),
      pPreviousEntry( nullptr ),
      EntryCount( 0 ),
      pLastEntry( nullptr ) {
    // FUN_00612110
    debugSetDescription( kDefaultDescription );
}

edAudioObject::~edAudioObject() {
    // FUN_006123a0
    unlinkAllEntries();
}

void edAudioObject::addNewEntry( edAudioObject *pEntry ) {
    // FUN_00611f90
    ScopedMutexLock lock( &GetAudioGraphMutex() );

    if ( pLastEntry != nullptr ) {
        pLastEntry->pNextEntry = pEntry;
    }

    pEntry->pParent = this;
    pEntry->pNextEntry = nullptr;
    pEntry->pPreviousEntry = pLastEntry;

    EntryCount++;
    pLastEntry = pEntry;

    pEntry->onEntryAttached( this );
}

void edAudioObject::unlinkEntry( edAudioObject *pEntry ) {
    // FUN_00611fe0
    if ( pEntry == nullptr ) {
        return;
    }

    ScopedMutexLock lock( &GetAudioGraphMutex() );

    pEntry->onEntryDetached( this );

    if ( pEntry->pNextEntry != nullptr ) {
        pEntry->pNextEntry->pPreviousEntry = pEntry->pPreviousEntry;
    }
    if ( pEntry->pPreviousEntry != nullptr ) {
        pEntry->pPreviousEntry->pNextEntry = pEntry->pNextEntry;
    }
    if ( pEntry == pLastEntry ) {
        pLastEntry = pEntry->pPreviousEntry;
    }

    pEntry->pParent = nullptr;
    pEntry->pNextEntry = nullptr;
    pEntry->pPreviousEntry = nullptr;
    EntryCount--;
}

void edAudioObject::unlinkAllEntries() {
    // FUN_00612190 -> FUN_00612050
    ScopedMutexLock lock( &GetAudioGraphMutex() );

    while ( pLastEntry != nullptr ) {
        edAudioObject *pEntry = pLastEntry;

        pEntry->onEntryDetached( this );

        if ( pEntry->pNextEntry != nullptr ) {
            pEntry->pNextEntry->pPreviousEntry = pEntry->pPreviousEntry;
        }
        if ( pEntry->pPreviousEntry != nullptr ) {
            pEntry->pPreviousEntry->pNextEntry = pEntry->pNextEntry;
        }
        if ( pEntry == pLastEntry ) {
            pLastEntry = pLastEntry->pPreviousEntry;
        }

        pEntry->pParent = nullptr;
        pEntry->pNextEntry = nullptr;
        pEntry->pPreviousEntry = nullptr;
        EntryCount--;
    }
}

void edAudioObject::debugSetDescription( const char *pInDescription ) {
    // FUN_006120f0
    snprintf( pDescription, sizeof( pDescription ), "%s", pInDescription );
}

void edAudioObject::gatherEntries( edAudioDSPNode &node,
                                   edAudioProcessInputs &inputs ) {
    // FUN_00612280
    for ( edAudioObject *pEntry = pLastEntry; pEntry != nullptr;
          pEntry = pEntry->pPreviousEntry ) {
        float *pEntryBuffer = pEntry->process( node );

        if ( pEntry->bMuted ) {
            continue;
        }

        bool bAlreadyGathered = false;
        for ( int32_t i = 0; i < inputs.Count; i++ ) {
            if ( inputs.ppBuffers[i] == pEntryBuffer ) {
                bAlreadyGathered = true;
                break;
            }
        }

        if ( !bAlreadyGathered && inputs.Count < kMaxAudioModuleInputs ) {
            inputs.ppBuffers[inputs.Count] = pEntryBuffer;
            inputs.Count++;
        }
    }
}

float *edAudioObject::process( const edAudioDSPNode &node ) {
    // FUN_006121b0
    edAudioDSPNode localSettings = node;

    float *pInputBuffers[kMaxAudioModuleInputs] = {};
    edAudioProcessInputs inputs = {};
    inputs.ppBuffers = pInputBuffers;

    if ( !prepare( localSettings ) || bBypassed ) {
        return localSettings.pWorkingBuffer;
    }

    localSettings.Volume *= bMuted ? 0.0f : Volume;
    localSettings.Pitch *= Pitch;

    gatherEntries( localSettings, inputs );
    internalProcess( localSettings, inputs );

    return localSettings.pWorkingBuffer;
}

edAudioBuffer::edAudioBuffer( int32_t channelCount )
    : edAudioObject( channelCount, channelCount ), pBuffer( nullptr ) {
    // FUN_00612430
}

edAudioBuffer::~edAudioBuffer() {
    // FUN_006124e0
    dynamicAudioMemoryFree();
}

void edAudioBuffer::onEntryAttached( edAudioObject * ) {
    dynamicAudioMemoryAllocate();
}

void edAudioBuffer::onEntryDetached( edAudioObject * ) {
    dynamicAudioMemoryFree();
}

void edAudioBuffer::dynamicAudioMemoryAllocate() {
    // FUN_00612480
    pBuffer = static_cast<float *>(
        edAudioHeap::Allocate( OutputChannelCount << 12 ) );
}

void edAudioBuffer::dynamicAudioMemoryFree() {
    // FUN_00612460
    edAudioHeap::Free( pBuffer );
    pBuffer = nullptr;
}

bool edAudioBuffer::prepare( edAudioDSPNode &node ) {
    // FUN_006124a0
    node.pWorkingBuffer = pBuffer;
    if ( pBuffer == nullptr ) {
        return true;
    }

    const uint32_t sampleCount = node.FrameCount * OutputChannelCount;
    for ( uint32_t i = 0; i < sampleCount; i++ ) {
        pBuffer[i] = 0.0f;
    }

    return true;
}

edAudioChannelAdapter::edAudioChannelAdapter( int32_t inputCount,
                                              int32_t outputCount )
    : edAudioSpatializer( inputCount, outputCount ) {
    // FUN_006126b0
    fillMatrix( 0.0f );

    if ( inputCount == outputCount ) {
        for ( int32_t channel = 0; channel < inputCount; channel++ ) {
            setGain( 1.0f, channel, channel );
        }
        return;
    }

    switch ( inputCount ) {
    case 1:
        switch ( outputCount ) {
        case 2:
            setGain( 1.0f, 0, 0 );
            setGain( 1.0f, 0, 1 );
            break;
        case 4:
            for ( int32_t d = 0; d < 4; d++ ) {
                setGain( 1.0f, 0, d );
            }
            break;
        case 5:
            for ( int32_t d = 0; d < 4; d++ ) {
                setGain( 1.0f, 0, d );
            }
            setGain( 0.5f, 0, 4 );
            break;
        case 6:
        case 8:
            for ( int32_t d = 0; d < 6; d++ ) {
                setGain( 1.0f, 0, d );
            }
            break;
        }
        break;
    case 2:
        switch ( outputCount ) {
        case 1:
            setGain( kHalfPower, 0, 0 );
            setGain( kHalfPower, 1, 0 );
            break;
        case 4:
            setGain( 0.5f, 0, 0 );
            setGain( 0.5f, 1, 1 );
            setGain( 0.5f, 0, 2 );
            setGain( 0.5f, 1, 3 );
            break;
        case 5:
            setGain( 1.0f, 0, 0 );
            setGain( 1.0f, 1, 1 );
            setGain( 1.0f, 0, 2 );
            setGain( 1.0f, 1, 3 );
            setGain( 0.5f, 0, 4 );
            setGain( 0.5f, 1, 4 );
            break;
        case 6:
        case 8:
            setGain( 1.0f, 0, 0 );
            setGain( 1.0f, 1, 1 );
            setGain( 1.0f, 0, 2 );
            setGain( 1.0f, 1, 3 );
            setGain( 0.5f, 0, 4 );
            setGain( 0.5f, 1, 4 );
            setGain( 0.5f, 0, 5 );
            setGain( 0.5f, 1, 5 );
            break;
        }
        break;
    case 4:
        switch ( outputCount ) {
        case 1:
            for ( int32_t s = 0; s < 4; s++ ) {
                setGain( 0.5f, s, 0 );
            }
            break;
        case 2:
            setGain( 0.5f, 0, 0 );
            setGain( 0.5f, 1, 1 );
            setGain( 0.5f, 2, 0 );
            setGain( 0.5f, 3, 1 );
            break;
        case 5:
            for ( int32_t c = 0; c < 4; c++ ) {
                setGain( 1.0f, c, c );
            }
            setGain( 0.5f, 0, 4 );
            setGain( 0.5f, 1, 4 );
            break;
        case 6:
        case 8:
            for ( int32_t c = 0; c < 4; c++ ) {
                setGain( 1.0f, c, c );
            }
            setGain( 0.5f, 0, 4 );
            setGain( 0.5f, 1, 4 );
            for ( int32_t s = 0; s < 4; s++ ) {
                setGain( 0.25f, s, 5 );
            }
            break;
        }
        break;
    case 5:
        switch ( outputCount ) {
        case 1:
            for ( int32_t s = 0; s < 5; s++ ) {
                setGain( 0.5f, s, 0 );
            }
            break;
        case 2:
            setGain( 0.5f, 0, 0 );
            setGain( 0.5f, 1, 1 );
            setGain( 0.5f, 2, 0 );
            setGain( 0.5f, 3, 1 );
            break;
        case 4:
            for ( int32_t c = 0; c < 4; c++ ) {
                setGain( 1.0f, c, c );
            }
            setGain( 0.5f, 4, 0 );
            setGain( 0.5f, 4, 1 );
            break;
        case 6:
        case 8:
            for ( int32_t c = 0; c < 4; c++ ) {
                setGain( 1.0f, c, c );
            }
            setGain( 1.0f, 4, 4 );
            for ( int32_t s = 0; s < 4; s++ ) {
                setGain( 0.25f, s, 5 );
            }
            break;
        }
        break;
    }
}

edAudioSpatializer::edAudioSpatializer( int32_t inputCount,
                                        int32_t outputCount )
    : edAudioObject( inputCount, outputCount ), Matrix{} {
    // FUN_00615200
    debugSetDescription( "Spatializer" ); // FUN_00615520
}

edAudioSpatializer::~edAudioSpatializer() {}

void edAudioSpatializer::setGain( float gain,
                                  int32_t sourceChannel,
                                  int32_t destinationChannel ) {
    // FUN_00615230
    Matrix[sourceChannel + destinationChannel * kMaxMatrixChannels] = gain;
}

float edAudioSpatializer::getGain( int32_t sourceChannel,
                                   int32_t destinationChannel ) const {
    // FUN_00615250
    return Matrix[sourceChannel + destinationChannel * kMaxMatrixChannels];
}

void edAudioSpatializer::fillMatrix( float value ) {
    // FUN_00615270
    for ( int32_t destination = 0; destination < OutputChannelCount;
          destination++ ) {
        for ( int32_t source = 0; source < InputChannelCount; source++ ) {
            Matrix[source + destination * kMaxMatrixChannels] = value;
        }
    }
}

void edAudioSpatializer::pan( const Eigen::Vector3f &direction,
                              float focus,
                              int32_t sourceChannel ) {
    // FUN_00615550
    if ( direction.squaredNorm() == 0.0f ) {
        for ( int32_t destination = 0; destination < OutputChannelCount;
              destination++ ) {
            setGain( 1.0f, sourceChannel, destination );
        }

        return;
    }

    const Eigen::Vector3f unitDirection = direction.normalized();

    const float elevationFade = 1.0f - fabsf( unitDirection.y() );

    for ( int32_t destination = 0; destination < OutputChannelCount;
          destination++ ) {
        const Eigen::Vector3f &speaker =
            gEdAudio.getSpeakerDirection( destination );

        const float cosAngle = unitDirection.dot( speaker );
        const float gain = ( cosAngle - 1.0f ) * elevationFade * focus + 1.0f;

        if ( getGain( sourceChannel, destination ) < gain ) {
            setGain( gain, sourceChannel, destination );
        }
    }
}

void edAudioSpatializer::internalProcess( edAudioDSPNode &node,
                                          const edAudioProcessInputs &inputs ) {
    // FUN_006153e0 -> FUN_006152c0
    if ( inputs.Count == 0 || node.pWorkingBuffer == nullptr ) {
        return;
    }

    // Only the first entry's buffer is routed.
    const float *pSource = inputs.ppBuffers[0];
    if ( pSource == nullptr ) {
        return;
    }

    for ( int32_t destination = 0; destination < OutputChannelCount;
          destination++ ) {
        const float *pRow = &Matrix[destination * kMaxMatrixChannels];

        float rowSum = 0.0f;
        for ( int32_t source = 0; source < InputChannelCount; source++ ) {
            rowSum += pRow[source];
        }
        if ( rowSum <= 0.0f ) {
            continue;
        }

        float *pDestinationPlane =
            node.pWorkingBuffer + node.FrameCount * destination;

        for ( int32_t source = 0; source < InputChannelCount; source++ ) {
            const float gain = pRow[source];
            if ( gain <= 0.0f ) {
                continue;
            }

            const float *pSourcePlane = pSource + node.FrameCount * source;
            for ( uint32_t frame = 0; frame < node.FrameCount; frame++ ) {
                pDestinationPlane[frame] += pSourcePlane[frame] * gain;
            }
        }
    }
}

edAudioCluster::edAudioCluster()
    : edAudioSpatializer( 1, kSpatialChannelCount ),
      Buffer( 1 ),
      Position( Eigen::Vector3f::Zero() ) {
    // FUN_00615130
    Buffer.debugSetDescription( "Cluster buffer" );
    addNewEntry( &Buffer );

    debugSetDescription( "Cluster" );
}

edAudioCluster::~edAudioCluster() {
    // FUN_00615100
    unlinkEntry( &Buffer );

    Buffer.unlinkAllEntries();
    unlinkAllEntries();
}

edAudioMixer::edAudioMixer( int32_t channelCount )
    : edAudioObject( channelCount, channelCount ) {
    // FUN_00612500
}

edAudioMixer::~edAudioMixer() {
    // FUN_00612690
}

void edAudioMixer::internalProcess( edAudioDSPNode &node,
                                    const edAudioProcessInputs &inputs ) {
    // FUN_00612520
    float *pWorkingBuffer = node.pWorkingBuffer;
    if ( pWorkingBuffer == nullptr ) {
        return;
    }

    for ( int32_t channel = 0; channel < OutputChannelCount; channel++ ) {
        const uint32_t firstSample = node.FrameCount * channel;
        const uint32_t lastSample = firstSample + node.FrameCount;

        for ( int32_t input = 0; input < inputs.Count; input++ ) {
            const float *pSource = inputs.ppBuffers[input];
            for ( uint32_t sample = firstSample; sample < lastSample;
                  sample++ ) {
                pWorkingBuffer[sample] += pSource[sample];
            }
        }
    }
}
