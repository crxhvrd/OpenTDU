#pragma once

#include "core/locale.h"
#include "core/popup_callback.h"
#include "game/mng_base.h"
#include "render/render_api.h"

#include "flash/entities/flash_entity.h"
#include "flash/entities/flash_slider.h"
#include "flash/implementation/flash_transform.h"

class FlashPlayer;

enum class eDisplayMode : int32_t {
    DM_Windowed = 0,
    // Exclusive fullscreen
    DM_Fullscreen = 1,
    // Borderless / desktop fullscreen
    DM_Borderless = 2,

    DM_Count
};

eDisplayMode GetActiveDisplayMode();
void ApplyDisplayMode( const eDisplayMode displayMode );
const char *GetDisplayModeLabel( const eDisplayMode displayMode );
void ApplyVSync( const bool bEnabled );
void ApplyHDRDisplay( const bool bEnabled );
bool IsHDRDisplaySupported();

struct VideoResolutionOption {
    uint32_t Width = 0u;
    uint32_t Height = 0u;
    std::vector<uint32_t> RefreshRates;
};

struct AudioMixerControlDesc {
    const char *pVarName;   // e.g. "/:vol_Engine"
    const char *pGaugeName; // e.g. "/gauge_1/slider/mute"
    const char *pMixerName; // e.g. "MixerVehicle"
};

class AudioOptions {
public:
    static constexpr int32_t kNumMixerControls = 9;

public:
    AudioOptions();
    ~AudioOptions();

    void tick();
    void bindFlashPlayer( FlashPlayer *pFlashPlayer, int32_t orientation );

    void selectVolumeControl( const char *pVarName );
    void setMuteGauge( const char *pWhatGauge );
    void applyVolumes();
    void refreshControls();
    void deactivate();

private:
    struct MixerControl {
        FlashSlider Slider;
        struct Mixer *pMixer = nullptr;
        const AudioMixerControlDesc *pDesc = nullptr;
        FlashPlayer *pFlashPlayer = nullptr;
        bool bMuted = false;

        inline bool isValid() const { return pMixer != nullptr; }

        void refreshFromMixer();
        void toggleMute();
        void showMuteFrame() const;
    };

private:
    MixerControl controls[kNumMixerControls];
    bool bActive;
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
    void setLocale( const char *pLocale, const bool bSynchronous );

    void FUN_006c5c10();
    void FUN_006c59d0();
    void FUN_006c60c0( int32_t param_1 );
    void FUN_006c6030();
    void FUN_006c67b0();

    void FUN_006c6650();

    // OTDU_ADDITION: the retail video rows, edited from the OpenTDU video
    // settings (OpenTDUVideoSettings) instead of the CONFIGPC video page.
    // Applying goes through the retail path (resize, keep prompt, revert).
    void beginVideoEdit();
    std::string getResolutionLabel() const;
    std::string getRefreshRateLabel() const;
    const char *getAntiAliasingLabel() const;
    std::string getLODQualityLabel() const;
    inline bool isHDRRenderingEnabled() const { return bHDREnabled; }
    void stepResolution( const int32_t direction );
    void stepRefreshRate( const int32_t direction );
    void stepAntiAliasing( const int32_t direction );
    void toggleHDRRendering();
    void stepLODQuality( const int32_t direction );
    void applyVideoEdit( const eDisplayMode newDisplayMode,
                         const bool bNewVSyncEnabled,
                         const bool bNewHDRDisplayEnabled );

private:
    void showVideoPageFrame( const char *pLabel );

private:
    static constexpr size_t kNumLocalizedStrings = 9;
    static constexpr size_t kLocalizedStringLength = 0x40;

    class VideoRevertedCallback : public PopupCallback {
    public:
        MngOptions *pOwner = nullptr;
        void onClose( ePopupAnswer answer, void *, int32_t ) override;
    };

    class VideoApplyCallback : public PopupCallback {
    public:
        MngOptions *pOwner = nullptr;
        void onClose( ePopupAnswer answer, void *, int32_t ) override;
    };

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
    eAntiAliasingMethod antiAliasingMode;
    uint32_t videoWidth;
    uint32_t videoHeight;
    uint32_t refreshRate;
    eAntiAliasingMethod activeAntiAliasingMode;
    eDisplayMode activeDisplayMode;
    bool bActiveHDREnabled;
    bool bVSyncEnabled;
    bool bActiveVSyncEnabled;
    bool bHDRDisplayEnabled;
    bool bActiveHDRDisplayEnabled;
    AudioOptions *pAudioOptions;

    VideoRevertedCallback videoRevertedCallback;
    VideoApplyCallback videoApplyCallback;

    char pLocalizedStrings[kNumLocalizedStrings][kLocalizedStringLength];
    bool field30_0x80c;
    bool bHDREnabled;
    // OTDU_ADDITION: applied from the OpenTDU video settings, whose page is
    // not the CONFIGPC one
    bool bExternalVideoEdit;
    bool bHDRSupported;

private:
    void retrieveLocalizedStrings();
    void disableAllSliders();
    void onLanguageChanged( const bool bSkipFade );

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
    void showLODQualityLabel();

    bool isVideoModeLocked() const;
    void FUN_006c7b80();
    void FUN_006c7950( bool param_1 );
    void startLanguageChange( const eLocale locale );
    void FUN_006c7fb0();
    void FUN_006c6db0();
    void commitPlayerOptions();
    void flashGPSOption();
    void revertSystemParameters();
    void toggleHDR();

    void showNextLODQuality();
    void showPreviousLODQuality();
    void showLanguageLabel();
    void enterSeatSetupCamera();
    void requestSaveToDevice( bool bOverwrite );
};

extern MngOptions *gpMngOptions;
