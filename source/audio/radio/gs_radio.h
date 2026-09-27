#pragma once

#include "game/gs_base.h"

class AudioClient;
class edStore;
class RadioListener;
class RadioStation;
class RadioTitle;

class GSRadio : public GameSystem {
public:
    const char *getName() const override { /* FUN_0042af70 */
        return "[GSRadio]";
    }

    inline bool isOnAir() const { return bOnAir; }
    inline float getVolume() const { return Volume; }
    inline void setListener( RadioListener *pInListener ) {
        pListener = pInListener;
    }

public:
    GSRadio();
    ~GSRadio();

    bool initialize( TestDriveGameInstance *pGameInstance ) override;
    void tick( float totalTime, float deltaTime ) override;
    void terminate() override;
    void pause() override;

    virtual RadioStation *addStation( const char *pStationFile,
                                      const char *pWorkingDir ) {
        // FUN_00422ab0
        return nullptr;
    }
    virtual void removeStation( RadioStation *pStation ) {
        /* FUN_00614df0 */ /* NOP */ }
    virtual const char *getStationName() = 0;
    virtual const char *getArtist() = 0;
    virtual const char *getTitle() = 0;
    virtual bool turnOn();
    virtual bool turnOff();
    virtual void onForegroundCinematicBegin() { /* FUN_007c9820 */ /* NOP */ }
    virtual void onForegroundCinematicEnd() { /* FUN_007c9810 */ /* NOP */ }
    virtual bool setVolume( float volume ) = 0;
    virtual bool selectNextStation() = 0;
    virtual bool selectPreviousStation() = 0;
    virtual bool playNextTrack() = 0;
    virtual bool skipTrack() = 0;
    virtual bool selectStationByHashcode( uint64_t hashcode ) = 0;

    void restoreSettings( edStore &store );
    void saveSettings( edStore &store );

protected:
    AudioClient *pAudioClient;
    RadioListener *pListener;
    RadioTitle *pTitle;
    bool bOnAir;
    bool bOn;
    float Volume;
    bool bTurnOnPending;
    float TurnOnTime;
    bool bTurnOffPending;
    float TurnOffTime;
    float ChannelHoldTime;
    bool bChannelHeld;
    bool bChannelReleased;

private:
    void scheduleTurnOn();
    void scheduleTurnOff();
    void increaseVolume( const float step );
    void decreaseVolume( const float step );
    void nextStation();
    void updateTitle( const float deltaTime, const float totalTime );
};

extern GSRadio *gpRadio;
