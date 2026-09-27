#include "shared.h"
#include "gs_config.h"

#include "audio/radio/gs_radio.h"

void GSConfig::registerRadioCommands() {
    registerCommand( "ADDSTATION", [&]( const char *pArg ) {
        // FUN_00989610
        std::string argStr( pArg );
        size_t it = 0;
        const std::string stationName = ParseString( argStr, it );

        char stationFile[512] = {};
        snprintf( stationFile, sizeof( stationFile ),
                  "%sSound/Radio/%s.station", getResRootPath(),
                  stationName.c_str() );

        char stationWorkingDir[512] = {};
        snprintf( stationWorkingDir, sizeof( stationWorkingDir ), "%sRadio/",
                  retrieveVFSRoot() );

        if ( gpRadio != nullptr ) {
            gpRadio->addStation( stationFile, stationWorkingDir );
        }
    } );
}
