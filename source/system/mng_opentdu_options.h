#pragma once

#include "flash/entities/flash_entity.h"
#include "opentdu_mods_settings.h"
#include "opentdu_video_settings.h"
#include "options_widget_page.h"

class FlashPlayer;

// OTDU_ADDITION: drives the OPENTDU movie, a copy of the CONFIGPC options page
// generated on first launch (see AssetExtractor::BuildGeneratedMovies)
class MngOpenTDUOptions : public FlashEntity {
public:
    inline bool isOpen() const { return state != OS_Closed; }

public:
    MngOpenTDUOptions();
    ~MngOpenTDUOptions();

    static void RegisterMovie();

    // Binds itself once both movies are loaded
    void unbind();

    void tick( float totalTime, float deltaTime ) override;
    bool onMessage( FlashMessage *pMessage, FlashPlayer *pPlayer ) override;

private:
    enum eOpenState : uint8_t {
        OS_Closed,
        OS_Open,
        OS_Closing,
    };

private:
    bool bind();
    void open();
    void close();
    void finishClose();

    void tickOptionsEntry();
    void tickCategoryList();
    void onCategorySelected( const int32_t category );

    bool setMovieActive( const bool bActive );
    void setOptionsPageVisible( const bool bVisible );

private:
    FlashPlayer *pMovie;
    FlashPlayer *pOptionsMovie;
    OptionsWidgetPage settingsPage;
    OpenTDUVideoSettings videoSettings;
    OpenTDUModsSettings modsSettings;
    int32_t optionsCursor;
    int32_t categoryCursor;
    eOpenState state;
    uint8_t bBackLatched : 1;
    // Opened from the retail video entry, for the video settings only
    uint8_t bVideoPageRequested : 1;
    uint8_t bDirectVideoPage : 1;
    uint8_t bPageHidden : 1;
};

extern MngOpenTDUOptions *gpOpenTDUOptions;
