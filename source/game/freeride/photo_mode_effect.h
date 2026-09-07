#pragma once

#include "flash/entities/flash_slider.h"

class FlashPlayer;
struct FlashMessage;

// Post-FX "photo filter" controller owned by MngPhotoMode. It exposes 9 Flash
// sliders that, while photo mode is active, tweak the active camera FOV and the
// post-processing stack (HDRI / radial blur).
class PhotoModeEffect {
public:
    inline bool isPopupOpen() const { return bPopupOpen; }

public:
    PhotoModeEffect();
    ~PhotoModeEffect();

    void start();
    void stop();
    void tick( float deltaTime );
    bool onMessage( FlashMessage *pMessage );
    void setFlashPlayer( FlashPlayer *pPlayer );

private:
    static constexpr int32_t kNumSliders = 9;

private:
    inline bool isParameterEnabled( const int32_t index ) const {
        return ( ( activeFilterMask >> index ) & 1 ) != 0;
    }

private:
    void setSlidersRange();
    void saveParametersBeforeOptions();
    void restoreParametersBeforeOptions();
    void refreshGauges();
    void setActiveSlider( int32_t index );
    void applyParameter( int32_t index );
    void applyAllParameters();

    void applyFilterPreset( int32_t index );
    void loadFilterPresetNone();
    void loadFilterPresetBlackAndWhite();
    void loadFilterPresetSepia();
    void stepTimeOfDay( uint32_t selector, bool bNext );
    void toggleAvatarInShot();
    void refreshAvatarLabels( bool bVisible );

    void applyCameraFOV();
    void applyRadialBlur();
    void applyColorFilters();

private:
    float pParametersBeforeOptions[kNumSliders];
    float pParametersBeforePhotoMode[kNumSliders];
    FlashSlider pSliders[kNumSliders];
    int32_t activeSliderIndex;
    int32_t activeFilterIndex;
    bool bPopupOpen;
    int32_t filterIndexBeforeOptions;
    bool bAvatarVisibleBeforeOptions;
    bool bCameraTranslationWasEnabled;
    FlashPlayer *pFlashPlayer;
    uint32_t activeFilterMask;
    uint32_t field_0x42c;
};
