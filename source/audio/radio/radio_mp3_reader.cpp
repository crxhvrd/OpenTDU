#include "shared.h"
#include "radio_mp3_reader.h"

#include "filesystem/file_direct_access.h"

#define MINIMP3_IMPLEMENTATION
#define MINIMP3_ONLY_MP3
#include "minimp3.h"

RadioMp3Reader::RadioMp3Reader( FileDirectAccess *pInFile )
    : pFile( pInFile ),
      pDecoder( nullptr ),
      input(),
      inputBytes( 0 ),
      interleaved(),
      planes(),
      ChannelCount( 0 ),
      SampleRate( 0 ),
      bEndOfFile( false ) {
    mp3dec_t *pMp3Decoder = new mp3dec_t();
    mp3dec_init( pMp3Decoder );
    pDecoder = pMp3Decoder;

    // Two chunks so a frame straddling a read boundary always fits.
    input.resize( kReadChunkSize * 2 );
    interleaved.resize( MINIMP3_MAX_SAMPLES_PER_FRAME );
}

RadioMp3Reader::~RadioMp3Reader() {
    delete static_cast<mp3dec_t *>( pDecoder );
    pDecoder = nullptr;
}

bool RadioMp3Reader::probe() {
    const bool bDecodedFrame = decodeFrame() > 0;
    seek( 0 );
    return bDecodedFrame;
}

bool RadioMp3Reader::getFormat( Format &format ) const {
    if ( ChannelCount <= 0 || SampleRate <= 0 ) {
        return false;
    }

    format.ChannelCount = ChannelCount;
    format.PlaneStride = kMaxFramesPerMPEGFrame;
    format.SampleRate = SampleRate;
    format.CodecFlags = 0;
    format.EndSample = 0;
    format.LoopEndSample = 0;
    format.MarkerCount = 0;

    return true;
}

const int16_t *RadioMp3Reader::decode( int32_t &sampleCount ) {
    sampleCount = 0;

    const int32_t frameCount = decodeFrame();
    if ( frameCount <= 0 || ChannelCount <= 0 ) {
        return nullptr;
    }

    const size_t planesSize =
        size_t( ChannelCount ) * size_t( kMaxFramesPerMPEGFrame );
    if ( planes.size() < planesSize ) {
        planes.resize( planesSize );
    }

    for ( int32_t channel = 0; channel < ChannelCount; channel++ ) {
        int16_t *pPlane = planes.data() + channel * kMaxFramesPerMPEGFrame;
        for ( int32_t frame = 0; frame < frameCount; frame++ ) {
            pPlane[frame] = interleaved[frame * ChannelCount + channel];
        }
    }

    sampleCount = frameCount;
    return planes.data();
}

int32_t RadioMp3Reader::seek( int32_t position ) {
    if ( position > 0 ) {
        OTDU_UNIMPLEMENTED_ONCE; // TODO: RADIO (no sample accurate MP3 seek)
    }

    inputBytes = 0;
    bEndOfFile = false;
    mp3dec_init( static_cast<mp3dec_t *>( pDecoder ) );

    if ( pFile != nullptr ) {
        pFile->seek( 0 );
    }

    return 0;
}

bool RadioMp3Reader::refillInput() {
    if ( bEndOfFile || pFile == nullptr ) {
        return false;
    }

    const uint32_t space = uint32_t( input.size() ) - inputBytes;
    if ( space == 0 ) {
        return false;
    }

    const uint32_t readBytes = pFile->readFromFile(
        input.data() + inputBytes, Min( space, kReadChunkSize ) );
    if ( readBytes == 0 ) {
        bEndOfFile = true;
        return false;
    }

    inputBytes += readBytes;
    return true;
}

int32_t RadioMp3Reader::decodeFrame() {
    mp3dec_t *pMp3Decoder = static_cast<mp3dec_t *>( pDecoder );

    while ( true ) {
        mp3dec_frame_info_t info = {};
        const int32_t frameCount =
            mp3dec_decode_frame( pMp3Decoder, input.data(), int( inputBytes ),
                                 interleaved.data(), &info );

        if ( info.frame_bytes > 0 ) {
            inputBytes -= uint32_t( info.frame_bytes );
            memmove( input.data(), input.data() + info.frame_bytes,
                     inputBytes );
        }

        if ( frameCount > 0 ) {
            ChannelCount = info.channels;
            SampleRate = info.hz;
            return frameCount;
        }

        // Nothing consumed means the decoder wants more of the stream; ID3 tags
        // and other padding come back as consumed bytes without any sample.
        if ( info.frame_bytes == 0 && !refillInput() ) {
            return 0;
        }
    }
}
