#include "shared.h"
#include "gs_radio.h"

#include "audio/gs_audio.h"
#include "audio/radio/radio_listener.h"
#include "audio/radio/radio_title.h"
#include "config/gs_config.h"
#include "core/ed_store.h"
#include "game/mng_ingame_menu.h"
#include "gs_timer.h"
#include "input/input_device.h"

GSRadio *gpRadio = nullptr; // DAT_013a4950

static constexpr float kMaxVolume = 1.0f;       // FLOAT_00f4165c
static constexpr float kVolumeSpeed = 0.75f;    // DAT_00f416a4
static constexpr float kDoubleTapWindow = 0.4f; // DAT_00f4175c
static constexpr uint32_t kTitleVisibleChars = 0x10;

static bool IsRadioChannelHeld() {
    // FUN_007c98d0
    PlayerConfiguration *pConfig = GetPlayerConfig();
    return pConfig != nullptr && IsActionDown( pConfig->pRadioChanR );
}

static bool IsRadioVolumeLessHeld() {
    // FUN_007c9900
    PlayerConfiguration *pConfig = GetPlayerConfig();
    return pConfig != nullptr && IsActionDown( pConfig->pRadioVolumeLess );
}

static bool IsRadioVolumeMoreHeld() {
    // FUN_007c9930
    PlayerConfiguration *pConfig = GetPlayerConfig();
    return pConfig != nullptr && IsActionDown( pConfig->pRadioVolumeMore );
}

static bool IsInGameMenuRightHeld() {
    // FUN_007c9960
    PlayerConfiguration *pConfig = GetPlayerConfig();
    return pConfig != nullptr && IsActionDown( pConfig->pInGameMenuRight );
}

static bool IsInGameMenuDownHeld() {
    // FUN_007c9990
    PlayerConfiguration *pConfig = GetPlayerConfig();
    return pConfig != nullptr && IsActionDown( pConfig->pInGameMenuDown );
}

static bool IsInGameMenuUpHeld() {
    // FUN_007c99c0
    PlayerConfiguration *pConfig = GetPlayerConfig();
    return pConfig != nullptr && IsActionDown( pConfig->pInGameMenuUp );
}

GSRadio::GSRadio()
    : GameSystem(),
      pAudioClient( nullptr ),
      pListener( nullptr ),
      pTitle( nullptr ),
      bOnAir( false ),
      bOn( false ),
      Volume( 0.0f ),
      bTurnOnPending( false ),
      TurnOnTime( 0.0f ),
      bTurnOffPending( false ),
      TurnOffTime( 0.0f ),
      ChannelHoldTime( 0.0f ),
      bChannelHeld( false ),
      bChannelReleased( false ) {
    // FUN_007c9b20
    gpRadio = this;
}

GSRadio::~GSRadio() {
    if ( gpRadio == this ) {
        gpRadio = nullptr;
    }
}

bool GSRadio::initialize( TestDriveGameInstance * ) {
    // FUN_007ca070
    if ( gpAudio != nullptr ) {
        pAudioClient = gpAudio->createClient( "ClientRadio" );
    }

    if ( pAudioClient == nullptr ) {
        OTDU_LOG_WARN( "No 'ClientRadio' audio client, the radio will stay "
                       "silent\n" );
    }

    pTitle = new RadioTitle();
    pTitle->setVisibleChars( kTitleVisibleChars );
    pTitle->resetScroll();
    pTitle->setFlashVariable( "/:RadioTitle" );
    pTitle->resetScroll();

    Volume = 0.0f;
    bOn = false;

    return true;
}

void GSRadio::tick( float totalTime, float deltaTime ) {
    // FUN_007c9da0
    const float gameTime = gpTimer->GameTotalTime;

    if ( bTurnOffPending && TurnOffTime < gameTime ) {
        turnOff();
        bTurnOffPending = false;
    }

    if ( bTurnOnPending && TurnOnTime < gameTime ) {
        bOn = turnOn();
        bTurnOnPending = false;
    }

    if ( gpMngInGameMenu != nullptr && gpMngInGameMenu->canOpenMenu() ) {
        bool bVolumeMore = IsRadioVolumeMoreHeld();
        bool bVolumeLess = IsRadioVolumeLessHeld();
        bool bChannel = IsRadioChannelHeld();

        if ( gpMngInGameMenu->canOpenMenu() &&
             gpMngInGameMenu->getOpenedMenu() == IGM_Radio ) {
            bVolumeMore |= IsInGameMenuUpHeld();
            bVolumeLess |= IsInGameMenuDownHeld();
            bChannel |= IsInGameMenuRightHeld();
        }

        if ( !bOn ) {
            ChannelHoldTime = 0.0f;
            bChannelReleased = false;
            bChannelHeld = false;
        } else {
            ChannelHoldTime += deltaTime;

            if ( bChannel ) {
                if ( !bChannelHeld && !bChannelReleased ) {
                    ChannelHoldTime = 0.0f;
                }
                bChannelHeld = true;
                gpMngInGameMenu->updateMenuTimeout();
            } else {
                if ( bChannelReleased ) {
                    if ( bChannelHeld ) {
                        if ( ChannelHoldTime < kDoubleTapWindow ) {
                            playNextTrack();
                            gpMngInGameMenu->updateMenuTimeout();
                        }
                        bChannelReleased = false;
                    } else if ( ChannelHoldTime >= kDoubleTapWindow ) {
                        nextStation();
                        gpMngInGameMenu->updateMenuTimeout();
                        bChannelReleased = false;
                    }
                } else if ( bChannelHeld ) {
                    bChannelReleased = true;
                }
                bChannelHeld = false;
            }
        }

        if ( bVolumeMore ) {
            increaseVolume( deltaTime * kVolumeSpeed );
            gpMngInGameMenu->updateMenuTimeout();
        }

        if ( bVolumeLess ) {
            decreaseVolume( deltaTime * kVolumeSpeed );
            gpMngInGameMenu->updateMenuTimeout();
        }
    }

    updateTitle( deltaTime, totalTime );
}

void GSRadio::terminate() {
    // FUN_007ca000
    if ( pTitle != nullptr ) {
        delete pTitle;
    }
    pTitle = nullptr;
    pAudioClient = nullptr;
}

void GSRadio::pause() {
    // FUN_007c99f0
    turnOff();
}

bool GSRadio::turnOn() {
    // FUN_007c9840
    return true;
}

bool GSRadio::turnOff() {
    // FUN_007c9830
    Volume = 0.0f;
    bOn = false;
    return true;
}

void GSRadio::restoreSettings( edStore &store ) {
    // FUN_007c9a00
    const uint64_t stationHashcode = store.readULongLong();
    Volume = store.readFloat();

    bOn = Volume > 0.0f;
    bOn = selectStationByHashcode( stationHashcode ) && bOn;
    if ( !bOn ) {
        Volume = 0.0f;
    }
}

void GSRadio::saveSettings( edStore &store ) {
    // FUN_007c9a70
    store.writeULongLong( GetIdentifier64bit( getStationName() ) );
    store.writeFloat( Volume );
}

void GSRadio::scheduleTurnOn() {
    // FUN_007c9ae0
    const float delay =
        ( pListener != nullptr ) ? pListener->getTurnOnDelay() : 0.0f;
    TurnOnTime = gpTimer->GameTotalTime + delay;
    bTurnOnPending = true;
}

void GSRadio::scheduleTurnOff() {
    // FUN_007c9aa0
    if ( !bOn ) {
        return;
    }

    const float delay =
        ( pListener != nullptr ) ? pListener->getTurnOffDelay() : 0.0f;
    TurnOffTime = gpTimer->GameTotalTime + delay;
    bTurnOffPending = true;
}

void GSRadio::increaseVolume( const float step ) {
    // FUN_007c9be0
    if ( Volume < kMaxVolume ) {
        const float newVolume = Clamp( Volume + step, 0.0f, kMaxVolume );
        if ( Volume == 0.0f ) {
            scheduleTurnOn();
        }
        if ( setVolume( newVolume ) ) {
            Volume = newVolume;
        }
    }

    if ( pListener != nullptr ) {
        pListener->onVolumeUp();
    }
}

void GSRadio::decreaseVolume( const float step ) {
    // FUN_007c9b60
    if ( Volume > 0.0f ) {
        const float newVolume = Clamp( Volume - step, 0.0f, kMaxVolume );
        if ( setVolume( newVolume ) ) {
            Volume = newVolume;
            if ( newVolume == 0.0f ) {
                scheduleTurnOff();
            }
        }
    }

    if ( pListener != nullptr ) {
        pListener->onVolumeDown();
    }
}

void GSRadio::nextStation() {
    // FUN_007c9890
    if ( pListener != nullptr ) {
        pListener->onNextStation();
    }
    selectNextStation();
}

void GSRadio::updateTitle( const float deltaTime, const float ) {
    // FUN_007c9c80
    char title[0x84] = {};
    if ( strlen( getTitle() ) == 0 ) {
        strncpy( title, getStationName(), sizeof( title ) - 1 );
    } else if ( strlen( getArtist() ) == 0 ) {
        strncpy( title, getTitle(), sizeof( title ) - 1 );
    } else {
        snprintf( title, sizeof( title ), "%s%s%s", getArtist(), " - ",
                  getTitle() );
    }

    pTitle->setText( title );
    pTitle->setLogo( getStationName() );
    pTitle->setVolume( Volume );
    pTitle->update( deltaTime );
}
