#pragma once

#include "audio/gs_audio.h"

class EntitySoundEmitter;
class ScenaricBank;

class CinematicMusicEmitter {
public:
    CinematicMusicEmitter( uint8_t *pInLeftRef, uint8_t *pInRightRef );
    ~CinematicMusicEmitter();

    void resolveEmitters( ScenaricBank *pBank );
    void installSoundBanks();
    void uninstallSoundBanks();
    bool hasBothEmitters() const;
    bool areEmittersResolved() const;
    bool areSoundBanksInstalling() const;
    void play( bool bLoop, float fadeTime );
    void stop( float fadeTime );
    void setPaused( bool bPaused, float fadeTime );
    void releaseChannels();

private:
    EntitySoundEmitter *getEmitter( uint8_t *pRef ) const;

private:
    uint8_t *pLeftRef;  
    uint8_t *pRightRef; 
    AudioStereoSource source;
    bool bAddedToPipeline;
    bool bReleased;       
    float pendingFadeTime; 
};
