#pragma once

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

class TutorialHint {
public:
    static constexpr float kDefaultTimerCondition = 5.0f; // DAT_00f41750
    static constexpr float kDefaultTimerOnScreen = 7.0f; // DAT_00f41844

public:
    explicit TutorialHint( const uint32_t id );
    virtual ~TutorialHint();
    virtual void cancel();

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

class TutorialHintMap : public TutorialHint {
public:
    TutorialHintMap();

public:
    bool bField_0x1c;
};

class TutorialHintTrigger : public TutorialHint {
public:
    static constexpr uint8_t kNoKind = 0xffu;

public:
    TutorialHintTrigger( const uint32_t id,
                    const uint8_t kind,
                    const uint8_t subKind,
                    const uint32_t extraChallengeID = 0u );

public:
    int32_t Field_0x1c;       
    int32_t Field_0x20;        
    int32_t Field_0x24;       
    int32_t Field_0x28;      
    uint8_t Kind;              
    uint8_t SubKind;           
    uint32_t ExtraChallengeID; 
    bool bExtraChallenge;     
    bool bField_0x35;        
};

uint32_t GetTutorialHintID( const char *pTag );
TutorialHint *CreateTutorialHint( const uint32_t id );
