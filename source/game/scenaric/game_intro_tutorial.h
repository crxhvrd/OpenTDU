#pragma once

#include "game/scenaric/scenaric_bank.h"

class Spots;
class EntityCinematic;
struct SpotMap;
class GMMap;
class FlashPlayer;

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
static constexpr int32_t kTutorialStateChallenge = 10;
static constexpr int32_t kTutorialStateHome = 11;
static constexpr int32_t kTutorialStateRunning = 13;

class GameIntroTutorial {
public:
    GameIntroTutorial();
    ~GameIntroTutorial();

    void initialize( Spots &spotList );
    void resolveSpots( Spots &spotList );
    inline int32_t getStep() const { return tutorialStep; }
    inline int32_t getCarRentalTime() const { return carRentalTime; }
    inline bool isModalHintVisible() const { return bModalHintVisible; }

    bool isActive() const;
    bool hidesSpot( const SpotMap *pSpot ) const;
    void reset();
    void tick();

    void onIntroCinematicEnded();
    EntityCinematic *loadLandingCinematic();
    void onIntroFinished();
    void onCarRented( const int32_t carID, const int32_t rentalTime );
    void onDealerGoToMap();
    void onCarBought();
    void onHouseBought( SpotMap *pHouse );
    void FUN_007fbd80( bool param_2 );
    void computeMapFocus( GMMap *pMap,
                          Eigen::Vector4f *pOutFocus,
                          const bool bSelectSpot );
    void sendFlashVariables( FlashPlayer *pPlayer );

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
    int32_t carRentalTime;

    ScenaricBank landingCinematicBank;

    uint8_t bModalHintVisible : 1;
    uint8_t bMapForIntro : 1;
    uint8_t bMustTeleport : 1;
    uint8_t bInstallingCheatCars : 1;
};
