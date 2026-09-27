#pragma once

#include "filesystem/file_direct_access.h"

#include <memory>

class AudioClient;
class edAudioStreamEntry;
class edAudioStreamEntryListener;

class RadioStreamPlayer {
public:
    static bool IsDecodableFile( const char *pFilename );

public:
    RadioStreamPlayer( AudioClient *pInAudioClient,
                       edAudioStreamEntryListener *pListener );
    ~RadioStreamPlayer();

    bool play( const char *pFilename, const bool bResolveFilename );
    void stop();
    void setVolume( const float volume );

private:
    AudioClient *pAudioClient;
    FileDirectAccess File;
    std::unique_ptr<edAudioStreamEntry> pEntry;
};
