#include "shared.h"
#include "gs_subtitles.h"

#include "flash/gs_flash.h"
#include "flash/movie_player.h"

GSSubtitles *gpSubtitles = nullptr;

GSSubtitles::GSSubtitles()
    : GameSystem(),
      pListeners{},
      numListeners( 0 ),
      pActiveSubtitle( nullptr ),
      bEnabled( true ),
      pFlashPlayer( nullptr ),
      bDisplayed( false ) {
    gpSubtitles = this;
}

GSSubtitles::~GSSubtitles() {}

bool GSSubtitles::initialize( TestDriveGameInstance * ) {
    // FUN_00980ee0
    numListeners = 0;
    pActiveSubtitle = nullptr;
    bEnabled = true;

    uint32_t validGM = 0u;
    uint32_t movieIndex = 0u;
    pFlashPlayer = nullptr;
    if ( gpFlash->getFlashMovieIndex( &validGM, &movieIndex, "GENERAL" ) ) {
        MoviePlayer *pMovie = gpFlash->getMoviePlayer( int32_t( validGM ),
                                                       int32_t( movieIndex ) );
        if ( pMovie != nullptr && pMovie->MovieBank.isLoaded() ) {
            pFlashPlayer = pMovie->MovieBank.getFlashPlayer();
        }
    }

    bDisplayed = false;
    for ( edAudioStreamEntryListener *&pListener : pListeners ) {
        pListener = nullptr;
    }

    return true;
}

void GSSubtitles::terminate() {
    // FUN_00980eb0
}

void GSSubtitles::registerListener( edAudioStreamEntryListener *pListener ) {
    // FUN_00980e90
    if ( numListeners < kMaxListeners ) {
        pListeners[numListeners] = pListener;
        numListeners++;
    }
}

void GSSubtitles::unregisterListener( edAudioStreamEntryListener *pListener ) {
    // FUN_00980e30
    int32_t index = 0;
    while ( index < numListeners && numListeners < kMaxListeners &&
            pListeners[index] != pListener ) {
        index++;
    }

    if ( index < numListeners && numListeners < kMaxListeners ) {
        pListeners[index] = pListeners[numListeners - 1];
        pListeners[numListeners - 1] = nullptr;
        numListeners--;
    }
}
