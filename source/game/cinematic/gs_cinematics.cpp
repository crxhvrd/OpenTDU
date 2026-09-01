#include "shared.h"
#include "gs_cinematics.h"

#include "audio/gs_audio.h"
#include "game/scenaric/entity/entity_sound_density.h"

GSCinematics *gpCinematics = nullptr;

GSCinematics::GSCinematics()
    : GameSystem(), pMusicClient( nullptr ), bInitialized( false ) {
    gpCinematics = this;
}

GSCinematics::~GSCinematics() {}

bool GSCinematics::initialize( TestDriveGameInstance * ) {
    // FUN_009afde0
    bInitialized = true;

    for ( EntitySoundDensity::DensitySlot &slot : EntitySoundDensity::Slots ) {
        slot = EntitySoundDensity::DensitySlot();
    }

    if ( gpAudio != nullptr ) {
        pMusicClient = gpAudio->createClient( "ClientCinematicMusic" );
    }

    if ( pMusicClient == nullptr ) {
        OTDU_LOG_WARN( "No 'ClientCinematicMusic' audio client, cinematic music will stay silent\n" );
    }

    return true;
}

void GSCinematics::terminate() {
    // FUN_009afdd0
    pMusicClient = nullptr;
    bInitialized = false;
}

PipelineStereo *GSCinematics::getMusicPipeline() const {
    return ( pMusicClient != nullptr ) ? pMusicClient->getStereoPipeline()
                                       : nullptr;
}
