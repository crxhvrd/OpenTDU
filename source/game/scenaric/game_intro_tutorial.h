#pragma once

class Spots;
struct SpotMap;

class GameIntroTutorial {
public:
    GameIntroTutorial();
    ~GameIntroTutorial();

    void initialize( Spots &spotList );

    bool isActive() const;
    void reset();

    void FUN_007fbd80( bool param_2 );

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

    uint8_t bModalHintVisible : 1;
    uint8_t bMapForIntro : 1;
};
