#include "shared.h"
#include "gm_scene.h"

#include "game/scenaric/mng_actor.h"
#include "game/scenaric/entity/entity_cinematic.h"
#include "world/gs_world.h"
#include "world/gs_aircraft.h"
#include "world/gs_boat.h"

#include "gs_timer.h"
#include "tdu_instance.h"
#include "flash/gs_flash.h"
#include "flash/implementation/flash_player.h"
#include "player_data/achievements_data.h"
#include "player_data/driver.h"
#include "player_data/gs_playerdata.h"
#include "player_data/player_data_interface.h"
#include "world/weather/gs_weather.h"
#include "world/weather/hdri_config.h"

#include "render/postfx/gs_postfx.h"
#include "render/postfx/effects/postfx_hdri.h"
#include "render/postfx/effects/postfx_desaturate.h"
#include "render/postfx/effects/postfx_radial_blur.h"
#include "render/postfx/effects/postfx_modulate.h"

#include "config/gs_config.h"
#include "database/gs_database.h"
#include "game/vehicle/car_state.h"
#include "game/vehicle/vehicle_base.h"
#include "game/scenaric/local_player.h"
#include "input/input_device.h"
#include "input/gs_device.h"
#include "core/maths_helper.h"
#include "flash/audio/flash_sounds.h"
#include "flash/movie_player.h"
#include "render/mng_shadow.h"
#include "render/mng_reflection.h"
#include "render/render_scene.h"

GameModeScene *gpActiveGMScene = nullptr; // DAT_010e7c80

uint32_t gFrameAccumulator = 0;  // DAT_010e7c74
float gNextFPSSampleTime = 0.0f; // FLOAT_010e7c78
uint32_t gCurrentFPS = 0;

// Dirty disk watchdog
uint32_t gFreezeWatchdogCounter = 0; // DAT_016a6a4c

GameModeScene::GameModeScene()
    : WorldListener(),
      CinematicPlayer(),
      FlashStreamListener(),
      GameMode(),
      mngFlash( this ),
      mngShadow(),
      mngActor(),
      mngLifeSet(),
      mngZone(),
      mngFade(),
      mngDbgScnrc(),
      mngCinematics(),
      mngMusic(),
      mngFlashLocalize(),
      mngPanel(),
      showcaseCarControl(),
      mngRental(),
      mngDebugHUD(),
      mngFlashCounter(),
      moneyWidget(),
      onlineWidget(),
      rentalWidget(),
      carInfosWidget(),
      bank() {
    // FUN_007bc3e0
    registerManager( &mngFlash );
    registerManager( &mngShadow );
    registerManager( &mngActor );
    registerManager( &mngLifeSet );
    registerManager( &mngZone );
    registerManager( &mngDbgScnrc );
    registerManager( &mngCinematics );
    registerManager( &mngMusic );
    registerManager( &mngFade );
    registerManager( &mngFlashLocalize );
    registerManager( &mngPanel );
    registerManager( &showcaseCarControl );
    registerManager( &mngRental );
    registerManager( &mngFlashCounter );
    registerManager( &mngDebugHUD );

    gpActiveGMScene = this;

    // The standalone scene actor manager is the primary one (GMBase sets
    // MngActor.bIsManager on it).
    mngActor.setIsManager( true );

    weatherDesc[0] = '\0';
    musicDesc[0] = '\0';
    saturationCoef = 1.0f; // FLOAT_00f4165c
    cinematicTimer = 0.0f;
    bWeatherSet = false;
    bWithWorld = false;
    bNeedWeatherUpdate = false;
    bFlashInstalled = false;
    bVerifyGameFreeze = false;
    bFrontEndMusicLaunched = false;
    bScenarioTutorialMustReactivateFlash = false;
    bWorldEntitiesInstanciated = false;
    pCinematicIntroE = nullptr;
    pCinematicIntroI = nullptr;
    pCinematicLoop = nullptr;
    pCurrentSpotWorld = nullptr;
    pSavedPhysicsControl = nullptr;

    gpFlash->registerFlashListener( this );

    OTDU_UNIMPLEMENTED; // TODO: SCENARIC
}

GameModeScene::~GameModeScene() {
    // FUN_007bc120
    OTDU_UNIMPLEMENTED;
    gpFlash->unregisterFlashListener( this );
    gpActiveGMScene = nullptr;
    gpMngFade = nullptr;
}

bool GameModeScene::isStaticFrontEndScene() const {
    // FUN_007bbda0
    switch ( currentGameMode ) {
    case GM_Home:
    case GM_Goodies:
    case GM_AvatarShowcase:
    case GM_Dressing:
    case GM_AfterMarket:
    case GM_Estates:
    case GM_Club:
        return true;
    case GM_Shop:
        OTDU_UNIMPLEMENTED; // TODO: GMShop
        return true;
    case GM_Garage:
    case GM_Living:
        if ( pCinematicIntroE != nullptr &&
             ( ( pCinematicIntroE->getFlags() >> 2 ) & 1 ) != 0 ) {
            return false;
        }
        return true;
    default:
        return false;
    }
}

void GameModeScene::initialize() {
    // FUN_007bdb20
    if ( gpWeather != nullptr ) {
        gpWeather->registerListener( this );
    }

    if ( !bWithWorld ) {
        OTDU_UNIMPLEMENTED; // TODO: SCENARICS/AI
    }

    cinematicTimer = 0.0f; // field48_0xfdc8
}

void GameModeScene::reset() {
    // FUN_007bd670
    const eGameMode activeGM = gpTestDriveInstance->getActiveGameMode();
    if ( activeGM > GM_Invalid && activeGM < GM_Count ) {
        gpWeather->setCurrentWeatherDesc(
            gpWeather->getWeatherDescName( activeGM ) );
    }

    if ( gpPlayerData != nullptr && gpPlayerData->isOnline() ) {
        OTDU_UNIMPLEMENTED; // TODO: ONLINE (P2P clock sync ->
                            // GSWeather::setTime)
    }

    // HDR / radial-blur enable state for the current game mode.
    toggleHDRRadialBlur();

    // Refresh the radial-blur mask bitmap on the radial-blur step.
    PostFXStepRadialBlur *pRadialBlur =
        (PostFXStepRadialBlur *)
            gpPostFX->getEffect<ePostFXStep::PFXS_RadialBlur>();
    if ( pRadialBlur != nullptr && pRadialBlur->getInput() != nullptr ) {
        ( (PostFXNodeRadialBlur *)pRadialBlur->getInput() )
            ->setMask( gpPostFX->retrieveRadialBlurBitmap( RBB_Mask ) );
    }

    updatePostFX();

    OTDU_UNIMPLEMENTED; // TODO:: WORLD

    cinematicTimer = 0.0f; // field48_0xfdc8

    if ( gpFlash != nullptr && gpFlash->getMouse() != nullptr ) {
        gpFlash->getMouse()->field12_0x30 = false;
    }
}

void GameModeScene::tick( float /*deltaTime*/, float /*totalTime*/ ) {
    // FUN_007bd5a0

    // Deferred world-entity template preparation: run each scenaric bank's
    // per-template loadResources()/Init() exactly once, and only after the
    // world has streamed in its first sector so the commonworld bitmap bank is
    // registered and shared entity textures bind. No global instancing happens
    // — world entities are sourced per-sector (.PMI) and by gameplay spawners.
    if ( !bWorldEntitiesInstanciated && gpScenaric != nullptr &&
         gpWorld != nullptr && gpWorld->isWorldLoaded() ) {
        gpScenaric->prepareWorldEntityTemplates();

        if ( gpBoat != nullptr ) {
            gpBoat->spawnBoats();
        }

        if ( gpAircraft != nullptr ) {
            gpAircraft->spawnAircrafts();
        }

        bWorldEntitiesInstanciated = true;
    }

    OTDU_UNIMPLEMENTED;

    updatePostFX();

    // Once-per-second FPS sampling. The original measures wall-clock since app
    // start via QueryPerformanceCounter; here we use the system clock the timer
    // service already maintains.
    const float nowSeconds =
        ( gpTimer != nullptr ) ? gpTimer->SystemTotalTime : 0.0f;
    gFrameAccumulator++;
    if ( gNextFPSSampleTime < nowSeconds ) {
        gNextFPSSampleTime = nowSeconds + 1.0f;
        gCurrentFPS = gFrameAccumulator;
        gFrameAccumulator = 0;
    }

    // Freeze watchdog — after 60 frames the scene flags a game-freeze check.
    gFreezeWatchdogCounter++;
    if ( 59 < gFreezeWatchdogCounter ) {
        bVerifyGameFreeze = true;
    }

    launchCine();
    stopCine();
    updateWeather();
}

void GameModeScene::pause() {
    // FUN_007bcf10
    OTDU_UNIMPLEMENTED; // TODO: POSTFX
    OTDU_UNIMPLEMENTED; // TODO: TUTORIAL

    // Stop the intro / loop spot cinematics if still playing.
    if ( pCinematicIntroE != nullptr ) {
        pCinematicIntroE->stop( true );
    }
    if ( pCinematicIntroI != nullptr ) {
        pCinematicIntroI->stop( true );
    }
    if ( pCinematicLoop != nullptr ) {
        pCinematicLoop->stop( true );
    }

    moneyWidget.detach();
    onlineWidget.detach();
    rentalWidget.detach();
    carInfosWidget.detach();

    OTDU_UNIMPLEMENTED; // TODO: DETAILS
}

void GameModeScene::destroy() {
    // FUN_007bd190
    if ( gpWeather != nullptr ) {
        gpWeather->setUpdateTimerEnabled( false );
        gpWeather->setCurrentWeatherDesc(
            gpWeather->getWeatherDescName( GM_BootMenu ) );
        gpWeather->unregisterListener( this );
    }
}

void GameModeScene::onResourceStreamed() {
    // FUN_007bca90
    MoviePlayer *pHud = gpFlash->getMoviePlayer( 1, 0 );
    if ( pHud != nullptr && pHud->MovieBank.isLoaded() &&
         pHud->MovieBank.getFlashPlayer() != nullptr ) {
        FlashPlayer *pPlayer = pHud->MovieBank.getFlashPlayer();
        moneyWidget.pMovie = pPlayer;
        onlineWidget.pMovie = pPlayer;
        rentalWidget.pMovie = pPlayer;
        carInfosWidget.pMovie = pPlayer;

        IPlayerData *pSave = gpPlayerData->getLoadedSave();

        Driver *pDriver = pSave->getDriver();
        carInfosWidget.pTarget = pDriver;
        pDriver->subscribe( &carInfosWidget );

        onlineWidget.pTarget = gpPlayerData;
        gpPlayerData->subscribe( &onlineWidget );

        pDriver = pSave->getDriver();
        moneyWidget.pTarget = pDriver;
        pDriver->subscribe( &moneyWidget );

        pDriver = pSave->getDriver();
        rentalWidget.pTarget = pDriver;
        pDriver->subscribe( &rentalWidget );

        pPlayer->setVariableValue(
            "/:state_Metric", gpPlayerData->isUsingImperialUnits() ? 0 : 1 );

        FlashPlayer *pGeneralPlayer = gpFlash->getFlashPlayer( "GENERAL" );
        pPlayer->setVariableValue( "/:state_Demo", 0 );
        if ( pGeneralPlayer != nullptr ) {
            pGeneralPlayer->setVariableValue( "/:state_Demo", 0 );
        }

        pPlayer->setVariableValue( "/:Unlock_Chrono",
                                   pSave->getAchievements()->isRewardLocked(
                                       AchievementsData::kChronoRewardHashcode )
                                       ? 0
                                       : 1 );
        pPlayer->setVariableValue(
            "/:Unlock_Snapshot", pSave->getAchievements()->isRewardLocked(
                                     AchievementsData::kSnapshotRewardHashcode )
                                     ? 0
                                     : 1 );
        pPlayer->setVariableValue(
            "/:Unlock_GME",
            pSave->getAchievements()->isRewardLocked(
                AchievementsData::kGameModeEditorRewardHashcode )
                ? 0
                : 1 );

        pPlayer->setVariableValue( "/:IsMSKeyboard", 0 );

        const uint32_t padConfig = gpPlayerData->getLocalPlayerData().PadConfig;
        pPlayer->setVariableValue( "/:IsAlternateButton",
                                   ( padConfig == 3u ) ? 1 : 0 );
        pPlayer->setVariableValue( "/:IsAlternateButton2",
                                   ( padConfig == 4u ) ? 1 : 0 );

        pPlayer->setVariableValue(
            "/:player_level", pSave->getAchievements()->getPlayerLevel() + 1 );
        pPlayer->setVariableValue( "/:player_reputation",
                                   int32_t( pSave->getDriver()->getLevel() ) );

        pPlayer->setVariableValue( "/:state_Cha", 0 );
    }

    MngFlashLocalize::OnFlashInstall();

    bFlashInstalled = true;
}

bool GameModeScene::updatePostFX() {
    // FUN_007bccd0
    if ( isStaticFrontEndScene() ) {
        return true;
    }

    if ( gpWeather->isConfigDirty() ) {
        gpWeather->updateActiveConfig();
    }

    const WeatherConfig &weather = gpWeather->getActiveWeatherConfig();
    const float desaturation = weather.getDesaturation();
    const float contrast = weather.getContrast();
    const Eigen::Vector3f balance = weather.getBalance();

    PostFXStepDesaturate *pDesaturate =
        (PostFXStepDesaturate *)
            gpPostFX->getEffect<ePostFXStep::PFXS_Desaturation>();
    if ( pDesaturate != nullptr ) {
        PostFXNodeDesaturate *pNode =
            (PostFXNodeDesaturate *)pDesaturate->getInput();
        if ( pNode != nullptr && pNode->isEnabled() ) {
            pNode->setSaturation(
                Clamp( saturationCoef * desaturation, 0.0f, 1.0f ) );
            pNode->setRGBFactor( balance.x(), balance.y(), balance.z() );
            pNode->setContrast( contrast );
        }
    }

    HDRIConfig *pHDRConfig = weather.getHDRConfig();
    PostFXStepHDRI *pHDRI =
        (PostFXStepHDRI *)gpPostFX->getEffect<ePostFXStep::PFXS_HDRI>();
    if ( pHDRI != nullptr ) {
        PostFXNodeHDR *pNode = (PostFXNodeHDR *)pHDRI->getInput();
        if ( pNode != nullptr && pNode->isEnabled() ) {
            pNode->setContrast( contrast );
            pNode->setSaturation(
                Clamp( saturationCoef * desaturation, 0.0f, 1.0f ) );
            pNode->setRGBFactor( balance.x(), balance.y(), balance.z() );

            if ( pHDRConfig != nullptr ) {
                pNode->setGlowMod( pHDRConfig->GlowMod );
                pNode->setStarMod( pHDRConfig->StarMod );
                pHDRI->getGlowModulate()->setCoefficient0(
                    pHDRConfig->BrightThresholdGlow );
                pHDRI->getStarModulate()->setCoefficient0(
                    pHDRConfig->BrightThresholdStar );
            }
        }
    }

    return true;
}

namespace {
    EntityCinematic *FindCinematic( ScenaricBank *pBank, const char *pName ) {
        // FUN_007dac10
        return static_cast<EntityCinematic *>(
            pBank->findEntity( eEntityType::ET_CINE, pName ) );
    }

    void SetHUDMoviesPaused( bool bPaused ) {
        const int32_t numMovies = gpFlash->getNumMovies( 1 );
        for ( int32_t i = 0; i < numMovies; i++ ) {
            MoviePlayer *pMovie = gpFlash->getMoviePlayer( 1, i );
            if ( pMovie == nullptr ) {
                continue;
            }
            pMovie->IsMoviePaused = bPaused ? ( pMovie->IsMoviePaused | 2 )
                                            : ( pMovie->IsMoviePaused & ~2 );
        }
    }

    void SetReflectionSceneEnabled( bool bEnabled ) {
        if ( gpMngReflection == nullptr ) {
            return;
        }
        RenderScene *pScene = gpMngReflection->getBoundScene();
        if ( pScene != nullptr ) {
            pScene->setEnabled( bEnabled );
        }
    }

    inline bool IsMappedActionPressed( DeviceAction *pAction ) {
        return pAction != nullptr && pAction->NumAddedChannels != 0 &&
               IsActionPressed( pAction );
    }

    bool IsCinematicSkipRequested() {
        PlayerConfiguration *pPlayerConfig =
            ( gpConfig != nullptr ) ? gpConfig->pPlayerConfig : nullptr;
        if ( pPlayerConfig == nullptr ) {
            return false;
        }

        if ( IsMappedActionPressed( pPlayerConfig->pMenuPause ) ||
             IsMappedActionPressed( pPlayerConfig->pMenuValidate ) ) {
            return true;
        }

        return gpDevice != nullptr &&
               IsChannelJustPressed(
                   gpDevice->getMouseState().pChannelButton0 );
    }
} // namespace

static constexpr float kCinematicIntroFadeTime = 500.0f; // DAT_00f417f4
static constexpr float kMillisecondsToSeconds = 0.001f;  // FLOAT_00f4167c
static constexpr float kCinematicFadeInDuration = 0.5f;  // FLOAT_00f41674

void GameModeScene::IntroExtToIntroInCallback( void *pData, void *pSource ) {
    // FUN_007bceb0
    if ( gpMngFade == nullptr ) {
        return;
    }

    const float fadeTimeInMs =
        static_cast<float>( reinterpret_cast<intptr_t>( pSource ) );
    gpMngFade->startTransition( ( gpMngFade->getTransitionFlags() & ~8u ) | 5u,
                                fadeTimeInMs * kMillisecondsToSeconds );

    GameModeScene *pScene = static_cast<GameModeScene *>( pData );
    if ( pScene != nullptr && pScene->pCinematicIntroE != nullptr ) {
        pScene->pCinematicIntroE->stop( true );
    }
}

void GameModeScene::installCinematics( SpotMap *pSpot, ScenaricBank *pBank ) {
    // FUN_007bc920
    if ( pBank == nullptr || pSpot == nullptr ) {
        return;
    }

    pCurrentSpotWorld = pSpot;
    pCinematicIntroI = nullptr;

    bool bIsBike = false;
    CarState *pPlayerCar = gpScenaric->getLocalPlayer().getCarState();
    if ( pPlayerCar != nullptr && pPlayerCar->getResource() != nullptr ) {
        bIsBike = gpDatabase->isBike(
            gpDatabase->getCarDBEntry( pPlayerCar->getResource()->CarID ) );
    }

    if ( currentGameMode != GM_DriveIn || !bIsBike ) {
        const int32_t tutorialStep = gpScenaric->getIntroTutorial().getStep();
        if ( tutorialStep != kTutorialStatePending &&
             tutorialStep != kTutorialStateRunning ) {
            pCinematicIntroI = FindCinematic( pBank, "C_INTRO_TUTORIAL" );
        }

        if ( pCinematicIntroI == nullptr ) {
            static const char *const kIntroNames[] = {
                "C_INTRO_I", "C_INTRO_I_1", "C_INTRO_I_2", "C_INTRO" };
            for ( const char *pName : kIntroNames ) {
                pCinematicIntroI = FindCinematic( pBank, pName );
                if ( pCinematicIntroI != nullptr ) {
                    break;
                }
            }
        }

        if ( pCinematicIntroI != nullptr ) {
            pCinematicIntroI->clearEnded();
            pCinematicIntroI->createTDUInterface();
        }
    }

    static const char *const kLoopNames[] = { "C_INT_L", "C_INTERIOR_L",
                                              "C_LIVING" };
    pCinematicLoop = nullptr;
    for ( const char *pName : kLoopNames ) {
        pCinematicLoop = FindCinematic( pBank, pName );
        if ( pCinematicLoop != nullptr ) {
            break;
        }
    }

    if ( pCinematicLoop != nullptr ) {
        pCinematicLoop->clearEnded();
        if ( pCinematicIntroI != nullptr ) {
            pCinematicLoop->createTDUInterface();
        }
    }

    OTDU_LOG_DEBUG( "Cinematics installed for spot 0x%016llx: introI='%s' "
                    "loop='%s'\n",
                    (unsigned long long)pSpot->Hashcode,
                    pCinematicIntroI != nullptr
                        ? pCinematicIntroI->getInstanceName().c_str()
                        : "<none>",
                    pCinematicLoop != nullptr
                        ? pCinematicLoop->getInstanceName().c_str()
                        : "<none>" );
}

void GameModeScene::applyCineWeather() {
    if ( weatherDesc[0] == '\0' ) {
        const eGameMode activeGM = gpTestDriveInstance->getActiveGameMode();
        if ( activeGM > GM_Invalid && activeGM < GM_Count ) {
            gpWeather->setCurrentWeatherDesc(
                gpWeather->getWeatherDescName( activeGM ) );
        }
    } else {
        gpWeather->setCurrentWeatherDesc( weatherDesc );
    }
    bNeedWeatherUpdate = false;
}

void GameModeScene::launchCine() {
    // FUN_007bd1c0
    if ( ( transitionFlags & 1 ) != 0 ) {
        return;
    }

    if ( !bWeatherSet ) {
        if ( pCinematicIntroE != nullptr ) {
            gpWeather->setCurrentWeatherDesc(
                gpWeather->getWeatherDescName( GM_FreeRide ) );
        } else {
            applyCineWeather();
        }
        bWeatherSet = true;
        bNeedWeatherUpdate = false;
    }

    if ( !bFrontEndMusicLaunched &&
         ( pCinematicIntroE == nullptr ||
           !pCinematicIntroE->hasMusicEmitter() ) &&
         ( pCinematicIntroI == nullptr ||
           !pCinematicIntroI->hasMusicEmitter() ) ) {
        gpFlash->getFlashSound()->playFrontEndMusic( true );
        bFrontEndMusicLaunched = true;
    }

    if ( pCurrentSpotWorld == nullptr ) {
        return;
    }

    if ( pCinematicIntroE != nullptr ) {
        if ( !pCinematicIntroE->isPlaying() && !pCinematicIntroE->hasEnded() ) {
            pCinematicIntroE->setFadeTime( kCinematicIntroFadeTime );
            pCinematicIntroE->setGameModeInterface( this );
            pCinematicIntroE->launchSpotCine( pCurrentSpotWorld, 0.0f );

            SetReflectionSceneEnabled( false );
            gpMngShadow->setCastersEnabled( true );
            SetHUDMoviesPaused( true );
        }
        return;
    }

    if ( pCinematicIntroI != nullptr ) {
        if ( !pCinematicIntroI->isPlaying() && !pCinematicIntroI->hasEnded() ) {
            pCinematicIntroI->setGameModeInterface( this );
            pCinematicIntroI->launchSpotCine( pCurrentSpotWorld,
                                              cinematicTimer );

            SetReflectionSceneEnabled( true );
            gpMngShadow->setCastersEnabled( true );
            SetHUDMoviesPaused( true );
            applyCineWeather();
        }
        return;
    }

    if ( pCinematicLoop != nullptr && !pCinematicLoop->isPlaying() &&
         !pCinematicLoop->hasEnded() ) {
        pCinematicLoop->setGameModeInterface( this );
        pCinematicLoop->launchSpotCine( pCurrentSpotWorld, cinematicTimer );

        SetReflectionSceneEnabled( true );
        gpMngShadow->setCastersEnabled( true );
        SetHUDMoviesPaused( false );
        applyCineWeather();
    }
}

void GameModeScene::stopCine() {
    // FUN_007bd060
    if ( !IsCinematicSkipRequested() ) {
        return;
    }

    EntityCinematic *pCine = pCinematicIntroE;
    if ( pCine != nullptr && pCine->isPlaying() ) {
        if ( !pCine->isSkippable() ) {
            return;
        }
        notifyCinematicEndFade( pCine );
        pCine->markSkipped();
        return;
    }

    pCine = pCinematicIntroI;
    if ( pCine == nullptr || !pCine->isPlaying() || !pCine->isSkippable() ) {
        return;
    }

    notifyCinematicEndFade( pCine );
    pCine->markSkipped();
    pCine->stop( false );
}

void GameModeScene::notifyCinematicEndFade( EntityCinematic *pCine ) {
    if ( pCine->getGameModeInterface() == nullptr ) {
        return;
    }

    const float fadeStart =
        pCine->getDuration() - pCine->getFadeTime() * 0.001f;
    if ( pCine->getTime() < fadeStart ) {
        pCine->getGameModeInterface()->executeAction( CE_StartEndFade, pCine );
    }
}

bool GameModeScene::executeAction( int32_t param_2, void *param_3 ) {
    // FUN_007bd850
    EntityCinematic *pCine = static_cast<EntityCinematic *>( param_3 );

    switch ( param_2 ) {
    case CE_StartEndFade:
        if ( pCinematicIntroE != nullptr && pCinematicIntroE == pCine &&
             gpMngFade != nullptr &&
             ( gpMngFade->getTransitionFlags() & 1 ) == 0 ) {
            const float fadeTimeInMs = pCinematicIntroE->getFadeTime();
            gpMngFade->startTransition(
                ( gpMngFade->getTransitionFlags() & ~0xcu ) | 1u,
                fadeTimeInMs * kMillisecondsToSeconds,
                &GameModeScene::IntroExtToIntroInCallback,
                reinterpret_cast<void *>(
                    static_cast<intptr_t>( fadeTimeInMs ) ),
                this );
        }
        break;

    case CE_Started:
        if ( pCinematicIntroE != nullptr && pCinematicIntroE == pCine &&
             gpMngFade != nullptr ) {
            gpMngFade->setTransitionColorRGB( 0x00, 0x00, 0x00 );
            gpMngFade->startTransition(
                ( gpMngFade->getTransitionFlags() & ~8u ) | 5u,
                kCinematicFadeInDuration );
        }
        return true;

    case CE_Ended: {
        if ( pCinematicIntroE != nullptr && pCinematicIntroE == pCine ) {
            pCinematicIntroE = nullptr;
            applyCineWeather();
            break;
        }

        if ( pCinematicIntroI == nullptr || pCinematicIntroI != pCine ) {
            break;
        }

        pCinematicIntroI = nullptr;

        OTDU_UNIMPLEMENTED_ONCE; // TODO: TUTORIAL (relaunch the STS_CarRental
                                 // dialog)

        if ( pCinematicLoop != nullptr && !pCinematicLoop->isPlaying() ) {
            pCinematicLoop->setGameModeInterface( this );
            pCinematicLoop->launchSpotCine( pCurrentSpotWorld, 0.0f );

            gpMngShadow->setCastersEnabled( true );
            SetReflectionSceneEnabled( true );
        }

        SetHUDMoviesPaused( false );

        OTDU_UNIMPLEMENTED_ONCE; // TODO: CAMERA (snapshot the vehicle camera
                                 // the intro left behind)
        break;
    }

    default:
        break;
    }

    return false;
}

void GameModeScene::updateWeather() {
    // FUN_007bd540
    if ( !bNeedWeatherUpdate ) {
        return;
    }

    const char *pWeatherDesc = weatherDesc;
    if ( weatherDesc[0] == '\0' ) {
        const eGameMode activeGM = gpTestDriveInstance->getActiveGameMode();
        if ( activeGM <= GM_Invalid || activeGM >= GM_Count ) {
            bNeedWeatherUpdate = false;
            return;
        }
        pWeatherDesc = gpWeather->getWeatherDescName( activeGM );
    }

    gpWeather->setCurrentWeatherDesc( pWeatherDesc );
    bNeedWeatherUpdate = false;
}

void GameModeScene::toggleHDRRadialBlur() {
    // FUN_007bc030
    ToggleHDRAndRadialBlur();
}
