#pragma once

struct SpotMap;
class Entity;

enum eTutorialHintID : uint32_t {
    TMID_Back = 0x6261636bu,             // 'back'
    TMID_BackView = 0x62616376u,         // 'bacv'
    TMID_CameraView = 0x63616d76u,       // 'camv'
    TMID_DPad = 0x64706164u,             // 'dpad'
    TMID_Gearbox = 0x67656172u,          // 'gear'
    TMID_InstantChallenge = 0x69636861u, // 'icha'
    TMID_LookAround = 0x6c6f6f6bu,       // 'look'
    TMID_Star = 0x73746172u,             // 'star'

    TMID_Map = 0x6d617020u, // 'map '

    TMID_AfterMarket = 0x61667465u,   // 'afte'
    TMID_Clothes = 0x636c6f74u,       // 'clot'
    TMID_Club = 0x636c7562u,          // 'club'
    TMID_Convoy = 0x636f6e31u,        // 'con1'
    TMID_Dealer = 0x6465616cu,        // 'deal'
    TMID_DealerBike = 0x64656142u,    // 'deaB'
    TMID_DrivingSchool = 0x64726976u, // 'driv'
    TMID_Estate = 0x65737461u,        // 'esta'
    TMID_HitchHiker = 0x68697463u,    // 'hitc'
    TMID_PeiLarge = 0x7065694cu,      // 'peiL'
    TMID_PeiSmall = 0x70656953u,      // 'peiS'
    TMID_RaceMulti = 0x7261634du,     // 'racM'
    TMID_RaceSolo = 0x72616353u,      // 'racS'
    TMID_Rental = 0x72656e74u,        // 'rent'
    TMID_Shopping = 0x73686f70u,      // 'shop'
    TMID_TimeSolo = 0x74656d53u,      // 'temS'
    TMID_Transport = 0x7472616eu,     // 'tran'
    TMID_SpeedMulti = 0x7669744du,    // 'vitM'
    TMID_SpeedSolo = 0x76697453u,     // 'vitS'
};

// CTutMsg (base vtable 0x00f0a61c)
class TutorialHint {
public:
    static constexpr float kDefaultTimerCondition = 5.0f; // DAT_00f41750
    static constexpr float kDefaultTimerOnScreen = 7.0f;  // DAT_00f41844

public:
    explicit TutorialHint( const uint32_t id );
    virtual ~TutorialHint();

    virtual bool isConditionMet() = 0;
    virtual bool canDisplay() {
        // FUN_0042df00
        return true;
    }
    virtual bool canRearm() {
        // FUN_00630320
        return false;
    }
    virtual bool isCompleted() {
        // FUN_00630320
        return false;
    }
    virtual void onSpotLaunched( SpotMap * ) {
        // FUN_00614df0
    }
    virtual void onExtraChallengeLaunched( Entity * ) {
        // FUN_00614df0
    }
    virtual void onMapRequested() {
        // FUN_0063f2b0
    }
    virtual const char *getName() const {
        // FUN_00405e00
        return "UNKN";
    }
    virtual void onDisplayed() {
        // FUN_0063f2b0
    }
    virtual void update( SpotMap *, Entity * ) {
        // FUN_00407f80
    }

public:
    uint32_t ID;
    int32_t DBLine;
    float CurrentTimer;
    float TimerCondition;
    float TimerOnScreen;
    bool bSeen;
    bool bOnScreen;
    bool bCancel;
};

class TutorialHintBack : public TutorialHint {
public:
    TutorialHintBack();

    bool isConditionMet() override;
    bool canRearm() override;
    const char *getName() const override {
        // FUN_00405370
        return "botr";
    }
};

class TutorialHintLookAround : public TutorialHint {
public:
    TutorialHintLookAround();

    bool isConditionMet() override;
    bool canRearm() override {
        // FUN_0042df00
        return true;
    }
    bool isCompleted() override;
    const char *getName() const override {
        // FUN_004053e0
        return "look";
    }
};

class TutorialHintDPad : public TutorialHint {
public:
    TutorialHintDPad();

    bool isConditionMet() override {
        // FUN_0042df00
        return true;
    }
    bool canRearm() override {
        // FUN_0042df00
        return true;
    }
    bool isCompleted() override;
    const char *getName() const override {
        // FUN_004053b0
        return "dpad";
    }
};

class TutorialHintBackView : public TutorialHint {
public:
    TutorialHintBackView();

    bool isConditionMet() override;
    bool isCompleted() override;
    const char *getName() const override {
        // FUN_00405490
        return "bakv";
    }
};

class TutorialHintGearbox : public TutorialHint {
public:
    TutorialHintGearbox();

    bool isConditionMet() override;
    bool isCompleted() override;
    const char *getName() const override {
        // FUN_00405860
        return "gear";
    }
};

class TutorialHintCameraView : public TutorialHint {
public:
    TutorialHintCameraView();

    bool isConditionMet() override {
        // FUN_0042df00
        return true;
    }
    bool isCompleted() override;
    const char *getName() const override {
        // FUN_004054d0
        return "camv";
    }
};

class TutorialHintStar : public TutorialHint {
public:
    TutorialHintStar();

    bool isConditionMet() override;
    bool isCompleted() override;
    const char *getName() const override {
        // FUN_00405700
        return "star";
    }
};

class TutorialHintInstantChallenge : public TutorialHint {
public:
    TutorialHintInstantChallenge();

    bool isConditionMet() override;
    const char *getName() const override {
        // FUN_004055b0
        return "icha";
    }
};

class TutorialHintMap : public TutorialHint {
public:
    TutorialHintMap();

    bool isConditionMet() override;
    bool isCompleted() override;
    void onMapRequested() override;
    const char *getName() const override {
        // FUN_00405540
        return "map ";
    }

public:
    bool bMapRequested;
};

class TutorialHintTrigger : public TutorialHint {
public:
    static constexpr uint8_t kNoKind = 0xffu;
    static constexpr float kTimerCondition = 0.1f; // FLOAT_00f41668
    static constexpr float kRearmDelay = 60.0f;    // FLOAT_00f41744

public:
    TutorialHintTrigger( const uint32_t id,
                         const uint8_t kind,
                         const uint8_t subKind,
                         const uint32_t extraChallengeID = 0u );

    bool isConditionMet() override;
    bool canRearm() override;
    void onSpotLaunched( SpotMap *pSpot ) override;
    const char *getName() const override {
        // FUN_00405740
        return "spot";
    }
    void onDisplayed() override;
    void update( SpotMap *pSpot, Entity *pExtraChallenge ) override;

    void setLaunchedExtraChallenge( Entity *pExtraChallenge );

public:
    SpotMap *pLaunchedSpot;
    SpotMap *pNearestSpot;
    Entity *pLaunchedExtraChallenge;
    Entity *pNearestExtraChallenge;
    uint8_t Kind;
    uint8_t SubKind;
    uint32_t ExtraChallengeID;
    bool bExtraChallenge;
    bool bRearming;
    float RearmTimer;
};

uint32_t GetTutorialHintID( const char *pTag );
TutorialHint *CreateTutorialHint( const uint32_t id );
