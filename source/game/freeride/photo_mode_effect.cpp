#include "shared.h"
#include "photo_mode_effect.h"

#include "flash/entities/flash_entity.h"
#include "render/camera/camera_game.h"
#include "render/camera/camera_showcase.h"
#include "render/camera/gs_camera.h"
#include "render/postfx/effects/postfx_desaturate.h"
#include "render/postfx/effects/postfx_hdri.h"
#include "render/postfx/effects/postfx_radial_blur.h"
#include "flash/implementation/flash_player.h"
#include <cmath>
#include "flash/mng_flash_localize.h"
#include "render/postfx/gs_postfx.h"
#include "world/weather/gs_weather.h"
#include "game/scenaric/gs_scenaric.h"
#include "game/scenaric/entity/entity_driver.h"
#include "game/vehicle/car_state.h"
#include <cstdio>

static constexpr const char *kSliderVarNames[9] = { // DAT_00f9c150
    "/:PM_FOV",      "/:PM_RadialBlur", "/:PM_Saturation", "/:null", "/:null",
    "/:PM_Contrast", "/:null",          "/:null",          "/:null" };

static constexpr int32_t kPhotoCameraMode = 6;

static constexpr uint64_t kFilterNameHashcodes[3] = {
    0x03a8ac52ull, 0x03c66d82ull, 0x03b72b42ull }; // DAT_00f9c178
// Indexed by todSeconds * 4/86400
static constexpr const char *kWeatherLabels[4] = { "A", "B", "C",
                                                   "D" };   // DAT_00f9c190
static constexpr float kWeatherLabelScale = 4.6296296e-05f; // DAT_00f420f8

static constexpr uint64_t kAvatarShownHashcode = 0x033020f2ull;
static constexpr uint64_t kAvatarHiddenHashcode = 0x03b7ee92ull;

static constexpr const char *kGaugeClipFormat =
    "menus/frame/frame/popup/gauge_%d/slider";  // DAT_00f3ce68
static constexpr float kGaugeLastFrame = 15.0f; // FLOAT_00f41700
static constexpr const char *kSaturationLockedVarName = "/:IsSaturationLocked";
static constexpr const char *kFilterRowParamVarName =
    "/menus/frame/frame/popup/gauge_7:param";
static constexpr const char *kTempNameVarName = "/:NameTemp";
static constexpr const char *kPopupClipPath = "/menus/frame/frame/popup";
static constexpr const char *kLockFrameName = "lock_delock";

enum eSlider : int32_t {
    PMS_FOV = 0,
    PMS_RadialBlur = 1,
    PMS_Saturation = 2,
    PMS_Unused3 = 3,
    PMS_TimeOfDay = 4,
    PMS_Contrast = 5,
    PMS_RedFactor = 6,
    PMS_GreenFactor = 7,
    PMS_BlueFactor = 8,
};

enum eTimeStep : uint32_t {
    TS_Weather = 1,
    TS_Hour = 2,
};

static constexpr int32_t kNumFilterPresets = 3;
static constexpr float kWeatherBlockSeconds = 21600.0f; // DAT_00f420f4
static constexpr float kHourStepSeconds = 900.0f;       // DAT_00f41c10
static constexpr float kHourDisplayScale = 2.0f;        // DAT_00f416b0
static constexpr float kHourDisplayOffset = 25200.0f;   // DAT_00f420f0

PostFXNodeDesaturate *GetLiveDesaturateNode() {
    if ( gpPostFX == nullptr ) {
        return nullptr;
    }
    PostFXStep *pStep = gpPostFX->getEffect<ePostFXStep::PFXS_Desaturation>();
    if ( pStep == nullptr || !pStep->getInput()->isEnabled() ) {
        return nullptr;
    }
    return ( (PostFXStepDesaturate *)pStep )->pNode;
}

PostFXNodeHDR *GetLiveHDRNode() {
    if ( gpPostFX == nullptr ) {
        return nullptr;
    }
    PostFXStep *pStep = gpPostFX->getEffect<ePostFXStep::PFXS_HDRI>();
    if ( pStep == nullptr || !pStep->getInput()->isEnabled() ) {
        return nullptr;
    }
    return ( (PostFXStepHDRI *)pStep )->getHDRNode();
}

PostFXNodeRadialBlur *GetRadialBlurNode() {
    if ( gpPostFX == nullptr ) {
        return nullptr;
    }
    PostFXStep *pStep = gpPostFX->getEffect<ePostFXStep::PFXS_RadialBlur>();
    if ( pStep == nullptr ) {
        return nullptr;
    }
    return (PostFXNodeRadialBlur *)pStep->getInput();
}

const char *FormatTimeOfDay( const TimeOfDay &time ) {
    // FUN_007d0b80
    static char pFormatted[32]; // DAT_016a6a24
    snprintf( pFormatted, sizeof( pFormatted ), "%02d:%02d:%02d",
              (int32_t)floorf( time.Hours ), (int32_t)floorf( time.Minutes ),
              (int32_t)floorf( time.Seconds ) );
    return pFormatted;
}

EntityDriver *GetShotDriver() {
    CarState *pCarState = ( gpScenaric != nullptr )
                              ? gpScenaric->getLocalPlayer().getCarState()
                              : nullptr;
    return ( pCarState != nullptr ) ? pCarState->getDriver() : nullptr;
}

CameraShowcase *GetPhotoCamera() {
    return ( gpCamera != nullptr )
               ? (CameraShowcase *)gpCamera->findCameraWithMode(
                     kPhotoCameraMode, gpCamera->getBoundCarState() )
               : nullptr;
}

PhotoModeEffect::PhotoModeEffect()
    : activeSliderIndex( -1 ),
      activeFilterIndex( 0 ),
      bPopupOpen( false ),
      filterIndexBeforeOptions( 0 ),
      bAvatarVisibleBeforeOptions( true ),
      bCameraTranslationWasEnabled( false ),
      pFlashPlayer( nullptr ),
      activeFilterMask( 0xffffffff ),
      field_0x42c( 0xffffffff ) {
    // FUN_009b5340
    setSlidersRange();

    for ( int32_t i = 0; i < kNumSliders; i++ ) {
        pParametersBeforeOptions[i] = 0.0f;
        pParametersBeforePhotoMode[i] = 0.0f;
    }
}

PhotoModeEffect::~PhotoModeEffect() {
    // FUN_00431e60
}

void PhotoModeEffect::setSlidersRange() {
    // FUN_009b50b0
    for ( int32_t i = 0; i < kNumSliders; i++ ) {
        pSliders[i].setAutoVelocity( true );
    }

    pSliders[0].setRange( 0.3f, 1.7f );
    pSliders[1].setRange( 0.0f, 16.0f );
    pSliders[2].setRange( 0.0f, 1.0f );
    pSliders[3].setRange( 0.0f, 4.0f );
    pSliders[4].setRange( 0.0f, 86400.0f );
    pSliders[5].setRange( 0.75f, 2.0f );
    pSliders[6].setRange( -1.0f, 1.0f );
    pSliders[7].setRange( -1.0f, 1.0f );
    pSliders[8].setRange( -1.0f, 1.0f );
}

void PhotoModeEffect::setFlashPlayer( FlashPlayer *pPlayer ) {
    // FUN_009b51a0
    pFlashPlayer = pPlayer;

    for ( int32_t i = 0; i < kNumSliders; i++ ) {
        pSliders[i].rebind( pFlashPlayer, kSliderVarNames[i] );
    }
}

void PhotoModeEffect::applyCameraFOV() {
    CameraGame *pCamera = GetPhotoCamera();
    if ( pCamera != nullptr ) {
        pCamera->setFOVAndAspectRatio( pCamera->getAspectRatio(),
                                       pSliders[PMS_FOV].getValue() );
    }
}

void PhotoModeEffect::applyRadialBlur() {
    PostFXNodeRadialBlur *pNode = GetRadialBlurNode();
    if ( pNode != nullptr ) {
        pNode->setIntensity( pSliders[PMS_RadialBlur].getValue() );
    }
}

void PhotoModeEffect::applyColorFilters() {
    float saturation = pSliders[PMS_Saturation].getValue();
    if ( 0.0f > saturation ) {
        saturation = 0.0f;
    } else if ( saturation > 1.0f ) {
        saturation = 1.0f;
    }

    const float contrast = pSliders[PMS_Contrast].getValue();
    const float red = pSliders[PMS_RedFactor].getValue();
    const float green = pSliders[PMS_GreenFactor].getValue();
    const float blue = pSliders[PMS_BlueFactor].getValue();

    PostFXNodeDesaturate *pDesaturate = GetLiveDesaturateNode();
    if ( pDesaturate != nullptr ) {
        pDesaturate->setSaturation( saturation );
        pDesaturate->setContrast( contrast );
        pDesaturate->setRGBFactor( red, green, blue );
    }

    PostFXNodeHDR *pHDR = GetLiveHDRNode();
    if ( pHDR != nullptr ) {
        pHDR->setSaturation( saturation );
        pHDR->setContrast( contrast );
        pHDR->setRGBFactor( red, green, blue );
    }
}

void PhotoModeEffect::applyParameter( const int32_t index ) {
    // FUN_009b53f0
    if ( index < 0 || index >= kNumSliders || !isParameterEnabled( index ) ) {
        return;
    }

    switch ( index ) {
    case PMS_FOV:
        applyCameraFOV();
        break;
    case PMS_RadialBlur:
        applyRadialBlur();
        break;
    case PMS_Saturation:
    case PMS_Contrast:
    case PMS_RedFactor:
    case PMS_GreenFactor:
    case PMS_BlueFactor:
        applyColorFilters();
        break;
    default:
        // NOTE: sliders 3 and 4 are not PostFX (time of day is managed by
        // GSWeather)
        break;
    }
}

void PhotoModeEffect::applyAllParameters() {
    // FUN_009b5900
    if ( isParameterEnabled( PMS_FOV ) ) {
        applyCameraFOV();
    }
    if ( isParameterEnabled( PMS_RadialBlur ) ) {
        applyRadialBlur();
    }

    applyParameter( PMS_Saturation );
    for ( int32_t i = PMS_Contrast; i <= PMS_BlueFactor; i++ ) {
        if ( isParameterEnabled( i ) ) {
            applyColorFilters();
        }
    }

    refreshGauges();
}

void PhotoModeEffect::refreshGauges() {
    // FUN_009b5760
    if ( pFlashPlayer == nullptr ) {
        return;
    }

    for ( int32_t i = 0; i < kNumSliders; i++ ) {
        char clipPath[64];
        snprintf( clipPath, sizeof( clipPath ), kGaugeClipFormat, i + 1 );

        if ( !pFlashPlayer->spriteExists( clipPath ) ) {
            continue;
        }

        const float minValue = pSliders[i].getMinValue();
        const float maxValue = pSliders[i].getMaxValue();

        float normalized =
            ( pSliders[i].getValue() - minValue ) / ( maxValue - minValue );
        if ( 0.0f > normalized ) {
            normalized = 0.0f;
        } else if ( normalized > 1.0f ) {
            normalized = 1.0f;
        }

        const int32_t frameIndex =
            (int32_t)floorf( normalized * kGaugeLastFrame );
        pFlashPlayer->goToFrameAndPlay( frameIndex, clipPath, false );
    }

    pFlashPlayer->setVariableValue( kSaturationLockedVarName,
                                    (int32_t)( activeFilterIndex != 0 ) );
    pFlashPlayer->showFrame( kLockFrameName, kPopupClipPath, false );
}

void PhotoModeEffect::loadFilterPresetNone() {
    // FUN_009b5710
    pSliders[PMS_Saturation].setValue( 0.85f );
    pSliders[PMS_Contrast].setValue( 1.426f );
    pSliders[PMS_RedFactor].setValue(
        pParametersBeforePhotoMode[PMS_RedFactor] );
    pSliders[PMS_GreenFactor].setValue(
        pParametersBeforePhotoMode[PMS_GreenFactor] );
    pSliders[PMS_BlueFactor].setValue(
        pParametersBeforePhotoMode[PMS_BlueFactor] );
}

void PhotoModeEffect::loadFilterPresetBlackAndWhite() {
    // FUN_009b56c0
    pSliders[PMS_Saturation].setValue( 0.0f );
    pSliders[PMS_Contrast].setValue( 1.0f );
    pSliders[PMS_RedFactor].setValue(
        pParametersBeforePhotoMode[PMS_RedFactor] );
    pSliders[PMS_GreenFactor].setValue(
        pParametersBeforePhotoMode[PMS_GreenFactor] );
    pSliders[PMS_BlueFactor].setValue(
        pParametersBeforePhotoMode[PMS_BlueFactor] );
}

void PhotoModeEffect::loadFilterPresetSepia() {
    // FUN_009b5670
    pSliders[PMS_Saturation].setValue( 0.0f );
    pSliders[PMS_Contrast].setValue( 0.9f );
    pSliders[PMS_RedFactor].setValue( 0.3f );
    pSliders[PMS_GreenFactor].setValue( 0.0f );
    pSliders[PMS_BlueFactor].setValue( -0.3f );
}

void PhotoModeEffect::applyFilterPreset( const int32_t index ) {
    // FUN_009b63c0
    activeFilterIndex = index;

    switch ( index ) {
    case 0:
        loadFilterPresetNone();
        break;
    case 1:
        loadFilterPresetBlackAndWhite();
        break;
    case 2:
        loadFilterPresetSepia();
        break;
    default:
        // The original leaves the index written but applies nothing.
        return;
    }

    applyAllParameters();

    if ( gpMngFlashLocalize == nullptr || pFlashPlayer == nullptr ) {
        return;
    }

    const uint64_t nameHashcode = kFilterNameHashcodes[activeFilterIndex];
    gpMngFlashLocalize->setVarLocalized( pFlashPlayer, kTempNameVarName,
                                         nameHashcode );
    gpMngFlashLocalize->setVarLocalized( pFlashPlayer, "/:PM_Filters",
                                         nameHashcode );
    gpMngFlashLocalize->setVarLocalized( pFlashPlayer, kFilterRowParamVarName,
                                         nameHashcode );
}

void PhotoModeEffect::stepTimeOfDay( const uint32_t selector,
                                     const bool bNext ) {
    // FUN_009b5e30
    if ( gpWeather == nullptr ) {
        return;
    }

    // The day is one 86400 s line split into four 6 h blocks. The block index
    // IS the weather letter, and the offset inside the block is the hour: the
    // two arrows on the popup move along the same value.
    const float totalSeconds = gpWeather->getCurrentTime().toSeconds();
    const int32_t blockIndex =
        (int32_t)floorf( totalSeconds * kWeatherLabelScale );
    const int32_t withinBlock =
        (int32_t)totalSeconds % (int32_t)kWeatherBlockSeconds;
    const int32_t direction = bNext ? 1 : -1;

    int32_t newBlockIndex = blockIndex;
    if ( ( selector & TS_Weather ) != 0 ) {
        newBlockIndex = ( blockIndex + direction ) % 4;
        if ( newBlockIndex < 0 ) {
            newBlockIndex += 4;
        }

        TimeOfDay steppedTime;
        TimeOfDay::ToStdUnit( (float)newBlockIndex * kWeatherBlockSeconds +
                                  (float)withinBlock,
                              steppedTime );
        gpWeather->setTime( true, &steppedTime );

        if ( pFlashPlayer != nullptr ) {
            pFlashPlayer->setVariableValue( kTempNameVarName,
                                            kWeatherLabels[newBlockIndex] );
            pFlashPlayer->setVariableValue( "/:PM_Weather",
                                            kWeatherLabels[newBlockIndex] );
        }
    }

    if ( ( selector & TS_Hour ) != 0 ) {
        float steppedOffset =
            (float)( ( withinBlock +
                       (int32_t)( (float)direction * kHourStepSeconds ) ) %
                     (int32_t)kWeatherBlockSeconds );
        if ( steppedOffset < 0.0f ) {
            steppedOffset += kWeatherBlockSeconds;
        }

        // The readout is not the world time: the 6 h block is stretched over
        // 07:00 - 19:00, so the row always reads as daylight.
        TimeOfDay displayedTime;
        TimeOfDay::ToStdUnit( steppedOffset * kHourDisplayScale +
                                  kHourDisplayOffset,
                              displayedTime );
        if ( pFlashPlayer != nullptr ) {
            const char *pText = FormatTimeOfDay( displayedTime );
            pFlashPlayer->setVariableValue( kTempNameVarName, pText );
            pFlashPlayer->setVariableValue( "/:PM_Hour", pText );
        }

        // The world time keeps the block the popup opened on - the original
        // reuses the index it read before the weather half could change it.
        TimeOfDay steppedTime;
        TimeOfDay::ToStdUnit( (float)blockIndex * kWeatherBlockSeconds +
                                  steppedOffset,
                              steppedTime );
        gpWeather->setTime( true, &steppedTime );
    }
}

void PhotoModeEffect::refreshAvatarLabels( const bool bVisible ) {
    if ( gpMngFlashLocalize == nullptr || pFlashPlayer == nullptr ) {
        return;
    }

    const uint64_t hashcode =
        bVisible ? kAvatarShownHashcode : kAvatarHiddenHashcode;
    gpMngFlashLocalize->setVarLocalized( pFlashPlayer, kTempNameVarName,
                                         hashcode );
    gpMngFlashLocalize->setVarLocalized( pFlashPlayer, "/:PM_Avatar",
                                         hashcode );
}

void PhotoModeEffect::toggleAvatarInShot() {
    // FUN_009b6490
    EntityDriver *pDriver = GetShotDriver();
    if ( pDriver == nullptr ) {
        return;
    }

    const int32_t refCount = pDriver->getInstanceRefCount();
    pDriver->setInstanceRefCount( ( refCount < 1 ) ? refCount + 1
                                                   : refCount - 1 );

    refreshAvatarLabels( pDriver->getInstanceRefCount() >= 1 );
}

void PhotoModeEffect::restoreParametersBeforeOptions() {
    // FUN_009b60d0
    for ( int32_t i = 0; i < kNumSliders; i++ ) {
        pSliders[i].setValue( pParametersBeforeOptions[i] );
    }
    activeFilterIndex = filterIndexBeforeOptions;

    TimeOfDay restoredTime;
    TimeOfDay::ToStdUnit( pParametersBeforeOptions[PMS_TimeOfDay],
                          restoredTime );
    if ( restoredTime.Hours < 0.0f || restoredTime.Hours >= 24.0f ) {
        restoredTime.Hours = 0.0f;
    }
    if ( restoredTime.Minutes < 0.0f || restoredTime.Minutes >= 60.0f ) {
        restoredTime.Minutes = 0.0f;
    }
    if ( restoredTime.Seconds < 0.0f || restoredTime.Seconds >= 60.0f ) {
        restoredTime.Seconds = 0.0f;
    }

    if ( gpWeather != nullptr ) {
        gpWeather->setTime( true, &restoredTime );
    }

    // TODO: This seems like a bug from the original p-code (ref count keeps
    // increasing if the players doesnt toggle avatar visibility; resulting in
    // the pool entry never being released)
    EntityDriver *pDriver = GetShotDriver();
    if ( pDriver != nullptr ) {
        if ( bAvatarVisibleBeforeOptions ) {
            pDriver->setInstanceRefCount( pDriver->getInstanceRefCount() + 1 );
        } else if ( pDriver->getInstanceRefCount() > 0 ) {
            pDriver->setInstanceRefCount( pDriver->getInstanceRefCount() - 1 );
        }
    }

    applyAllParameters();
}

void PhotoModeEffect::saveParametersBeforeOptions() {
    // FUN_009b5200
    for ( int32_t i = 0; i < kNumSliders; i++ ) {
        pParametersBeforeOptions[i] = pSliders[i].getValue();
    }
    filterIndexBeforeOptions = activeFilterIndex;

    if ( gpWeather != nullptr ) {
        pParametersBeforeOptions[PMS_TimeOfDay] =
            gpWeather->getCurrentTime().toSeconds();
    }

    const EntityDriver *pDriver = GetShotDriver();
    bAvatarVisibleBeforeOptions =
        ( pDriver == nullptr ) || ( pDriver->getInstanceRefCount() >= 1 );
}

void PhotoModeEffect::setActiveSlider( const int32_t index ) {
    // FUN_009b52c0
    if ( activeSliderIndex != -1 ) {
        pSliders[activeSliderIndex].setFocusState( false );
        activeSliderIndex = -1;
    }

    activeSliderIndex = index;
    pSliders[index].setFocusState( true );
}

void PhotoModeEffect::start() {
    // FUN_009b62b0
    pParametersBeforePhotoMode[PMS_FOV] =
        ( gpCamera != nullptr && gpCamera->getActiveCamera() != nullptr )
            ? gpCamera->getActiveCamera()->getFOV()
            : 0.0f;
    pParametersBeforePhotoMode[PMS_RadialBlur] = 0.0f;

    PostFXNodeDesaturate *pDesaturate = GetLiveDesaturateNode();
    if ( pDesaturate != nullptr ) {
        pParametersBeforePhotoMode[PMS_Saturation] =
            pDesaturate->getSaturation();
        pParametersBeforePhotoMode[PMS_Contrast] = pDesaturate->getContrast();
        pParametersBeforePhotoMode[PMS_RedFactor] = pDesaturate->getRedFactor();
        pParametersBeforePhotoMode[PMS_GreenFactor] =
            pDesaturate->getGreenFactor();
        pParametersBeforePhotoMode[PMS_BlueFactor] =
            pDesaturate->getBlueFactor();
    }

    PostFXNodeHDR *pHDR = GetLiveHDRNode();
    if ( pHDR != nullptr ) {
        pParametersBeforePhotoMode[PMS_Saturation] = pHDR->getSaturation();
        pParametersBeforePhotoMode[PMS_Contrast] = pHDR->getContrast();
        pParametersBeforePhotoMode[PMS_RedFactor] = pHDR->getRedFactor();
        pParametersBeforePhotoMode[PMS_GreenFactor] = pHDR->getGreenFactor();
        pParametersBeforePhotoMode[PMS_BlueFactor] = pHDR->getBlueFactor();
    }

    if ( gpWeather != nullptr ) {
        pParametersBeforePhotoMode[PMS_TimeOfDay] =
            gpWeather->getCurrentTime().toSeconds();
    }

    stop();
}

void PhotoModeEffect::stop() {
    // FUN_009b61e0
    for ( int32_t i = 0; i < kNumSliders; i++ ) {
        pSliders[i].setValue( pParametersBeforePhotoMode[i] );
    }
    activeFilterIndex = 0;

    TimeOfDay restoredTime;
    TimeOfDay::ToStdUnit( pParametersBeforePhotoMode[PMS_TimeOfDay],
                          restoredTime );
    if ( restoredTime.Hours < 0.0f || restoredTime.Hours >= 24.0f ) {
        restoredTime.Hours = 0.0f;
    }
    if ( restoredTime.Minutes < 0.0f || restoredTime.Minutes >= 60.0f ) {
        restoredTime.Minutes = 0.0f;
    }
    if ( restoredTime.Seconds < 0.0f || restoredTime.Seconds >= 60.0f ) {
        restoredTime.Seconds = 0.0f;
    }

    if ( gpWeather != nullptr ) {
        gpWeather->setTime( true, &restoredTime );
    }

    applyAllParameters();
}

void PhotoModeEffect::tick( const float deltaTime ) {
    // FUN_009b5d70
    if ( activeSliderIndex != -1 ) {
        pSliders[activeSliderIndex].tick( deltaTime );
        if ( pSliders[activeSliderIndex].isDirty() ) {
            applyParameter( activeSliderIndex );
        }
    }

    if ( gpCamera == nullptr ||
         gpCamera->getActiveCamera() != GetPhotoCamera() ) {
        return;
    }

    if ( isParameterEnabled( PMS_RadialBlur ) ) {
        applyRadialBlur();
    }
    if ( isParameterEnabled( PMS_FOV ) ) {
        applyCameraFOV();
    }
}

bool PhotoModeEffect::onMessage( FlashMessage *pMessage ) {
    // FUN_009b6490
    switch ( *pMessage ) {
    case FlashMessage( "sfov" ): //  FOV slider
        setActiveSlider( PMS_FOV );
        return true;

    case FlashMessage( "srbl" ): // radial blur slider
        setActiveSlider( PMS_RadialBlur );
        return true;

    case FlashMessage( "ssat" ): // saturation slider
        setActiveSlider( PMS_Saturation );
        return true;

    case FlashMessage( "scon" ): // contrast slider
        setActiveSlider( PMS_Contrast );
        return true;

    case FlashMessage( "opin" ): { // options in
        if ( gpMngFlashLocalize != nullptr && pFlashPlayer != nullptr ) {
            gpMngFlashLocalize->setVarLocalized(
                pFlashPlayer, "/:PM_Filters",
                kFilterNameHashcodes[activeFilterIndex] );

            const float timeOfDay =
                ( gpWeather != nullptr )
                    ? gpWeather->getCurrentTime().toSeconds()
                    : 0.0f;
            int32_t weatherIndex = (int32_t)( timeOfDay * kWeatherLabelScale );
            if ( weatherIndex < 0 ) {
                weatherIndex = 0;
            } else if ( weatherIndex > 3 ) {
                weatherIndex = 3;
            }
            pFlashPlayer->setVariableValue( "/:PM_Weather",
                                            kWeatherLabels[weatherIndex] );

            // The hour row shows the offset inside the 6 h block stretched
            // over 07:00 - 19:00, not the world clock.
            TimeOfDay displayedTime;
            TimeOfDay::ToStdUnit(
                (float)( (int32_t)timeOfDay % (int32_t)kWeatherBlockSeconds ) *
                        kHourDisplayScale +
                    kHourDisplayOffset,
                displayedTime );
            const char *pHourText = FormatTimeOfDay( displayedTime );
            pFlashPlayer->setVariableValue( kTempNameVarName, pHourText );
            pFlashPlayer->setVariableValue( "/:PM_Hour", pHourText );

            const EntityDriver *pDriver = GetShotDriver();
            gpMngFlashLocalize->setVarLocalized(
                pFlashPlayer, "/:PM_Avatar",
                ( pDriver == nullptr || pDriver->getInstanceRefCount() >= 1 )
                    ? kAvatarShownHashcode
                    : kAvatarHiddenHashcode );
        }

        CameraShowcase *pCamera = GetPhotoCamera();
        if ( pCamera != nullptr ) {
            bCameraTranslationWasEnabled = pCamera->canMoveCamera();
            pCamera->setCanMoveCamera( false );
        }

        saveParametersBeforeOptions();
        bPopupOpen = true;
        return true;
    }

    case FlashMessage( "opou" ): { // options out
        CameraShowcase *pCamera = GetPhotoCamera();
        if ( pCamera != nullptr ) {
            pCamera->setCanMoveCamera( bCameraTranslationWasEnabled );
        }
        bPopupOpen = false;
        return true;
    }

    case FlashMessage( "vaop" ): // validate options
        saveParametersBeforeOptions();
        return true;

    case FlashMessage( "caop" ): // cancel options
        restoreParametersBeforeOptions();
        return true;

    case FlashMessage( "nefi" ): // next filter
        applyFilterPreset( ( activeFilterIndex + 1 == kNumFilterPresets )
                               ? 0
                               : activeFilterIndex + 1 );
        return true;

    case FlashMessage( "prfi" ): // previous filter
        applyFilterPreset( ( activeFilterIndex - 1 < 0 )
                               ? kNumFilterPresets - 1
                               : activeFilterIndex - 1 );
        return true;

    case FlashMessage( "newe" ): // next weather
        stepTimeOfDay( TS_Weather, true );
        return true;

    case FlashMessage( "prwe" ): // previous weather
        stepTimeOfDay( TS_Weather, false );
        return true;

    case FlashMessage( "neho" ): // next hour
        stepTimeOfDay( TS_Hour, true );
        return true;

    case FlashMessage( "prho" ): //  previous hour
        stepTimeOfDay( TS_Hour, false );
        return true;

    case FlashMessage( "nexa" ): // next avatar
    case FlashMessage( "prav" ): // prev avatar
        toggleAvatarInShot();
        return true;

    case FlashMessage( "noga" ): // leave the slider row
        if ( activeSliderIndex == -1 ) {
            return false;
        }
        pSliders[activeSliderIndex].setFocusState( false );
        activeSliderIndex = -1;
        return false;

    default:
        break;
    }

    return false;
}
