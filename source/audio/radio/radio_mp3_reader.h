#pragma once

#include "audio/implementation/ed_audio_stream_entry.h"

#include <vector>

struct FileDirectAccess;

// minimp3-based MP3 decoder
class RadioMp3Reader final : public edAudioStreamReader {
public:
    static constexpr uint32_t kReadChunkSize = 0x4000;
    static constexpr int32_t kMaxFramesPerMPEGFrame = 1152;

public:
    explicit RadioMp3Reader( FileDirectAccess *pInFile );
    ~RadioMp3Reader() override;

    bool probe();

    bool getFormat( Format &format ) const override;
    const int16_t *decode( int32_t &sampleCount ) override;
    int32_t seek( int32_t position ) override;

private:
    bool refillInput();
    int32_t decodeFrame();

private:
    FileDirectAccess *pFile;
    void *pDecoder;
    std::vector<uint8_t> input;
    uint32_t inputBytes;
    std::vector<int16_t> interleaved;
    std::vector<int16_t> planes;
    int32_t ChannelCount;
    int32_t SampleRate;
    bool bEndOfFile;
};
