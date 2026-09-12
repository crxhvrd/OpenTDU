#pragma once

class Spots;
struct SpotMap;

// TODO: CONVERT TO ENUM
static constexpr int32_t kTutorialStatePending = 0;    
static constexpr int32_t kTutorialStateIntro = 2;      
static constexpr int32_t kTutorialStateAirplane = 3;   
static constexpr int32_t kTutorialStateCarRental = 4;  
static constexpr int32_t kTutorialStateFirstDrive = 5; 
static constexpr int32_t kTutorialStateBuyHouse = 6;
static constexpr int32_t kTutorialStateHouseBought = 7;
static constexpr int32_t kTutorialStateBuyCar = 8;
static constexpr int32_t kTutorialStateCarBought = 9;
static constexpr int32_t kTutorialStateHome = 11;
static constexpr int32_t kTutorialStateRunning = 13;

class GameIntroTutorial {
public:
    GameIntroTutorial();
    ~GameIntroTutorial();

    void initialize( Spots &spotList );
    inline int32_t getStep() const { return tutorialStep; }
    inline bool isModalHintVisible() const { return bModalHintVisible; }

    bool isActive() const;
    void reset();
    void tick();

    void FUN_007fbd80( bool param_2 );

private:
    void setStep( const int32_t step );
    void beginStep( const int32_t step, const int32_t previousStep );
    void endStep( const int32_t step );

private:
    int32_t tutorialStep;

    // Intro/tutorial waypoints
    SpotMap *pCarRental;  // "ECR_A_6703"
    SpotMap *pRealEstate; // "ERE_A_6627"

    // Car dealerships (Audi/Alfa/Saturn)
    SpotMap *pSpotECD12E; // "ECD_12E_7757"
    SpotMap *pSpotECD8F;  //  "ECD_8F_7756"
    SpotMap *pSpotECD12B; //  "ECD_12B_7777"

    SpotMap *pSpotIntroRace; //  "RSFE0028"

    SpotMap *pHome;      
    int32_t carDealer;  
    int32_t carIDRental; 

    uint8_t bModalHintVisible : 1;
    uint8_t bMapForIntro : 1;
    uint8_t bMustTeleport : 1;
};
