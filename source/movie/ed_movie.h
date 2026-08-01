#pragma once

#include "core/ed_base.h"

#include "bink_audio_stream.h"

class BinkMovie;
struct BinkMovieNode;
struct BinkFramePointer;
class edAudioStreamEntry;

class edMovie : public edBase {
public:
    edMovie();
    ~edMovie();

    virtual int32_t initialize() override;
    virtual void destroy() override;

    BinkFramePointer *openBik( const char *param_2 );
    void stepBikPlayback();

    bool attachAudioVoice( edAudioStreamEntry *pVoice );
    void detachAudioVoice( edAudioStreamEntry *pVoice );

    uint32_t SizeForUnknown;
    void *pUnknownPointer;

    BinkMovieNode *pActiveMovie;
    BinkMovieNode *pActiveMovieIt;

private:
    MovieAudioPipeline audioPipeline;
};

extern edMovie gEdMovie;