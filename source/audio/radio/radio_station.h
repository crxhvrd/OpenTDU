#pragma once

struct RadioTrack {
    char Path[261];
    char Title[65];
    char Artist[65];
    bool bUserTrack;
    bool bPlayed;
};

class RadioStation {
public:
    static constexpr int32_t kMaxTracks = 30;
    static constexpr size_t kMaxNameLength = 64;

    inline const char *getName() const { return Name; }
    inline int32_t getNumTracks() const { return NumTracks; }
    inline const RadioTrack *getCurrentTrack() const {
        return ( CurrentTrack < NumTracks ) ? &Tracks[CurrentTrack] : nullptr;
    }

public:
    RadioStation();
    virtual ~RadioStation();

    void loadPlaylist( const void *pStationData,
                       const uint32_t stationDataSize,
                       const char *pWorkingDir );
    void loadFolder( const char *pName, const char *pFolder );

    void resetPlayedTracks();
    bool pickNextTrack();

private:
    char Name[65];
    int32_t NumTracks;
    int32_t NumTracksLeft;
    int32_t CurrentTrack;
    RadioTrack Tracks[kMaxTracks];

private:
    void addTrack( const RadioTrack &track );
    void addTrackFile( const char *pFilePath, const char *pTitleSource );
    void addPlaylistFile( const char *pPlaylistPath );
    bool readTrackTags( RadioTrack &track );
};
