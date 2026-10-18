#include "shared.h"
#include "game_cheats.h"

bool gbAllHousesWithDefCar = false;      // DAT_010e7538
int32_t gDefaultMoney = 100000000;       // DAT_00f7f574
std::string gDefaultHouse = "EPH_L3A_F"; // DAT_00f7f528
std::vector<int32_t> gDefaultGarageConfig = { 0x75, 0x3f4, 0xe6, 0x6b, 0x72,
                                              0xca, 0x70,  -1 }; // DAT_00f7f548
bool gbInstallCheatCars = false;                                 // DAT_010e8046
bool gbGMEDesigner = false;                                      // DAT_010e7ce0
bool gbICESolo = false;                                          // DAT_010e7c31
bool gbAllChallengesSolo = false;                                // DAT_010e7c32

bool gbNewGameSkipIntro =
    false; // Jump straight to GMFreeride on new game (+ install cheat cars)
