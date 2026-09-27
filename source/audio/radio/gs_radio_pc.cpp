#include "shared.h"
#include "gs_radio_pc.h"

#include "audio/gs_audio.h"
#include "audio/radio/radio_station.h"
#include "audio/radio/radio_stream_player.h"
#include "config/gs_config.h"
#include "filesystem/gs_boot_datas.h"
#include "filesystem/gs_file.h"
#include "player_data/user_profile.h"

#include <filesystem>

static bool ReleasePoolStation( AmbiancePool<RadioStation> *pPool,
                                RadioStation *pStation ) {
    if ( pPool == nullptr ) {
        return false;
    }

    for ( int32_t i = 0; i < pPool->getCapacity(); i++ ) {
        if ( pPool->get( i ) == pStation ) {
            pPool->free( pStation );
            return true;
        }
    }
    return false;
}

static RadioStation *AllocatePoolStation( AmbiancePool<RadioStation> *pPool ) {
    // FUN_0042b050
    RadioStation *pStation = pPool->allocate();
    if ( pStation != nullptr ) {
        *pStation = RadioStation();
    }
    return pStation;
}

GSRadioPC::GSRadioPC()
    : GSRadio(),
      edAudioStreamEntryListener(),
      pStreamPlayer( nullptr ),
      pStationPool( nullptr ),
      pUserStationPool( nullptr ),
      NumStations( 0 ),
      CurrentStation( 0 ),
      pStations{},
      StationName{},
      Artist{},
      Title{},
      bPlayNextTrack( false ) {
    // FUN_007c8610
}

GSRadioPC::~GSRadioPC() {
    // FUN_0042af80
}

bool GSRadioPC::initialize( TestDriveGameInstance *pGameInstance ) {
    // FUN_007c8c20
    GSRadio::initialize( pGameInstance );

    pStreamPlayer = new RadioStreamPlayer( pAudioClient, this );

    // FUN_0042afb0
    pStationPool = new AmbiancePool<RadioStation>();
    pStationPool->create( kStationPoolCapacity );

    // FUN_0042b000
    pUserStationPool = new AmbiancePool<RadioStation>();
    pUserStationPool->create( kStationPoolCapacity );

    return true;
}

void GSRadioPC::tick( float totalTime, float deltaTime ) {
    // FUN_007c8730
    pStreamPlayer->setVolume( Volume );

    if ( bPlayNextTrack ) {
        playNextTrack();
    }

    const ClientConfig *pConfig =
        ( pAudioClient != nullptr ) ? pAudioClient->getConfig() : nullptr;
    bOnAir = bOn && pConfig != nullptr && !pConfig->bPaused;

    GSRadio::tick( totalTime, deltaTime );
}

void GSRadioPC::terminate() {
    // FUN_007c8790
    delete pStreamPlayer;
    pStreamPlayer = nullptr;

    for ( RadioStation *&pStation : pStations ) {
        if ( pStation != nullptr ) {
            releaseStation( pStation );
            pStation = nullptr;
        }
    }
    NumStations = 0;
    CurrentStation = 0;

    delete pStationPool;
    pStationPool = nullptr;

    delete pUserStationPool;
    pUserStationPool = nullptr;

    memset( StationName, 0, sizeof( StationName ) );
    memset( Title, 0, sizeof( Title ) );
    memset( Artist, 0, sizeof( Artist ) );

    GSRadio::terminate();
}

void GSRadioPC::pause() {
    // FUN_007c86c0
    turnOff();

    for ( int32_t i = 0; i < pUserStationPool->getCapacity(); i++ ) {
        RadioStation *pStation = pUserStationPool->get( i );
        if ( pStation != nullptr ) {
            removeStation( pStation );
        }
    }
}

void GSRadioPC::reset() {
    // FUN_007c8a10
    GameSystem::reset();

    const std::filesystem::path radioFolder =
        std::filesystem::path( gUserProfileRegister.getProfileFolderPath() ) /
        "Radio";

    // FindFirstFileA/FindNextFileA over "<profile>\Radio\*"
    std::error_code errorCode;
    for ( const std::filesystem::directory_entry &entry :
          std::filesystem::directory_iterator( radioFolder, errorCode ) ) {
        if ( pUserStationPool->getAllocatedCount() ==
             pUserStationPool->getCapacity() ) {
            break;
        }

        if ( !entry.is_directory( errorCode ) ) {
            continue;
        }

        const std::string stationName = entry.path().filename().string();
        const std::string stationFolder = entry.path().string();

        RadioStation *pStation = AllocatePoolStation( pUserStationPool );
        pStation->loadFolder( stationName.c_str(), stationFolder.c_str() );

        if ( pStation->getNumTracks() < 1 ) {
            releaseStation( pStation );
        } else {
            registerStation( pStation );
        }
    }
}

RadioStation *GSRadioPC::addStation( const char *pStationFile,
                                     const char *pWorkingDir ) {
    // FUN_007c88a0
    if ( pStationPool->getAllocatedCount() == pStationPool->getCapacity() ) {
        return nullptr;
    }

    void *pStationData = nullptr;
    uint32_t stationDataSize = 0;
    bool bOwnsStationData = false;

    if ( gpConfig->bSearchInBootFirst ) {
        pStationData =
            gpBootDatas->findInCache( pStationFile, &stationDataSize );
        if ( pStationData == nullptr ) {
            if ( !gpFile->loadFile( pStationFile, &pStationData,
                                    &stationDataSize ) ) {
                return nullptr;
            }
            bOwnsStationData = true;
        }
    } else if ( gpFile->loadFile( pStationFile, &pStationData,
                                  &stationDataSize ) ) {
        bOwnsStationData = true;
    } else {
        pStationData =
            gpBootDatas->findInCache( pStationFile, &stationDataSize );
        if ( pStationData == nullptr ) {
            return nullptr;
        }
    }

    RadioStation *pStation = AllocatePoolStation( pStationPool );
    if ( pStation != nullptr ) {
        pStation->loadPlaylist( pStationData, stationDataSize, pWorkingDir );
        OTDU_LOG_DEBUG( "Radio station '%s' loaded from '%s' (%d tracks)\n",
                        pStation->getName(), pStationFile,
                        pStation->getNumTracks() );
    }
    registerStation( pStation );

    if ( bOwnsStationData ) {
        TestDrive::Free( pStationData );
    }

    return pStation;
}

void GSRadioPC::removeStation( RadioStation *pStation ) {
    // FUN_007c8490
    int32_t stationIndex = 0;
    while ( stationIndex < kMaxStations &&
            pStations[stationIndex] != pStation ) {
        stationIndex++;
    }
    if ( stationIndex == kMaxStations ) {
        return;
    }

    const bool bWasCurrentStation = ( pStations[CurrentStation] == pStation );
    if ( bWasCurrentStation ) {
        pStreamPlayer->stop();
    }

    if ( pStation != nullptr ) {
        releaseStation( pStation );
    }

    for ( int32_t i = stationIndex; i < NumStations - 1; i++ ) {
        pStations[i] = pStations[i + 1];
    }
    NumStations--;
    pStations[NumStations] = nullptr;

    if ( CurrentStation >= stationIndex ) {
        CurrentStation = ( CurrentStation > 0 ) ? CurrentStation - 1 : 0;
    }

    if ( CurrentStation < NumStations ) {
        strncpy( StationName, pStations[CurrentStation]->getName(),
                 sizeof( StationName ) - 1 );
        if ( bWasCurrentStation && bOn ) {
            bPlayNextTrack = !playCurrentTrack();
        }
        return;
    }

    StationName[0] = '\0';
    turnOff();
}

bool GSRadioPC::turnOn() {
    // FUN_007c8410
    if ( CurrentStation >= NumStations ) {
        return false;
    }

    RadioStation *pStation = pStations[CurrentStation];
    pStation->resetPlayedTracks();
    if ( !pStation->pickNextTrack() ) {
        return false;
    }

    strncpy( StationName, pStation->getName(), sizeof( StationName ) - 1 );
    bPlayNextTrack = !playCurrentTrack();
    return true;
}

bool GSRadioPC::turnOff() {
    // FUN_007c83c0
    clearTrackInfos();
    pStreamPlayer->stop();

    Volume = 0.0f;
    bOn = false;
    return true;
}

bool GSRadioPC::selectNextStation() {
    // FUN_007c81f0
    clearTrackInfos();

    CurrentStation++;
    if ( CurrentStation >= NumStations ) {
        CurrentStation = 0;
    }

    return turnOn();
}

bool GSRadioPC::selectPreviousStation() {
    // FUN_007c81c0
    clearTrackInfos();

    CurrentStation--;
    if ( CurrentStation < 0 ) {
        CurrentStation = ( NumStations > 0 ) ? NumStations - 1 : 0;
    }

    return turnOn();
}

bool GSRadioPC::playNextTrack() {
    // FUN_007c8370
    clearTrackInfos();

    if ( CurrentStation >= NumStations ||
         !pStations[CurrentStation]->pickNextTrack() ) {
        return false;
    }

    const bool bPlaying = playCurrentTrack();
    bPlayNextTrack = !bPlaying;
    return bPlaying;
}

bool GSRadioPC::selectStationByHashcode( uint64_t hashcode ) {
    // FUN_007c8300
    CurrentStation = 0;

    for ( int32_t i = 0; i < NumStations; i++ ) {
        if ( GetIdentifier64bit( pStations[i]->getName() ) != hashcode ) {
            continue;
        }

        CurrentStation = i;
        return bOn && turnOn();
    }

    return false;
}

void GSRadioPC::onPipeEntryPlayed( edAudioStreamEntry * ) {
    // FUN_007c8140
    const RadioTrack *pTrack =
        ( CurrentStation < NumStations )
            ? pStations[CurrentStation]->getCurrentTrack()
            : nullptr;
    if ( pTrack == nullptr ) {
        clearTrackInfos();
        return;
    }

    strncpy( Artist, pTrack->Artist, sizeof( Artist ) - 1 );
    strncpy( Title, pTrack->Title, sizeof( Title ) - 1 );
}

void GSRadioPC::onPipeEntryEnded( edAudioStreamEntry * ) {
    // FUN_007c8130
    bPlayNextTrack = true;
}

void GSRadioPC::clearTrackInfos() {
    Title[0] = '\0';
    Artist[0] = '\0';
}

bool GSRadioPC::playCurrentTrack() {
    // FUN_007c8260
    const RadioTrack *pTrack = pStations[CurrentStation]->getCurrentTrack();
    if ( pTrack == nullptr ) {
        return false;
    }

    return pStreamPlayer->play( pTrack->Path, !pTrack->bUserTrack );
}

void GSRadioPC::registerStation( RadioStation *pStation ) {
    // FUN_007c82b0
    if ( pStation == nullptr ) {
        return;
    }

    for ( int32_t i = 0; i < kMaxStations; i++ ) {
        if ( pStations[i] != nullptr ) {
            continue;
        }

        pStations[i] = pStation;
        NumStations++;
        if ( i == CurrentStation ) {
            strncpy( StationName, pStations[CurrentStation]->getName(),
                     sizeof( StationName ) - 1 );
        }
        return;
    }
}

void GSRadioPC::releaseStation( RadioStation *pStation ) {
    // FUN_0042b1a0 / FUN_0042b250
    if ( !ReleasePoolStation( pStationPool, pStation ) ) {
        ReleasePoolStation( pUserStationPool, pStation );
    }
}
