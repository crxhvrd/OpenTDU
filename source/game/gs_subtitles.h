#pragma once

#include "game/gs_base.h"

class edAudioStreamEntryListener;
class FlashPlayer;

class GSSubtitles : public GameSystem {
public:
    const char *getName() const override { return "Service : Subtitles"; }

public:
    static constexpr int32_t kMaxListeners = 0x20;

public:
    GSSubtitles();
    ~GSSubtitles();

    bool initialize( TestDriveGameInstance * ) override;
    void terminate() override;

    void registerListener( edAudioStreamEntryListener *pListener );
    void unregisterListener( edAudioStreamEntryListener *pListener );

private:
    edAudioStreamEntryListener *pListeners[kMaxListeners];
    int32_t numListeners;
    void *pActiveSubtitle;
    bool bEnabled;
    FlashPlayer *pFlashPlayer;
    bool bDisplayed;
};

extern GSSubtitles *gpSubtitles;
