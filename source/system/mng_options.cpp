#include "shared.h"
#include "mng_options.h"

#include "audio/gs_audio.h"
#include "core/format.h"
#include "database/gs_database.h"
#include "flash/gs_flash.h"
#include "flash/implementation/flash_player.h"
#include "flash/implementation/ed_flash.h"
#include "flash/implementation/flash_resource.h"
#include "flash/mng_fade.h"
#include "flash/mng_flash_localize.h"
#include "flash/modal_message_box.h"
#include "game/gm_base.h"
#include "game/mng_hud.h"
#include "gs_timer.h"
#include "input/gs_device.h"
#include "player_data/achievements_data.h"
#include "player_data/gs_playerdata.h"
#include "player_data/player_data_interface.h"
#include "player_data/opentdu_settings.h"
#include "render/camera/gs_camera.h"
#include "render/ed_render.h"
#include "render/gs_render.h"
#include "render/mng_env_map.h"
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
static constexpr uint64_t kUnitSystemMetricLabel = 0x035905B2ull;
static constexpr uint64_t kUnitSystemLabelStride = 1000000ull;

static constexpr uint64_t GetUnitSystemLabel( const int32_t unitType ) {
    return kUnitSystemMetricLabel +
           ( ( unitType != 0 ) ? kUnitSystemLabelStride : 0ull );
}

static constexpr int32_t kBDDMenus = 0;
static constexpr int32_t kBDDTutorial = 11;

static constexpr uint64_t kKeepVideoSettingsHashcode =
    0x38e4a6aull; // "_Do you want to keep these settings ?"
static constexpr uint64_t kVideoSettingsRevertedHashcode =
    0x39d8caaull; // "_The previous mode was restored"

static constexpr int32_t kApplyTimeoutInSeconds = 10;

static void PatchVideoPageUpNavigation( FlashPlayer *pPlayer );
static void ApplyVSync( const bool bEnabled );

// OpenTDU Advanced Video Settings
enum eExtraVideoRow : int32_t {
    EVR_DisplayMode = 0,
    EVR_FrameRateCap = 1,
    EVR_Reflections = 2,
    EVR_VSync = 3,

    EVR_Count
};

struct ExtraVideoRowDesc {
    const char *pLabel;
    const char *pPreviousMessage;
    const char *pNextMessage;
    bool bCheckbox;
};

static constexpr ExtraVideoRowDesc kExtraVideoRows[EVR_Count] = {
    {"DISPLAY MODE", "prdm",  "nedm",  false},
    {"FRAME RATE",   "prfc",  "nefc",  false},
    {"REFLECTIONS",  "prdr",  "nedr",  false},
    {"VSYNC",        nullptr, nullptr, true },
};
static constexpr int32_t kFrameRateCaps[] = { 0, 30, 60, 120, 144, 240 };

static constexpr const char *kVideoMenuPath = "/menus/ss_menus/choice";
static constexpr int32_t kNumRetailVideoRows = 6;
static constexpr int32_t kResolutionRow = 1;
static constexpr int32_t kRefreshRateRow = 2;
static constexpr int32_t kAntiAliasingRow = 3;
static constexpr int32_t kHDRRow = 4;
static constexpr int32_t kGammaRow = 5;
static constexpr int32_t kNumExtraVideoRows = EVR_Count;

static constexpr uint16_t kVideoPanelDepths[] = { 0, 1, 2, 4, 5, 7, 8, 10 };
static_assert( std::size( kVideoPanelDepths ) ==
               MngOptions::kNumVideoPanelPieces );
static constexpr uint16_t kVideoPanelTitleDepth = 11;
// Stage centre (12800) minus the choice clip origin (1470 + 1312)
static constexpr float kStageCenterX = 10018.0f;
static constexpr float kVideoRowCenterX = 5018.0f;
static constexpr float kVideoPanelTitleOffsetX = 711.0f;
static constexpr float kVideoRowSpacing = 1363.0f;

struct CheckboxRowChild {
    uint16_t Depth;
    float MinX;
    float MaxX;
};
static constexpr CheckboxRowChild kCheckboxRowUnmirroredChildren[] = {
    {9,  723.0f, 5628.0f}, // sm_name (label)
    {14, -83.0f, 187.0f }, // checkbox
};

static constexpr uint16_t kFirstExtraPanelDepth = 17;
static constexpr uint16_t kExtraPanelTitleDepth =
    kFirstExtraPanelDepth + uint16_t( std::size( kVideoPanelDepths ) );
static constexpr uint16_t kFirstExtraRowDepth = 1125;

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
      antiAliasingMode( eAntiAliasingMethod::AAM_Disabled ),
      videoWidth( 0 ),
      videoHeight( 0 ),
      refreshRate( 0 ),
      activeAntiAliasingMode( eAntiAliasingMethod::AAM_Disabled ),
      activeDisplayMode( eDisplayMode::DM_Windowed ),
      bActiveHDREnabled( false ),
      extraVideoRowsState( eExtraVideoRowsState::EVRS_Closed ),
      bExtraVideoPanelFocused( false ),
      hoveredExtraVideoRow( -1 ),
      bExtraVideoMouseDown( false ),
      focusedVideoRow( -1 ),
      bVSyncEnabled( false ),
      bActiveVSyncEnabled( false ),
      bVideoPanelPieceTinted(),
      videoPanelPieceTints(),
      pAudioOptions( nullptr ),
      field30_0x80c( false ),
      bHDREnabled( false ),
      bHDRSupported( false ) {
    // FUN_006c5f20
    memset( pLocalizedStrings, 0, sizeof( pLocalizedStrings ) );

    videoRevertedCallback.pOwner = this;
    videoApplyCallback.pOwner = this;

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

    terminate();
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

    onLanguageChanged( false );

    updateExtraVideoRows();
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
        if ( !isVideoModeLocked() ) {
            showPreviousRefreshRate();
        } else {
            showRefreshRateLabel( false );
        }
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
        pAudioOptions->applyVolumes();
        pAudioOptions->deactivate();
        return true;
    }

    case 0x6175696e: // "auin"
        pAudioOptions->refreshControls();
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
        startLanguageChange(
            eLocale( kTDULanguages[audioLanguage].LocaleIndex ) );
        gpPlayerData->getLocalPlayerData().bSubtitles =
            pPlayer->getVariableAsInt( "/:IsSubtitles" ) != 0;
        return true;

    case 0x63686f70: // "chop" — commit gameplay options
        commitPlayerOptions();
        return true;

    case 0x67616d73: // "gams"
        disableAllSliders();
        gammaSlider.setFocusState( true );
        return true;

    case 0x6469696e: // "diin" — GPS option
        flashGPSOption();
        return true;

    case 0x656e746f: // "ento"
        revertSystemParameters();
        return false;

    case 0x6864636c: // "hdcl"
        toggleHDR();
        return false;

    case 0x6d6f7573: // "mous"
        disableAllSliders();
        mouseSensitivitySlider.setFocusState( true );
        return true;

    case 0x6e657273: // "ners" — next resolution
        if ( !isVideoModeLocked() ) {
            showVideoOptions();
        } else {
            showResolutionLabel();
        }
        return false;

    case 0x6e656672: // "nefr" — next refresh rate
        if ( !isVideoModeLocked() ) {
            showRefreshRate();
        } else {
            showRefreshRateLabel( false );
        }
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

    case 0x6e656663: // "nefc" — next frame rate cap
        showNextFrameRateCap( 1 );
        return false;

    case 0x6e656472: // "nedr" — next reflection quality
    case 0x70726472: // "prdr" — previous reflection quality
        toggleDenseReflections();
        return false;

    case 0x6e656c67: // "nelg" — next language
        audioLanguage++;
        if ( audioLanguage > 4 ) {
            audioLanguage = 0;
        }
        showLanguageLabel();
        return true;

    case 0x6e656c6f: // "nelo"
        showNextLODQuality();
        return false;

    case 0x70726161: // "praa" — previous anti-aliasing
        showPreviousAntiAliasing();
        return false;

    case 0x6e6f6761: // "noga" — leave the seat-setup view
        if ( seatPositionSlider.hasFocus() || seatHeightSlider.hasFocus() ) {
            if ( cameraBeforeSeatSetup != -1 ) {
                gpCamera->setTransitionCameraMode( cameraBeforeSeatSetup );
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
        if ( !isVideoModeLocked() ) {
            showPreviousVideoOptions();
        } else {
            showResolutionLabel();
        }
        return false;

    case 0x70726765: // "prge" — previous gearbox mode
        gearboxMode--;
        FUN_006c7b80();
        return true;

    case 0x7072646d: // "prdm" — previous display mode
        showPreviousDisplayMode();
        return false;

    case 0x70726663: // "prfc" — previous frame rate cap
        showNextFrameRateCap( -1 );
        return false;

    case 0x70726c67: // "prlg" — previous language
        audioLanguage--;
        if ( audioLanguage < 0 ) {
            audioLanguage = 4;
        }
        showLanguageLabel();
        return true;

    case 0x70726c6f: // "prlo"
        showPreviousLODQuality();
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
        extraVideoRowsState = eExtraVideoRowsState::EVRS_Opening;
        return false;

    case 0x73746465: // "stde" — save to device
        requestSaveToDevice( false );
        return false;

    case 0x73746476: // "stdv" — save to device (overwrite)
        requestSaveToDevice( true );
        return false;

    case 0x75736d65:   // "usme"
    case 0x7573696d: { // "usim" — both 4CCs toggle, they are the two arrows of
                       // the same option row
        unitType = ( unitType != 1 ) ? 1 : 0;

        if ( gpMngFlashLocalize != nullptr ) {
            const uint64_t labelHash = GetUnitSystemLabel( unitType );
            gpMngFlashLocalize->setVarLocalized( pPlayer, "/:UnitSystem",
                                                 labelHash );
            gpMngFlashLocalize->setVarLocalized( pPlayer, "/:NameTemp",
                                                 labelHash );
        }
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

    OTDU_UNIMPLEMENTED_ONCE; // TODO: FILESYSTEM
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
        OpenTDUSettings::Save();

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

    ApplyDisplayMode( activeDisplayMode );
    ApplyVSync( bActiveVSyncEnabled );

    gpRender->resize( videoWidth, videoHeight, refreshRate,
                      activeAntiAliasingMode, bActiveHDREnabled );
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
    // FUN_006c98b0 (inlined at the head of the tick)
    if ( deviceSelected == 0 ) {
        return;
    }

    OTDU_UNIMPLEMENTED_ONCE; // TODO: FILESYSTEM
}

static void SetMoviesFlag( const int32_t level,
                           const uint32_t mask,
                           const bool bPausedFlags,
                           const bool bSet ) {
    const int32_t numMovies = gpFlash->getNumMovies( level );
    for ( int32_t i = 0; i < numMovies; i++ ) {
        MoviePlayer *pMovie = gpFlash->getMoviePlayer( level, i );
        if ( pMovie == nullptr ) {
            continue;
        }

        uint32_t &flags =
            bPausedFlags ? reinterpret_cast<uint32_t &>( pMovie->IsMoviePaused )
                         : pMovie->Flags;
        flags = bSet ? ( flags | mask ) : ( flags & ~mask );
    }
}

void MngOptions::setLocale( const char *pLocale, const bool bSynchronous ) {
    // FUN_006c97e0
    if ( !bSynchronous ) {
        startLanguageChange( TestDrive::StringToLocale( pLocale ) );
        return;
    }

    languageRefreshStep = 1;
    activeLocale = TestDrive::StringToLocale( pLocale );
    SetMoviesFlag( 1, 0x100, false, true );

    do {
        onLanguageChanged( bSynchronous );
    } while ( languageRefreshStep != 0 );
}

void MngOptions::onLanguageChanged( const bool bSkipFade ) {
    // FUN_006c9390
    MngFade *pFade = gpMngFade;

    switch ( languageRefreshStep ) {
    case 1: {
        if ( pFade != nullptr && ( pFade->getTransitionFlags() & 1 ) != 0 ) {
            return;
        }

        const int32_t fontIndex = GSFlash::GetUnicodeFontIndex( activeLocale );
        const int32_t currentLocale = gpFlash->getUnicodeFontLocale();
        const int32_t currentFontIndex =
            ( currentLocale < 0 )
                ? -1
                : GSFlash::GetUnicodeFontIndex( currentLocale );
        if ( fontIndex == -1 || currentFontIndex == fontIndex ) {
            gpFlash->setUnicodeFontLocale( activeLocale );
            languageRefreshStep = 5;
        } else {
            languageRefreshStep = 2;
        }

        if ( pFade != nullptr && !bSkipFade ) {
            pFade->setTransitionFlags( pFade->getTransitionFlags() | 8 );
        }

        SetMoviesFlag( 0, 0x100, true, true );
        SetMoviesFlag( 1, 0x100, true, true );
        break;
    }
    case 2:
        languageRefreshStep = 3;
        gpFlash->releaseUnicodeFont();
        break;
    case 3:
        languageRefreshStep = 4;
        break;
    case 4:
        languageRefreshStep = 5;
        gpFlash->setUnicodeFontLocale( activeLocale );
        break;
    case 5: {
        languageRefreshStep = 6;

        char pLocale[16] = {};
        TestDrive::LocaleToString( activeLocale, pLocale );
        gpPlayerData->setDefaultLanguage( pLocale, false );
        gpDatabase->switchLocale( activeLocale );

        gpPlayerData->setHasHardcoreMode( true );
        if ( gpPlayerData->getLoadedSave() != nullptr ) {
            gpPlayerData->getLoadedSave()
                ->getAchievements()
                ->applyHardcoreLevelTexts();
        }

        FlashPlayer *pMainPlayer = gpFlash->getLoadedFlashPlayer( 1, 0 );
        if ( pMainPlayer != nullptr ) {
            gpDatabase->setFlashLocalization( 0, 0u, pMainPlayer );
        }

        OTDU_UNIMPLEMENTED_ONCE; // TODO: FLASH

        retrieveLocalizedStrings();
        refreshOptionsScreen();

        FlashPlayer *pGeneralPlayer = gpFlash->getFlashPlayer( "GENERAL" );
        const char *pLoadingMessage =
            gpDatabase->getStringByHashcode( 0, 0x36c66a2 );
        if ( pGeneralPlayer != nullptr && pLoadingMessage != nullptr ) {
            pGeneralPlayer->setVariableValue( "/:msg", pLoadingMessage );
        }

        if ( gpTestDriveInstance != nullptr &&
             gpTestDriveInstance->getActiveGameMode() != GM_Login ) {
            gpPlayerData->requestSave( 1 );
            gpPlayerData->notify( 0 );
        }

        activeLocale = eLocale::L_Unknown;

        OTDU_UNIMPLEMENTED_ONCE; // TODO: ONLINE
        break;
    }
    case 6: {
        languageRefreshStep = 7;
        if ( pFade != nullptr && !bSkipFade ) {
            pFade->restartTransition( ( pFade->getTransitionFlags() & ~8u ) | 5,
                                      pFade->getTransitionDuration() );
        }

        SetMoviesFlag( 0, 0x100, true, false );
        SetMoviesFlag( 1, 0x100, true, false );

        if ( pPlayer == nullptr ) {
            break;
        }

        FlashClip choiceClip;
        if ( !pPlayer->getClipPlayer( "/menus/ss_menus/choice",
                                      &choiceClip ) ) {
            languageRefreshStep = 6;
            break;
        }

        const int32_t currentFrame =
            choiceClip.getFrameProperty( eFlashProperty::FP_CurrentFrame );
        pPlayer->setVariableValue( "/:NoRedirection",
                                   int32_t( currentFrame <= 1 ) );
        choiceClip.goToLabelAndPlay( "param_end", true );
        break;
    }
    case 7:
        if ( pFade != nullptr && ( pFade->getTransitionFlags() & 1 ) != 0 ) {
            return;
        }

        languageRefreshStep = 0;
        SetMoviesFlag( 1, 0x100, false, false );
        break;
    default:
        break;
    }
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

void MngOptions::startLanguageChange( const eLocale locale ) {
    // FUN_006c6730
    activeLocale = locale;
    languageRefreshStep = 1;
    SetMoviesFlag( 1, 0x100, false, true );

    if ( gpMngFade != nullptr ) {
        gpMngFade->restartTransition(
            ( gpMngFade->getTransitionFlags() & ~0xeu ) | 1, 0.5f );
    }
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

static void SetPadConfig( const uint32_t padConfig ) {
    // FUN_00898700
    gpPlayerData->getLocalPlayerData().PadConfig = padConfig;

    MoviePlayer *pMovie = gpFlash->getMoviePlayer( 1, 0 );
    if ( pMovie == nullptr || !pMovie->MovieBank.isLoaded() ) {
        return;
    }

    FlashPlayer *pPlayer = pMovie->MovieBank.getFlashPlayer();
    if ( pPlayer != nullptr ) {
        pPlayer->setVariableValue( "/:IsAlternateButton",
                                   int32_t( padConfig == 3u ) );
    }
}

void MngOptions::commitPlayerOptions() {
    // FUN_006c81f0
    LocalPlayerData &localPlayerData = gpPlayerData->getLocalPlayerData();

    FUN_006c7fb0();

    localPlayerData.bAuto = ( gearboxMode == 0 );
    localPlayerData.DrivingHelpLevel = driveAssists;

    localPlayerData.bStakeEnabled =
        pPlayer->getVariableAsInt( "/:IsStake" ) == 1;

    char stakeValue[32] = {};
    pPlayer->getVariableAsString( "/:StakeValue", stakeValue );
    localPlayerData.StakeValue = uint32_t( StringToFloat( stakeValue ) );

    localPlayerData.PadSensitivity = padSensitivitySlider.getValue();

    localPlayerData.bUseWheel = pPlayer->getVariableAsInt( "/:IsWheel" ) == 1;
    localPlayerData.bInvertMouse =
        pPlayer->getVariableAsInt( "/:IsMouseInvert" ) == 1;
    localPlayerData.bAutoClutch =
        pPlayer->getVariableAsInt( "/:IsAutoClutch" ) == 1;
    localPlayerData.bHShifterEnabled =
        pPlayer->getVariableAsInt( "/:IsHShifter" ) == 1;
    localPlayerData.bTrackIREnabled =
        pPlayer->getVariableAsInt( "/:IsTrackIR" ) == 1;
    localPlayerData.bFFBInvert =
        pPlayer->getVariableAsInt( "/:IsFFBInvert" ) == 1;

    localPlayerData.Linearity = padLinearitySlider.getValue();
    localPlayerData.QuiverSensitivity = quiverSensitivitySlider.getValue();
    localPlayerData.BarSensitivity = barSensitivitySlider.getValue();
    localPlayerData.MouseSensitivity = mouseSensitivitySlider.getValue();
    localPlayerData.CameraShake = cameraShakeSlider.getValue();
    localPlayerData.RadialBlurIntensity = radialBlurSlider.getValue();
    localPlayerData.SeatPositionHeight = seatHeightSlider.getValue();
    localPlayerData.SeatPositionPosition = seatPositionSlider.getValue();

    gpPlayerData->applyDrivingOptions();

    FUN_006c6db0();

    SetPadConfig( uint32_t( pPlayer->getVariableAsInt( "/:PadConfig" ) ) );
    if ( gpConfig != nullptr ) {
        gpConfig->parseDeviceIni();
    }

    localPlayerData.VoicesVolume = voicesVolumeSlider.getValue();
    localPlayerData.VoicesThreshold = voicesThresholdSlider.getValue();
    localPlayerData.VoicesInput = voicesInputSlider.getValue();
    localPlayerData.bUseLoopBack =
        pPlayer->getVariableAsInt( "/:IsLoopBack" ) != 0;
    localPlayerData.bEnablePlayerVoices =
        pPlayer->getVariableAsInt( "/:IsPlayerVoices" ) != 0;

    OTDU_UNIMPLEMENTED_ONCE; // TODO: ONLINE
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
    if ( MoviePlayer *pMovie = gpFlash->getMoviePlayer( 1, 0 ) ) {
        if ( pMovie->MovieBank.isLoaded() ) {
            if ( FlashPlayer *pInGame = pMovie->MovieBank.getFlashPlayer() ) {
                pInGame->setVariableValue(
                    "/:IsHelpSystem", int32_t( localPlayerData.bHelpSystem ) );
            }
        }
    }

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

    gpMngFlashLocalize->setVarLocalized(
        pPlayer, "/:UnitSystem",
        GetUnitSystemLabel( localPlayerData.UnitSystem ) );
}

void MngOptions::bindFlashPlayer( FlashPlayer *pFlashPlayer,
                                  int32_t orientation ) {
    // FUN_006c6ac0
    pPlayer = pFlashPlayer;

    PatchVideoPageUpNavigation( pPlayer );

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

    pAudioOptions->bindFlashPlayer( pPlayer, orientation );

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

void MngOptions::revertSystemParameters() {
    // FUN_006c61b0
    FlashPlayer *pGeneral = nullptr;
    uint32_t validGM = 0u;
    uint32_t movieIndex = 0u;
    if ( gpFlash->getFlashMovieIndex( &validGM, &movieIndex, "GENERAL" ) ) {
        MoviePlayer *pMovie = gpFlash->getMoviePlayer( validGM, movieIndex );
        if ( pMovie != nullptr && pMovie->MovieBank.isLoaded() ) {
            pGeneral = pMovie->MovieBank.getFlashPlayer();
        }
    }

    if ( pGeneral != nullptr ) {
        pGeneral->setVariableValue( "/:IsAvertTimeOut", int32_t( 0 ) );
    }

    if ( gpFlashMessageBox != nullptr ) {
        gpFlashMessageBox->forceClose();
    }

    FUN_006c59d0();
    FUN_006c6030();

    pPlayer->showFrame( "open_system", "/menus/ss_menus/choice", true );
}

void MngOptions::toggleHDR() {
    // FUN_006c6a00
    bHDREnabled = !bHDREnabled;
    showAntiAliasingMode();
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

void MngOptions::showNextLODQuality() {
    // FUN_006c7190
    lodQuality = ( lodQuality < 2 ) ? lodQuality + 1 : 0;

    showLODQualityLabel();
    gpRender->setLODQuality( uint32_t( lodQuality ) );
}

void MngOptions::showPreviousLODQuality() {
    // FUN_006c71d0
    lodQuality = ( lodQuality < 1 ) ? 2 : lodQuality - 1;

    showLODQualityLabel();
    gpRender->setLODQuality( uint32_t( lodQuality ) );
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
    if ( cameraBeforeSeatSetup != -1 || gpActiveGameMode == nullptr ) {
        return;
    }

    const eGameMode activeGameMode = gpActiveGameMode->getCurrentGameMode();
    if ( activeGameMode != GM_FreeRide && activeGameMode != GM_Mission ) {
        return;
    }

    cameraBeforeSeatSetup = gpCamera->getActiveTransitionIndex();

    gpCamera->setTransitionCameraMode(
        gpCamera->findFirstSeatAdjustableTransitionMode() );
}

void MngOptions::requestSaveToDevice( bool ) {
    // FUN_006c81f0
    OTDU_UNIMPLEMENTED_ONCE; // TODO: FILESYSTEM
}

// DAT_00f03f30
static constexpr AudioMixerControlDesc
    kAudioMixerControls[AudioOptions::kNumMixerControls] = {
        {"/:vol_Engine",     "/gauge_1/slider/mute", "MixerVehicle"  },
        {"/:vol_Collisions", "/gauge_2/slider/mute", "MixerCollision"},
        {"/:vol_GeneralSFX", "/gauge_3/slider/mute", "MixerAmbiance" },
        {"/:vol_Cinematic",  "/gauge_4/slider/mute", "MixerCinematic"},
        {"/:vol_Music",      "/gauge_5/slider/mute", "MixerMusic"    },
        {"/:vol_UI",         "/gauge_6/slider/mute", "MixerUI"       },
        {"/:vol_Voices",     "/gauge_7/slider/mute", "MixerVoice"    },
        {"/:vol_VoicesGPS",  "/gauge_8/slider/mute", "MixerGPS"      },
        {"/:vol_Master",     "/gauge_9/slider/mute", "MixerMaster"   }
};

// DAT_00f9c208
static constexpr const char *kMuteGaugeRoot = "menus/ss_menus/choice";

void AudioOptions::MixerControl::showMuteFrame() const {
    if ( pFlashPlayer == nullptr || pDesc == nullptr ) {
        return;
    }

    char gaugePath[64] = {};
    snprintf( gaugePath, sizeof( gaugePath ), "%s%s", kMuteGaugeRoot,
              pDesc->pGaugeName );

    pFlashPlayer->showFrame( bMuted ? "off" : "on", gaugePath, false );
}

void AudioOptions::MixerControl::refreshFromMixer() {
    // FUN_006c58b0
    Slider.setValue( pMixer->Volume );
    bMuted = pMixer->bMuted;

    showMuteFrame();
}

void AudioOptions::MixerControl::toggleMute() {
    // FUN_006c5430
    bMuted = !bMuted;

    showMuteFrame();
}

AudioOptions::AudioOptions() : bActive( false ) {
    // FUN_006c57e0
    for ( int32_t i = 0; i < kNumMixerControls; i++ ) {
        Mixer *pMixer =
            ( gpAudio != nullptr )
                ? gpAudio->findMixer( kAudioMixerControls[i].pMixerName )
                : nullptr;
        if ( pMixer == nullptr ) {
            continue;
        }

        controls[i].pMixer = pMixer;
        controls[i].pDesc = &kAudioMixerControls[i];

        controls[i].Slider.setVerticalDrag( true );
        controls[i].Slider.setRange( 0.0f, 1.0f );
    }
}

AudioOptions::~AudioOptions() {
    // FUN_006c5750
}

void AudioOptions::tick() {
    // FUN_006c5910
    if ( !bActive ) {
        return;
    }

    for ( MixerControl &control : controls ) {
        if ( control.isValid() ) {
            control.Slider.tick( gpTimer->SystemDeltaTime );
        }
    }
}

void AudioOptions::bindFlashPlayer( FlashPlayer *pFlashPlayer,
                                    int32_t orientation ) {
    // FUN_006c56d0
    for ( MixerControl &control : controls ) {
        if ( !control.isValid() ) {
            continue;
        }

        control.pFlashPlayer = pFlashPlayer;
        control.Slider.rebind( pFlashPlayer, control.pDesc->pVarName,
                               orientation );
    }
}

void AudioOptions::selectVolumeControl( const char *pVarName ) {
    // FUN_006c5550
    for ( MixerControl &control : controls ) {
        if ( !control.isValid() ) {
            continue;
        }

        control.Slider.setFocusState(
            strcmp( control.pDesc->pVarName, pVarName ) == 0 );
    }
}

void AudioOptions::setMuteGauge( const char *pWhatGauge ) {
    // FUN_006c5610
    for ( MixerControl &control : controls ) {
        if ( !control.isValid() ) {
            continue;
        }

        if ( strcmp( control.pDesc->pGaugeName, pWhatGauge ) == 0 ) {
            control.toggleMute();
            return;
        }
    }
}

void AudioOptions::applyVolumes() {
    // FUN_006c54e0
    for ( MixerControl &control : controls ) {
        if ( !control.isValid() ) {
            continue;
        }

        control.pMixer->bMuted = control.bMuted;
        control.pMixer->Volume = control.Slider.getValue();
    }
}

void AudioOptions::refreshControls() {
    // FUN_006c5970
    for ( MixerControl &control : controls ) {
        if ( control.isValid() ) {
            control.refreshFromMixer();
        }
    }

    bActive = true;
}

void AudioOptions::deactivate() { bActive = false; }

static const char *GetDisplayModeLabel( const eDisplayMode displayMode ) {
    switch ( displayMode ) {
    case eDisplayMode::DM_Fullscreen:
        return "FULLSCREEN";
    case eDisplayMode::DM_Borderless:
        return "BORDERLESS";
    case eDisplayMode::DM_Windowed:
    default:
        return "WINDOWED";
    }
}

static void PatchVideoPageUpNavigation( FlashPlayer *pPlayer ) {
    static constexpr uint8_t kSignature[] = {
        0x96, 0x00, 0x09, 0x00, 0x00, 'n',  'u',  'm',  '_',  's',
        's',  'm',  0x00, 0x00, 0x1c, 0x00, 0x96, 0x00, 0x0d, 0x00,
        0x00, 'n',  'b',  '_',  's',  's',  'm',  's',  '_',  'm',
        'a',  'x',  0x00, 0x00, 0x1c, 0x00, 0x96, 0x00, 0x03, 0x00,
        0x00, '1',  0x00, 0x00, 0x0b, 0x00, 0x0e, 0x00 };
    static constexpr size_t kConstantOffset = 41;

    if ( pPlayer == nullptr || pPlayer->getBoundFlashResource() == nullptr ) {
        return;
    }

    FlashResourceHeader *pHeader =
        pPlayer->getBoundFlashResource()->getHeader();
    if ( pHeader == nullptr ) {
        return;
    }

    uint8_t *pBegin = (uint8_t *)pHeader;
    uint8_t *pEnd = pBegin + pHeader->FileSize;
    uint8_t *pMatch = std::search( pBegin, pEnd, std::begin( kSignature ),
                                   std::end( kSignature ) );
    if ( pMatch == pEnd ) {
        OTDU_LOG_WARN( "Video page navigation signature not found; the extra "
                       "rows will break up navigation\n" );
        return;
    }

    pMatch[kConstantOffset] = uint8_t( '1' + kNumExtraVideoRows );
}

eDisplayMode GetActiveDisplayMode() {
    if ( gpConfig == nullptr || gpConfig->bWindowed ) {
        return eDisplayMode::DM_Windowed;
    }

    return ( gpConfig->FullscreenMode == eFullscreenMode::FM_Manual )
               ? eDisplayMode::DM_Fullscreen
               : eDisplayMode::DM_Borderless;
}

static void ApplyVSync( const bool bEnabled ) {
    if ( gpRender == nullptr || gpConfig == nullptr ) {
        return;
    }

    gpRender->setVSyncMode( bEnabled
                                ? eVSyncMode( Clamp( gpConfig->NbVBL, 1, 4 ) )
                                : eVSyncMode::VSM_Immediate );
}

void ApplyDisplayMode( const eDisplayMode displayMode ) {
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

    activeAntiAliasingMode = antiAliasingMode;
    bActiveHDREnabled = bHDREnabled;
    activeDisplayMode = GetActiveDisplayMode();
    bActiveVSyncEnabled = ( gpRender != nullptr ) &&
                          gpRender->getVSyncMode() != eVSyncMode::VSM_Immediate;
    bVSyncEnabled = bActiveVSyncEnabled;
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
}

static bool CloneExtraVideoPanel( FlashPlayer *pPlayer ) {
    for ( size_t i = 0; i < std::size( kVideoPanelDepths ); i++ ) {
        FlashCloneDesc panel;
        panel.SourceDepth = kVideoPanelDepths[i];
        panel.Depth = uint16_t( kFirstExtraPanelDepth + i );
        panel.Mirror = eFlashCloneMirror::FCM_Geometry;
        panel.MirrorAxisX = kStageCenterX;
        if ( !pPlayer->cloneObject( kVideoMenuPath, panel ) ) {
            return false;
        }
    }

    FlashCloneDesc title;
    title.SourceDepth = kVideoPanelTitleDepth;
    title.Depth = kExtraPanelTitleDepth;
    title.Mirror = eFlashCloneMirror::FCM_Position;
    title.MirrorAxisX = kStageCenterX;
    title.OffsetX = kVideoPanelTitleOffsetX;
    pPlayer->cloneObject( kVideoMenuPath, title );

    for ( int32_t i = 0; i < kNumExtraVideoRows; i++ ) {
        char cloneName[8];
        snprintf( cloneName, sizeof( cloneName ), "sur%d",
                  kNumRetailVideoRows + 1 + i );

        FlashCloneDesc row;
        row.pCloneName = cloneName;
        row.Depth = uint16_t( kFirstExtraRowDepth + i );
        row.MirrorAxisX = kStageCenterX;
        if ( kExtraVideoRows[i].bCheckbox ) {
            row.pSourceName = "sur4";
            row.Mirror = eFlashCloneMirror::FCM_Geometry;
        } else {
            row.pSourceName = "sur1";
            row.Mirror = eFlashCloneMirror::FCM_Position;
            row.OffsetX = -2.0f * kVideoRowCenterX;
            row.OffsetY = float( i ) * kVideoRowSpacing;
        }
        if ( !pPlayer->cloneObject( kVideoMenuPath, row ) ) {
            return false;
        }
    }

    return true;
}

static void RemoveExtraVideoPanel( FlashPlayer *pPlayer ) {
    for ( int32_t i = 0; i < kNumExtraVideoRows; i++ ) {
        pPlayer->removeObject( kVideoMenuPath,
                               uint16_t( kFirstExtraRowDepth + i ) );
    }
    pPlayer->removeObject( kVideoMenuPath, kExtraPanelTitleDepth );
    for ( size_t i = 0; i < std::size( kVideoPanelDepths ); i++ ) {
        pPlayer->removeObject( kVideoMenuPath,
                               uint16_t( kFirstExtraPanelDepth + i ) );
    }
}

static int32_t GetFrameRateCapIndex() {
    const int32_t maxFrameRate = GetMaxFrameRate();
    for ( int32_t i = 0; i < int32_t( std::size( kFrameRateCaps ) ); i++ ) {
        if ( kFrameRateCaps[i] == maxFrameRate ) {
            return i;
        }
    }

    return 0;
}

void MngOptions::updateExtraVideoRows() {
    if ( pPlayer == nullptr ||
         extraVideoRowsState == eExtraVideoRowsState::EVRS_Closed ) {
        return;
    }

    char path[64];
    snprintf( path, sizeof( path ), "%s/sur%d", kVideoMenuPath,
              kNumRetailVideoRows );
    const bool bLastRetailRowExists = pPlayer->spriteExists( path );

    if ( extraVideoRowsState == eExtraVideoRowsState::EVRS_Open &&
         !bLastRetailRowExists ) {
        RemoveExtraVideoPanel( pPlayer );
        hoveredExtraVideoRow = -1;
        focusedVideoRow = -1;
        bExtraVideoPanelFocused = false;
        showExtraVideoPanelFocus();
        extraVideoRowsState = eExtraVideoRowsState::EVRS_Closed;
        return;
    }

    if ( extraVideoRowsState == eExtraVideoRowsState::EVRS_Open ) {
        updateCheckboxRowsOrientation();
        updateExtraVideoRowsHover();

        snprintf( path, sizeof( path ), "%s:num_ssm", kVideoMenuPath );
        const int32_t focusedRow = pPlayer->getVariableAsInt( path );
        if ( focusedRow != focusedVideoRow ) {
            const int32_t previousRow = focusedVideoRow;
            focusedVideoRow = focusedRow;
            highlightCheckboxRow( focusedRow );
            if ( isVideoModeLocked() ) {
                skipLockedVideoRow( focusedRow, previousRow );
                showVideoModeRowsLock();
            }
        }

        const bool bFocused = focusedRow > kNumRetailVideoRows;
        if ( bFocused != bExtraVideoPanelFocused ) {
            bExtraVideoPanelFocused = bFocused;
            showExtraVideoPanelFocus();
        }
    }

    snprintf( path, sizeof( path ), "%s:nb_ssms_max", kVideoMenuPath );
    if ( bLastRetailRowExists &&
         pPlayer->getVariableAsInt( path ) == kNumRetailVideoRows ) {
        setupExtraVideoRows();
        extraVideoRowsState = eExtraVideoRowsState::EVRS_Open;
    }
}

void MngOptions::setupExtraVideoRows() {
    char path[64];
    snprintf( path, sizeof( path ), "%s/sur%d", kVideoMenuPath,
              kNumRetailVideoRows + 1 );
    if ( !pPlayer->spriteExists( path ) ) {
        for ( int32_t i = 0; i < kNumVideoPanelPieces; i++ ) {
            bVideoPanelPieceTinted[i] = pPlayer->getObjectColorTransform(
                kVideoMenuPath, kVideoPanelDepths[i],
                &videoPanelPieceTints[i] );
        }

        if ( !CloneExtraVideoPanel( pPlayer ) ) {
            OTDU_LOG_WARN( "Failed to append the extra rows to the video "
                           "settings page\n" );
            RemoveExtraVideoPanel( pPlayer );
            return;
        }
    }

    bExtraVideoPanelFocused = false;
    showExtraVideoPanelFocus();

    for ( int32_t i = 0; i < kNumExtraVideoRows; i++ ) {
        const ExtraVideoRowDesc &desc = kExtraVideoRows[i];
        const int32_t rowIndex = kNumRetailVideoRows + 1 + i;

        snprintf( path, sizeof( path ), "/:System_m_%d", rowIndex );
        pPlayer->setVariableValueNonLinked( path, desc.pLabel );
        snprintf( path, sizeof( path ), "/:System_help%d", rowIndex );
        pPlayer->setVariableValueNonLinked( path, "apply" );

        if ( desc.bCheckbox ) {
            snprintf( path, sizeof( path ), "/:System_help%d", rowIndex );
            pPlayer->setVariableValueNonLinked( path, "apply_checkbox" );
            snprintf( path, sizeof( path ), "%s/sur%d:s_name", kVideoMenuPath,
                      rowIndex );
            pPlayer->setVariableValueNonLinked( path, desc.pLabel );

            snprintf( path, sizeof( path ), "%s/sur%d", kVideoMenuPath,
                      rowIndex );
            pPlayer->showFrame( bVSyncEnabled ? "set_on" : "off_lock", path,
                                false );
            continue;
        }

        snprintf( path, sizeof( path ), "%s/sur%d:param_name", kVideoMenuPath,
                  rowIndex );
        pPlayer->setVariableValueNonLinked( path, desc.pLabel );
        snprintf( path, sizeof( path ), "%s/sur%d:msg1", kVideoMenuPath,
                  rowIndex );
        pPlayer->setVariableValueNonLinked( path, desc.pPreviousMessage );
        snprintf( path, sizeof( path ), "%s/sur%d:msg2", kVideoMenuPath,
                  rowIndex );
        pPlayer->setVariableValueNonLinked( path, desc.pNextMessage );

        showExtraVideoRowValue( i, false );

        snprintf( path, sizeof( path ), "%s/sur%d", kVideoMenuPath, rowIndex );
        pPlayer->showFrame( "set", path, false );
    }

    snprintf( path, sizeof( path ), "%s:nb_ssms_max", kVideoMenuPath );
    pPlayer->setVariableValue( path, kNumRetailVideoRows + kNumExtraVideoRows );

    showVideoModeRowsLock();
}

void MngOptions::updateExtraVideoRowsHover() {
    int32_t hoveredRow = -1;
    for ( int32_t i = 0; i < kNumExtraVideoRows; i++ ) {
        const int32_t rowIndex = kNumRetailVideoRows + 1 + i;
        if ( isMouseOverVideoRow( rowIndex ) ) {
            hoveredRow = rowIndex;
            break;
        }
    }

    const bool bMouseDown = gEdFlash.Input.bMouseButton0Down;
    const bool bClicked = bMouseDown && !bExtraVideoMouseDown;
    bExtraVideoMouseDown = bMouseDown;
    if ( bClicked && hoveredRow > 0 &&
         kExtraVideoRows[hoveredRow - kNumRetailVideoRows - 1].bCheckbox ) {
        toggleVSyncCheckbox( hoveredRow );
    }

    if ( hoveredRow == hoveredExtraVideoRow ) {
        return;
    }

    hoveredExtraVideoRow = hoveredRow;
    if ( hoveredRow < 0 ) {
        return;
    }

    char path[64];
    snprintf( path, sizeof( path ), "%s:num_ssm", kVideoMenuPath );
    if ( pPlayer->getVariableAsInt( path ) != hoveredRow ) {
        selectVideoRow( hoveredRow, true );
    }
}

bool MngOptions::isMouseOverVideoRow( const int32_t rowIndex ) {
    char path[64];
    snprintf( path, sizeof( path ), "%s/sur%d", kVideoMenuPath, rowIndex );

    FlashClip clip;
    if ( !pPlayer->getClipPlayer( path, &clip ) ) {
        return false;
    }

    const float mouseX = gEdFlash.Input.MouseCoordinates[0];
    const float mouseY = gEdFlash.Input.MouseCoordinates[1];

    FlashRECT viewport;
    clip.getPropertyViewport( eFlashProperty::FP_Viewport, viewport );
    return viewport.X <= mouseX && mouseX <= viewport.X + viewport.Width &&
           viewport.Y <= mouseY && mouseY <= viewport.Y + viewport.Height;
}

bool MngOptions::isVideoRowSelectable( const int32_t rowIndex ) {
    switch ( rowIndex ) {
    case kResolutionRow:
    case kRefreshRateRow:
        return !isVideoModeLocked();
    case kAntiAliasingRow:
        return pPlayer->getVariableAsInt( "/:IsAAAvailable" ) == 1;
    case kHDRRow:
        return pPlayer->getVariableAsInt( "/:IsHDRAvailable" ) == 1;
    default:
        return true;
    }
}

void MngOptions::skipLockedVideoRow( const int32_t focusedRow,
                                     const int32_t previousRow ) {
    if ( isVideoRowSelectable( focusedRow ) ) {
        return;
    }

    if ( previousRow >= 1 && isVideoRowSelectable( previousRow ) &&
         isMouseOverVideoRow( focusedRow ) ) {
        selectVideoRow( previousRow, false );
        return;
    }

    char path[64];
    snprintf( path, sizeof( path ), "%s:nb_ssms_max", kVideoMenuPath );
    const int32_t numRows = pPlayer->getVariableAsInt( path );
    if ( numRows < 1 ) {
        return;
    }

    const bool bDown = ( previousRow < 1 ) ||
                       ( focusedRow == previousRow + 1 ) ||
                       ( previousRow == numRows && focusedRow == 1 );

    int32_t rowIndex = focusedRow;
    for ( int32_t i = 0; i < numRows; i++ ) {
        rowIndex = bDown ? ( rowIndex % numRows ) + 1
                         : ( ( rowIndex + numRows - 2 ) % numRows ) + 1;
        if ( isVideoRowSelectable( rowIndex ) ) {
            selectVideoRow( rowIndex, false );
            return;
        }
    }
}

void MngOptions::selectVideoRow( const int32_t rowIndex,
                                 const bool bPlaySound ) {
    char path[64];

    disableAllSliders();
    if ( bPlaySound ) {
        gpFlash->getFlashSound()->playFrontEndSound( "move", 0, false );
    }

    snprintf( path, sizeof( path ), "%s/contlum:opened", kVideoMenuPath );
    if ( pPlayer->getVariableAsInt( path ) != 0 ) {
        snprintf( path, sizeof( path ), "%s/contlum", kVideoMenuPath );
        pPlayer->showFrame( "out", path, true );
    }

    snprintf( path, sizeof( path ), "%s:nb_ssms_max", kVideoMenuPath );
    const int32_t numRows = pPlayer->getVariableAsInt( path );
    for ( int32_t i = 1; i <= numRows; i++ ) {
        snprintf( path, sizeof( path ), "%s/sur%d:display", kVideoMenuPath, i );
        const bool bDisplayed = pPlayer->getVariableAsInt( path ) == 1;

        snprintf( path, sizeof( path ), "%s/sur%d", kVideoMenuPath, i );
        pPlayer->showFrame( bDisplayed ? "off" : "off_lock", path, false );
        snprintf( path, sizeof( path ), "%s/sur%d/title", kVideoMenuPath, i );
        pPlayer->showFrame( "default", path, false );
        snprintf( path, sizeof( path ), "%s/sur%d/cart", kVideoMenuPath, i );
        pPlayer->showFrame( "default", path, false );
    }

    snprintf( path, sizeof( path ), "%s:num_ssm", kVideoMenuPath );
    pPlayer->setVariableValue( path, rowIndex );

    char displayPath[64];
    snprintf( displayPath, sizeof( displayPath ), "%s/sur%d:display",
              kVideoMenuPath, rowIndex );
    const bool bDisplayed = pPlayer->getVariableAsInt( displayPath ) == 1;

    const int32_t extraRow = rowIndex - kNumRetailVideoRows - 1;
    snprintf( path, sizeof( path ), "%s/sur%d", kVideoMenuPath, rowIndex );
    if ( rowIndex == kHDRRow ||
         ( extraRow >= 0 && kExtraVideoRows[extraRow].bCheckbox ) ) {
        pPlayer->showFrame( bDisplayed ? "change" : "change_lock", path, true );
    } else {
        if ( rowIndex == kGammaRow ) {
            snprintf( path, sizeof( path ), "%s/contlum", kVideoMenuPath );
            pPlayer->showFrame( "in", path, true );
            snprintf( path, sizeof( path ), "%s/sur%d", kVideoMenuPath,
                      rowIndex );
        }
        pPlayer->showFrame( bDisplayed ? "in2" : "lock", path, true );
    }
    focusedVideoRow = rowIndex;

    char help[64] = {};
    snprintf( path, sizeof( path ), "/:System_help%d", rowIndex );
    if ( pPlayer->getVariableAsString( path, help ) ) {
        pPlayer->setVariableValueNonLinked( "/:WhatHelp", help );
        pPlayer->showFrame( help, "/help", true );
    }
}

void MngOptions::updateCheckboxRowsOrientation() {
    for ( int32_t i = 0; i < kNumExtraVideoRows; i++ ) {
        if ( !kExtraVideoRows[i].bCheckbox ) {
            continue;
        }

        char rowPath[64];
        snprintf( rowPath, sizeof( rowPath ), "%s/sur%d", kVideoMenuPath,
                  kNumRetailVideoRows + 1 + i );
        for ( const CheckboxRowChild &child : kCheckboxRowUnmirroredChildren ) {
            float matrix[6];
            if ( !pPlayer->getObjectMatrix( rowPath, child.Depth, matrix ) ||
                 matrix[0] < 0.0f ) {
                continue;
            }

            matrix[4] += matrix[0] * ( child.MinX + child.MaxX );
            matrix[0] = -matrix[0];
            pPlayer->setObjectMatrix( rowPath, child.Depth, matrix );
        }
    }
}

void MngOptions::highlightCheckboxRow( const int32_t rowIndex ) {
    const int32_t row = rowIndex - kNumRetailVideoRows - 1;
    if ( row < 0 || row >= kNumExtraVideoRows ||
         !kExtraVideoRows[row].bCheckbox ) {
        return;
    }

    char path[64];
    snprintf( path, sizeof( path ), "%s/sur%d:display", kVideoMenuPath,
              rowIndex );
    const bool bChecked = pPlayer->getVariableAsInt( path ) == 1;

    snprintf( path, sizeof( path ), "%s/sur%d", kVideoMenuPath, rowIndex );
    pPlayer->showFrame( bChecked ? "change" : "change_lock", path, true );
}

void MngOptions::toggleVSyncCheckbox( const int32_t rowIndex ) {
    char path[64];
    snprintf( path, sizeof( path ), "%s/sur%d:display", kVideoMenuPath,
              rowIndex );
    const bool bChecked = pPlayer->getVariableAsInt( path ) == 1;

    snprintf( path, sizeof( path ), "%s/sur%d", kVideoMenuPath, rowIndex );
    pPlayer->showFrame( bChecked ? "disable" : "enable", path, true );

    bVSyncEnabled = !bChecked;
}

bool MngOptions::onVideoPageInvalidAction() {
    if ( pPlayer == nullptr ||
         extraVideoRowsState != eExtraVideoRowsState::EVRS_Open ) {
        return false;
    }

    char path[64];
    snprintf( path, sizeof( path ), "%s:num_ssm", kVideoMenuPath );
    const int32_t rowIndex = pPlayer->getVariableAsInt( path );
    const int32_t row = rowIndex - kNumRetailVideoRows - 1;
    if ( row < 0 || row >= kNumExtraVideoRows ||
         !kExtraVideoRows[row].bCheckbox ) {
        return false;
    }

    toggleVSyncCheckbox( rowIndex );
    return true;
}

bool MngOptions::isVideoModeLocked() const {
    return displayMode == eDisplayMode::DM_Borderless;
}

void MngOptions::showVideoModeRowsLock() {
    static constexpr int32_t kVideoModeRows[] = { kResolutionRow,
                                                  kRefreshRateRow };

    if ( pPlayer == nullptr ||
         extraVideoRowsState == eExtraVideoRowsState::EVRS_Closed ) {
        return;
    }

    char path[64];
    snprintf( path, sizeof( path ), "%s:num_ssm", kVideoMenuPath );
    const int32_t focusedRow = pPlayer->getVariableAsInt( path );

    const bool bLocked = isVideoModeLocked();
    for ( const int32_t rowIndex : kVideoModeRows ) {
        snprintf( path, sizeof( path ), "%s/sur%d", kVideoMenuPath, rowIndex );
        if ( !bLocked || rowIndex != focusedRow ) {
            pPlayer->showFrame( bLocked ? "lock" : "set", path, false );
        }
        pPlayer->setClipProperty( path, eFlashProperty::FP_Alpha,
                                  bLocked ? 20.0f : 100.0f );
    }
}

void MngOptions::showExtraVideoPanelFocus() {
    const CXForm untinted = {};
    for ( int32_t i = 0; i < kNumVideoPanelPieces; i++ ) {
        if ( !bVideoPanelPieceTinted[i] ) {
            continue;
        }

        const CXForm &tinted = videoPanelPieceTints[i];
        pPlayer->setObjectColorTransform( kVideoMenuPath, kVideoPanelDepths[i],
                                          bExtraVideoPanelFocused ? untinted
                                                                  : tinted );
        pPlayer->setObjectColorTransform(
            kVideoMenuPath, uint16_t( kFirstExtraPanelDepth + i ),
            bExtraVideoPanelFocused ? tinted : untinted );
    }
}

void MngOptions::showExtraVideoRowValue( const int32_t row,
                                         const bool bHighlighted ) {
    if ( pPlayer == nullptr ) {
        return;
    }

    char label[32] = {};
    switch ( row ) {
    case EVR_DisplayMode:
        strncpy( label, GetDisplayModeLabel( displayMode ),
                 sizeof( label ) - 1 );
        break;
    case EVR_FrameRateCap: {
        const int32_t maxFrameRate = GetMaxFrameRate();
        if ( maxFrameRate == 0 ) {
            strncpy( label, "UNLIMITED", sizeof( label ) - 1 );
        } else {
            snprintf( label, sizeof( label ), "%d FPS", maxFrameRate );
        }
        break;
    }
    case EVR_Reflections:
        strncpy( label, gbDebugEnvMapDenseReflection ? "HIGH" : "DEFAULT",
                 sizeof( label ) - 1 );
        break;
    default:
        return;
    }

    char path[64];
    snprintf( path, sizeof( path ), "%s/sur%d:param", kVideoMenuPath,
              kNumRetailVideoRows + 1 + row );
    pPlayer->setVariableValueNonLinked( path, label );
    if ( bHighlighted ) {
        pPlayer->setVariableValueNonLinked( "/:NameTemp", label );
    }
}

void MngOptions::showNextFrameRateCap( const int32_t direction ) {
    const int32_t numCaps = int32_t( std::size( kFrameRateCaps ) );
    const int32_t index =
        ( GetFrameRateCapIndex() + direction + numCaps ) % numCaps;

    SetMaxFrameRate( kFrameRateCaps[index] );
    showExtraVideoRowValue( EVR_FrameRateCap, true );
    OpenTDUSettings::Save();
}

void MngOptions::toggleDenseReflections() {
    gbDebugEnvMapDenseReflection = !gbDebugEnvMapDenseReflection;
    showExtraVideoRowValue( EVR_Reflections, true );
    OpenTDUSettings::Save();
}

void MngOptions::showNextDisplayMode() {
    displayMode = eDisplayMode( ( int32_t( displayMode ) + 1 ) %
                                int32_t( eDisplayMode::DM_Count ) );
    showExtraVideoRowValue( EVR_DisplayMode, true );
    showVideoModeRowsLock();
}

void MngOptions::showPreviousDisplayMode() {
    const int32_t numModes = int32_t( eDisplayMode::DM_Count );
    displayMode =
        eDisplayMode( ( int32_t( displayMode ) + numModes - 1 ) % numModes );
    showExtraVideoRowValue( EVR_DisplayMode, true );
    showVideoModeRowsLock();
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
        ( refreshRate == hz ) && ( activeDisplayMode == displayMode ) &&
        ( activeAntiAliasingMode == antiAliasingMode ) &&
        ( bActiveHDREnabled == bHDREnabled ) &&
        ( bActiveVSyncEnabled == bVSyncEnabled );
    if ( bUnchanged ) {
        FUN_006c59d0();
        pPlayer->showFrame( "valid_apply", "/menus/ss_menus/choice", true );
        return;
    }

    ApplyDisplayMode( displayMode );
    ApplyVSync( bVSyncEnabled );

    if ( gpRender != nullptr &&
         gpRender->resize( width, height, hz, antiAliasingMode,
                           bHDREnabled ) ) {
        if ( FlashPlayer *pGeneral = gpFlash->getFlashPlayer( "GENERAL" ) ) {
            pGeneral->setVariableValue( "/:IsAvertTimeOut", int32_t( 1 ) );
            pGeneral->setVariableValue( "/:avert_time_out",
                                        int32_t( kApplyTimeoutInSeconds ) );
        }

        TestDrive::DisplayModalPrompt(
            true, kKeepVideoSettingsHashcode, 0, nullptr, &videoApplyCallback,
            this, 0, nullptr, true, false, MPT_Warning, gpFlashMessageBox );
        return;
    }

    ApplyDisplayMode( activeDisplayMode );
    ApplyVSync( bActiveVSyncEnabled );

    if ( gpRender != nullptr &&
         gpRender->resize( videoWidth, videoHeight, refreshRate,
                           activeAntiAliasingMode, bActiveHDREnabled ) ) {
        TestDrive::DisplayModalPrompt( false, kVideoSettingsRevertedHashcode, 0,
                                       nullptr, &videoRevertedCallback, this, 0,
                                       nullptr, true, false, MPT_Warning,
                                       gpFlashMessageBox );
        return;
    }

    OTDU_LOG_ERROR( "Failed to apply the video settings (%ux%u@%uHz)\n", width,
                    height, hz );
}

void MngOptions::VideoRevertedCallback::onClose( ePopupAnswer,
                                                 void *,
                                                 int32_t ) {
    // FUN_006c5c10
    pOwner->FUN_006c5c10();
}

void MngOptions::VideoApplyCallback::onClose( ePopupAnswer answer,
                                              void *,
                                              int32_t ) {
    // FUN_006c60c0
    pOwner->FUN_006c60c0( int32_t( answer ) );
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
