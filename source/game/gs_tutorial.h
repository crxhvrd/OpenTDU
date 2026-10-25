#pragma once

#include "game/gs_base.h"

class TutorialHint;
class edStore;
class edAudioSample;
class AudioClient;
class edAudioStreamEntry;
class FlashPlayer;
class Entity;
struct SpotMap;

class GSTutorial : public GameSystem {
public:
    const char *getName() const override { return "Service : Tutorial"; }

public:
    static constexpr int32_t kMaxTutorialHints = 0x40;
    static constexpr int32_t kNumHelpContexts = 0x40;
    static constexpr int32_t kSoundNameLength = 0x20;
    static constexpr uint32_t kTutorialDatabaseIndex = 0xbu;

public:
    inline int32_t getNumTutorialHints() const { return numTutorialMsgs; }

public:
    GSTutorial();
    ~GSTutorial();

    bool initialize( TestDriveGameInstance * ) override;
    void tick( float totalTime, float deltaTime ) override;
    void pause() override;
    void terminate() override;

    void resetProgress();
    void onMapRequested();
    void onSpotLaunched( SpotMap *pSpot );
    void onExtraChallengeLaunched( Entity *pExtraChallenge );
    void displayHelp( FlashPlayer *pPlayer, const int32_t context );
    void stopVoice();
    TutorialHint *findMessage( const uint32_t id );
    void deserialize( edStore &stream );
    void serialize( edStore &stream ) const;

private:
    void showHint( TutorialHint *pHint );
    void hideHint( TutorialHint *pHint );
    void updateHints( const float deltaTime );
    void loadVoice( const char *pSoundName );
    void onVoiceLoaded();
    void releaseVoice();

public:
    int32_t numTutorialMsgs = 0;
    bool bFlashPopupOn = false;
    TutorialHint *pTutorialHints[kMaxTutorialHints] = {};
    bool bEnabled;
    bool bPopupDisplayed;
    void *pSoundBuffer;
    uint32_t SoundBufferSize;
    std::unique_ptr<edAudioSample> pSoundSample;
    std::shared_ptr<bool> pInstallGuard;
    AudioClient *pAudioClient;
    edAudioStreamEntry *pSoundInstance;
    bool bLoadingSound;
    bool bHelpLaunched[kNumHelpContexts] = {};
    char SoundName[kSoundNameLength] = {};
};

extern GSTutorial *gpTutorial;
extern bool gbTutorialEnabled;
extern bool gbScenarioTutorialEnabled;
