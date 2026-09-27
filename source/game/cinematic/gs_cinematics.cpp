#include "shared.h"
#include "gs_cinematics.h"

#include "audio/gs_audio.h"
#include "game/cinematic/mng_cinematics.h"
#include "game/scenaric/entity/entity_sound_density.h"

GSCinematics *gpCinematics = nullptr;

GSCinematics::GSCinematics()
    : GameSystem(),
      pMusicClient( nullptr ),
      field_0x138( 0 ),
      field_0x13c( 0 ),
      bInitialized( false ) {
    gpCinematics = this;
}

GSCinematics::~GSCinematics() {}

void GSCinematics::tick( float totalTime, float deltaTime ) {
    // FUN_009afe30
    if ( gpMngCinematics != nullptr ) {
        gpMngCinematics->tickCinematics( deltaTime, totalTime );
    }

    if ( field_0x138 >= 10 ) {
        field_0x13c = field_0x138;
    }
}

bool GSCinematics::isCinematicPlaying( eCinematicType type,
                                       bool bRequireAudio,
                                       bool bIncludeRequested ) const {
    // FUN_009afd80
    if ( gpMngCinematics == nullptr ) {
        return false;
    }
    return gpMngCinematics->isCinematicPlaying( type, bRequireAudio,
                                                bIncludeRequested );
}

bool GSCinematics::initialize( TestDriveGameInstance * ) {
    // FUN_009afde0
    bInitialized = true;
    field_0x138 = 0;

    for ( EntitySoundDensity::DensitySlot &slot : EntitySoundDensity::Slots ) {
        slot = EntitySoundDensity::DensitySlot();
    }

    if ( gpAudio != nullptr ) {
        pMusicClient = gpAudio->createClient( "ClientCinematicMusic" );
    }

    if ( pMusicClient == nullptr ) {
        OTDU_LOG_WARN( "No 'ClientCinematicMusic' audio client, cinematic "
                       "music will stay silent\n" );
    }

    return true;
}

void GSCinematics::terminate() {
    // FUN_009afdd0
    pMusicClient = nullptr;
    bInitialized = false;
}

ClientConfig *GSCinematics::getMusicClientConfig() const {
    if ( pMusicClient == nullptr || gpAudio == nullptr ) {
        return nullptr;
    }
    return gpAudio->getClientConfig( pMusicClient->getConfig()->pName );
}

PipelineStereo *GSCinematics::getMusicPipeline() const {
    return ( pMusicClient != nullptr ) ? pMusicClient->getStereoPipeline()
                                       : nullptr;
}
