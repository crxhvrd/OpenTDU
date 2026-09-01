#include "shared.h"
#include "cinematic_music_emitter.h"

#include "audio/implementation/ed_audio_sample.h"
#include "game/cinematic/gs_cinematics.h"
#include "game/scenaric/entity/entity_sound_density.h"

static void UnlinkChannelFromPipeline( edAudioStreamEntry &channel ) {
    edAudioObject *pParent = channel.getParent();
    if ( pParent != nullptr ) {
        pParent->unlinkEntry( &channel );
    }
}

CinematicMusicEmitter::CinematicMusicEmitter( uint8_t *pInLeftRef,
                                              uint8_t *pInRightRef )
    : pLeftRef( pInLeftRef ),
      pRightRef( pInRightRef ),
      bAddedToPipeline( false ),
      bReleased( false ),
      pendingFadeTime( 0.0f ) {
    // FUN_009c77f0 / FUN_009c7870
    source.getLeftChannel().debugSetDescription( "CinematicMusic Left" );
    source.getRightChannel().debugSetDescription( "CinematicMusic Right" );

    PipelineStereo *pPipeline = ( gpCinematics != nullptr )
                                    ? gpCinematics->getMusicPipeline()
                                    : nullptr;
    if ( pPipeline != nullptr ) {
        pPipeline->addSource( &source );
        bAddedToPipeline = true;
    }
}

CinematicMusicEmitter::~CinematicMusicEmitter() { releaseChannels(); }

EntitySoundEmitter *CinematicMusicEmitter::getEmitter( uint8_t *pRef ) const {
    if ( pRef == nullptr ) {
        return nullptr;
    }

    return *reinterpret_cast<EntitySoundEmitter **>( pRef );
}

void CinematicMusicEmitter::resolveEmitters( ScenaricBank *pBank ) {
    // FUN_009c7840
    if ( pLeftRef != nullptr ) {
        ResolveEntityRef( pLeftRef, pBank );
    }

    if ( pRightRef != nullptr ) {
        ResolveEntityRef( pRightRef, pBank );
    }
}

void CinematicMusicEmitter::installSoundBanks() {
    // FUN_009c76a0
    EntitySoundEmitter *pLeft = getEmitter( pLeftRef );
    if ( pLeft != nullptr ) {
        pLeft->installSoundBank();
    }

    EntitySoundEmitter *pRight = getEmitter( pRightRef );
    if ( pRight != nullptr ) {
        pRight->installSoundBank();
    }
}

void CinematicMusicEmitter::uninstallSoundBanks() {
    // FUN_009c7910
    source.getLeftChannel().setSample( nullptr );
    source.getRightChannel().setSample( nullptr );

    EntitySoundEmitter *pLeft = getEmitter( pLeftRef );
    if ( pLeft != nullptr ) {
        pLeft->uninstallSoundBank();
    }

    EntitySoundEmitter *pRight = getEmitter( pRightRef );
    if ( pRight != nullptr ) {
        pRight->uninstallSoundBank();
    }
}

bool CinematicMusicEmitter::hasBothEmitters() const {
    return getEmitter( pLeftRef ) != nullptr &&
           getEmitter( pRightRef ) != nullptr;
}

bool CinematicMusicEmitter::areEmittersResolved() const {
    // FUN_009c76e0
    const EntitySoundEmitter *pLeft = getEmitter( pLeftRef );
    const EntitySoundEmitter *pRight = getEmitter( pRightRef );

    return pLeft != nullptr && pRight != nullptr &&
           pLeft->getSoundBankState() != 0 && pRight->getSoundBankState() != 0;
}

bool CinematicMusicEmitter::areSoundBanksInstalling() const {
    // FUN_009c7780
    if ( !areEmittersResolved() ) {
        return false;
    }

    return getEmitter( pLeftRef )->getSoundBankState() ==
               EntitySoundDensity::SDS_Installing &&
           getEmitter( pRightRef )->getSoundBankState() ==
               EntitySoundDensity::SDS_Installing;
}

void CinematicMusicEmitter::play( bool bLoop, float fadeTime ) {
    // FUN_009c78a0
    EntitySoundEmitter *pLeft = getEmitter( pLeftRef );
    EntitySoundEmitter *pRight = getEmitter( pRightRef );
    if ( pLeft != nullptr && pRight != nullptr ) {
        source.setSamples( pLeft->getSample(), pRight->getSample() );
    }

    source.setLoopState( bLoop );

    pendingFadeTime = fadeTime;
    source.play();
}

void CinematicMusicEmitter::stop( float fadeTime ) {
    // FUN_009b0260
    source.stop( fadeTime );
}

void CinematicMusicEmitter::setPaused( bool bPaused, float fadeTime ) {
    // FUN_009c7640
    if ( bPaused ) {
        source.getLeftChannel().pause( fadeTime );
        source.getRightChannel().pause( fadeTime );
        return;
    }

    if ( source.isStarted() ) {
        return;
    }

    if ( source.getLeftChannel().isPaused() &&
         source.getRightChannel().isPaused() ) {
        pendingFadeTime = fadeTime;
        source.play();
    }
}

void CinematicMusicEmitter::releaseChannels() {
    if ( bReleased ) {
        return;
    }

    source.stop( 0.0f );
    source.setSamples( nullptr, nullptr );

    if ( bAddedToPipeline ) {
        UnlinkChannelFromPipeline( source.getLeftChannel() );
        UnlinkChannelFromPipeline( source.getRightChannel() );
        bAddedToPipeline = false;
    }

    bReleased = true;
}
