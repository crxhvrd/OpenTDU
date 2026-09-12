#pragma once

#include "core/locale.h"
#include "core/popup_callback.h"
#include "game/mng_base.h"
#include "render/render_api.h"

#include "flash/entities/flash_entity.h"
#include "flash/entities/flash_slider.h"

class FlashPlayer;

enum class eDisplayMode : int32_t {
    DM_Windowed = 0,
    // Exclusive fullscreen
    DM_Fullscreen = 1,
    // Borderless / desktop fullscreen
    DM_Borderless = 2,

    DM_Count
};

struct VideoResolutionOption {
    uint32_t Width = 0u;
    uint32_t Height = 0u;
    std::vector<uint32_t> RefreshRates;
};

class AudioOptions {
public:
    AudioOptions();
    ~AudioOptions();

    void tick();

    void selectVolumeControl( const char *pVarName );
    void setMuteGauge( const char *pWhatGauge );
    void applySpeakerMode();
    void resetActiveControl();
    void deactivate();
};

class MngOptions : public Manager, public PopupCallback, public FlashEntity {
public:
    const char *getName() const override { /* FUN_00431fd0 */
        return "Options";
    }

public:
    MngOptions();
    ~MngOptions();

    virtual bool initialize( TestDriveGameInstance * ) override;
    virtual void tick( float, float ) override;
    virtual void terminate() override;

    bool onMessage( FlashMessage *pMessage,
                    class FlashPlayer *pPlayer ) override;
    void
    onClose( ePopupAnswer answer, void *param_2, int32_t param_3 ) override;
    void bindFlashPlayer( FlashPlayer *pFlashPlayer, int32_t orientation );
    void refreshOptionsScreen();

    void FUN_006c5c10();
    void FUN_006c59d0();
    void FUN_006c60c0( int32_t param_1 );
    void FUN_006c6030();
    void FUN_006c67b0();

    void FUN_006c6650();

private:
    static constexpr size_t kNumLocalizedStrings = 9;
    static constexpr size_t kLocalizedStringLength = 0x40;

private:
    FlashSlider padSensitivitySlider;
    FlashSlider padLinearitySlider;
    FlashSlider barSensitivitySlider;
    FlashSlider mouseSensitivitySlider;
    FlashSlider quiverSensitivitySlider;
    FlashSlider seatPositionSlider;
    FlashSlider seatHeightSlider;
    FlashSlider cameraShakeSlider;
    FlashSlider radialBlurSlider;
    FlashSlider voicesVolumeSlider;
    FlashSlider voicesThresholdSlider;
    FlashSlider voicesInputSlider;
    FlashSlider gammaSlider;

    FlashPlayer *pPlayer;

    std::vector<VideoResolutionOption> videoModes;
    int32_t videoModeIndex;
    int32_t refreshRateIndex;
    bool bVideoModesCached;
    int32_t gearboxMode;
    int32_t driveAssists;
    int32_t audioLanguage;

    int32_t savegameStep;
    int32_t deviceSelected;
    int32_t cameraBeforeSeatSetup;
    eLocale activeLocale;
    int32_t speakerMode;
    int32_t unitType;
    int32_t languageRefreshStep;
    int32_t lodQuality;
    eDisplayMode displayMode;
    uint32_t videoWidth;
    uint32_t videoHeight;
    uint32_t refreshRate;
    eAntiAliasingMethod antiAliasingMode;
    AudioOptions *pAudioOptions;

    char pLocalizedStrings[kNumLocalizedStrings][kLocalizedStringLength];
    bool field30_0x80c;
    bool bHDREnabled;
    bool bHDRSupported;

private:
    void retrieveLocalizedStrings();
    void disableAllSliders();
    void deviceSaveTick();
    void onLanguageChanged();

    void buildVideoModeList();
    void showVideoOptions();
    void showPreviousVideoOptions();
    void showPreviousRefreshRate();
    void showPreviousAntiAliasing();
    void showRefreshRate();
    void showAntiAliasing();
    void applySystemParameters();

    void showResolutionLabel();
    void showRefreshRateLabel( const bool bEditable );
    void showAntiAliasingMode();
    void showAntiAliasingLabel();
    bool isAntiAliasingSupported( const eAntiAliasingMethod method ) const;
    const char *getAntiAliasingLabel() const;
    void showLODQualityLabel();

    void showDisplayModeLabel();
    void showNextDisplayMode();
    void showPreviousDisplayMode();
    void FUN_006c7b80();
    void FUN_006c7950( bool param_1 );
    void FUN_006c6730( int32_t param_1 );
    void FUN_006c7fb0();
    void FUN_006c6db0();
    void flashGPSOption();
    void FUN_006c61b0();
    void FUN_006c6a00();

    void FUN_006c7190();
    void FUN_006c71d0();
    void FUN_006c7210();
    void showLanguageLabel();
    void enterSeatSetupCamera();
    void requestSaveToDevice( bool bOverwrite );
};

extern MngOptions *gpMngOptions;
