#pragma once

#include "core/ed_base.h"

class edAudioBackend;

// Low level audio module (module name "edAudio", see 00ef1af8).
class edAudio : public edBase {
public:
    inline edAudioBackend *getBackend() const { return pBackend; }
    inline int32_t getChannelCount() const { return channelCount; }

public:
    edAudio();
    ~edAudio();

    int32_t initialize() override;
    void destroy() override;

    void setVolume( float volume );

private:
    edAudioBackend *pBackend;
    int32_t channelCount;
};

extern edAudio gEdAudio;
