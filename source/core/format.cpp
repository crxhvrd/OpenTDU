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

char *FormatRaceTime( char *pOutText, const float seconds ) {
    // FUN_00730760
    if ( pOutText == nullptr ) {
        return nullptr;
    }
    if ( seconds == 0.0f ) {
        strcpy( pOutText, "0'00\"00" );
        return pOutText;
    }

    const int32_t milliseconds = int32_t( seconds * 1000.0f );
    const int32_t subSecond = milliseconds % 1000;
    const int32_t wholeMilliseconds = milliseconds - subSecond;
    const int32_t minuteRemainder = wholeMilliseconds % 60000;
    sprintf( pOutText, "%3d'%02d\"%02d",
             ( wholeMilliseconds - minuteRemainder ) / 60000,
             minuteRemainder / 1000, subSecond / 10 );
    return pOutText;
}
