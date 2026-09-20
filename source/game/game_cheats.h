#pragma once

// TODO: Temporary header; will move those
// globals to the right place later

// Enables cheats:
// -UNLOCKALLROADS
// -DELOCKCHALLENGESROADS
// -ALLHOUSESWITHDEFCARS
// -DEFAULTMONEY
//
// Off in non devbuilds to match what the original binary did.
#ifdef OTDU_DEVBUILD
static constexpr bool kDevCheatsEnabled = true;
#else
static constexpr bool kDevCheatsEnabled = false;
#endif

extern bool gbAllHousesWithDefCar;
extern int32_t gDefaultMoney;
extern std::string gDefaultHouse;
extern std::vector<int32_t> gDefaultGarageConfig;
extern bool gbInstallCheatCars;
extern bool gbNewGameSkipIntro;
extern bool gbGMEDesigner;
extern bool gbICESolo;
extern bool gbAllChallengesSolo;
