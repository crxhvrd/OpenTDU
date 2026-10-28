#include "shared.h"
#include "gm_intro.h"

#include "tdu_instance.h"
#include "config/gs_config.h"
#include "core/arg_parser.h"
#include "core/maths_helper.h"
#include "filesystem/gs_file.h"
#include "flash/gs_flash.h"
#include "flash/mng_fade.h"
#include "flash/implementation/flash_player.h"
#include "ai/gs_ai.h"
#include "ai/ai_pool.h"
#include "game/avatar/avatar_configuration.h"
#include "game/avatar/gs_avatar.h"
#include "game/game_cheats.h"
#include "game/gs_intro_pool.h"
#include "game/scenaric/avatar_manager.h"
#include "game/scenaric/bank_scene_arrays.h"
#include "game/scenaric/entity/entity_avatar.h"
#include "game/scenaric/entity/entity_camera.h"
#include "game/scenaric/entity/entity_cinematic.h"
#include "game/scenaric/entity/entity_driver.h"
#include "game/scenaric/entity/entity_factory.h"
#include "game/scenaric/entity/entity_pnj.h"
#include "game/scenaric/game_intro_tutorial.h"
#include "game/scenaric/gs_scenaric.h"
#include "game/vehicle/car_state.h"
#include "game/vehicle/gs_car_state.h"
#include "game/vehicle/vehicle_base.h"
#include "game/vehicle/gs_car_display.h"
#include "player_data/gs_playerdata.h"
#include "player_data/player_data_interface.h"
#include "player_data/saved_avatar.h"
#include "physics/gs_physics.h"
#include "physics/vehicle/physics_vehicle.h"
#include "render/camera/camera_game.h"
#include "render/camera/gs_camera.h"
#include "render/2dm.h"
#include "render/material.h"
#include "render/gs_render.h"
#include "render/render_scene.h"
#include "render/postfx/gs_postfx.h"
#include "render/postfx/effects/postfx_blur.h"
#include "render/postfx/effects/postfx_fake_dof.h"
#include "render/postfx/effects/postfx_radial_blur.h"
#include "world/details/gs_details.h"
#include "world/gs_aircraft.h"
#include "world/gs_world.h"
#include "world/gs_world_streaming.h"
#include "world/mng_sun_flare.h"
#include "world/weather/gs_weather.h"

#include "game/cinematic/cin_res_collection.h"
#include "game/cinematic/cinematic_music_emitter.h"

GMIntro *gpGMIntro = nullptr; // DAT_010e7d08

static constexpr int32_t kNumIntroStates = 8;
static constexpr int32_t kNumIntroAvatarPresets = 8;

int32_t gIntroStartState = 1;      // DAT_00f94750
int32_t gIntroCmdAvatarIndex = -1; // DAT_00f94754

static CmdLineArg CmdLineArgIntroState( "introstate", []( const char *pArg ) {
    const int32_t state = atoi( pArg );
    if ( state > 0 && state < kNumIntroStates ) {
        gIntroStartState = state;
    }
} );

static CmdLineArg CmdLineArgIntroAvatar( "introavatar", []( const char *pArg ) {
    const int32_t index = atoi( pArg );
    if ( index >= 0 && index < kNumIntroAvatarPresets ) {
        gIntroCmdAvatarIndex = index;
    }
} );

static constexpr uint32_t kIntroStateBankMask[kNumIntroStates] = {
    0x1, 0x2, 0x2, 0x2, 0x4, 0x4, 0x4, 0x8 }; // DAT_00f03e84

static constexpr const char *kIntroBankPaths[4] = {
    // PTR_s__00f03ea4
    "", "Scenarics/Cinematics/intro/start.bnk",
    "Scenarics/Cinematics/intro/inplane.bnk",
    "Scenarics/Cinematics/intro/finish.bnk" };

static constexpr int32_t kIntroStateFadeType[8] = { 0, 0, 0, 3, 1,
                                                    2, 1, 3 }; // DAT_00f03eb4

static constexpr const char *kIntroAvatarPresets[kNumIntroAvatarPresets] = {
    // DAT_00f94730
    "AvatarM_Misfit",      "AvatarM_Gentleman",   "AvatarM_Sport",
    "AvatarF_Rock",        "AvatarF_Nature",      "AvatarM_Hiphop",
    "AvatarF_StarFashion", "AvatarM_Sportstudent" };

static constexpr uint32_t kIntroPoolSize = 0x1c924c3;  // DAT_00f7f174
static constexpr uint32_t kIntroPoolOffset = 0xa80000; // DAT_00a80000
static constexpr float kAvatarCarouselStep = 0.7f;     // DAT_00f41828
static constexpr float kFarViewTransitionTime = 0.6f;  // DAT_00f416c8
static constexpr float kCloseUpTransitionTime = 0.5f;  // FLOAT_00f41674
static constexpr float kAvatarChoiceNearPlane = 0.01f;
static constexpr float kSpeedBlurRate = 4.0f;          // FLOAT_00f418d4
static constexpr float kMaxBlurSpeed = 200.0f;         // FLOAT_00f4170c
static constexpr float kBlurSpeedScale = 0.005f;       // DAT_00f41bb0
static constexpr float kConstantBlurIntensity = 20.0f; // DAT_00f03ee0
static constexpr float kInstantBlurRate = 1000.0f;     // DAT_00f41718
static constexpr float kCentisecondsToSeconds = 0.01f; // DAT_00f416d4

static inline Eigen::Vector4f GetStreamOrigin() {
    return gpWorldStreaming->isEnabled() ? gpWorldStreaming->getPosition()
                                         : Eigen::Vector4f::Zero();
}

static inline void SetLogoFade( const bool bRenderLogo ) {
    if ( gpMngFade != nullptr ) {
        gpMngFade->setTransitionFlags( gpMngFade->getTransitionFlags() | 8u );
        gpMngFade->setRenderLogo( bRenderLogo );
    }
}

static inline EntityCinematic *FindIntroCinematic( ScenaricBank &bank,
                                                   const char *pName ) {
    // FUN_007dac10
    return static_cast<EntityCinematic *>(
        bank.findEntity( eEntityType::ET_CINE, pName ) );
}

static inline void
InstallAvatarConfiguration( EntityAvatar *pAvatar,
                            const AvatarConfiguration *pConfig,
                            const bool bAsync ) {
    if ( pAvatar != nullptr ) {
        pAvatar->installConfiguration( pConfig, 0, bAsync, false );
    }
}

static void SetBankMaterialOT( const ScenaricBank &bank,
                               const uint64_t hashcode,
                               const uint16_t ot ) {
    // FUN_0076c390
    for ( const BankRenderResource<Render2DM> &resource :
          bank.getRender2DMs() ) {
        if ( resource.object == nullptr ||
             resource.object->getMaterialArray() == nullptr ) {
            continue;
        }

        RenderFile::Section *pSection =
            resource.object->getMaterial( hashcode );
        if ( pSection != nullptr ) {
            reinterpret_cast<Material *>( pSection + 1 )->OT = ot;
        }
    }
}

static void UpdateSunSceneBlurFlag() {
    RenderScene *pSunScene =
        gpRender->getRenderPassAtIndex( eRenderPass::RP_Sun ).pScene;

    PostFXStep *pRadialBlur =
        gpPostFX->getEffect<ePostFXStep::PFXS_RadialBlur>();
    PostFXStep *pFakeDOF = gpPostFX->getEffect<ePostFXStep::PFXS_FakeDOF>();
    const bool bIsBlurred =
        ( pRadialBlur != nullptr && pRadialBlur->getInput()->isEnabled() ) ||
        ( pFakeDOF != nullptr && pFakeDOF->getInput()->isEnabled() );

    uint32_t flags = pSunScene->getFlags();
    if ( bIsBlurred ) {
        flags &= ~0x20u;
    } else {
        flags |= 0x20u;
    }
    pSunScene->setFlags( flags );
}

static void SetFakeDOFScenesEnabled( const bool bEnabled ) {
    RenderScene *pPrePass =
        gpRender->getRenderPassAtIndex( eRenderPass::RP_FakeDOFPrePass ).pScene;
    if ( pPrePass != nullptr ) {
        pPrePass->setEnabled( bEnabled );
    }

    RenderScene *pFakeDOF =
        gpRender->getRenderPassAtIndex( eRenderPass::RP_FakeDOF ).pScene;
    if ( pFakeDOF != nullptr ) {
        pFakeDOF->setEnabled( bEnabled );
    }
}

static void SetCockpitRadialBlurEnabled( const bool bEnabled ) {
    if ( gpPostFX->isFXLocked() ) {
        return;
    }

    PostFXStep *pCockpitBlur =
        gpPostFX->getEffect<ePostFXStep::PFXS_RadialBlurCockpit>();
    if ( pCockpitBlur == nullptr ) {
        return;
    }

    pCockpitBlur->getInput()->setEnabled( bEnabled );

    PostFXStep *pHDRI = gpPostFX->getEffect<ePostFXStep::PFXS_HDRI>();
    if ( pHDRI != nullptr ) {
        if ( bEnabled ) {
            pHDRI->getInput()->getFlagsRW() |= 1u;
        } else {
            pHDRI->getInput()->getFlagsRW() &= ~1u;
        }
    }

    PostFXStep *pDesaturation =
        gpPostFX->getEffect<ePostFXStep::PFXS_Desaturation>();
    if ( bEnabled ) {
        pDesaturation->getInput()->getFlagsRW() |= 1u;
    } else {
        pDesaturation->getInput()->getFlagsRW() &= ~1u;
    }
}

static inline PostFXNodeRadialBlur *GetCockpitRadialBlurNode() {
    return static_cast<PostFXStepRadialBlurCockpit *>(
               gpPostFX->getEffect<ePostFXStep::PFXS_RadialBlurCockpit>() )
        ->pNode;
}

static Eigen::Vector4f LerpClamped( const Eigen::Vector4f &from,
                                    const Eigen::Vector4f &to,
                                    const float t ) {
    // FUN_008d1c30
    Eigen::Vector4f result = from;
    for ( int32_t i = 0; i < 3; i++ ) {
        const float value = from[i] + ( to[i] - from[i] ) * t;
        result[i] = std::clamp( value, std::min( from[i], to[i] ),
                                std::max( from[i], to[i] ) );
    }
    return result;
}

GMIntro::GMIntro()
    : GameModeWorld( 0 ),
      mngMovie(),
      introShowcaseCarControl(),
      pFlashPlayer( nullptr ),
      bMovieLoaded( false ),
      state( 0 ),
      pendingState( 0 ),
      pBankHeapCursor{ nullptr, nullptr },
      pBankHeapBase{ nullptr, nullptr },
      bankHeapHalfSize( 0 ),
      bankHeapBuffer( 0 ),
      currentBankMask( 0 ),
      previousBankMask( 0 ),
      pIntroSc01( nullptr ),
      pIntroSc02( nullptr ),
      pIntroSc03( nullptr ),
      pCamSelectionClose( nullptr ),
      pCamSelectionFar( nullptr ),
      pChoiceCinematics{ nullptr, nullptr, nullptr, nullptr },
      pInplaneSc02( nullptr ),
      pInplaneSc04( nullptr ),
      pFinishCine( nullptr ),
      pAvatarChoiceSoundLeft( nullptr ),
      pAvatarChoiceSoundRight( nullptr ),
      pAvatarChoiceCine( nullptr ),
      bAvatarChosen( false ),
      camFarOffset( Eigen::Vector4f::Zero() ),
      camCloseOffset( Eigen::Vector4f::Zero() ),
      camFarTargetOffset( Eigen::Vector4f::Zero() ),
      camCloseTargetOffset( Eigen::Vector4f::Zero() ),
      camTransitionElapsed( 0.0f ),
      camTransitionDuration( 0.0f ),
      carouselDirection( 0.0f ),
      camTransitionStartPosition( Eigen::Vector4f::Zero() ),
      camTransitionEndPosition( Eigen::Vector4f::Zero() ),
      camTransitionStartTarget( Eigen::Vector4f::Zero() ),
      camTransitionEndTarget( Eigen::Vector4f::Zero() ),
      carouselTime( 0.0f ),
      selectedAvatar( 0 ),
      bCloseUpView( false ),
      pChoiceAvatars{},
      pIntroAvatars{},
      pBankNPCAvatars{},
      numBankNPCAvatars( 0 ),
      bIntroAvatarsSpawned( false ),
      saturationFadeFrom( 0.0f ),
      saturationFadeTo( 0.0f ),
      saturationFadeCurrent( 0.0f ),
      saturationFadeElapsed( 0.0f ),
      saturationFadeDuration( 0.0f ),
      pPendingCinematic( nullptr ),
      choiceCinematicIndex( 0 ),
      bCinematicStarted( false ),
      blurTarget( 0.0f ),
      blurRate( 0.0f ),
      bBlurConstant( false ),
      blurCurrent( 0.0f ),
      blurPreviousCameraPosition( Eigen::Vector4f::Zero() ),
      blurSpeed( 0.0f ),
      bCockpitZFar( false ),
      bCockpitZFarNoPostCockpit( false ),
      bWeatherPending( false ),
      pendingWeatherName{},
      delayedStateCountdown( -1 ),
      delayedState( 0 ),
      numDrawSuspensions( 0 ),
      bForceStreamCheck( false ),
      bSavedSunEnabled( false ) {
    // FUN_0076d780
    setTransitionFlags( 0x25 );

    registerManager( &introShowcaseCarControl );
    gpGMIntro = this;
    registerManager( &mngMovie );
}

GMIntro::~GMIntro() {
    // FUN_0076d6d0
    gpGMIntro = nullptr;
}

void GMIntro::initialize() {
    // FUN_0076f670
    GameModeWorld::initialize();

    gpDetails->getDetailByHash( kDetailHashBirds )->setEnabled( false );
    gpAI->setTrafficCategories( false, 0 );
    gpAircraft->setDrawAircrafts( false );

    if ( gpMngSunFlare != nullptr ) {
        bSavedSunEnabled = gbSunEnabled;
        gbSunEnabled = false;
    }

    gpScenaric->getAvatarManager().releaseDriverAvatar();

    uint8_t *pPool = static_cast<uint8_t *>( gpIntroPool->pIntroPool );
    bankHeapHalfSize =
        ( ( kIntroPoolSize + kIntroPoolOffset ) >> 1 ) & ~( 0x10u - 1u );
    pBankHeapCursor[0] = pPool;
    pBankHeapCursor[1] = pPool + bankHeapHalfSize;
    pBankHeapBase[0] = pBankHeapCursor[0];
    pBankHeapBase[1] = pBankHeapCursor[1];
}

void GMIntro::reset() {
    // FUN_0076f570
    GameModeWorld::reset();

    bIntroAvatarsSpawned = false;
    if ( gIntroCmdAvatarIndex != -1 ) {
        AvatarConfiguration config;
        if ( gpAvatar->loadStyleConfiguration(
                 config, kIntroAvatarPresets[gIntroCmdAvatarIndex] ) ) {
            gpPlayerData->getLoadedSave()->getAvatar()->Config = config;
        }
        if ( gIntroStartState > 2 ) {
            selectedAvatar = gIntroCmdAvatarIndex;
        }
    }

    pFlashPlayer = gpFlash->getFlashPlayer( "INTRO" );

    RenderScene *pPostCockpit =
        gpRender->getRenderPassAtIndex( eRenderPass::RP_PostCockpit ).pScene;
    if ( pPostCockpit != nullptr ) {
        pPostCockpit->setEnabled( true );
    }

    SetLogoFade( true );
}

void GMIntro::tick( float deltaTime, float totalTime ) {
    // FUN_0076f320
    GameModeWorld::tick( deltaTime, totalTime );

    if ( delayedStateCountdown >= 0 ) {
        if ( delayedStateCountdown == 0 ) {
            if ( gpMngFade != nullptr ) {
                gpMngFade->setRenderLogo( false );
            }
            setState( false, delayedState );
            delayedState = 0;
        }
        delayedStateCountdown--;
    }

    if ( bCinematicStarted ) {
        if ( gpMngFade != nullptr ) {
            gpMngFade->setRenderLogo( false );
            if ( ( gpMngFade->getTransitionFlags() & 8u ) != 0 && state != 5 ) {
                gpMngFade->setTransitionFlags( gpMngFade->getTransitionFlags() &
                                               ~8u );
            }
        }
        bDraw = true;
        numDrawSuspensions = 0;

        if ( bAvatarChosen ) {
            stopAvatarChoiceMusic();
            bAvatarChosen = false;
        }
        bCinematicStarted = false;
    }

    tickSaturationFade( deltaTime );
    tickRadialBlur( deltaTime );

    switch ( state ) {
    case 0:
        swapBanks( gIntroStartState );
        setState( false, gIntroStartState );
        break;
    case 2:
        tickAvatarChoice( deltaTime );
        break;
    case 5: {
        const bool bSkip = MngMovie::SkipMovieRequested();
        BinkFramePointer *pMovie = mngMovie.getActiveMovie();
        const bool bWaitForEnd =
            bMovieLoaded && pMovie != nullptr && !pMovie->isDonePlaying();
        if ( bWaitForEnd && !bSkip ) {
            break;
        }

        if ( bSkip && pMovie != nullptr && pMovie->pContent->isPlaying() ) {
            pMovie->pContent->pause();
        }
        bMovieLoaded = false;
        setState( bSkip, 6 );
        break;
    }
    default:
        break;
    }

    if ( pendingState != 0 && isBanksReady( pendingState ) ) {
        enterState( pendingState );
        pendingState = 0;
    }

    if ( pPendingCinematic != nullptr &&
         gpWorld->isStreamedAround( gpWorld->getStreamingPositionPtr(), true,
                                    bForceStreamCheck ) &&
         gpScenaric->getAvatarManager().areAvatarsReady() &&
         ( transitionFlags & 1u ) == 0 ) {
        bForceStreamCheck = false;
        pPendingCinematic->createTDUInterface();
        if ( pPendingCinematic->areResourcesReady() ) {
            pPendingCinematic->start( true );
            pPendingCinematic = nullptr;
        }
    }

    if ( bWeatherPending ) {
        gpWeather->setCurrentWeatherDesc( pendingWeatherName );
        bWeatherPending = false;
    }
}

void GMIntro::pause() {
    // FUN_0076df10
    GSCarDisplay::SetRealTimeEnvMappedCar( nullptr );

    RenderScene *pPostCockpit =
        gpRender->getRenderPassAtIndex( eRenderPass::RP_PostCockpit ).pScene;
    if ( pPostCockpit != nullptr ) {
        pPostCockpit->setEnabled( false );
    }

    saturationFadeFrom = saturationFadeCurrent;
    saturationFadeTo = 0.0f;
    saturationFadeElapsed = 0.0f;
    saturationFadeDuration = 0.0f;

    if ( gpMngFade != nullptr ) {
        gpMngFade->setRenderLogo( false );
        gpMngFade->setTransitionFlags( gpMngFade->getTransitionFlags() & ~8u );
    }
    numDrawSuspensions = 0;
    bDraw = true;

    GameModeWorld::pause();
}

void GMIntro::destroy() {
    // FUN_0076dda0
    gpAircraft->setDrawAircrafts( true );

    AiPool *pAIPool = gpAI->getAIPool();
    pAIPool->setTrafficPaused( false );
    pAIPool->setField0x7C( true );
    pAIPool->setField0x85( true );
    gpAI->setPauseTraffic( false );

    gpDetails->getDetailByHash( kDetailHashBirds )->setEnabled( true );
    if ( gpMngSunFlare != nullptr ) {
        gbSunEnabled = bSavedSunEnabled;
    }

    selectAvatars( AS_ReleaseAll, -1 );
    unloadBanks( currentBankMask );
    currentBankMask = 0;
    unloadBanks( previousBankMask );
    previousBankMask = 0;

    GameModeWorld::destroy();

    static constexpr uint16_t kGenderStarterClothes[2] = { 0x407, 0x208 };
    const int32_t gender = gpAvatar->getSavedAvatarGender();
    if ( gender < 2 ) {
        gpAvatar->unlockClothe( kGenderStarterClothes[gender], 1u );
    }

    SpawnDriverAvatar( false, false );
}

bool GMIntro::onMessage( FlashMessage *pMessage ) {
    // FUN_0076cc90
    if ( GameModeWorld::onMessage( pMessage ) ) {
        return true;
    }

    const bool bTransitionRunning = isCameraTransitionRunning();
    switch ( *pMessage ) {
    case FlashMessage( "ccha" ): // confirm the highlighted avatar
        if ( !bTransitionRunning ) {
            confirmAvatar();
        }
        break;
    case FlashMessage( "bcha" ): // back to the far view
        if ( !bTransitionRunning ) {
            switchToFarView();
        }
        break;
    case FlashMessage( "ncha" ): // next avatar
        if ( !bTransitionRunning ) {
            selectNextAvatar();
        }
        break;
    case FlashMessage( "scha" ): // close-up view
        if ( !bTransitionRunning ) {
            switchToCloseUpView();
        }
        break;
    case FlashMessage( "pcha" ): // previous avatar
        if ( !bTransitionRunning ) {
            selectPreviousAvatar();
        }
        break;
    default:
        break;
    }

    return false;
}

bool GMIntro::executeAction( int32_t param_2, void *param_3, void *pData ) {
    // FUN_0076e0a0
    EntityCinematic *pCinematic = static_cast<EntityCinematic *>( param_3 );

    switch ( param_2 ) {
    case CE_Started: {
        bCinematicStarted = true;
        releaseSelectedAvatarReference( pCinematic );

        const bool bIsChoiceCinematic =
            std::find( std::begin( pChoiceCinematics ),
                       std::end( pChoiceCinematics ),
                       pCinematic ) != std::end( pChoiceCinematics );
        if ( !bIsChoiceCinematic ) {
            return false;
        }

        OTDU_UNIMPLEMENTED_ONCE; // TODO: Ford/Moto/SG drivers setup for the cut
                                 // intro cutscene.
        return false;
    }

    case CE_Finished:
        saturationCoef = 1.0f;
        return false;

    case CE_FlashMessage: {
        const char *pMessage = static_cast<const char *>( pData );
        const bool bHasArgument = pMessage[4] == ':' && pMessage[5] != '\0';

        switch ( FlashMessage( pMessage ) ) {
        case FlashMessage( "pwlo" ): // power logo
            if ( gpMngFade != nullptr ) {
                gpMngFade->setRenderLogo( true );
            }
            return true;

        case FlashMessage( "fxca" ): { // desaturation fade
            if ( !bHasArgument ) {
                break;
            }
            float target = 0.0f;
            int32_t centiseconds = 0;
            sscanf( pMessage + 5, "%f:%d", &target, &centiseconds );
            const float duration =
                static_cast<float>( centiseconds ) * kCentisecondsToSeconds;
            if ( 0.0f <= target && target <= 1.0f && 0.0f <= duration ) {
                startSaturationFade( target, duration );
            }
            return true;
        }

        case FlashMessage( "flsh" ): // drive the INTRO flash movie
            pFlashPlayer->showFrame( pMessage + 5, "/", true );
            return true;

        case FlashMessage( "fxbl" ): { // speed-driven radial blur
            if ( !bHasArgument ) {
                break;
            }
            float target = 0.0f;
            sscanf( pMessage + 5, "%f", &target );
            startSpeedBlur( target );
            return true;
        }

        case FlashMessage( "fxrv" ): { // constant radial blur
            if ( !bHasArgument ) {
                break;
            }
            int32_t bEnabled = 0;
            float duration = 0.0f;
            sscanf( pMessage + 5, "%d:%f", &bEnabled, &duration );
            const float rate = ( duration <= 0.0f )
                                   ? kInstantBlurRate
                                   : kConstantBlurIntensity / duration;
            startConstantBlur( bEnabled != 0 ? kConstantBlurIntensity : 0.0f,
                               rate );
            if ( bEnabled == 0 ) {
                blurCurrent = kConstantBlurIntensity;
            }
            return true;
        }

        case FlashMessage( "shak" ): { // camera shake
            if ( !bHasArgument ) {
                break;
            }
            float amplitude = 0.0f;
            float speed = 0.0f;
            sscanf( pMessage + 5, "%f:%f", &amplitude, &speed );
            if ( pCinematic != nullptr ) {
                pCinematic->getCinResCollection().setCameraShake( amplitude,
                                                                  speed );
            }
            return true;
        }

        case FlashMessage( "wthr" ): { // weather change
            char weatherName[64];
            strncpy( weatherName, pMessage + 5, sizeof( weatherName ) - 1 );
            weatherName[sizeof( weatherName ) - 1] = '\0';
            char *pSeparator = strchr( weatherName, ':' );
            if ( pSeparator != nullptr ) {
                *pSeparator = '\0';
                strcpy( pendingWeatherName, weatherName );
                bWeatherPending = true;
            }
            return true;
        }

        case FlashMessage( "zfar" ): { // cockpit z range
            if ( !bHasArgument ) {
                break;
            }
            int32_t bEnabled = 0;
            int32_t bNoPostCockpit = 0;
            sscanf( pMessage + 5, "%d:%d", &bEnabled, &bNoPostCockpit );
            setZFarMode( bEnabled != 0, pCinematic, bNoPostCockpit != 0 );
            if ( bEnabled != 0 ) {
                EnableFakeDOF();
            } else {
                DisableFakeDOF();
            }
            return true;
        }

        default:
            break;
        }
        return false;
    }

    case CE_Removed: {
        stopRadialBlur();
        mngActor.setRenderingInCockpitPass( false );
        bCockpitZFar = false;
        bCockpitZFarNoPostCockpit = false;
        DisableFakeDOF();

        const bool bSkipped = pCinematic->wasSkipped();
        if ( pCinematic == pIntroSc01 && !bSkipped ) {
            launchCinematic( &pIntroSc01->getModelMatrix(), pIntroSc02, true );
            return false;
        }
        if ( pCinematic == pIntroSc02 ||
             ( bSkipped && pCinematic == pIntroSc01 ) ) {
            setState( bSkipped, 2 );
            return false;
        }
        if ( pCinematic == pIntroSc03 ) {
            setState( bSkipped, 4 );
            return false;
        }
        if ( pCinematic == pInplaneSc02 ) {
            setState( bSkipped, 5 );
            return false;
        }

        for ( int32_t i = 0; i < kNumChoiceCinematics - 1; i++ ) {
            if ( pCinematic != pChoiceCinematics[i] || bSkipped ) {
                continue;
            }

            ReleaseDriverFromCar();
            if ( i == 0 ) {
                bForceStreamCheck = true;
            }
            SetLogoFade( true );

            EntityCinematic *pNextCinematic = pChoiceCinematics[i + 1];
            if ( pNextCinematic != nullptr ) {
                launchCinematic( nullptr, pNextCinematic, true );
                choiceCinematicIndex = i + 1;
            }
            return false;
        }

        const bool bIsChoiceCinematic =
            std::find( std::begin( pChoiceCinematics ),
                       std::end( pChoiceCinematics ),
                       pCinematic ) != std::end( pChoiceCinematics );
        if ( pCinematic == pChoiceCinematics[kNumChoiceCinematics - 1] ||
             ( bSkipped && bIsChoiceCinematic ) ) {
            ReleaseDriverFromCar();
            setState( bSkipped, 6 );
            return false;
        }
        if ( pCinematic == pInplaneSc04 ) {
            setState( bSkipped, 7 );
            return false;
        }
        if ( pCinematic == pFinishCine ) {
            SetLogoFade( true );
            gpScenaric->getIntroTutorial().onIntroFinished();
        }
        return false;
    }

    default:
        break;
    }

    return false;
}

EntityAvatar *GMIntro::addIntroAvatarReference( const int32_t index ) {
    // FUN_0076b410
    EntityAvatar *pAvatar = pIntroAvatars[index];
    gpScenaric->getAvatarManager().addReference( pAvatar );
    return pAvatar;
}

GMIntro::IntroCarSlot *GMIntro::FindIntroCarSlot( const char *pName ) {
    // FUN_007e4f00 (inlined)
    if ( gpGMIntro == nullptr || pName == nullptr ) {
        return nullptr;
    }

    const int32_t index = pName[3] - '0';
    if ( index < 0 || index >= kNumIntroCarSlots ) {
        return nullptr;
    }
    return &gpGMIntro->introCarSlots[index];
}

GMIntro::IntroCarSlot *GMIntro::acquireIntroCar( const char *pName,
                                                 bool &bOutBindActor ) {
    // FUN_007e6b10
    static constexpr int32_t kRemappedCarID = 0x110;
    static constexpr int32_t kRemappedCarIDTarget = 0x5e1;

    bOutBindActor = false;
    IntroCarSlot *pSlot = FindIntroCarSlot( pName );
    if ( pSlot == nullptr ) {
        return nullptr;
    }

    const char *pSeparator = std::strchr( pName + 5, '_' );
    if ( pSeparator == nullptr ) {
        return nullptr;
    }

    int32_t carID = atoi( pName + 5 );
    if ( carID == kRemappedCarID ) {
        carID = kRemappedCarIDTarget;
    }

    if ( pSlot->RefCount < 1 ) {
        if ( installIntroCar( carID, *pSlot ) == nullptr ) {
            return nullptr;
        }
        pSlot->pName = pName;
        bOutBindActor = true;
        return pSlot;
    }

    pSlot->RefCount++;
    pSlot->CinematicRefCount++;
    if ( pSlot->CinematicRefCount == 1 ) {
        bOutBindActor = true;
        PhysicsVehicle *pVehicle = pSlot->pCarState->getPhysicsVehicle();
        if ( pVehicle != nullptr ) {
            PhysicsLock lock;
            pVehicle->setPostponedPause( 2 );
        }
    }
    return pSlot;
}

CarState *GMIntro::installIntroCar( const int32_t carID, IntroCarSlot &slot ) {
    // FUN_0076eda0
    if ( slot.RefCount != 0 ) {
        return slot.pCarState;
    }

    VehicleSpawnParams params;
    params.Transform.row( 3 ) = GetStreamOrigin();
    params.Transform( 3, 3 ) = 1.0f;
    params.bSetPosition = true;
    params.bAddPhysicsPaused = true;
    params.bResetCar = true;
    params.bCinematicMode = true;
    params.PhantomMode = 1;

    VehicleBaseResources *pResource = gpCarState->loadVehicleResources(
        carID, 0, 0, ~0ull, 0, 0, eVehicleResourceType::VRT_Traffic );
    CarState *pCarState =
        gpCarState->installVehicle( pResource, params, VIT_AI, -1 );
    if ( pCarState == nullptr ) {
        if ( pResource != nullptr ) {
            gpCarState->releaseResourceEntry( pResource );
        }
        return nullptr;
    }

    PhysicsVehicle *pVehicle = pCarState->getPhysicsVehicle();
    if ( pVehicle != nullptr ) {
        PhysicsLock lock;
        pVehicle->resetToTransform( params.Transform );
    }

    slot.pCarState = pCarState;
    slot.RefCount++;
    return pCarState;
}

void GMIntro::releaseIntroCar( IntroCarSlot *pSlot ) {
    // FUN_0076cf30
    pSlot->RefCount--;
    pSlot->CinematicRefCount--;
    if ( pSlot->CinematicRefCount == 0 && pSlot->pActor != nullptr ) {
        PhysicsVehicle *pVehicle = pSlot->pCarState->getPhysicsVehicle();
        if ( pVehicle != nullptr ) {
            PhysicsLock lock;
            pVehicle->setPostponedPause( 1 );
        }
        pSlot->pName = nullptr;
        pSlot->NumPartsUpdated = 0;
        pSlot->NumParts = 0;
        pSlot->bPosePending = false;
        pSlot->pActor->pIntroCarSlot = nullptr;
    }

    if ( pSlot->RefCount != 0 ) {
        return;
    }

    CarState *pCarState = pSlot->pCarState;
    *pSlot = IntroCarSlot();
    if ( pCarState == nullptr ) {
        return;
    }

    PhysicsVehicle *pVehicle = pCarState->getPhysicsVehicle();
    if ( pVehicle != nullptr ) {
        PhysicsLock lock;
        pVehicle->setKeyframed( false, false );
    }

    VehicleBase *pDisplay = pCarState->getVehicleInstance();
    if ( pDisplay != nullptr ) {
        pDisplay->unloadInterior();
    }
    gpCarState->unloadVehicle( pCarState, false, false );
}

GMIntro::IntroCarPart *GMIntro::allocateIntroCarPart( const char *pName ) {
    // FUN_0076b450
    IntroCarPart *pPart = nullptr;
    for ( IntroCarPart &part : introCarParts ) {
        if ( !part.bUsed ) {
            pPart = &part;
            break;
        }
    }
    if ( pPart == nullptr ) {
        return nullptr;
    }
    pPart->bUsed = true;

    // FUN_0076b350
    IntroCarSlot *pSlot = FindIntroCarSlot( pName );
    pPart->pName = pName;
    pPart->PartIndex = -1;
    pPart->pSlot = pSlot;
    if ( pSlot != nullptr ) {
        pSlot->NumParts =
            std::min( pSlot->NumParts + 1, kMaxIntroCarSlotParts );
    }
    return pPart;
}

void GMIntro::setState( const bool bSkipped, int32_t newState ) {
    // FUN_0076dfb0
    OTDU_LOG_DEBUG( "GMIntro: state %d -> %d%s\n", state, newState,
                    bSkipped ? " (skipped)" : "" );
    leaveState();
    state = newState;

    if ( bSkipped ) {
        SetLogoFade( true );
        if ( newState > 2 ) {
            SetLogoFade( true );
            gpScenaric->getIntroTutorial().onIntroFinished();
            return;
        }

        newState = 2;
        enterState( newState );
    } else {
        switch ( kIntroStateFadeType[newState] ) {
        case 1:
            SetLogoFade( true );
            break;
        case 2:
            if ( gpMngFade != nullptr ) {
                gpMngFade->setTransitionFlags( gpMngFade->getTransitionFlags() |
                                               8u );
            }
            break;
        case 3:
            bDraw = false;
            numDrawSuspensions++;
            if ( gpMngFade != nullptr ) {
                gpMngFade->setRenderLogo( false );
            }
            break;
        default:
            break;
        }

        if ( newState == 5 || newState == 2 ) {
            enterState( newState );
        } else {
            pendingState = newState;
        }
    }

    if ( newState + 1 < kNumStates ) {
        swapBanks( newState + 1 );
    }
}

void GMIntro::leaveState() {
    // FUN_0076deb0
    if ( state == 2 ) {
        if ( pAvatarChoiceCine != nullptr ) {
            pAvatarChoiceCine->setPaused( false );
            pAvatarChoiceCine->stop( true );
        }
        gpFlash->getMouse()->field12_0x30 = true;
    } else if ( state == 5 ) {
        ReleaseDriverFromCar();
    }

    unloadBanks( previousBankMask );
    previousBankMask = 0;
}

void GMIntro::enterState( const int32_t newState ) {
    // FUN_0076d5b0
    switch ( newState ) {
    case 1:
        selectAvatars( AS_LoadAll, -1 );
        if ( pIntroSc01 != nullptr ) {
            launchCinematic( nullptr, pIntroSc01, true );
        }
        break;
    case 2:
        gpFlash->getMouse()->field12_0x30 = false;
        selectAvatars( AS_LoadAll, -1 );
        enterAvatarChoice();
        break;
    case 3:
        bAvatarChosen = true;
        selectAvatars( AS_AllButSelected, 7 );
        SpawnDriverAvatar( false, true );
        if ( pIntroSc03 != nullptr ) {
            launchCinematic( &pIntroSc01->getModelMatrix(), pIntroSc03, true );
        }
        break;
    case 4:
        selectAvatars( AS_Remaining, 7 );
        launchCinematic( nullptr, pInplaneSc02, true );
        break;
    case 5:
        bMovieLoaded = mngMovie.loadMovie( "IntroTDU.bik" );
        bCinematicStarted = true;
        break;
    case 6:
        selectAvatars( AS_ReleaseAll, selectedAvatar );
        launchCinematic( nullptr, pInplaneSc04, true );
        break;
    case 7:
        launchCinematic( nullptr, pFinishCine, true );
        break;
    default:
        break;
    }
}

void GMIntro::stopAvatarChoiceMusic() {
    // FUN_0076b6d0
    if ( pAvatarChoiceMusic != nullptr ) {
        pAvatarChoiceMusic->stop( 0.0f );
    }
}

void GMIntro::enterAvatarChoice() {
    // FUN_0076d350
    if ( pAvatarChoiceCine != nullptr ) {
        launchCinematic( &pIntroSc01->getModelMatrix(), pAvatarChoiceCine,
                         false );
        pAvatarChoiceCine->setPaused( true );
    }

    pFlashPlayer->showFrame( "select_perso", "/", true );

    carouselDirection = 0.0f;
    camTransitionDuration = 0.0f;
    camTransitionElapsed = 0.0f;
    selectedAvatar = 0;
    bCloseUpView = false;

    if ( pAvatarChoiceMusic != nullptr ) {
        // FUN_0076caf0
        pAvatarChoiceMusic->play( true, 0.0f );
    }

    bAvatarChosen = false;
    carouselTime = 0.0f;
    computeAvatarCameraOffsets();

    CameraGame *pCamera =
        gpCamera->findCameraWithMode( 0, gpCamera->getBoundCarState() );
    pCamera->setFOVAndAspectRatio( pCamSelectionFar->getRatio(),
                                   pCamSelectionFar->getFOV() );

    Eigen::Vector4f position;
    Eigen::Vector4f target;
    getAvatarCameraView( selectedAvatar, bCloseUpView, position, target );
    startCameraTransition( position, target, 0.0f );

    gpCamera->setCameraMode( 0, 0, 1.0f );
}

void GMIntro::swapBanks( const int32_t newState ) {
    // FUN_0076cd90
    const uint32_t mask = kIntroStateBankMask[newState];
    if ( currentBankMask == mask ) {
        return;
    }

    pBankHeapCursor[bankHeapBuffer] = pBankHeapBase[bankHeapBuffer];
    for ( int32_t slot = 1; slot < kNumBankSlots; slot++ ) {
        if ( ( mask & ( 1u << slot ) ) != 0 ) {
            loadBank( slot, bankHeapBuffer );
        }
    }

    bankHeapBuffer = ( bankHeapBuffer - 1 ) & 1;
    previousBankMask = currentBankMask;
    currentBankMask = mask;
}

void GMIntro::loadBank( const int32_t slot, const int32_t heapBuffer ) {
    // FUN_0076cbb0
    IntroBankSlot &bankSlot = bankSlots[slot];
    bankSlot.State = BSS_Loading;

    std::string bankPath = gpConfig->getResRootPath();
    bankPath += kIntroBankPaths[slot];

    bankSlot.pMemory = pBankHeapCursor[heapBuffer];
    gpFile->loadBank( bankPath.c_str(), &bankSlot.pMemory, &bankSlot.Size,
                      &bankSlot, [&bankSlot]( int32_t type, int32_t bSuccess ) {
                          // FUN_0076b700
                          if ( type == 4 && bSuccess == 1 ) {
                              bankSlot.State = BSS_Loaded;
                          }
                      } );

    const uint32_t alignedSize = ( bankSlot.Size + 0xfu ) & ~0xfu;
    pBankHeapCursor[heapBuffer] =
        static_cast<uint8_t *>( pBankHeapCursor[heapBuffer] ) + alignedSize;
}

void GMIntro::unloadBank( const int32_t slot ) {
    // FUN_0076d4e0
    IntroBankSlot &bankSlot = bankSlots[slot];
    if ( bankSlot.State == BSS_Loading ) {
        gpFile->flushPendingAsync();
    }

    if ( bankSlot.State != BSS_Installed ) {
        bankSlot.State = BSS_None;
        return;
    }

    AvatarManager &avatarManager = gpScenaric->getAvatarManager();
    for ( uint32_t i = 0; i < numBankNPCAvatars; i++ ) {
        avatarManager.releaseAvatar( pBankNPCAvatars[i] );
        pBankNPCAvatars[i] = nullptr;
    }
    numBankNPCAvatars = 0;

    gpScenaric->unloadBank( &bankSlot.Bank );
    bankSlot.State = BSS_None;
}

void GMIntro::unloadBanks( const uint32_t mask ) {
    // FUN_0076dc70
    if ( ( mask & 2u ) != 0 ) {
        if ( pAvatarChoiceMusic != nullptr ) {
            pAvatarChoiceMusic->releaseChannels();
            pAvatarChoiceMusic.reset();
        }
        unloadBank( 1 );
        pIntroSc01 = nullptr;
        pIntroSc02 = nullptr;
        pIntroSc03 = nullptr;
        pAvatarChoiceCine = nullptr;
        pAvatarChoiceSoundLeft = nullptr;
        pAvatarChoiceSoundRight = nullptr;
        pCamSelectionFar = nullptr;
        pCamSelectionClose = nullptr;
    }
    if ( ( mask & 4u ) != 0 ) {
        unloadBank( 2 );
        pInplaneSc02 = nullptr;
        pInplaneSc04 = nullptr;
    }
    if ( ( mask & 8u ) != 0 ) {
        unloadBank( 3 );
        pFinishCine = nullptr;
    }
}

bool GMIntro::isBanksReady( const int32_t newState ) {
    // FUN_0076eba0
    const uint32_t mask = kIntroStateBankMask[newState];
    bool bReady = true;

    if ( ( mask & 2u ) != 0 ) {
        IntroBankSlot &bankSlot = bankSlots[1];
        if ( bankSlot.State == BSS_Loading ) {
            bReady = false;
        } else if ( bankSlot.State == BSS_Loaded ) {
            installBank( 1 );
            ScenaricBank &bank = bankSlot.Bank;

            pIntroSc01 = FindIntroCinematic( bank, "C_INTRO_SC_01" );
            pIntroSc01->setGameModeInterface( this );
            pIntroSc02 = FindIntroCinematic( bank, "C_INTRO_SC_02" );
            pIntroSc02->setGameModeInterface( this );
            pIntroSc03 = FindIntroCinematic( bank, "C_INTRO_SC_03" );
            pIntroSc03->setGameModeInterface( this );
            pIntroSc03->setActorsDriveStreaming( false );
            pAvatarChoiceCine = FindIntroCinematic( bank, "C_AVATAR_CHOICE" );
            pAvatarChoiceCine->setGameModeInterface( this );

            pAvatarChoiceSoundLeft =
                bank.findEntity( eEntityType::ET_SDEM, "Avatar_Choice_L" );
            pAvatarChoiceSoundRight =
                bank.findEntity( eEntityType::ET_SDEM, "Avatar_Choice_R" );
            if ( pAvatarChoiceSoundLeft != nullptr &&
                 pAvatarChoiceSoundRight != nullptr ) {
                // FUN_009c7960
                pAvatarChoiceMusic = std::make_unique<CinematicMusicEmitter>(
                    reinterpret_cast<uint8_t *>( &pAvatarChoiceSoundLeft ),
                    reinterpret_cast<uint8_t *>( &pAvatarChoiceSoundRight ) );
            }

            pCamSelectionFar = static_cast<EntityCamera *>(
                bank.findEntity( eEntityType::ET__CAM, "CAM_Selection_01" ) );
            pCamSelectionClose = static_cast<EntityCamera *>(
                bank.findEntity( eEntityType::ET__CAM, "CAM_Selection_02" ) );
            bankSlot.State = BSS_Installed;
        }
    }

    if ( ( mask & 4u ) != 0 ) {
        IntroBankSlot &bankSlot = bankSlots[2];
        if ( bankSlot.State == BSS_Loading ) {
            bReady = false;
        } else if ( bankSlot.State == BSS_Loaded ) {
            installBank( 2 );
            ScenaricBank &bank = bankSlot.Bank;

            pInplaneSc02 = FindIntroCinematic( bank, "C_SC_02" );
            pInplaneSc02->setGameModeInterface( this );
            pInplaneSc02->setActorsDriveStreaming( false );
            pInplaneSc04 = FindIntroCinematic( bank, "C_SC_04" );
            pInplaneSc04->setGameModeInterface( this );
            bankSlot.State = BSS_Installed;
        }
    }

    if ( ( mask & 8u ) != 0 ) {
        IntroBankSlot &bankSlot = bankSlots[3];
        if ( bankSlot.State == BSS_Loading ) {
            return false;
        }
        if ( bankSlot.State == BSS_Loaded ) {
            installBank( 3 );
            pFinishCine = FindIntroCinematic( bankSlot.Bank, "C_CINE" );
            pFinishCine->setGameModeInterface( this );
            bankSlot.State = BSS_Installed;
        }
    }

    return bReady;
}

void GMIntro::installBank( const int32_t slot ) {
    // FUN_0076e950
    IntroBankSlot &bankSlot = bankSlots[slot];
    ScenaricBank &bank = bankSlot.Bank;

    if ( &bank != gpScenaric->getWorldScenaricBank() ) {
        gpScenaric->registerBank( &bank );
    }
    bank.load( bankSlot.pMemory, 0, 0, nullptr, false );
    bank.forceLoad();
    bank.activate( nullptr );

    AvatarManager &avatarManager = gpScenaric->getAvatarManager();
    for ( int32_t i = 0; i < bank.getEntityCount(); i++ ) {
        Entity *pEntity = bank.getEntity( i );
        if ( pEntity->getType() != eEntityType::ET_CPNJ ) {
            continue;
        }

        AvatarConfiguration config;
        ReadAvatarConfigFromPNJ(
            config, static_cast<EntityPnj *>( pEntity )->getPnjName() );
        if ( numBankNPCAvatars >= kMaxBankNPCAvatars ) {
            continue;
        }

        EntityAvatar *pAvatar =
            avatarManager.allocateAvatar( bank.getEntityHashcode( i ), 0 );
        InstallAvatarConfiguration( pAvatar, &config, true );
        pBankNPCAvatars[numBankNPCAvatars++] = pAvatar;
    }

    if ( slot == 1 ) {
        static constexpr uint16_t kLightsMaterialOT = 0x1c;
        SetBankMaterialOT( bank, GetIdentifier64bit( "light_sol2" ),
                           kLightsMaterialOT );
        SetBankMaterialOT( bank, GetIdentifier64bit( "lum_01" ),
                           kLightsMaterialOT );
        SetBankMaterialOT( bank, GetIdentifier64bit( "SCD_porte_TS_02" ),
                           kLightsMaterialOT );
    } else if ( slot == 3 ) {
        strncpy( pendingWeatherName, "CINE_AIRPORT_HA",
                 sizeof( pendingWeatherName ) - 1 );
        bWeatherPending = true;
    }

    bankSlot.State = BSS_Installed;
}

void GMIntro::launchCinematic( const Matrix4x4f_t *pFrame,
                               EntityCinematic *pCinematic,
                               const bool bDeferred ) {
    // FUN_0076ce10
    if ( pCinematic == nullptr ) {
        return;
    }

    saturationFadeFrom = saturationFadeCurrent;
    saturationFadeTo = 0.0f;
    saturationFadeElapsed = 0.0f;
    saturationFadeDuration = 0.0f;

    Matrix4x4f_t frame =
        ( pFrame != nullptr ) ? *pFrame : pCinematic->getModelMatrix();

    ScenaricBank *pBank = pCinematic->getBank();
    if ( gpActiveGM != nullptr && pBank != nullptr &&
         pBank->getActiveScenery() != nullptr ) {
        pBank->getActiveScenery()->shiftObjects( GetStreamOrigin() );
    }

    Eigen::Vector4f streamingPosition = frame.row( 3 ).transpose();
    gpWorld->setStreamingPosition( &streamingPosition );

    Matrix4x4f_t offset = frame;
    offset.row( 3 ) -= GetStreamOrigin().transpose();
    pCinematic->setTransformOffset( offset );

    if ( bDeferred ) {
        pPendingCinematic = pCinematic;
    } else {
        pCinematic->start( true );
    }
}

void GMIntro::selectAvatars( const int32_t mode, const int32_t selectedIndex ) {
    // FUN_0076d1e0
    AvatarManager &avatarManager = gpScenaric->getAvatarManager();
    for ( int32_t i = 0; i < kNumChoiceAvatars; i++ ) {
        bool bLoad = false;
        switch ( mode ) {
        case AS_LoadAll:
            bLoad = true;
            break;
        case AS_AllButSelected:
            bLoad = i != selectedIndex;
            break;
        case AS_SelectedOnly:
            bLoad = i == selectedIndex;
            break;
        case AS_Remaining:
            bLoad = i != selectedIndex && i != selectedAvatar;
            break;
        default:
            break;
        }

        EntityAvatar *&pAvatar = pChoiceAvatars[i];
        if ( !bLoad ) {
            if ( pAvatar != nullptr ) {
                avatarManager.releaseAvatar( pAvatar );
                pAvatar = nullptr;
            }
            continue;
        }

        if ( pAvatar != nullptr ) {
            continue;
        }

        AvatarConfiguration config;
        const char *pPresetName = kIntroAvatarPresets[i];
        if ( !gpAvatar->loadStyleConfiguration( config, pPresetName ) ) {
            gpAvatar->loadStyleConfiguration( config, "hostess" );
        }

        pAvatar = avatarManager.allocateAvatar(
            GetIdentifier64bit( pPresetName ), 0 );
        InstallAvatarConfiguration( pAvatar, &config, true );
    }
}

void GMIntro::releaseSelectedAvatarReference( EntityCinematic *pSender ) {
    // FUN_0076b690
    if ( state <= 2 ) {
        return;
    }

    EntityAvatar *pAvatar = static_cast<EntityAvatar *>(
        pSender->getCinResCollection().findActorEntity(
            kIntroAvatarPresets[selectedAvatar] ) );
    if ( pAvatar != nullptr ) {
        pAvatar->removeInstanceReference();
    }
}

void GMIntro::startSaturationFade( const float target, const float duration ) {
    // FUN_0076b320
    saturationFadeFrom = saturationFadeCurrent;
    saturationFadeTo = target;
    saturationFadeElapsed = 0.0f;
    saturationFadeDuration = duration;
}

void GMIntro::tickSaturationFade( const float deltaTime ) {
    // FUN_0076b4f0
    if ( saturationFadeCurrent == saturationFadeTo ) {
        return;
    }

    saturationFadeElapsed += deltaTime;
    if ( saturationFadeElapsed >= saturationFadeDuration ) {
        saturationFadeDuration = 0.0f;
        saturationFadeCurrent = saturationFadeTo;
    } else {
        const float value = saturationFadeFrom +
                            ( saturationFadeTo - saturationFadeFrom ) *
                                saturationFadeElapsed / saturationFadeDuration;
        saturationFadeCurrent =
            std::clamp( value, std::min( saturationFadeFrom, saturationFadeTo ),
                        std::max( saturationFadeFrom, saturationFadeTo ) );
    }

    saturationCoef = 1.0f - saturationFadeCurrent;
}

void GMIntro::startSpeedBlur( const float target ) {
    // FUN_0076b480
    gpCamera->setUseBlur( true );
    blurPreviousCameraPosition =
        *gpCamera->getActiveCamera()->getWorldPositionRW();
    blurSpeed = 0.0f;
    blurTarget = target;
    blurRate = kSpeedBlurRate;
    bBlurConstant = false;
}

void GMIntro::startConstantBlur( const float target, const float rate ) {
    // FUN_0076bf40
    SetCockpitRadialBlurEnabled( true );

    blurPreviousCameraPosition =
        *gpCamera->getActiveCamera()->getWorldPositionRW();
    blurSpeed = 0.0f;
    blurTarget = target;
    bBlurConstant = true;
    blurRate = rate;
}

void GMIntro::stopRadialBlur() {
    // FUN_0076bec0
    gpCamera->setUseBlur( false );
    SetCockpitRadialBlurEnabled( false );

    blurSpeed = 0.0f;
    blurTarget = 0.0f;
    blurCurrent = 0.0f;

    GetCockpitRadialBlurNode()->setMask(
        gpPostFX->retrieveRadialBlurBitmap( RBB_Mask ) );
}

void GMIntro::tickRadialBlur( const float deltaTime ) {
    // FUN_0076b780
    CameraGame *pCamera = gpCamera->getActiveCamera();
    PostFXNodeRadialBlur *pNode = GetCockpitRadialBlurNode();
    pNode->setMask( gpPostFX->retrieveRadialBlurBitmap( RBB_MaskMap ) );

    const float step = blurRate * deltaTime;
    float value = blurTarget;
    if ( value - blurCurrent > step ) {
        value = blurCurrent + step;
    }
    if ( value - blurCurrent < -step ) {
        value = blurCurrent - step;
    }
    blurCurrent = value;

    if ( blurCurrent == 0.0f ) {
        pNode->setIntensity( 0.0f );
        pNode->setCenter( 0.5f, 0.5f );
        pNode->setMask( gpPostFX->retrieveRadialBlurBitmap( RBB_Mask ) );
        return;
    }

    const Eigen::Vector4f cameraPosition = *pCamera->getWorldPositionRW();
    float speed = blurSpeed;
    if ( deltaTime > 0.0f ) {
        speed =
            ( cameraPosition - blurPreviousCameraPosition ).norm() / deltaTime;
    }

    const float maxStep = deltaTime * kMaxBlurSpeed;
    if ( speed - blurSpeed > maxStep ) {
        speed = blurSpeed + maxStep;
    }
    if ( -maxStep > speed - blurSpeed ) {
        speed = blurSpeed - maxStep;
    }
    blurSpeed = std::clamp( speed, 0.0f, kMaxBlurSpeed );
    blurPreviousCameraPosition = cameraPosition;

    const float factor =
        bBlurConstant
            ? 1.0f
            : std::clamp( blurSpeed * kBlurSpeedScale, 0.0f, 1.0f ) * 4.0f +
                  1.0f;

    const Eigen::Vector3f target = pCamera->getTarget();
    const Eigen::Vector4f viewTarget = TDUMat4x4MulVec(
        pCamera->getWorldToCam(),
        Eigen::Vector4f( target.x(), target.y(), target.z(), 1.0f ) );
    RenderScene *pNearScene =
        gpRender->getRenderPassAtIndex( eRenderPass::RP_Near ).pScene;
    Eigen::Vector4f clip = TDUMat4x4MulVec(
        pNearScene->getFrustumWrite().getProjectionMatrix(), viewTarget );
    clip /= clip.w();

    pNode->setIntensity( factor * blurCurrent );
    pNode->setCenter( ( clip.x() + 1.0f ) * 0.5f, ( 1.0f - clip.y() ) * 0.5f );
}

void GMIntro::setZFarMode( const bool bEnabled,
                           EntityCinematic *pSender,
                           const bool bNoPostCockpit ) {
    // FUN_0076ba80
    if ( pSender != nullptr ) {
        pSender->getCinResCollection().setVeryFarScene( bEnabled );
    }

    mngActor.setRenderingInCockpitPass( bEnabled );
    bCockpitZFar = bEnabled;
    bCockpitZFarNoPostCockpit = bNoPostCockpit;
}

void GMIntro::EnableFakeDOF() {
    // FUN_0076bdf0
    PostFXStepFakeDOF *pFakeDOF = static_cast<PostFXStepFakeDOF *>(
        gpPostFX->getEffect<ePostFXStep::PFXS_FakeDOF>() );
    if ( pFakeDOF == nullptr ) {
        return;
    }

    if ( !gpPostFX->isFXLocked() ) {
        pFakeDOF->getInput()->setEnabled( true );
        UpdateSunSceneBlurFlag();
        SetFakeDOFScenesEnabled( true );
    }
    pFakeDOF->pBlur->setNumPass( 1 );
}

void GMIntro::DisableFakeDOF() {
    // FUN_0076bd60
    PostFXStep *pFakeDOF = gpPostFX->getEffect<ePostFXStep::PFXS_FakeDOF>();
    if ( gpPostFX->isFXLocked() || pFakeDOF == nullptr ) {
        return;
    }

    pFakeDOF->getInput()->setEnabled( false );
    UpdateSunSceneBlurFlag();
    SetFakeDOFScenesEnabled( false );
}

void GMIntro::computeAvatarCameraOffsets() {
    // FUN_0076c130
    Matrix4x4f_t reference = pIntroSc01->getModelMatrix();
    reference.row( 3 ) -= GetStreamOrigin().transpose();

    const Matrix4x4f_t farView = pCamSelectionFar->getModelMatrix() * reference;
    const Matrix4x4f_t closeView =
        pCamSelectionClose->getModelMatrix() * reference;
    const Eigen::Vector4f avatarPosition =
        pChoiceAvatars[0]->getModelMatrix().row( 3 ).transpose();

    const Eigen::Vector4f origin( 0.0f, 0.0f, 0.0f, 1.0f );
    const Eigen::Vector4f forward( 0.0f, 0.0f, 1.0f, 1.0f );
    camFarOffset = TDUMat4x4MulVec( farView, origin ) - avatarPosition;
    camFarTargetOffset = TDUMat4x4MulVec( farView, forward ) - avatarPosition;
    camCloseOffset = TDUMat4x4MulVec( closeView, origin ) - avatarPosition;
    camCloseTargetOffset =
        TDUMat4x4MulVec( closeView, forward ) - avatarPosition;
}

void GMIntro::getAvatarCameraView( const int32_t index,
                                   const bool bCloseUp,
                                   Eigen::Vector4f &outPosition,
                                   Eigen::Vector4f &outTarget ) const {
    // FUN_0076b5b0
    const Eigen::Vector4f avatarPosition =
        pChoiceAvatars[index]->getModelMatrix().row( 3 ).transpose();
    if ( bCloseUp ) {
        outPosition = camCloseOffset + avatarPosition;
        outTarget = camCloseTargetOffset + avatarPosition;
    } else {
        outPosition = camFarOffset + avatarPosition;
        outTarget = camFarTargetOffset + avatarPosition;
    }
}

void GMIntro::startCameraTransition( const Eigen::Vector4f &endPosition,
                                     const Eigen::Vector4f &endTarget,
                                     const float duration ) {
    // FUN_0076c640
    camTransitionElapsed = 0.0f;
    camTransitionDuration = duration;

    CameraGame *pCamera = gpCamera->getActiveCamera();
    const Eigen::Vector4f origin = GetStreamOrigin();
    camTransitionStartPosition = *pCamera->getWorldPositionRW() - origin;
    const Eigen::Vector3f target = pCamera->getTarget();
    camTransitionStartTarget =
        Eigen::Vector4f( target.x(), target.y(), target.z(), 1.0f ) - origin;
    camTransitionEndPosition = endPosition;
    camTransitionEndTarget = endTarget;

    if ( duration == 0.0f ) {
        tickCameraTransition( 0.0f );
    }
}

void GMIntro::tickCameraTransition( const float deltaTime ) {
    // FUN_0076c510
    if ( !isCameraTransitionRunning() && deltaTime != 0.0f ) {
        return;
    }

    camTransitionElapsed += deltaTime;
    CameraGame *pCamera =
        gpCamera->findCameraWithMode( 0, gpCamera->getBoundCarState() );
    if ( camTransitionElapsed >= camTransitionDuration ) {
        camTransitionDuration = 0.0f;
        camTransitionElapsed = 0.0f;
        pCamera->setPositionOffseted( camTransitionEndPosition );
        pCamera->setTargetOffseted( camTransitionEndTarget );
        return;
    }

    const float t =
        ( cosf( ( camTransitionElapsed / camTransitionDuration + 1.0f ) *
                static_cast<float>( M_PI ) ) +
          1.0f ) *
        0.5f;
    pCamera->setPositionOffseted( LerpClamped( camTransitionStartPosition,
                                               camTransitionEndPosition, t ) );
    pCamera->setTargetOffseted(
        LerpClamped( camTransitionStartTarget, camTransitionEndTarget, t ) );
}

void GMIntro::switchToFarView() {
    // FUN_0076c7f0
    Eigen::Vector4f position;
    Eigen::Vector4f target;
    getAvatarCameraView( selectedAvatar, false, position, target );
    startCameraTransition( position, target, kFarViewTransitionTime );
    bCloseUpView = false;
    carouselDirection = 0.0f;
}

void GMIntro::switchToCloseUpView() {
    // FUN_0076c890
    Eigen::Vector4f position;
    Eigen::Vector4f target;
    getAvatarCameraView( selectedAvatar, true, position, target );
    startCameraTransition( position, target, kCloseUpTransitionTime );
    bCloseUpView = true;
    carouselDirection = 0.0f;
}

void GMIntro::selectNextAvatar() {
    // FUN_0076c930
    if ( selectedAvatar >= kNumChoiceAvatars - 1 ) {
        return;
    }

    Eigen::Vector4f position;
    Eigen::Vector4f target;
    getAvatarCameraView( selectedAvatar + 1, bCloseUpView, position, target );
    startCameraTransition( position, target, kAvatarCarouselStep );
    selectedAvatar++;
    carouselDirection = -1.0f;
}

void GMIntro::selectPreviousAvatar() {
    // FUN_0076c9a0
    if ( selectedAvatar <= 0 ) {
        return;
    }

    Eigen::Vector4f position;
    Eigen::Vector4f target;
    getAvatarCameraView( selectedAvatar - 1, bCloseUpView, position, target );
    startCameraTransition( position, target, kAvatarCarouselStep );
    selectedAvatar--;
    carouselDirection = 1.0f;
}

void GMIntro::confirmAvatar() {
    // FUN_0076c790
    SavedAvatar *pSavedAvatar = gpPlayerData->getLoadedSave()->getAvatar();
    gpAvatar->readAvatarConfig( pChoiceAvatars[selectedAvatar],
                                pSavedAvatar->Config );

    gpAvatar->unlockSavedAvatarClothes();

    delayedState = 3;
    delayedStateCountdown = 2;
    if ( gpMngFade != nullptr ) {
        gpMngFade->setRenderLogo( true );
    }
}

void GMIntro::tickAvatarChoice( const float deltaTime ) {
    // FUN_0076ca10
    float time = static_cast<float>( selectedAvatar ) * kAvatarCarouselStep;
    if ( camTransitionDuration != 0.0f ) {
        time += ( 1.0f - camTransitionElapsed / camTransitionDuration ) *
                carouselDirection * kAvatarCarouselStep;
    }
    carouselTime = time;
    pAvatarChoiceCine->setTime( carouselTime );

    tickCameraTransition( deltaTime );

    CameraGame *pCamera = gpCamera->getActiveCamera();
    pCamera->setNearPlane( kAvatarChoiceNearPlane );
    pCamera->setFarPlane( gpRender->getDefaultFarPlane() );

    pFlashPlayer->setVariableValue( "/:IsFlashButtonOn",
                                    !isCameraTransitionRunning() );
}

void GMIntro::SpawnDriverAvatar( const bool bKeepReference,
                                 const bool bAsync ) {
    // FUN_0076d130
    AvatarManager &avatarManager = gpScenaric->getAvatarManager();
    if ( avatarManager.getDriverAvatar() != nullptr ) {
        return;
    }

    EntityAvatar *pDriver = avatarManager.allocateAvatar( ~0ull, 1 );
    if ( pDriver == nullptr ) {
        return;
    }

    InstallAvatarConfiguration(
        pDriver, TestDrive::GetSavedAvatarConfiguration(), bAsync );
    if ( !bKeepReference ) {
        pDriver->removeInstanceReference();
    }
}

void GMIntro::ReleaseDriverFromCar() {
    // FUN_0076cb60
    EntityAvatar *pAvatar = gpScenaric->getAvatarManager().getDriverAvatar();
    if ( pAvatar == nullptr ) {
        return;
    }

    EntityDriver *pDriver = pAvatar->getDriver();
    if ( pDriver != nullptr && pDriver->getCarState() != nullptr ) {
        pDriver->getCarState()->unloadDriver();
    }
}
