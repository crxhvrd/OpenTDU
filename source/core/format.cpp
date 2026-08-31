#include "shared.h"
#include "format.h"

#include "flash/mng_flash_localize.h"
#include "player_data/gs_playerdata.h"

// The localized "%1 credits" wrapper (TDU_Menu).
static constexpr uint64_t kMoneyFormatHashcode = 0x39d61b2ull;

const char *FormatMoneyString( char *param_1, int32_t param_2 ) {
    // FUN_00719ad0
    char local_60[32];
    char acStack_40[32];
    char local_20[32];

    *param_1 = '\0';

    while ( 999 < param_2 ) {
        // The US locale groups thousands with a comma, every other one with a
        // space.
        const char *pSeparator =
            ( strcmp( gpPlayerData->getLanguage(), "us" ) == 0 ) ? "," : " ";

        const int32_t iVar3 = param_2 % 1000;
        param_2 = param_2 / 1000;

        snprintf( local_60, sizeof( local_60 ), "%s%03d%s", pSeparator, iVar3,
                  param_1 );
        strcpy( param_1, local_60 );
    }

    snprintf( local_20, sizeof( local_20 ), "%d%s", param_2, param_1 );
    strcpy( param_1, local_20 );

    if ( gpMngFlashLocalize != nullptr ) {
        gpMngFlashLocalize->makeSentenceFromHashcode(
            acStack_40, sizeof( acStack_40 ), kMoneyFormatHashcode, param_1 );
        strcpy( param_1, acStack_40 );
    }

    return param_1;
}
