#pragma once

#include "audio/radio/gs_radio.h"
#include "audio/ambiance_environment.h"
#include "audio/implementation/ed_audio_stream_entry.h"

class RadioStreamPlayer;

class GSRadioPC : public GSRadio, public edAudioStreamEntryListener {
public:
    static constexpr int32_t kMaxStations =
        8; // Bumped to 8 to allow radio mods
    static constexpr int32_t kStationPoolCapacity = kMaxStations;
    static constexpr int32_t kUserStationPoolCapacity = 4;

public:
    inline const char *getStationName() override { /* FUN_007c8250 */
        return StationName;
    }
    inline const char *getArtist() override { /* FUN_007c8240 */
        return Artist;
    }
    inline const char *getTitle() override { /* FUN_007c8230 */ return Title; }
    inline bool setVolume( float ) override { /* FUN_007c8220 */ return true; }
    inline bool skipTrack() override { /* FUN_007c81b0 */
        return playNextTrack();
    }

public:
    GSRadioPC();
    ~GSRadioPC();

    bool initialize( TestDriveGameInstance *pGameInstance ) override;
    void tick( float totalTime, float deltaTime ) override;
    void terminate() override;
    void pause() override;
    void reset() override;

    RadioStation *addStation( const char *pStationFile,
                              const char *pWorkingDir ) override;
    void removeStation( RadioStation *pStation ) override;
    bool turnOn() override;
    bool turnOff() override;
    bool selectNextStation() override;
    bool selectPreviousStation() override;
    bool playNextTrack() override;
    bool selectStationByHashcode( uint64_t hashcode ) override;

    void onPipeEntryPlayed( edAudioStreamEntry *pEntry ) override;
    void onPipeEntryEnded( edAudioStreamEntry *pEntry ) override;

private:
    RadioStreamPlayer *pStreamPlayer;
    AmbiancePool<RadioStation> *pStationPool;
    AmbiancePool<RadioStation> *pUserStationPool;
    int32_t NumStations;
    int32_t CurrentStation;
    RadioStation *pStations[kMaxStations];
    char StationName[65];
    char Artist[65];
    char Title[65];
    bool bPlayNextTrack;

private:
    void clearTrackInfos();
    bool playCurrentTrack();
    void registerStation( RadioStation *pStation );
    void releaseStation( RadioStation *pStation );
};
