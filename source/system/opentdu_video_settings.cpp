#include "shared.h"
#include "opentdu_video_settings.h"

#include "database/port_strings.h"
#include "flash/implementation/flash_player.h"
#include "gs_timer.h"
#include "player_data/gs_playerdata.h"
#include "player_data/opentdu_settings.h"
#include "render/ed_render.h"
#include "render/gs_render.h"
#include "render/mng_env_map.h"
#include "render/postfx/effects/postfx_hdri.h"
#include "world/otdu_lights.h"

static constexpr int32_t kFrameRateCaps[] = { 0, 30, 60, 120, 144, 240 };
// kAutoHDRPeakNits first
static constexpr float kHDRPeaks[] = { kAutoHDRPeakNits, 400.0f,  600.0f,
                                       800.0f,           1000.0f, 1500.0f,
                                       2000.0f,          4000.0f, 10000.0f };
static constexpr float kHDRPaperWhiteStep = 10.0f;
static constexpr float kHDRHighlightsStep = 0.05f;
// GSRender::setGamma keeps a tenth
static constexpr float kGammaStep = 0.1f;

static int32_t Wrap( const int32_t value, const int32_t count ) {
    return ( value % count + count ) % count;
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

static std::string FormatNits( const float nits ) {
    char text[32];
    snprintf( text, sizeof( text ), "%.0f NITS", nits );
    return text;
}

static RenderDevice *GetDevice() { return gEdRender.pRenderDevice; }

static float RoundToStep( const float value, const float step ) {
    return std::round( value / step ) * step;
}

static void SetGamma( const float gamma ) {
    gpPlayerData->getLocalPlayerData().Gamma =
        Clamp( RoundToStep( gamma, kGammaStep ), 0.0f, 1.0f );
    gpRender->setGamma( gpPlayerData->getGamma() );
}

// Next entry of 'values' (sorted) after or before 'value', which may sit
// between two entries
static float StepValue( const float *pValues,
                        const int32_t numValues,
                        const float value,
                        const int32_t direction ) {
    if ( direction > 0 ) {
        for ( int32_t i = 0; i < numValues; i++ ) {
            if ( pValues[i] > value ) {
                return pValues[i];
            }
        }
        return pValues[0];
    }

    for ( int32_t i = numValues - 1; i >= 0; i-- ) {
        if ( pValues[i] < value ) {
            return pValues[i];
        }
    }
    return pValues[numValues - 1];
}

bool OpenTDUVideoSettings::ImmediateSettings::operator==(
    const ImmediateSettings &other ) const {
    return MaxFrameRate == other.MaxFrameRate &&
           bDenseReflections == other.bDenseReflections &&
           NightLights == other.NightLights && LODQuality == other.LODQuality &&
           Gamma == other.Gamma && HDRPaperWhite == other.HDRPaperWhite &&
           HDRPeak == other.HDRPeak && HDRHighlights == other.HDRHighlights &&
           bSoftHighlights == other.bSoftHighlights;
}

OpenTDUVideoSettings::OpenTDUVideoSettings()
    : displayMode( eDisplayMode::DM_Windowed ),
      bVSyncEnabled( false ),
      bHDRDisplayEnabled( false ) {}

OpenTDUVideoSettings::ImmediateSettings
OpenTDUVideoSettings::CaptureImmediateSettings() {
    ImmediateSettings settings;
    settings.MaxFrameRate = GetMaxFrameRate();
    settings.bDenseReflections = gbDebugEnvMapDenseReflection;
    settings.NightLights = int32_t( GetOTDUGlowQuality() );
    settings.LODQuality = gpRender->getLODQuality();
    settings.Gamma = gpPlayerData->getGamma();
    settings.HDRPaperWhite = GetDevice()->getHDRPaperWhiteNits();
    settings.HDRPeak = GetDevice()->getHDRPeakNits();
    settings.HDRHighlights = GetDevice()->getHDRHighlightStrength();
    settings.bSoftHighlights = gbSoftHighlights;
    return settings;
}

void OpenTDUVideoSettings::RestoreImmediateSettings(
    const ImmediateSettings &settings ) {
    SetMaxFrameRate( settings.MaxFrameRate );
    gbDebugEnvMapDenseReflection = settings.bDenseReflections;
    SetOTDUGlowQuality( eOTDUGlowQuality( settings.NightLights ) );
    gpRender->setLODQuality( settings.LODQuality );
    SetGamma( settings.Gamma );
    GetDevice()->setHDROutput( GetDevice()->isHDROutputEnabled(),
                               settings.HDRPaperWhite );
    GetDevice()->setHDRPeakNits( settings.HDRPeak );
    GetDevice()->setHDRHighlightStrength( settings.HDRHighlights );
    gbSoftHighlights = settings.bSoftHighlights;
}

std::vector<OptionsTab>
OpenTDUVideoSettings::buildTabs( FlashPlayer *pPlayer ) {
    static constexpr ePortString kQualityLabels[GQ_Count] = {
        PS_Low, PS_Default, PS_High, PS_Ultra };

    // Retail labels, localized on the movie root
    auto movieLabel = [pPlayer]( const char *pVariable ) {
        return [pPlayer, pVariable]() {
            char label[64] = {};
            pPlayer->getVariableAsString( pVariable, label );
            return std::string( label );
        };
    };
    auto portLabel = []( const ePortString label ) {
        return [label]() { return std::string( GetPortString( label ) ); };
    };
    auto saveImmediate = []() { OpenTDUSettings::Save(); };
    auto isHDRSupported = []() { return IsHDRDisplaySupported(); };

    std::vector<OptionsTab> tabs( 3 );
    tabs[0].Label = movieLabel( "/:m_Display" )();
    tabs[1].Label = GetPortString( PS_Quality );
    tabs[2].Label = GetPortString( PS_HDR );

    OptionsRow row;

    // DISPLAY
    row = OptionsRow();
    row.Type = eOptionsRowType::ORT_Arrows;
    row.GetLabel = portLabel( PS_DisplayMode );
    row.GetValue = [this]() { return GetDisplayModeLabel( displayMode ); };
    row.Step = [this]( int32_t direction ) {
        displayMode = eDisplayMode( Wrap( int32_t( displayMode ) + direction,
                                          int32_t( eDisplayMode::DM_Count ) ) );
    };
    tabs[0].Rows.push_back( row );

    // The desktop mode is used in borderless
    auto isVideoModeEditable = [this]() {
        return displayMode != eDisplayMode::DM_Borderless;
    };

    row = OptionsRow();
    row.Type = eOptionsRowType::ORT_Arrows;
    row.GetLabel = movieLabel( "/:mpc_Resolution" );
    row.GetValue = []() { return gpMngOptions->getResolutionLabel(); };
    row.Step = []( int32_t direction ) {
        gpMngOptions->stepResolution( direction );
    };
    row.IsEnabled = isVideoModeEditable;
    tabs[0].Rows.push_back( row );

    row = OptionsRow();
    row.Type = eOptionsRowType::ORT_Arrows;
    row.GetLabel = movieLabel( "/:mpc_Frequency" );
    row.GetValue = []() { return gpMngOptions->getRefreshRateLabel(); };
    row.Step = []( int32_t direction ) {
        gpMngOptions->stepRefreshRate( direction );
    };
    row.IsEnabled = isVideoModeEditable;
    tabs[0].Rows.push_back( row );

    row = OptionsRow();
    row.Type = eOptionsRowType::ORT_Checkbox;
    row.GetLabel = portLabel( PS_VSync );
    row.IsChecked = [this]() { return bVSyncEnabled; };
    row.Step = [this]( int32_t ) { bVSyncEnabled = !bVSyncEnabled; };
    tabs[0].Rows.push_back( row );

    row = OptionsRow();
    row.Type = eOptionsRowType::ORT_Arrows;
    row.GetLabel = portLabel( PS_FrameRate );
    row.GetValue = []() -> std::string {
        if ( GetMaxFrameRate() == 0 ) {
            return GetPortString( PS_Unlimited );
        }
        return std::to_string( GetMaxFrameRate() ) + " FPS";
    };
    row.Step = [saveImmediate]( int32_t direction ) {
        SetMaxFrameRate(
            kFrameRateCaps[Wrap( GetFrameRateCapIndex() + direction,
                                 int32_t( std::size( kFrameRateCaps ) ) )] );
        saveImmediate();
    };
    tabs[0].Rows.push_back( row );

    // QUALITY
    row = OptionsRow();
    row.Type = eOptionsRowType::ORT_Arrows;
    row.GetLabel = movieLabel( "/:mpc_Antialiasing" );
    row.GetValue = []() -> std::string {
        const char *pLabel = gpMngOptions->getAntiAliasingLabel();
        return ( pLabel != nullptr ) ? pLabel : "";
    };
    row.Step = []( int32_t direction ) {
        gpMngOptions->stepAntiAliasing( direction );
    };
    tabs[1].Rows.push_back( row );

    row = OptionsRow();
    row.Type = eOptionsRowType::ORT_Checkbox;
    row.GetLabel = movieLabel( "/:mpc_HDR" );
    row.IsChecked = []() { return gpMngOptions->isHDRRenderingEnabled(); };
    row.Step = []( int32_t ) { gpMngOptions->toggleHDRRendering(); };
    tabs[1].Rows.push_back( row );

    row = OptionsRow();
    row.Type = eOptionsRowType::ORT_Gauge;
    row.GetLabel = movieLabel( "/:m_Luminosity" );
    row.GetRatio = []() { return gpPlayerData->getGamma(); };
    row.Step = []( int32_t direction ) {
        SetGamma( gpPlayerData->getGamma() + float( direction ) * kGammaStep );
    };
    row.SetRatio = []( float ratio ) { SetGamma( ratio ); };
    tabs[1].Rows.push_back( row );

    row = OptionsRow();
    row.Type = eOptionsRowType::ORT_Arrows;
    row.GetLabel = movieLabel( "/:mpc_LevelOfDetails" );
    row.GetValue = []() { return gpMngOptions->getLODQualityLabel(); };
    row.Step = []( int32_t direction ) {
        gpMngOptions->stepLODQuality( direction );
    };
    tabs[1].Rows.push_back( row );

    row = OptionsRow();
    row.Type = eOptionsRowType::ORT_Arrows;
    row.GetLabel = portLabel( PS_Reflections );
    row.GetValue = []() -> std::string {
        return GetPortString( gbDebugEnvMapDenseReflection ? PS_High
                                                           : PS_Default );
    };
    row.Step = [saveImmediate]( int32_t ) {
        gbDebugEnvMapDenseReflection = !gbDebugEnvMapDenseReflection;
        saveImmediate();
    };
    tabs[1].Rows.push_back( row );

    row = OptionsRow();
    row.Type = eOptionsRowType::ORT_Arrows;
    row.GetLabel = portLabel( PS_NightLights );
    row.GetValue = []() -> std::string {
        return GetPortString( kQualityLabels[GetOTDUGlowQuality()] );
    };
    row.Step = [saveImmediate]( int32_t direction ) {
        SetOTDUGlowQuality( eOTDUGlowQuality(
            Wrap( int32_t( GetOTDUGlowQuality() ) + direction, GQ_Count ) ) );
        saveImmediate();
    };
    tabs[1].Rows.push_back( row );

    // HDR
    row = OptionsRow();
    row.Type = eOptionsRowType::ORT_Checkbox;
    row.GetLabel = portLabel( PS_HDRDisplay );
    row.IsChecked = [this]() { return bHDRDisplayEnabled; };
    row.Step = [this]( int32_t ) { bHDRDisplayEnabled = !bHDRDisplayEnabled; };
    row.IsEnabled = isHDRSupported;
    tabs[2].Rows.push_back( row );

    row = OptionsRow();
    row.Type = eOptionsRowType::ORT_Gauge;
    row.GetLabel = []() {
        return std::string( GetPortString( PS_HDRPaperWhite ) ) + "  " +
               FormatNits( GetDevice()->getHDRPaperWhiteNits() );
    };
    row.GetRatio = []() {
        return ( GetDevice()->getHDRPaperWhiteNits() - kMinHDRPaperWhiteNits ) /
               ( kMaxHDRPaperWhiteNits - kMinHDRPaperWhiteNits );
    };
    auto setPaperWhite = [saveImmediate]( const float nits ) {
        GetDevice()->setHDROutput(
            GetDevice()->isHDROutputEnabled(),
            Clamp( RoundToStep( nits, kHDRPaperWhiteStep ),
                   kMinHDRPaperWhiteNits, kMaxHDRPaperWhiteNits ) );
        saveImmediate();
    };
    row.Step = [setPaperWhite]( int32_t direction ) {
        setPaperWhite( GetDevice()->getHDRPaperWhiteNits() +
                       float( direction ) * kHDRPaperWhiteStep );
    };
    row.SetRatio = [setPaperWhite]( float ratio ) {
        setPaperWhite( kMinHDRPaperWhiteNits +
                       ratio *
                           ( kMaxHDRPaperWhiteNits - kMinHDRPaperWhiteNits ) );
    };
    row.IsEnabled = isHDRSupported;
    tabs[2].Rows.push_back( row );

    row = OptionsRow();
    row.Type = eOptionsRowType::ORT_Arrows;
    row.GetLabel = portLabel( PS_HDRPeakBrightness );
    row.GetValue = []() -> std::string {
        if ( GetDevice()->isHDRPeakAuto() ) {
            return GetPortString( PS_Auto );
        }
        return FormatNits( GetDevice()->getHDRPeakNits() );
    };
    row.Step = [saveImmediate]( int32_t direction ) {
        GetDevice()->setHDRPeakNits(
            StepValue( kHDRPeaks, int32_t( std::size( kHDRPeaks ) ),
                       GetDevice()->getHDRPeakNits(), direction ) );
        saveImmediate();
    };
    row.IsEnabled = isHDRSupported;
    tabs[2].Rows.push_back( row );

    row = OptionsRow();
    row.Type = eOptionsRowType::ORT_Gauge;
    row.GetLabel = []() {
        char value[16];
        snprintf( value, sizeof( value ), "  %.2f",
                  GetDevice()->getHDRHighlightStrength() );
        return std::string( GetPortString( PS_HDRHighlights ) ) + value;
    };
    row.GetRatio = []() {
        return GetDevice()->getHDRHighlightStrength() /
               kMaxHDRHighlightStrength;
    };
    auto setHighlights = [saveImmediate]( const float strength ) {
        GetDevice()->setHDRHighlightStrength(
            Clamp( RoundToStep( strength, kHDRHighlightsStep ), 0.0f,
                   kMaxHDRHighlightStrength ) );
        saveImmediate();
    };
    row.Step = [setHighlights]( int32_t direction ) {
        setHighlights( GetDevice()->getHDRHighlightStrength() +
                       float( direction ) * kHDRHighlightsStep );
    };
    row.SetRatio = [setHighlights]( float ratio ) {
        setHighlights( ratio * kMaxHDRHighlightStrength );
    };
    row.IsEnabled = isHDRSupported;
    tabs[2].Rows.push_back( row );

    row = OptionsRow();
    row.Type = eOptionsRowType::ORT_Checkbox;
    row.GetLabel = portLabel( PS_SoftHighlights );
    row.IsChecked = []() { return gbSoftHighlights; };
    row.Step = [saveImmediate]( int32_t ) {
        gbSoftHighlights = !gbSoftHighlights;
        saveImmediate();
    };
    tabs[2].Rows.push_back( row );

    return tabs;
}

void OpenTDUVideoSettings::open( OptionsWidgetPage &page,
                                 FlashPlayer *pPlayer ) {
    gpMngOptions->beginVideoEdit();
    displayMode = GetActiveDisplayMode();
    bVSyncEnabled = gpRender->getVSyncMode() != eVSyncMode::VSM_Immediate;
    bHDRDisplayEnabled = GetDevice()->isHDROutputEnabled();
    openedSettings = CaptureImmediateSettings();

    page.open(
        buildTabs( pPlayer ), [this]() { confirm(); }, [this]() { cancel(); } );
}

void OpenTDUVideoSettings::confirm() {
    gpMngOptions->applyVideoEdit( displayMode, bVSyncEnabled,
                                  bHDRDisplayEnabled );
}

void OpenTDUVideoSettings::cancel() {
    if ( CaptureImmediateSettings() == openedSettings ) {
        return;
    }

    RestoreImmediateSettings( openedSettings );
    OpenTDUSettings::Save();
}
