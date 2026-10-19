#include "shared.h"
#include "gm_jail.h"

#include "tdu_instance.h"
#include "ai/ai_pool.h"
#include "ai/gs_ai.h"
#include "config/gs_config.h"
#include "core/maths_helper.h"
#include "flash/gs_flash.h"
#include "flash/mng_fade.h"
#include "flash/implementation/flash_player.h"
#include "game/scenaric/entity/entity_cinematic.h"
#include "game/scenaric/gs_scenaric.h"
#include "render/mng_shadow.h"
#include "world/gs_world.h"
#include "world/gs_world_streaming.h"
#include "world/mng_world.h"

static constexpr const char *kJailBankName = "scenarics/Cinematics/jail";
static constexpr float kJailCinematicYaw = 1.2235f;      // DAT_3f9c9ba6
static constexpr float kJailFadeInDuration = 1.0f;       // FLOAT_00f4165c
static constexpr float kSecondsToMinutes = 1.0f / 60.0f; // FLOAT_00f41998

static inline Eigen::Vector4f GetStreamOrigin() {
    return gpWorldStreaming->isEnabled() ? gpWorldStreaming->getPosition()
                                         : Eigen::Vector4f::Zero();
}

GMJail::GMJail()
    : GameModeWorldBase(),
      jailBank(),
      pJailCinematic( nullptr ),
      pPendingCinematic( nullptr ),
      elapsedTime( 0.0f ),
      jailDuration( 0.0f ),
      pFlashPlayer( nullptr ),
      bPopupShown( false ) {
    // FUN_009d2e40
    setTransitionFlags( 0x27 );
}

GMJail::~GMJail() {}

void GMJail::initialize() {
    // FUN_009d32b0
    GameModeWorldBase::initialize();

    std::string bankPath = gpConfig->getResRootPath();
    bankPath += kJailBankName;
    bankPath += ".bnk";

    jailBank.loadFromFile( bankPath.c_str(), nullptr, false );
    jailBank.forceLoad();
    jailBank.activate( nullptr );
}

void GMJail::reset() {
    // FUN_009d30f0
    GameModeScene::reset();
    gpAI->setTrafficCategories( false, 0u );

    pJailCinematic = static_cast<EntityCinematic *>(
        jailBank.findEntity( eEntityType::ET_CINE, "C_JAIL" ) );

    Eigen::Vector4f streamingPosition( 31830.93f, 20.684f, 37224.336f, 1.0f );
    Eigen::Vector4f shadowTarget( 31856.8f, 20.0f, 37230.8f, 1.0f );
    if ( gpMngShadow != nullptr ) {
        gpMngShadow->setTarget( &shadowTarget );
    }

    if ( pJailCinematic != nullptr ) {
        gpMngWorld->setDisableStreamingOnCar( true );
        gpWorld->setStreamingPosition( &streamingPosition );
        pJailCinematic->setPosition( streamingPosition, true );

        Matrix4x4f_t cinematicOffset = MakeRotationMatY( kJailCinematicYaw );
        cinematicOffset.row( 3 ) =
            ( streamingPosition - GetStreamOrigin() ).transpose();
        pJailCinematic->setTransformOffset( cinematicOffset );

        pJailCinematic->setGameModeInterface( this );
        pPendingCinematic = pJailCinematic;
    }

    elapsedTime = 0.0f;
    if ( gpMngFade != nullptr ) {
        gpMngFade->setTransitionFlags( gpMngFade->getTransitionFlags() | 8u );
        gpMngFade->setRenderLogo( true );
    }
}

void GMJail::tick( float deltaTime, float totalTime ) {
    // FUN_009d2f00
    GameModeScene::tick( deltaTime, totalTime );

    if ( pPendingCinematic != nullptr ) {
        if ( !gpWorld->isStreamedAround( gpWorld->getStreamingPositionPtr(),
                                         true, false ) ) {
            return;
        }

        pPendingCinematic->start( true );
        pPendingCinematic = nullptr;

        pFlashPlayer = gpFlash->getFlashPlayer( "GENERAL" );
        if ( gpMngFade != nullptr ) {
            gpMngFade->setRenderLogo( false );
            gpMngFade->restartTransition(
                ( gpMngFade->getTransitionFlags() & ~8u ) | 5u,
                kJailFadeInDuration );
        }
    }

    if ( pJailCinematic == nullptr ) {
        return;
    }

    elapsedTime += deltaTime;
    jailDuration = pJailCinematic->getDuration();

    float remainingTime = jailDuration - elapsedTime;
    if ( remainingTime < 0.0f ) {
        remainingTime = 0.0f;
    }

    if ( pFlashPlayer == nullptr ) {
        return;
    }

    if ( !bPopupShown && pFlashPlayer->spriteExists( "/jail_popup" ) ) {
        pFlashPlayer->showFrame( "on", "/jail_popup", false );
        bPopupShown = true;
    }

    const int32_t minutes =
        static_cast<int32_t>( remainingTime * kSecondsToMinutes );
    char seconds[64];
    snprintf( seconds, sizeof( seconds ), "%02d",
              static_cast<int32_t>( remainingTime ) - minutes );
    pFlashPlayer->setVariableValue( "/:min_jail", minutes );
    pFlashPlayer->setVariableValueNonLinked( "/:sec_jail", seconds );
}

void GMJail::draw( float /*deltaTime*/, float /*totalTime*/ ) {
    // FUN_009d2ce0
    // NOP
}

void GMJail::pause() {
    // FUN_009d2eb0
    AiPool *pAIPool = gpAI->getAIPool();
    pAIPool->setTrafficPaused( false );
    pAIPool->setField0x7C( true );
    pAIPool->setField0x85( true );
    gpAI->setPauseTraffic( false );

    if ( pJailCinematic != nullptr ) {
        pJailCinematic->stop( true );
    }
    gpMngWorld->setDisableStreamingOnCar( false );

    GameModeScene::pause();
}

void GMJail::destroy() {
    // FUN_009d3090
    jailBank.destroy();
    jailBank.terminate();
    jailBank.unload();

    GameModeWorldBase::destroy();
}

bool GMJail::onMessage( FlashMessage *pMessage ) {
    // FUN_009d2dc0
    if ( *pMessage != FlashMessage( "ende" ) ) {
        return false;
    }

    endJail();
    return true;
}

bool GMJail::executeAction( int32_t param_2,
                            void * /*param_3*/,
                            void * /*pData*/ ) {
    // FUN_009d2da0
    if ( param_2 == CE_Ended ) {
        endJail();
    }
    return true;
}

void GMJail::endJail() {
    // FUN_009d2cf0
    FlashPlayer *pGeneralPlayer = gpFlash->getFlashPlayer( "GENERAL" );
    if ( pGeneralPlayer != nullptr ) {
        pGeneralPlayer->showFrame( "off", "jail_popup", false );
    }

    if ( gpTestDriveInstance->getActiveGameMode() == GM_Jail ) {
        gpScenaric->setCurrentSpotIndex( -1 );
        gpTestDriveInstance->setNextGameMode( GM_Living );
    }

    if ( gpMngFade != nullptr ) {
        gpMngFade->setTransitionFlags( gpMngFade->getTransitionFlags() | 8u );
    }

    requestExit();
}
