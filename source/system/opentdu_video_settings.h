#pragma once

#include "mng_options.h"
#include "options_widget_page.h"

class FlashPlayer;

// OTDU_ADDITION: the VIDEO settings of the OPENTDU options movie (tabs of an
// OptionsWidgetPage)
class OpenTDUVideoSettings {
public:
    OpenTDUVideoSettings();

    void open( OptionsWidgetPage &page, FlashPlayer *pPlayer );

private:
    struct ImmediateSettings {
        int32_t MaxFrameRate = 0;
        bool bDenseReflections = false;
        int32_t NightLights = 0;
        uint32_t LODQuality = 0;
        float Gamma = 0.0f;
        float HDRPaperWhite = 0.0f;
        float HDRPeak = 0.0f;
        float HDRHighlights = 0.0f;
        bool bSoftHighlights = false;

        bool operator==( const ImmediateSettings &other ) const;
    };

private:
    std::vector<OptionsTab> buildTabs( FlashPlayer *pPlayer );
    static ImmediateSettings CaptureImmediateSettings();
    static void RestoreImmediateSettings( const ImmediateSettings &settings );

    void confirm();
    void cancel();

private:
    eDisplayMode displayMode;
    bool bVSyncEnabled;
    bool bHDRDisplayEnabled;
    ImmediateSettings openedSettings;
};
