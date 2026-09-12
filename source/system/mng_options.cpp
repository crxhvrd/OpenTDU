#include "shared.h"
#include "mng_options.h"

#include "audio/gs_audio.h"
#include "core/format.h"
#include "database/gs_database.h"
#include "flash/gs_flash.h"
#include "flash/implementation/flash_player.h"
#include "flash/mng_flash_localize.h"
#include "game/mng_hud.h"
#include "gs_timer.h"
#include "input/gs_device.h"
#include "player_data/gs_playerdata.h"
#include "render/camera/gs_camera.h"
#include "render/ed_render.h"
#include "render/gs_render.h"
#include "system/ed_sys.h"

#include "tdu_instance.h"

static constexpr const char *kSpeakerModes[2] = {
    "stereo", // DAT_00f3d784
    "5.1"     // DAT_00f3d780
};

static constexpr uint64_t kDrivingHelpLabels[4] = {
    0x03326AE2ull, 0x0341AD22ull, 0x0350EF62ull,
    0x03B7EE92ull }; // DAT_00f9c1c0

static constexpr uint64_t kDrivingHelpDescriptions[4] = {
    0x005558F6ull, 0x0056DF96ull, 0x00586636ull, 0x0059ECD6ull };

static constexpr uint64_t kGearboxLabels[2] = { 0x035227E2ull,
                                                0x033996D2ull }; // DAT_00f9c1f8

static constexpr uint64_t kLODQualityLabels[3] = {
    0x036BA352ull, 0x037AE592ull, 0x03C58322ull }; //  DAT_00f3d098

static constexpr uint64_t kAntiAliasingOffLabel = 0x03A8AC52ull;

static constexpr int32_t kBDDMenus = 0;
static constexpr int32_t kBDDTutorial = 11;

MngOptions *gpMngOptions = nullptr; // DAT_010e7ffc

MngOptions::MngOptions()
    : Manager(),
      pPlayer( nullptr ),
      videoModes(),
      videoModeIndex( 0 ),
      refreshRateIndex( 0 ),
      bVideoModesCached( false ),
      gearboxMode( 0 ),
      driveAssists( 0 ),
      audioLanguage( 0 ),
      savegameStep( 0 ),
      deviceSelected( 0 ),
      cameraBeforeSeatSetup( -1 ),
      activeLocale( eLocale::L_Unknown ),
      speakerMode( 0 ),
      unitType( 0 ),
      languageRefreshStep( 0 ),
      lodQuality( 0 ),
      displayMode( eDisplayMode::DM_Windowed ),
      videoWidth( 0 ),
      videoHeight( 0 ),
      refreshRate( 0 ),
      antiAliasingMode( eAntiAliasingMethod::AAM_Disabled ),
      pAudioOptions( nullptr ),
      field30_0x80c( false ),
      bHDREnabled( false ),
      bHDRSupported( false ) {
    // FUN_006c5f20
    memset( pLocalizedStrings, 0, sizeof( pLocalizedStrings ) );

    gpMngOptions = this;
}

MngOptions::~MngOptions() {
    // FUN_006c5e70
    gpMngOptions = nullptr;
}

bool MngOptions::initialize( TestDriveGameInstance * ) {
    // FUN_006c6810
    retrieveLocalizedStrings();
    savegameStep = 0;

    padSensitivitySlider.setAutoVelocity( true );
    padSensitivitySlider.setRange( 0.0f, 1.0f );

    padLinearitySlider.setAutoVelocity( true );
    padLinearitySlider.setRange( 0.0f, 1.0f );

    barSensitivitySlider.setAutoVelocity( true );
    barSensitivitySlider.setRange( 0.0f, 1.0f );

    mouseSensitivitySlider.setAutoVelocity( true );
    mouseSensitivitySlider.setRange( 0.0f, 1.0f );

    quiverSensitivitySlider.setAutoVelocity( true );
    quiverSensitivitySlider.setRange( 0.0f, 1.0f );

    seatPositionSlider.setAutoVelocity( true );
    seatPositionSlider.setRange( -1.0f, 1.0f );

    seatHeightSlider.setAutoVelocity( true );
    seatHeightSlider.setRange( -1.0f, 1.0f );

    gammaSlider.setAutoVelocity( true );
    gammaSlider.setRange( 0.0f, 1.0f );

    cameraShakeSlider.setAutoVelocity( true );
    cameraShakeSlider.setRange( 0.0f, 1.0f );

    radialBlurSlider.setAutoVelocity( true );
    radialBlurSlider.setRange( 0.0f, 1.0f );

    voicesVolumeSlider.setAutoVelocity( true );
    voicesVolumeSlider.setRange( 0.0f, 1.0f );

    voicesThresholdSlider.setAutoVelocity( true );
    voicesThresholdSlider.setRange( 0.0f, 1.0f );

    voicesInputSlider.setAutoVelocity( true );
    voicesInputSlider.setRange( 0.0f, 1.0f );

    pAudioOptions =
        new ( TestDrive::Alloc( sizeof( AudioOptions ) ) ) AudioOptions();

    return true;
}

void MngOptions::tick( float, float ) {
    // FUN_006c98b0
    const float deltaTime = gpTimer->SystemDeltaTime;

    deviceSaveTick();

    if ( padLinearitySlider.hasFocus() ) {
        padLinearitySlider.tick( deltaTime );
    }
    if ( padSensitivitySlider.hasFocus() ) {
        padSensitivitySlider.tick( deltaTime );
    }
    if ( barSensitivitySlider.hasFocus() ) {
        barSensitivitySlider.tick( deltaTime );
    }
    if ( mouseSensitivitySlider.hasFocus() ) {
        mouseSensitivitySlider.tick( deltaTime );
    }
    if ( quiverSensitivitySlider.hasFocus() ) {
        quiverSensitivitySlider.tick( deltaTime );
    }
    if ( cameraShakeSlider.hasFocus() ) {
        cameraShakeSlider.tick( deltaTime );
    }
    if ( radialBlurSlider.hasFocus() ) {
        radialBlurSlider.tick( deltaTime );
    }
    if ( seatPositionSlider.hasFocus() ) {
        seatPositionSlider.tick( deltaTime );
        gpCamera->setSeatPosition( seatHeightSlider.getValue(),
                                   seatPositionSlider.getValue() );
    }
    if ( seatHeightSlider.hasFocus() ) {
        seatHeightSlider.tick( deltaTime );
        gpCamera->setSeatPosition( seatHeightSlider.getValue(),
                                   seatPositionSlider.getValue() );
    }
    if ( gammaSlider.hasFocus() ) {
        gammaSlider.tick( deltaTime );
        gpRender->setGamma( gammaSlider.getValue() );
    }

    pAudioOptions->tick();

    if ( voicesVolumeSlider.hasFocus() ) {
        voicesVolumeSlider.tick( deltaTime );
    }
    if ( voicesThresholdSlider.hasFocus() ) {
        voicesThresholdSlider.tick( deltaTime );
    }
    if ( voicesInputSlider.hasFocus() ) {
        voicesInputSlider.tick( deltaTime );
    }

    onLanguageChanged();
}

void MngOptions::terminate() {
    // FUN_006c6090
    if ( pAudioOptions != nullptr ) {
        pAudioOptions->~AudioOptions();
        TestDrive::Free( pAudioOptions );
    }
    pAudioOptions = nullptr;
}

bool MngOptions::onMessage( FlashMessage *pMessage,
                            FlashPlayer *pInvokingPlayer ) {
    // FUN_006c81f0
    if ( pPlayer == nullptr ) {
        return false;
    }

    const char *pTag = pMessage->pAsChar;
    const uint32_t tag = ( (uint32_t)(uint8_t)pTag[0] << 24 ) |
                         ( (uint32_t)(uint8_t)pTag[1] << 16 ) |
                         ( (uint32_t)(uint8_t)pTag[2] << 8 ) |
                         (uint32_t)(uint8_t)pTag[3];

    switch ( tag ) {
    case 0x70726672: // "prfr" — previous refresh rate
        showPreviousRefreshRate();
        return false;

    case 0x6d757465: { // "mute"
        char acGauge[32] = {};
        pPlayer->getVariableAsString( "/:WhatGauge", acGauge );
        pAudioOptions->setMuteGauge( acGauge );
        return false;
    }

    case 0x636f7365: // "cose"
    case 0x6c62636c: // "lbcl"
    case 0x6576636c: // "evcl"
        disableAllSliders();
        return true;

    case 0x63686175: { // "chau" — apply speaker setup
        char acSpeakers[32] = {};
        if ( pPlayer->getVariableAsString( "/:Speakers", acSpeakers ) ) {
            if ( StringCmpI( acSpeakers, kSpeakerModes[0] ) == 0 ) {
                gpAudio->NbSpeakers = 2;
            } else if ( StringCmpI( acSpeakers, kSpeakerModes[1] ) == 0 ||
                        StringCmpI( acSpeakers, "5,1" ) == 0 ) {
                gpAudio->NbSpeakers = 5;
            }
        }
        pAudioOptions->applySpeakerMode();
        pAudioOptions->deactivate();
        return true;
    }

    case 0x6175696e: // "auin"
        pAudioOptions->resetActiveControl();
        FUN_006c67b0();
        return true;

    case 0x62617273: // "bars"
        disableAllSliders();
        barSensitivitySlider.setFocusState( true );
        return true;

    case 0x626b6f70: // "bkop" — back to the options screen
        pInvokingPlayer->setVariableValue( "/:NoRedirection", int32_t( 0 ) );
        pAudioOptions->deactivate();
        refreshOptionsScreen();
        gpCamera->setSeatPosition( gpPlayerData->getSeatPositionHeight(),
                                   gpPlayerData->getSeatPositionPosition() );
        gpRender->setGamma( gpPlayerData->getGamma() );
        gammaSlider.setFocusState( false );
        if ( field30_0x80c ) {
            gpRender->setLODQuality( lodQuality );
        }
        FUN_006c59d0();
        return true;

    case 0x63686468: // "chdh"
        return true;

    case 0x63686c61: // "chla" — apply audio language
        FUN_006c6730( audioLanguage );
        OTDU_UNIMPLEMENTED; // TODO: persist /:IsSubtitles to the save profile
                            // (not ported).
        return true;

    case 0x63686f70: // "chop" — commit gameplay options
        FUN_006c7fb0();
        OTDU_UNIMPLEMENTED_ONCE; // TODO: PLAYERSAVE
        FUN_006c6db0();
        return true;

    case 0x67616d73: // "gams"
        disableAllSliders();
        gammaSlider.setFocusState( true );
        return true;

    case 0x6469696e: // "diin" — GPS option
        flashGPSOption();
        return true;

    case 0x656e746f: // "ento"
        FUN_006c61b0();
        return false;

    case 0x6864636c: // "hdcl"
        FUN_006c6a00();
        return false;

    case 0x6d6f7573: // "mous"
        disableAllSliders();
        mouseSensitivitySlider.setFocusState( true );
        return true;

    case 0x6e657273: // "ners" — next resolution
        showVideoOptions();
        return false;

    case 0x6e656672: // "nefr" — next refresh rate
        showRefreshRate();
        return false;

    case 0x6e656161: // "neaa" — next anti-aliasing mode
        showAntiAliasing();
        return false;

    case 0x6e656175: // "neau" — next speaker mode
        speakerMode++;
        if ( speakerMode > 1 ) {
            speakerMode = 0;
        }
        pPlayer->setVariableValue( "/:Speakers", kSpeakerModes[speakerMode] );
        return true;

    case 0x6e656468: // "nedh" — next drive assist
        driveAssists++;
        if ( driveAssists > 3 ) {
            driveAssists = 0;
        }
        FUN_006c7950( true );
        return true;

    case 0x6e656765: // "nege" — next gearbox mode
        gearboxMode++;
        FUN_006c7b80();
        return true;

    case 0x6e65646d: // "nedm" — next display mode
        showNextDisplayMode();
        return false;

    case 0x6e656c67: // "nelg" — next language
        audioLanguage++;
        if ( audioLanguage > 4 ) {
            audioLanguage = 0;
        }
        showLanguageLabel();
        return true;

    case 0x6e656c6f: // "nelo"
        FUN_006c7190();
        return false;

    case 0x70726161: // "praa" — previous anti-aliasing
        showPreviousAntiAliasing();
        return false;

    case 0x6e6f6761: // "noga" — leave the seat-setup view
        if ( seatPositionSlider.hasFocus() || seatHeightSlider.hasFocus() ) {
            if ( cameraBeforeSeatSetup != -1 ) {
                OTDU_UNIMPLEMENTED; // TODO: CAMERA
            }
            cameraBeforeSeatSetup = -1;
        }
        disableAllSliders();
        return true;

    case 0x6f6c696e: { // "olin" — online options
        static constexpr const char *kSurNames[kNumLocalizedStrings] = {
            "menus/ss_menus/choice/sur1/:sm_name",
            "menus/ss_menus/choice/sur2/:sm_name",
            "menus/ss_menus/choice/sur3/:sm_name",
            "menus/ss_menus/choice/sur4/:sm_name",
            "menus/ss_menus/choice/sur5/:sm_name",
            "menus/ss_menus/choice/sur6/:sm_name",
            "menus/ss_menus/choice/sur7/:sm_name",
            "menus/ss_menus/choice/sur8/:sm_name",
            "menus/ss_menus/choice/sur9/:sm_name" };
        static constexpr const char *kOnlineMenuNames[kNumLocalizedStrings] = {
            "/:OnlineOp_m_1", "/:OnlineOp_m_2", "/:OnlineOp_m_3",
            "/:OnlineOp_m_4", "/:OnlineOp_m_5", "/:OnlineOp_m_6",
            "/:OnlineOp_m_7", "/:OnlineOp_m_8", "/:OnlineOp_m_9" };
        for ( size_t i = 0; i < kNumLocalizedStrings; i++ ) {
            pPlayer->setVariableValue( kSurNames[i], pLocalizedStrings[i] );
        }
        for ( size_t i = 0; i < kNumLocalizedStrings; i++ ) {
            pPlayer->setVariableValue( kOnlineMenuNames[i],
                                       pLocalizedStrings[i] );
        }
        FUN_006c6650();
        return true;
    }

    case 0x70616473: // "pads"
        disableAllSliders();
        padSensitivitySlider.setFocusState( true );
        return true;

    case 0x70726175: // "prau" — previous speaker mode
        speakerMode--;
        if ( speakerMode < 0 ) {
            speakerMode = 1;
        }
        pPlayer->setVariableValue( "/:Speakers", kSpeakerModes[speakerMode] );
        return true;

    case 0x70726468: // "prdh" — previous drive assist
        driveAssists--;
        if ( driveAssists < 0 ) {
            driveAssists = 3;
        }
        FUN_006c7950( true );
        return true;

    case 0x70727273: // "prrs" — previous resolution
        showPreviousVideoOptions();
        return false;

    case 0x70726765: // "prge" — previous gearbox mode
        gearboxMode--;
        FUN_006c7b80();
        return true;

    case 0x7072646d: // "prdm" — previous display mode
        showPreviousDisplayMode();
        return false;

    case 0x70726c67: // "prlg" — previous language
        audioLanguage--;
        if ( audioLanguage < 0 ) {
            audioLanguage = 4;
        }
        showLanguageLabel();
        return true;

    case 0x70726c6f: // "prlo"
        FUN_006c71d0();
        return false;

    case 0x71756976: // "quiv"
        disableAllSliders();
        quiverSensitivitySlider.setFocusState( true );
        return true;

    case 0x7261626c: // "rabl"
        disableAllSliders();
        radialBlurSlider.setFocusState( true );
        return true;

    case 0x7365706f: // "sepo" — seat position setup
        enterSeatSetupCamera();
        disableAllSliders();
        seatPositionSlider.setFocusState( true );
        return true;

    case 0x73656865: // "sehe" — seat height setup
        enterSeatSetupCamera();
        disableAllSliders();
        seatHeightSlider.setFocusState( true );
        return true;

    case 0x73686361: // "shca"
        disableAllSliders();
        cameraShakeSlider.setFocusState( true );
        return true;

    case 0x73796170: // "syap" — apply the video settings
        applySystemParameters();
        return false;

    case 0x7379696e: // "syin" — enter the video settings screen
        buildVideoModeList();
        return false;

    case 0x73746465: // "stde" — save to device
        requestSaveToDevice( false );
        return false;

    case 0x73746476: // "stdv" — save to device (overwrite)
        requestSaveToDevice( true );
        return false;

    case 0x75736d65:   // "usme"
    case 0x7573696d: { // "usim" — toggle unit system
        unitType = ( unitType != 1 ) ? 1 : 0;
        const uint64_t labelHash = ( unitType != 0 ) ? 0x368c7f2 : 0x35905b2;
        const char *pLabel = gpDatabase->getStringByHashcode( 0, labelHash );
        pPlayer->setVariableValue( "/:UnitSystem", pLabel );
        pPlayer->setVariableValue( "/:NameTemp", pLabel );
        return true;
    }

    case 0x766f7472: // "votr"
        disableAllSliders();
        voicesThresholdSlider.setFocusState( true );
        return true;

    case 0x76707669: // "vpvi"
        disableAllSliders();
        voicesInputSlider.setFocusState( true );
        return true;

    case 0x7670766f: // "vpvo"
        disableAllSliders();
        voicesVolumeSlider.setFocusState( true );
        return true;

    case 0x77686c69: // "whli"
        disableAllSliders();
        padLinearitySlider.setFocusState( true );
        return true;

    case 0x766f696e: // "voin"
        return true;

    case 0x766f6369: // "voci"
        pAudioOptions->selectVolumeControl( "/:vol_Cinematic" );
        return true;
    case 0x766f636f: // "voco"
        pAudioOptions->selectVolumeControl( "/:vol_Collisions" );
        return true;
    case 0x766f656e: // "voen"
        pAudioOptions->selectVolumeControl( "/:vol_Engine" );
        return true;
    case 0x766f6d61: // "voma"
        pAudioOptions->selectVolumeControl( "/:vol_Master" );
        return true;
    case 0x766f6d75: // "vomu"
        pAudioOptions->selectVolumeControl( "/:vol_Music" );
        return true;
    case 0x766f7366: // "vosf"
        pAudioOptions->selectVolumeControl( "/:vol_GeneralSFX" );
        return true;
    case 0x766f7569: // "voui"
        pAudioOptions->selectVolumeControl( "/:vol_UI" );
        return true;
    case 0x766f7667: // "vovg"
        pAudioOptions->selectVolumeControl( "/:vol_VoicesGPS" );
        return true;
    case 0x766f766f: // "vovo"
        pAudioOptions->selectVolumeControl( "/:vol_Voices" );
        return true;

    default:
        return false;
    }
}

void MngOptions::onClose( ePopupAnswer answer, void *, int32_t param_3 ) {
    // FUN_006c6eb0
    if ( answer != ePopupAnswer::PA_Accept || param_3 != 1 ) {
        return;
    }

    OTDU_UNIMPLEMENTED;
}

void MngOptions::FUN_006c5c10() {
    // FUN_006c5c10
    FUN_006c59d0();
    pPlayer->showFrame( "open_system", "/menus/ss_menus/choice", true );
}

void MngOptions::FUN_006c59d0() {
    // FUN_006c59d0
    videoModes.clear();
    videoModeIndex = 0;
    refreshRateIndex = 0;
    bVideoModesCached = false;

    field30_0x80c = false;
}

void MngOptions::FUN_006c60c0( int32_t param_1 ) {
    // FUN_006c60c0
    uint32_t local_8 = 0u;
    uint32_t local_4 = 0u;
    FlashPlayer *peVar3 = nullptr;
    if ( gpFlash->getFlashMovieIndex( &local_8, &local_4, "GENERAL" ) ) {
        MoviePlayer *pMVar2 = gpFlash->getMoviePlayer( local_8, local_4 );
        if ( pMVar2 != nullptr && pMVar2->MovieBank.isLoaded() ) {
            peVar3 = pMVar2->MovieBank.getFlashPlayer();
        }
    }

    // NOTE: the original passes the integer 0 here, not a null string.
    if ( peVar3 != nullptr ) {
        peVar3->setVariableValue( "/:IsAvertTimeOut", int32_t( 0 ) );
    }
    FUN_006c59d0();

    if ( param_1 == 0 ) {
        gpPlayerData->setResolution( gWindowWidth, gWindowHeight );
        gpPlayerData->setAntiAliasing( gpRender->getAAMethod() );
        gpPlayerData->setHDREnabled( gpRender->isHDREnabled() );
        gpPlayerData->setRefreshRate( gPickedRefreshRate );

        pPlayer->showFrame( "valid_apply", "/menus/ss_menus/choice", true );
    } else {
        FUN_006c6030();
        pPlayer->showFrame( "open_system", "/menus/ss_menus/choice", true );
    }
}

void MngOptions::FUN_006c6030() {
    // FUN_006c6030
    do {
        gEdSys.pollEvents();
        gEdRender.flushDrawCommands( false );
    } while ( gEdSys.bWindowActivated == false );
    gEdRender.flushDrawCommands( false );
    gpRender->resize( videoWidth, videoHeight, refreshRate, antiAliasingMode,
                      bHDREnabled );
}

void MngOptions::FUN_006c67b0() {
    // FUN_006c67b0
    if ( pPlayer != nullptr ) {
        speakerMode = 0;

        if ( gpAudio->NbSpeakers == 2 ) {
            speakerMode = 0;
        } else if ( gpAudio->NbSpeakers == 5 ) {
            speakerMode = 1;
        }

        pPlayer->setVariableValue( "/:Speakers", kSpeakerModes[speakerMode] );
    }
}

void MngOptions::showPreviousAntiAliasing() {
    // FUN_006c6520
    eAntiAliasingMethod method = antiAliasingMode;

    for ( int32_t attempt = 3; attempt > 0; attempt-- ) {
        method = eAntiAliasingMethod( int32_t( method ) - 1 );
        if ( int32_t( method ) < 0 ) {
            method = eAntiAliasingMethod::AAM_MSAA_X4;
        }

        if ( isAntiAliasingSupported( method ) ) {
            antiAliasingMode = method;
            showAntiAliasingLabel();
            return;
        }
    }
}

void MngOptions::disableAllSliders() {
    // FUN_006c5c40
    quiverSensitivitySlider.disable();
    padLinearitySlider.disable();
    padSensitivitySlider.disable();
    barSensitivitySlider.disable();
    mouseSensitivitySlider.disable();
    seatHeightSlider.disable();
    seatPositionSlider.disable();
    gammaSlider.disable();
    cameraShakeSlider.disable();
    radialBlurSlider.disable();
    voicesVolumeSlider.disable();
    voicesThresholdSlider.disable();
    voicesInputSlider.disable();
}

void MngOptions::FUN_006c6650() {
    // FUN_006c6650
    static constexpr int32_t kNumOnlineOps = 9;
    static constexpr const char *kOnlineOps[kNumOnlineOps] = {
        "/:OnlineOp_f_1", "/:OnlineOp_f_2", "/:OnlineOp_f_3",
        "/:OnlineOp_f_4", "/:OnlineOp_f_5", "/:OnlineOp_f_6",
        "/:OnlineOp_f_7", "/:OnlineOp_f_8", "/:OnlineOp_f_9" };

    for ( int32_t i = 0; i < kNumOnlineOps; i++ ) {
        pPlayer->setVariableValue( kOnlineOps[i],
                                   gpPlayerData->getOnlineOp( i ) );
    }
}

void MngOptions::retrieveLocalizedStrings() {
    // FUN_006c5aa0
    static constexpr uint64_t kStringHashcodes[kNumLocalizedStrings] = {
        0x3a21ca2, 0x3b15ee2, 0x3c0a122, 0x3b7a45a, 0x34815c2,
        0x3575802, 0x3669a42, 0x375dc82, 0x3851ec2 };

    for ( size_t i = 0; i < kNumLocalizedStrings; i++ ) {
        const char *pcVar1 =
            gpDatabase->getStringByHashcode( 0, kStringHashcodes[i] );

        pLocalizedStrings[i][0] = '\0';
        if ( pcVar1 == nullptr ) {
            continue;
        }

        strncpy( pLocalizedStrings[i], pcVar1, kLocalizedStringLength - 1 );
        pLocalizedStrings[i][kLocalizedStringLength - 1] = '\0';
    }
}

void MngOptions::deviceSaveTick() {
    // FUN_006c98b0
    if ( deviceSelected == 0 ) {
        return;
    }
    OTDU_UNIMPLEMENTED;
}

void MngOptions::onLanguageChanged() {
    // FUN_006c9390
    if ( languageRefreshStep == 0 ) {
        return;
    }
    OTDU_UNIMPLEMENTED;
}

void MngOptions::showPreviousRefreshRate() {
    // FUN_006c6610
    if ( videoModes.empty() ) {
        return;
    }

    const VideoResolutionOption &resolution = videoModes[videoModeIndex];
    if ( resolution.RefreshRates.empty() ) {
        return;
    }

    const int32_t numRefreshRates = int32_t( resolution.RefreshRates.size() );
    refreshRateIndex =
        ( refreshRateIndex + numRefreshRates - 1 ) % numRefreshRates;

    showRefreshRateLabel( false );
}

void MngOptions::FUN_006c7b80() {
    // FUN_006c7b80
    if ( gearboxMode < 0 ) {
        gearboxMode = 1;
    }
    if ( gearboxMode > 1 ) {
        gearboxMode = 0;
    }

    if ( gpMngFlashLocalize != nullptr ) {
        gpMngFlashLocalize->setVarLocalized( pPlayer, "/:WhatGear",
                                             kGearboxLabels[gearboxMode] );
        gpMngFlashLocalize->setVarLocalized( pPlayer, "/:NameTemp",
                                             kGearboxLabels[gearboxMode] );
    }

    // The clutch and the H-shifter are only offered in manual.
    const bool bManual = ( gearboxMode == 1 );
    pPlayer->setVariableValue( "/:IsAutoClutchAvailable", int32_t( bManual ) );
    pPlayer->setVariableValue( "/:IsHShifterAvailable", int32_t( bManual ) );

    pPlayer->showFrame( "update", "keymap/list/test", true );
}

void MngOptions::FUN_006c7950( bool bUpdateNameTemp ) {
    // FUN_006c7950
    if ( gpMngFlashLocalize == nullptr || pPlayer == nullptr ) {
        return;
    }

    const uint64_t labelHashcode = kDrivingHelpLabels[driveAssists];

    gpMngFlashLocalize->setVarLocalized( pPlayer, "/:DrivingHelp",
                                         labelHashcode );
    if ( bUpdateNameTemp ) {
        gpMngFlashLocalize->setVarLocalized( pPlayer, "/:NameTemp",
                                             labelHashcode );
    }

    gpMngFlashLocalize->setSysVar(
        "DRV_CUR", gpDatabase->getStringByHashcode( 0, labelHashcode ) );

    gpMngFlashLocalize->setDatabaseContext( kBDDTutorial );
    gpMngFlashLocalize->setVarLocalized(
        pPlayer, "/:Dv_help_info", kDrivingHelpDescriptions[driveAssists] );
    gpMngFlashLocalize->setDatabaseContext( kBDDMenus );

    gpMngFlashLocalize->clearAllSysVar();
}

void MngOptions::FUN_006c6730( int32_t ) {
    // FUN_006c6730
    OTDU_UNIMPLEMENTED;
}

void MngOptions::FUN_006c7fb0() {
    // FUN_006c7fb0
    LocalPlayerData &localPlayerData = gpPlayerData->getLocalPlayerData();

    for ( int32_t i = 0; i < 9; i++ ) {
        char pVariableName[32] = {};
        snprintf( pVariableName, sizeof( pVariableName ), "/:OnlineOp_f_%d",
                  i + 1 );

        const bool bEnabled = pPlayer->getVariableAsInt( pVariableName ) != 0;
        if ( localPlayerData.bOnlineOps[i] == bEnabled ) {
            continue;
        }

        localPlayerData.bOnlineOps[i] = bEnabled;

        if ( i == 0 && gpTestDriveInstance != nullptr &&
             gpTestDriveInstance->getActiveGameMode() == GM_FreeRide ) {
            OTDU_UNIMPLEMENTED_ONCE; // TODO: ONLINE (CP2PFreeRideMgr)
        }
    }
}

void MngOptions::FUN_006c6db0() {
    // FUN_006c6db0
    LocalPlayerData &localPlayerData = gpPlayerData->getLocalPlayerData();

    localPlayerData.bDispOP1 = pPlayer->getVariableAsInt( "/:DispOp_f_1" ) != 0;
    localPlayerData.bDisplayGPS =
        pPlayer->getVariableAsInt( "/:DispOp_f_2" ) != 0;

    // FUN_008986b0
    localPlayerData.bHelpSystem =
        pPlayer->getVariableAsInt( "/:DispOp_f_3" ) != 0;
    OTDU_UNIMPLEMENTED_ONCE; // TODO: PLAYERSAVE

    localPlayerData.bDisplayPlayerNames =
        pPlayer->getVariableAsInt( "/:DispOp_f_4" ) != 0;
    localPlayerData.bGPSRotation =
        pPlayer->getVariableAsInt( "/:GPSRotation" ) != 0;

    localPlayerData.Gamma = gammaSlider.getValue();
    gammaSlider.setFocusState( false );

    gpPlayerData->setUnitSystem( unitType != US_Metric );

    if ( gpMngHud != nullptr ) {
        gpMngHud->setGPSRotation( localPlayerData.bGPSRotation );
    }
}

void MngOptions::flashGPSOption() {
    // FUN_006c7a90
    if ( pPlayer == nullptr || gpMngFlashLocalize == nullptr ) {
        return;
    }

    const LocalPlayerData &localPlayerData = gpPlayerData->getLocalPlayerData();

    pPlayer->setVariableValue( "/:DispOp_f_1",
                               int32_t( localPlayerData.bDispOP1 ) );
    pPlayer->setVariableValue( "/:DispOp_f_2",
                               int32_t( localPlayerData.bDisplayGPS ) );
    pPlayer->setVariableValue( "/:DispOp_f_3",
                               int32_t( localPlayerData.bHelpSystem ) );
    pPlayer->setVariableValue( "/:DispOp_f_4",
                               int32_t( localPlayerData.bDisplayPlayerNames ) );
    pPlayer->setVariableValue( "/:GPSRotation",
                               int32_t( localPlayerData.bGPSRotation ) );

    unitType = localPlayerData.UnitSystem;

    const uint64_t labelHash =
        ( localPlayerData.UnitSystem != US_Metric ) ? 0x368c7f2 : 0x35905b2;
    gpMngFlashLocalize->setVarLocalized( pPlayer, "/:UnitSystem", labelHash );
}

void MngOptions::bindFlashPlayer( FlashPlayer *pFlashPlayer,
                                  int32_t orientation ) {
    // FUN_006c6ac0
    pPlayer = pFlashPlayer;

    padSensitivitySlider.bind( pPlayer, "/:PadSensitivity", orientation,
                               gpPlayerData->getPadSensitivity() );
    padLinearitySlider.bind( pPlayer, "/:Linearity", orientation,
                             gpPlayerData->getPadLinearity() );
    barSensitivitySlider.bind( pPlayer, "/:BarSensitivity", orientation,
                               gpPlayerData->getBarSensitivity() );
    mouseSensitivitySlider.bind( pPlayer, "/:MouseSensitivity", orientation,
                                 gpPlayerData->getMouseSensitivity() );
    quiverSensitivitySlider.bind( pPlayer, "/:QuiverSensitivity", orientation,
                                  gpPlayerData->getQuiverSensitivity() );
    seatPositionSlider.bind( pPlayer, "/:seat_Position", orientation,
                             gpPlayerData->getSeatPositionPosition() );
    seatHeightSlider.bind( pPlayer, "/:seat_Height", orientation,
                           gpPlayerData->getSeatPositionHeight() );
    gammaSlider.bind( pPlayer, "/:gamma", orientation,
                      gpPlayerData->getGamma() );
    cameraShakeSlider.bind( pPlayer, "/:shake_camera", orientation,
                            gpPlayerData->getCameraShake() );
    radialBlurSlider.bind( pPlayer, "/:radial_blur", orientation,
                           gpPlayerData->getRadialBlurIntensity() );

    OTDU_UNIMPLEMENTED; // TODO: AUDIO

    voicesThresholdSlider.bind( pPlayer, "/:voices_treshold", orientation,
                                gpPlayerData->getVoicesThreshold() );
    voicesInputSlider.bind( pPlayer, "/:voices_input", orientation,
                            gpPlayerData->getVoicesInput() );
    voicesVolumeSlider.bind( pPlayer, "/:voices_volume", orientation,
                             gpPlayerData->getVoicesVolume() );
}

void MngOptions::refreshOptionsScreen() {
    // FUN_006c7c60
    if ( pPlayer == nullptr ) {
        return;
    }

    const LocalPlayerData &localPlayerData = gpPlayerData->getLocalPlayerData();

    audioLanguage =
        TestDrive::LanguageToAudioLanguageIndex( gpPlayerData->getLanguage() );
    gpDatabase->setLanguage( pPlayer );

    FUN_006c67b0();

    gearboxMode = localPlayerData.bAuto ? 0 : 1;
    if ( gpMngFlashLocalize != nullptr ) {
        gpMngFlashLocalize->setVarLocalized( pPlayer, "/:WhatGear",
                                             kGearboxLabels[gearboxMode] );
    }

    driveAssists = localPlayerData.DrivingHelpLevel;
    FUN_006c7950( false );

    char stakeValue[32] = {};
    FormatMoneyString( stakeValue, int32_t( localPlayerData.StakeValue ) );
    pPlayer->setVariableValueNonLinked( "/:StakeValue", stakeValue );

    pPlayer->setVariableValue( "/:IsStake",
                               int32_t( localPlayerData.bStakeEnabled ) );
    pPlayer->setVariableValue( "/:IsWheel",
                               int32_t( localPlayerData.bUseWheel ) );
    pPlayer->setVariableValue( "/:IsMouseInvert",
                               int32_t( localPlayerData.bInvertMouse ) );
    pPlayer->setVariableValue( "/:IsAutoClutch",
                               int32_t( localPlayerData.bAutoClutch ) );
    pPlayer->setVariableValue( "/:IsHShifter",
                               int32_t( localPlayerData.bHShifterEnabled ) );
    pPlayer->setVariableValue( "/:IsTrackIR",
                               int32_t( localPlayerData.bTrackIREnabled ) );
    pPlayer->setVariableValue( "/:IsFFBInvert",
                               int32_t( localPlayerData.bFFBInvert ) );

    FUN_006c7b80();

    pPlayer->setVariableValue(
        "/:IsTrackIRAvailable",
        int32_t( gpDevice != nullptr && gpDevice->isHeadTrackingAvailable() ) );

    padLinearitySlider.setValue( localPlayerData.Linearity );
    padSensitivitySlider.setValue( localPlayerData.PadSensitivity );
    quiverSensitivitySlider.setValue( localPlayerData.QuiverSensitivity );
    barSensitivitySlider.setValue( localPlayerData.BarSensitivity );
    mouseSensitivitySlider.setValue( localPlayerData.MouseSensitivity );
    seatHeightSlider.setValue( localPlayerData.SeatPositionHeight );
    seatPositionSlider.setValue( localPlayerData.SeatPositionPosition );
    gammaSlider.setValue( localPlayerData.Gamma );
    cameraShakeSlider.setValue( localPlayerData.CameraShake );
    radialBlurSlider.setValue( localPlayerData.RadialBlurIntensity );
    voicesVolumeSlider.setValue( localPlayerData.VoicesVolume );
    voicesThresholdSlider.setValue( localPlayerData.VoicesThreshold );
    voicesInputSlider.setValue( localPlayerData.VoicesInput );

    pPlayer->setVariableValue( "/:IsLoopBack",
                               int32_t( localPlayerData.bUseLoopBack ) );
    pPlayer->setVariableValue( "/:IsPlayerVoices",
                               int32_t( localPlayerData.bEnablePlayerVoices ) );

    flashGPSOption();

    FUN_006c6650();

    pPlayer->setVariableValue( "/:PadConfig",
                               uint32_t( localPlayerData.PadConfig ) );
    pPlayer->setVariableValue( "/:IsSubtitlesOption", int32_t( 0 ) );
    pPlayer->setVariableValue( "/:IsSubtitles",
                               int32_t( localPlayerData.bSubtitles ) );
}

void MngOptions::FUN_006c61b0() {
    // FUN_006c61b0
    OTDU_UNIMPLEMENTED;
}

void MngOptions::FUN_006c6a00() {
    // FUN_006c6a00
    OTDU_UNIMPLEMENTED;
}

void MngOptions::showPreviousVideoOptions() {
    // FUN_006c6a70
    if ( videoModes.empty() ) {
        return;
    }

    const int32_t numResolutions = int32_t( videoModes.size() );
    videoModeIndex = ( videoModeIndex + numResolutions - 1 ) % numResolutions;
    refreshRateIndex = 0;

    showResolutionLabel();
    showRefreshRateLabel( true );
    showAntiAliasingMode();
}

void MngOptions::FUN_006c7190() {
    // FUN_006c7190
    OTDU_UNIMPLEMENTED;
}

void MngOptions::FUN_006c71d0() {
    // FUN_006c71d0
    OTDU_UNIMPLEMENTED;
}

void MngOptions::FUN_006c7210() {
    // FUN_006c7210
    OTDU_UNIMPLEMENTED;
}

void MngOptions::showLanguageLabel() {
    // FUN_006c81f0
    if ( gpMngFlashLocalize == nullptr || pPlayer == nullptr ) {
        return;
    }

    const uint64_t labelHashcode = kTDULanguages[audioLanguage].LabelHashcode;
    gpMngFlashLocalize->setVarLocalized( pPlayer, "/:WhatLanguage",
                                         labelHashcode );
    gpMngFlashLocalize->setVarLocalized( pPlayer, "/:NameTemp", labelHashcode );
}

void MngOptions::enterSeatSetupCamera() {
    // FUN_006c81f0
    OTDU_UNIMPLEMENTED;
}

void MngOptions::requestSaveToDevice( bool ) {
    // FUN_006c81f0
    OTDU_UNIMPLEMENTED;
}

AudioOptions::AudioOptions() {
    // FUN_006c57e0
    OTDU_UNIMPLEMENTED;
}

AudioOptions::~AudioOptions() {
    // FUN_006c5750
}

void AudioOptions::tick() {
    // FUN_006c5910
}

void AudioOptions::selectVolumeControl( const char * ) {
    // FUN_006c5550
    OTDU_UNIMPLEMENTED;
}

void AudioOptions::setMuteGauge( const char * ) {
    // FUN_006c5610
    OTDU_UNIMPLEMENTED;
}

void AudioOptions::applySpeakerMode() {
    // FUN_006c54e0
    OTDU_UNIMPLEMENTED;
}

void AudioOptions::resetActiveControl() {
    // FUN_006c5970
    OTDU_UNIMPLEMENTED;
}

void AudioOptions::deactivate() { OTDU_UNIMPLEMENTED; }

static const char *GetDisplayModeLabel( const eDisplayMode displayMode ) {
    switch ( displayMode ) {
    case eDisplayMode::DM_Fullscreen:
        return "Fullscreen";
    case eDisplayMode::DM_Borderless:
        return "Borderless";
    case eDisplayMode::DM_Windowed:
    default:
        return "Windowed";
    }
}

static eDisplayMode GetActiveDisplayMode() {
    if ( gpConfig == nullptr || gpConfig->bWindowed ) {
        return eDisplayMode::DM_Windowed;
    }

    return ( gpConfig->FullscreenMode == eFullscreenMode::FM_Manual )
               ? eDisplayMode::DM_Fullscreen
               : eDisplayMode::DM_Borderless;
}

static void ApplyDisplayMode( const eDisplayMode displayMode ) {
    if ( gpConfig == nullptr ) {
        return;
    }

    gpConfig->bWindowed = ( displayMode == eDisplayMode::DM_Windowed );
    if ( displayMode == eDisplayMode::DM_Fullscreen ) {
        gpConfig->FullscreenMode = eFullscreenMode::FM_Manual;
    } else if ( displayMode == eDisplayMode::DM_Borderless ) {
        gpConfig->FullscreenMode = eFullscreenMode::FM_Desktop;
    }
}

void MngOptions::buildVideoModeList() {
    // FUN_006c7210
    if ( pPlayer == nullptr ) {
        return;
    }

    field30_0x80c = true;

    videoWidth = ( gpRender != nullptr ) ? gpRender->getWidth() : gWindowWidth;
    videoHeight =
        ( gpRender != nullptr ) ? gpRender->getHeight() : gWindowHeight;
    refreshRate = uint32_t( gPickedRefreshRate );
    antiAliasingMode = ( gpRender != nullptr )
                           ? gpRender->getAAMethod()
                           : eAntiAliasingMethod::AAM_Disabled;
    bHDREnabled = ( gpRender != nullptr ) && gpRender->isHDREnabled();
    lodQuality =
        ( gpRender != nullptr ) ? int32_t( gpRender->getLODQuality() ) : 0;

    std::vector<DisplayMode> displayModes;
    gEdSys.enumerateDisplayModes( displayModes );

    videoModes.clear();
    for ( const DisplayMode &displayMode : displayModes ) {
        if ( videoModes.empty() ||
             videoModes.back().Width != displayMode.Width ||
             videoModes.back().Height != displayMode.Height ) {
            VideoResolutionOption resolution = {};
            resolution.Width = displayMode.Width;
            resolution.Height = displayMode.Height;
            videoModes.push_back( resolution );
        }

        videoModes.back().RefreshRates.push_back( displayMode.RefreshRate );
    }
    bVideoModesCached = true;

    if ( videoWidth != 0u && videoHeight != 0u ) {
        auto it = std::find_if( videoModes.begin(), videoModes.end(),
                                [this]( const VideoResolutionOption &mode ) {
                                    return mode.Width == videoWidth &&
                                           mode.Height == videoHeight;
                                } );
        if ( it == videoModes.end() ) {
            VideoResolutionOption current = {};
            current.Width = videoWidth;
            current.Height = videoHeight;
            current.RefreshRates.push_back( refreshRate );

            it = videoModes.insert(
                std::upper_bound( videoModes.begin(), videoModes.end(), current,
                                  []( const VideoResolutionOption &l,
                                      const VideoResolutionOption &r ) {
                                      return ( l.Width != r.Width )
                                                 ? l.Width < r.Width
                                                 : l.Height < r.Height;
                                  } ),
                current );
        } else if ( std::find( it->RefreshRates.begin(), it->RefreshRates.end(),
                               refreshRate ) == it->RefreshRates.end() ) {
            it->RefreshRates.push_back( refreshRate );
            std::sort( it->RefreshRates.begin(), it->RefreshRates.end() );
        }
    }

    videoModeIndex = int32_t( videoModes.size() ) - 1;
    refreshRateIndex = 0;
    for ( size_t i = 0; i < videoModes.size(); i++ ) {
        if ( videoModes[i].Width != videoWidth ||
             videoModes[i].Height != videoHeight ) {
            continue;
        }

        videoModeIndex = int32_t( i );
        for ( size_t j = 0; j < videoModes[i].RefreshRates.size(); j++ ) {
            if ( videoModes[i].RefreshRates[j] == refreshRate ) {
                refreshRateIndex = int32_t( j );
                break;
            }
        }
        break;
    }

    if ( videoModes.empty() ) {
        videoModeIndex = 0;
        OTDU_LOG_WARN( "No fullscreen display mode reported by SDL\n" );
        return;
    }

    showResolutionLabel();
    showRefreshRateLabel( false );

    bHDRSupported = false;
    if ( gEdRender.pRenderDevice != nullptr ) {
        const FormatCapabilities caps =
            gEdRender.pRenderDevice->getFormatCapabilities( VF_A16B16G16R16F );
        bHDRSupported = caps.bSupported && caps.bSupportBlending;
    }

    pPlayer->setVariableValue( "/:IsAAAvailable", int32_t( 1 ) );
    pPlayer->showFrame( "set", "/menus/ss_menus/choice/sur3", true );

    showAntiAliasingLabel();

    pPlayer->setVariableValue( "/:IsHDRAvailable", int32_t( bHDRSupported ) );
    pPlayer->setVariableValue( "/:IsHDR", int32_t( bHDREnabled ) );

    showLODQualityLabel();

    displayMode = GetActiveDisplayMode();
    showDisplayModeLabel();
}

void MngOptions::showDisplayModeLabel() {
    if ( pPlayer == nullptr ) {
        return;
    }

    pPlayer->setVariableValueNonLinked( "/:display_mode",
                                        GetDisplayModeLabel( displayMode ) );
}

void MngOptions::showNextDisplayMode() {
    displayMode = eDisplayMode( ( int32_t( displayMode ) + 1 ) %
                                int32_t( eDisplayMode::DM_Count ) );
    showDisplayModeLabel();
}

void MngOptions::showPreviousDisplayMode() {
    const int32_t numModes = int32_t( eDisplayMode::DM_Count );
    displayMode =
        eDisplayMode( ( int32_t( displayMode ) + numModes - 1 ) % numModes );
    showDisplayModeLabel();
}

void MngOptions::showVideoOptions() {
    // FUN_006c6a20
    if ( videoModes.empty() ) {
        return;
    }

    videoModeIndex = ( videoModeIndex + 1 ) % int32_t( videoModes.size() );
    refreshRateIndex = 0;

    showResolutionLabel();
    showRefreshRateLabel( true );
    showAntiAliasingMode();
}

void MngOptions::showRefreshRate() {
    // FUN_006c65d0
    if ( videoModes.empty() ) {
        return;
    }

    const VideoResolutionOption &resolution = videoModes[videoModeIndex];
    if ( resolution.RefreshRates.empty() ) {
        return;
    }

    refreshRateIndex =
        ( refreshRateIndex + 1 ) % int32_t( resolution.RefreshRates.size() );

    showRefreshRateLabel( false );
}

void MngOptions::showAntiAliasing() {
    // FUN_006c6470
    eAntiAliasingMethod method = antiAliasingMode;

    for ( int32_t attempt = 3; attempt > 0; attempt-- ) {
        method = eAntiAliasingMethod( int32_t( method ) + 1 );
        if ( int32_t( method ) > 2 ) {
            method = eAntiAliasingMethod::AAM_Disabled;
        }

        const eViewFormat backbufferFormat =
            bHDREnabled ? VF_A16B16G16R16F : VF_X8R8G8B8;

        bool bSupported = true;
        if ( gEdRender.pRenderDevice != nullptr ) {
            const FormatCapabilities caps =
                gEdRender.pRenderDevice->getFormatCapabilities(
                    backbufferFormat );
            if ( method == eAntiAliasingMethod::AAM_MSAA_X2 ) {
                bSupported = caps.bSupportMSAAx2;
            } else if ( method == eAntiAliasingMethod::AAM_MSAA_X4 ) {
                bSupported = caps.bSupportMSAAx4;
            }
        }

        if ( bSupported ) {
            antiAliasingMode = method;
            showAntiAliasingLabel();
            return;
        }
    }
}

void MngOptions::applySystemParameters() {
    // FUN_006c7010
    if ( videoModes.empty() || pPlayer == nullptr ) {
        return;
    }

    const VideoResolutionOption &resolution = videoModes[videoModeIndex];
    const uint32_t width = resolution.Width;
    const uint32_t height = resolution.Height;
    const uint32_t hz = resolution.RefreshRates.empty()
                            ? refreshRate
                            : resolution.RefreshRates[refreshRateIndex];

    const bool bUnchanged =
        ( videoWidth == width ) && ( videoHeight == height ) &&
        ( refreshRate == hz ) && ( GetActiveDisplayMode() == displayMode ) &&
        ( ( gpRender == nullptr ) ||
          ( gpRender->getAAMethod() == antiAliasingMode &&
            gpRender->isHDREnabled() == bool( bHDREnabled ) ) );
    if ( bUnchanged ) {
        FUN_006c59d0();
        pPlayer->showFrame( "valid_apply", "/menus/ss_menus/choice", true );
        return;
    }

    const eDisplayMode previousDisplayMode = GetActiveDisplayMode();
    ApplyDisplayMode( displayMode );

    if ( gpRender != nullptr &&
         gpRender->resize( width, height, hz, antiAliasingMode,
                           bHDREnabled ) ) {
        OTDU_UNIMPLEMENTED_ONCE; // TODO: FRONTEND (apply confirmation prompt)

        gpPlayerData->setResolution( int32_t( width ), int32_t( height ) );
        gpPlayerData->setRefreshRate( int32_t( hz ) );
        gpPlayerData->setAntiAliasing( antiAliasingMode );
        gpPlayerData->setHDREnabled( bHDREnabled );

        FUN_006c59d0();
        pPlayer->showFrame( "valid_apply", "/menus/ss_menus/choice", true );
        return;
    }

    ApplyDisplayMode( previousDisplayMode );

    OTDU_LOG_ERROR( "Failed to apply the video settings (%ux%u@%uHz)\n", width,
                    height, hz );
}

void MngOptions::showResolutionLabel() {
    // FUN_006c63f0
    if ( pPlayer == nullptr || videoModes.empty() ) {
        return;
    }

    const VideoResolutionOption &resolution = videoModes[videoModeIndex];

    char label[256] = {};
    snprintf( label, sizeof( label ), "%dx%d", resolution.Width,
              resolution.Height );

    pPlayer->setVariableValueNonLinked( "/:resolution", label );
    pPlayer->setVariableValueNonLinked( "/:NameTemp", label );
}

void MngOptions::showRefreshRateLabel( const bool bEditable ) {
    // FUN_006c6350
    if ( pPlayer == nullptr || videoModes.empty() ) {
        return;
    }

    const VideoResolutionOption &resolution = videoModes[videoModeIndex];
    const uint32_t hz = resolution.RefreshRates.empty()
                            ? refreshRate
                            : resolution.RefreshRates[refreshRateIndex];

    char label[256] = {};
    snprintf( label, sizeof( label ), "%d Hz", hz );

    pPlayer->setVariableValueNonLinked( "/:frequency", label );
    if ( bEditable ) {
        pPlayer->setVariableValueNonLinked( "/menus/ss_menus/choice/sur2:param",
                                            label );
        return;
    }

    pPlayer->setVariableValueNonLinked( "/:NameTemp", label );
}

void MngOptions::showAntiAliasingMode() {
    // FUN_006c6990
    eAntiAliasingMethod method = antiAliasingMode;
    while ( method != eAntiAliasingMethod::AAM_Disabled &&
            !isAntiAliasingSupported( method ) ) {
        method = eAntiAliasingMethod( int32_t( method ) - 1 );
    }

    if ( method == antiAliasingMode ) {
        return;
    }
    antiAliasingMode = method;

    const char *pLabel = getAntiAliasingLabel();
    if ( pLabel == nullptr || pPlayer == nullptr ) {
        return;
    }

    pPlayer->setVariableValueNonLinked( "/:anti_alias", pLabel );
    pPlayer->setVariableValueNonLinked( "/menus/ss_menus/choice/sur3:param",
                                        pLabel );
}

bool MngOptions::isAntiAliasingSupported(
    const eAntiAliasingMethod method ) const {
    if ( gEdRender.pRenderDevice == nullptr ) {
        return true;
    }

    const FormatCapabilities caps =
        gEdRender.pRenderDevice->getFormatCapabilities(
            bHDREnabled ? VF_A16B16G16R16F : VF_X8R8G8B8 );

    if ( method == eAntiAliasingMethod::AAM_MSAA_X2 ) {
        return caps.bSupportMSAAx2;
    }
    if ( method == eAntiAliasingMethod::AAM_MSAA_X4 ) {
        return caps.bSupportMSAAx4;
    }

    return true;
}

const char *MngOptions::getAntiAliasingLabel() const {
    if ( antiAliasingMode == eAntiAliasingMethod::AAM_MSAA_X2 ) {
        return "2X";
    }
    if ( antiAliasingMode == eAntiAliasingMethod::AAM_MSAA_X4 ) {
        return "4X";
    }

    return gpDatabase->getStringByHashcode( 0, kAntiAliasingOffLabel );
}

void MngOptions::showAntiAliasingLabel() {
    if ( pPlayer == nullptr ) {
        return;
    }

    const char *pLabel = nullptr;
    if ( antiAliasingMode == eAntiAliasingMethod::AAM_MSAA_X2 ) {
        pLabel = "2X";
    } else if ( antiAliasingMode == eAntiAliasingMethod::AAM_MSAA_X4 ) {
        pLabel = "4X";
    } else {
        pLabel = gpDatabase->getStringByHashcode( 0, kAntiAliasingOffLabel );
    }

    if ( pLabel == nullptr ) {
        return;
    }

    pPlayer->setVariableValueNonLinked( "/:anti_alias", pLabel );
    pPlayer->setVariableValueNonLinked( "/:NameTemp", pLabel );
}

void MngOptions::showLODQualityLabel() {
    if ( gpMngFlashLocalize == nullptr || pPlayer == nullptr ) {
        return;
    }

    const int32_t quality = Clamp( lodQuality, 0, 2 );
    gpMngFlashLocalize->setVarLocalized( pPlayer, "/:lod",
                                         kLODQualityLabels[quality] );
    gpMngFlashLocalize->setVarLocalized( pPlayer, "/:NameTemp",
                                         kLODQualityLabels[quality] );
}
