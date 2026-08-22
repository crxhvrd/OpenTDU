#pragma once

#include "game/gs_base.h"

class TutorialHint;
class edStore;

class GSTutorial : public GameSystem {
public:
    const char *getName() const override { return "Service : Tutorial"; }

public:
    static constexpr int32_t kMaxTutorialHints = 0x40;
    static constexpr int32_t kNumTutorialSteps = 0x40;
    static constexpr uint32_t kTutorialDatabaseIndex = 0xbu;

public:
    inline int32_t getNumTutorialHints() const { return numTutorialMsgs; }

public:
    GSTutorial();
    ~GSTutorial();

    bool initialize( TestDriveGameInstance * ) override;
    void terminate() override;

    void cancelDisplayedMessages();
    TutorialHint *findMessage( const uint32_t id );
    void deserialize( edStore &stream );
    void serialize( edStore &stream ) const;

public:
    int32_t numTutorialMsgs = 0;                          
    bool bFlashPopupOn = false;                           
    TutorialHint *pTutorialHints[kMaxTutorialHints] = {};
    bool bEnabled;                                        
    bool bPopupDisplayed;                                 
    bool bStepDone[kNumTutorialSteps] = {};               
};

extern GSTutorial *gpTutorial;

extern bool gbTutorialEnabled;
extern bool gbScenarioTutorialEnabled;
