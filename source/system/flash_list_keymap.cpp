#include "shared.h"
#include "flash_list_keymap.h"

#include "config/gs_config.h"
#include "database/gs_database.h"
#include "flash/gs_flash.h"
#include "flash/implementation/flash_player.h"
#include "flash/mng_flash_localize.h"
#include "flash/modal_message_box.h"
#include "flash/movie_player.h"
#include "input/ed_dev.h"
#include "input/input_device.h"
#include "input/input_templates.h"
#include "player_data/opentdu_settings.h"

static constexpr const char *kKeymapPath = "/keymap";           // DAT_00f2e81c
static constexpr const char *kKeymapListPath = "/keymap/list";  // DAT_00f2e85c
static constexpr const char *kBindKeyPath = "/BindKey";         // DAT_00f2ea8c
static constexpr const char *kNotLocalized = "_NOT LOCALIZED_"; // DAT_00f1664c

static constexpr float kCalibrationDelay = 2.0f; // DAT_00f416b0
static constexpr float kAxisDeadZone = 0.25f;    // DAT_00f416dc
static constexpr float kAxisSettleDelay = 0.5f;  // FLOAT_00f41674

static constexpr uint64_t kCalibrationTitle = 0x36b591aull;
static constexpr uint64_t kCalibrationMessage = 0x35c16daull;
static constexpr uint64_t kReleaseKeysMessage = 0x37a9b5aull;
static constexpr uint64_t kPressKeyMessage = 0x378edaaull;
static constexpr uint64_t kTooManyKeysMessage = 0x3882feaull;
static constexpr uint64_t kKeyConflictPrompt = 0x34cd49aull;
static constexpr uint64_t kUnboundLabel = 0x397722aull;
static constexpr uint64_t kKeyboardLabel = 0x3b5f6aaull;
static constexpr uint64_t kMouseLabel = 0x3a6b46aull;
static constexpr uint64_t kJoystickLabel = 0x32e501aull;
static constexpr uint64_t kWheelLabel = 0x33d925aull;
static constexpr uint64_t kPadLabel = 0x3c538eaull;

static const char *GetDatabaseString( const uint64_t hashcode ) {
    return ( gpDatabase != nullptr )
               ? gpDatabase->getStringByHashcode( 0, hashcode )
               : nullptr;
}

static const char *GetActionLabel( const SavedInputBinding &binding ) {
    // FUN_00896660
    const PCOptionMapping *pMapping = FindPCOptionMapping( binding.ActionName );
    const char *pLabel = ( pMapping != nullptr )
                             ? GetDatabaseString( pMapping->ActionID )
                             : nullptr;
    return ( pLabel != nullptr ) ? pLabel : kNotLocalized;
}

static void SetMoviesInputLocked( const bool bLocked ) {
    for ( int32_t levelIndex = 0; levelIndex < 2; levelIndex++ ) {
        const int32_t numMovies = gpFlash->getNumMovies( levelIndex );
        for ( int32_t movieIndex = 0; movieIndex < numMovies; movieIndex++ ) {
            MoviePlayer *pMoviePlayer =
                gpFlash->getMoviePlayer( levelIndex, movieIndex );
            if ( pMoviePlayer == nullptr ) {
                continue;
            }

            if ( bLocked ) {
                pMoviePlayer->Flags |= 0x20u;
            } else {
                pMoviePlayer->Flags &= ~0x20u;
            }
        }
    }
}

static InputChannel *FindDeviceChannel( const eDeviceType type,
                                        const char *pChannelName ) {
    Device *pDevice = gEdDev.getDevice( type, 0 );
    if ( pDevice == nullptr ) {
        return nullptr;
    }

    const int32_t channelIndex = pDevice->getChannelIndex( pChannelName );
    return ( channelIndex >= 0 ) ? &pDevice->getChannels()[channelIndex]
                                 : nullptr;
}

static bool IsWindowsKeyChannel( const InputChannel &channel ) {
    return strcmp( channel.pName, "ROS" ) == 0 ||
           strcmp( channel.pName, "APPLICATION" ) == 0;
}

static void OnKeyConflictPromptClosed( ePopupAnswer, void *, int32_t ) {
    // FUN_007c62a0
    for ( uint32_t i = 0; i < gEdDev.MaxNumActions; i++ ) {
        DeviceAction &action = gEdDev.pActions[i];
        if ( action.pChannel != nullptr ) {
            action.Flags &= 0xe0;
        }
    }
}

void KeyBindCapture::setMessage( const uint64_t messageHashcode ) {
    const char *pMessage = GetDatabaseString( messageHashcode );
    pPlayer->setVariableValue( "/BindKey:message",
                               ( pMessage != nullptr ) ? pMessage : "" );
}

void KeyBindCapture::openCalibration() {
    // FUN_007c43f0
    pCapturedChannel = nullptr;
    State = S_Calibrating;
    Timer = 0.0f;

    pPlayer->showFrame( "open", kBindKeyPath, false );
    const char *pTitle = GetDatabaseString( kCalibrationTitle );
    pPlayer->setVariableValue( "/BindKey:title",
                               ( pTitle != nullptr ) ? pTitle : "" );
    setMessage( kCalibrationMessage );

    SetMoviesInputLocked( true );
}

void KeyBindCapture::start( const char *pTitle ) {
    // FUN_007c4310
    if ( NumSnapshots == 0 ) {
        return;
    }

    pCapturedChannel = nullptr;
    State = S_WaitRelease;

    pPlayer->showFrame( "open", kBindKeyPath, false );
    pPlayer->setVariableValue( "/BindKey:title", pTitle );
    setMessage( kReleaseKeysMessage );

    SetMoviesInputLocked( true );
}

void KeyBindCapture::close() {
    // FUN_007c4280
    State = S_Idle;
    pPlayer->showFrame( "default", kBindKeyPath, false );

    SetMoviesInputLocked( false );
}

void KeyBindCapture::takeSnapshot() {
    // FUN_007c4160
    NumSnapshots = 0;

    for ( uint32_t deviceIdx = 0; deviceIdx < gEdDev.NumDevices; deviceIdx++ ) {
        Device *pDevice = gEdDev.ppDevices[deviceIdx];
        if ( pDevice == nullptr ||
             pDevice->getType() == eDeviceType::DT_Mouse ) {
            continue;
        }

        for ( uint32_t channelIdx = 0; channelIdx < pDevice->getNumChannels();
              channelIdx++ ) {
            if ( NumSnapshots >= kMaxSnapshots ) {
                return;
            }

            const InputChannel &channel = pDevice->getChannels()[channelIdx];
            ChannelSnapshot &snapshot = Snapshots[NumSnapshots];
            NumSnapshots++;

            strncpy( snapshot.DeviceName, pDevice->getName(),
                     sizeof( snapshot.DeviceName ) - 1 );
            strncpy( snapshot.ChannelName, channel.pName,
                     sizeof( snapshot.ChannelName ) - 1 );

            if ( channel.Unknown ==
                 int32_t( eInputChannelType::ICT_Digital ) ) {
                snapshot.ButtonState = ( channel.Flags & 0x10u ) >> 4;
            }
            if ( channel.Unknown == int32_t( eInputChannelType::ICT_Analog ) ) {
                snapshot.AxisValue = channel.InputValue;
            }
        }
    }
}

int32_t KeyBindCapture::detectChanges() {
    // FUN_007c3f60
    NumChangedChannels = 0;

    int32_t snapshotIdx = 0;
    for ( uint32_t deviceIdx = 0; deviceIdx < gEdDev.NumDevices; deviceIdx++ ) {
        Device *pDevice = gEdDev.ppDevices[deviceIdx];
        if ( pDevice == nullptr ||
             pDevice->getType() == eDeviceType::DT_Mouse ) {
            continue;
        }

        for ( uint32_t channelIdx = 0; channelIdx < pDevice->getNumChannels();
              channelIdx++, snapshotIdx++ ) {
            if ( snapshotIdx >= kMaxSnapshots ) {
                return NumChangedChannels;
            }

            InputChannel &channel = pDevice->getChannels()[channelIdx];
            if ( IsWindowsKeyChannel( channel ) ) {
                continue;
            }

            const ChannelSnapshot &snapshot = Snapshots[snapshotIdx];

            bool bChanged = false;
            uint8_t direction = 0u;
            if ( channel.Unknown ==
                 int32_t( eInputChannelType::ICT_Digital ) ) {
                const uint32_t buttonState = ( channel.Flags & 0x10u ) >> 4;
                bChanged = ( snapshot.ButtonState != buttonState );
                direction = uint8_t( buttonState ^ 1u );
            } else if ( channel.Unknown ==
                        int32_t( eInputChannelType::ICT_Analog ) ) {
                const float delta = snapshot.AxisValue - channel.InputValue;
                bChanged = ( kAxisDeadZone < fabsf( delta ) );
                direction = uint8_t( 0.0f < delta );
            }

            if ( !bChanged ) {
                continue;
            }

            if ( NumChangedChannels < kMaxChangedChannels ) {
                pChangedDevices[NumChangedChannels] = pDevice;
                pChangedChannels[NumChangedChannels] = &channel;
                ChangedDirections[NumChangedChannels] = direction;
            }
            NumChangedChannels++;
        }
    }

    return NumChangedChannels;
}

int32_t KeyBindCapture::tick( const float deltaTime ) {
    // FUN_007c44e0
    if ( State == S_Idle ) {
        return S_Idle;
    }

    const InputChannel *pEscape =
        FindDeviceChannel( eDeviceType::DT_Keyboard, "ESC" );
    const bool bEscapePressed =
        pEscape != nullptr && IsChannelJustPressed( pEscape );

    // Flags2 bit 0 = released this frame
    const InputChannel *pMouseButton =
        FindDeviceChannel( eDeviceType::DT_Mouse, "BUTTON_0" );
    const bool bMouseClicked =
        pMouseButton != nullptr && ( pMouseButton->Flags2 & 1u ) != 0;

    if ( bEscapePressed || ( State == S_Capturing && bMouseClicked ) ) {
        close();
        pCapturedChannel = nullptr;
    }

    switch ( State ) {
    case S_Calibrating:
        if ( NumSnapshots > 0 && detectChanges() == 0 ) {
            Timer += deltaTime;
            if ( kCalibrationDelay < Timer ) {
                close();
            }
            return State;
        }

        Timer = 0.0f;
        takeSnapshot();
        return State;

    case S_WaitRelease:
        setMessage( kReleaseKeysMessage );
        if ( detectChanges() != 0 ) {
            return State;
        }

        State = S_Capturing;
        Timer = 0.0f;
        return State;

    case S_Capturing: {
        const int32_t numChanged = detectChanges();
        if ( 2 < numChanged ||
             ( numChanged == 2 && pChangedDevices[0] != pChangedDevices[1] ) ) {
            setMessage( kTooManyKeysMessage );
            Timer = 0.0f;
            return State;
        }

        if ( numChanged == 0 ) {
            setMessage( kPressKeyMessage );
            Timer = 0.0f;
            return State;
        }

        Timer += deltaTime;

        InputChannel *pChannel = pChangedChannels[0];
        const bool bAnalog =
            pChannel->Unknown == int32_t( eInputChannelType::ICT_Analog );
        if ( Timer <= kAxisSettleDelay && bAnalog ) {
            return State;
        }

        if ( numChanged == 2 ) {
            if ( pChannel->field_0x2c != pChangedChannels[1]->field_0x2c ) {
                setMessage( kTooManyKeysMessage );
                Timer = 0.0f;
                return State;
            }
            bCapturedCombination = true;
        } else {
            bCapturedCombination = false;
        }

        pCapturedChannel = pChannel;
        bCapturedInverted = ChangedDirections[0] != 0u;
        Timer = 0.0f;

        if ( bAnalog ) {
            State = S_WaitAxisRelease;
            return State;
        }
        break;
    }

    case S_WaitAxisRelease:
        setMessage( kReleaseKeysMessage );
        if ( detectChanges() != 0 ) {
            return State;
        }
        break;

    default:
        return State;
    }

    close();
    return State;
}

FlashListKeymap::FlashListKeymap()
    : FlashList(),
      pConfigPlayer( nullptr ),
      pBindings( nullptr ),
      pPlayerConfig( nullptr ),
      backupBindings(),
      bCapturePending( false ),
      bCalibrating( false ),
      capture(),
      currentTab( 0 ),
      numTabs( SavedInputBindings::kNumCategories ),
      backupTemplateSelection() {
    // FUN_007c6320
    field16_0x67 = true;
}

void FlashListKeymap::bind( FlashPlayer *pPlayer,
                            SavedInputBindings *pLiveBindings,
                            PlayerConfiguration *pInputConfig ) {
    // FUN_007c6260
    pBindings = pLiveBindings;
    pConfigPlayer = pPlayer;
    pPlayerConfig = pInputConfig;
    setPlayer( pPlayer, kKeymapListPath );
    capture.pPlayer = pPlayer;
    bIsEmpty = false;
}

bool FlashListKeymap::onMessage( FlashMessage *pMessage,
                                 FlashPlayer *pPlayer ) {
    // FUN_007c6910
    if ( *pMessage == kTab ) {
        const int32_t tabIndex =
            pPlayer->getVariableAsInt( "/keymap/list:cur_onglet" );
        selectTab( tabIndex - 1 );
        return true;
    }

    if ( *pMessage == kCancel ) {
        pConfigPlayer->showFrame( "close", kKeymapPath, true );
        *pBindings = backupBindings;
        // OTDU_ADDITION
        InputTemplates::SetSelection( backupTemplateSelection );
        return true;
    }

    if ( *pMessage == kDefaults ) {
        pBindings->resetToDefaults();
        reset();
        InputTemplates::PromptFromDefaults(); // OTDU_ADDITION
        return true;
    }

    if ( *pMessage == kGetKey ) {
        startKeyCapture();
        return true;
    }

    if ( *pMessage == kDeleteKey ) {
        deleteSelectedKey();
        return true;
    }

    if ( *pMessage == kValidate ) {
        pConfigPlayer->showFrame( "close", kKeymapPath, true );
        if ( pPlayerConfig != nullptr ) {
            pBindings->applyToActions( *pPlayerConfig );
        }

        // OTDU_ADDITION
        if ( InputTemplates::GetSelection() != backupTemplateSelection ) {
            OpenTDUSettings::Save();
        }
        return true;
    }

    return FlashList::onMessage( pMessage, pPlayer );
}

void FlashListKeymap::tick( float deltaTime, float totalTime ) {
    // FUN_007c6b30
    FlashList::tick( deltaTime, totalTime );

    if ( !bCapturePending && !bCalibrating ) {
        return;
    }

    const int32_t captureState = capture.tick( deltaTime );
    if ( bCapturePending && captureState == KeyBindCapture::S_Idle ) {
        bCapturePending = false;
        onKeyCaptured();
        return;
    }

    if ( bCalibrating && captureState == KeyBindCapture::S_Idle ) {
        bCalibrating = false;
    }
}

int32_t FlashListKeymap::getListLength() const {
    // FUN_007c62f0
    if ( pBindings == nullptr ) {
        return 0;
    }

    if ( currentTab < 0 ) {
        return pBindings->NumEntries;
    }

    return pBindings->NumEntriesPerCategory[Min( currentTab, numTabs - 1 )];
}

void FlashListKeymap::open() {
    // FUN_007c68c0
    if ( bOpened ) {
        return;
    }

    backupBindings = *pBindings;
    backupTemplateSelection = InputTemplates::GetSelection(); // OTDU_ADDITION
    currentTab = 0;
    FlashList::open();
    field16_0x67 = true;

    bCalibrating = true;
    capture.openCalibration();
}

void FlashListKeymap::close() {
    // FUN_007c6230
    bOpened = false;
}

void FlashListKeymap::update( const int32_t entryIndex,
                              const int32_t lineIndex ) {
    // FUN_007c65d0
    if ( getListLength() <= entryIndex ) {
        setLineState( eLineState::FLLS_Empty, lineIndex );
        return;
    }

    char linePath[128];
    char variablePath[128];
    snprintf( linePath, sizeof( linePath ), "%s/line%d", movieName.c_str(),
              lineIndex + 1 );
    snprintf( variablePath, sizeof( variablePath ), "%s/line%d/:IsValid",
              movieName.c_str(), lineIndex + 1 );
    pMovie->setVariableValue( variablePath, 1 );
    pMovie->showFrame( "normal", linePath, false );

    const SavedInputBinding &binding =
        pBindings->Entries[getBindingIndex( entryIndex )];

    char deviceLabel[0x40];
    writeDeviceLabel( binding, deviceLabel, sizeof( deviceLabel ) );

    auto setColumn = [&]( const char *pColumn, const char *pValue,
                          const bool bLinked ) {
        snprintf( variablePath, sizeof( variablePath ), "%s/line%d:%s",
                  movieName.c_str(), lineIndex + 1, pColumn );
        if ( bLinked ) {
            pMovie->setVariableValue( variablePath, pValue );
        } else {
            pMovie->setVariableValueNonLinked( variablePath, pValue );
        }
    };

    setColumn( "action", GetActionLabel( binding ), true );

    if ( binding.DisplayName[0] == '\0' ) {
        const char *pUnbound = GetDatabaseString( kUnboundLabel );
        if ( pUnbound == nullptr ) {
            pUnbound = "";
        }
        setColumn( "key", pUnbound, true );
        setColumn( "device", pUnbound, true );
        return;
    }

    setColumn( "key", binding.DisplayName, false );
    setColumn( "device", deviceLabel, false );
}

int32_t FlashListKeymap::getBindingIndex( const int32_t entryIndex ) const {
    // FUN_00896060
    if ( currentTab < 0 || numTabs <= currentTab ) {
        return entryIndex;
    }

    const int32_t category = currentTab + 1;
    int32_t categoryIndex = 0;
    for ( int32_t i = 0; i < pBindings->NumEntries; i++ ) {
        const PCOptionMapping *pMapping =
            FindPCOptionMapping( pBindings->Entries[i].ActionName );
        if ( pMapping == nullptr || pMapping->Category != category ) {
            continue;
        }

        if ( categoryIndex == entryIndex ) {
            return i;
        }
        categoryIndex++;
    }

    return 0;
}

void FlashListKeymap::selectTab( const int32_t tabIndex ) {
    // FUN_007c67b0
    char tabPath[128];
    snprintf( tabPath, sizeof( tabPath ), "%s/onglet%d", movieName.c_str(),
              currentTab + 1 );
    pMovie->showFrame( "normal", tabPath, false );

    currentTab = Clamp( tabIndex, 0, numTabs - 1 );

    snprintf( tabPath, sizeof( tabPath ), "%s/onglet%d", movieName.c_str(),
              currentTab + 1 );
    pMovie->showFrame( "selected", tabPath, false );

    reset();
}

void FlashListKeymap::startKeyCapture() {
    // FUN_007c64e0
    pConfigPlayer->setVariableValue( "/:IsWrongSound", 0 );

    const int32_t bindingIndex =
        getBindingIndex( firstLine + selectedLineIndex );
    bCapturePending = true;

    char variablePath[128];
    snprintf( variablePath, sizeof( variablePath ), "%s/line%d:%s",
              movieName.c_str(), selectedLineIndex + 1, "key" );
    pMovie->setVariableValueNonLinked( variablePath, "" );
    snprintf( variablePath, sizeof( variablePath ), "%s/line%d:%s",
              movieName.c_str(), selectedLineIndex + 1, "device" );
    pMovie->setVariableValueNonLinked( variablePath, "" );

    capture.start( GetActionLabel( pBindings->Entries[bindingIndex] ) );
}

void FlashListKeymap::deleteSelectedKey() {
    // FUN_007c6430
    const int32_t bindingIndex =
        getBindingIndex( firstLine + selectedLineIndex );

    // FUN_008960d0 (inlined)
    if ( strncmp( pBindings->Entries[bindingIndex].ActionName, "MENU_QUIT",
                  10 ) == 0 ) {
        pConfigPlayer->setVariableValue( "/:IsWrongSound", 1 );
        return;
    }

    pConfigPlayer->setVariableValue( "/:IsWrongSound", 0 );
    if ( bindingIndex >= 0 && bindingIndex < pBindings->NumEntries ) {
        pBindings->Entries[bindingIndex].setChannel( nullptr, false, false );
    }
    reset();
}

void FlashListKeymap::onKeyCaptured() {
    // FUN_007c6a50
    InputChannel *pChannel = capture.pCapturedChannel;
    if ( pChannel != nullptr ) {
        const int32_t bindingIndex =
            getBindingIndex( firstLine + selectedLineIndex );

        const char *pConflictingAction =
            findConflictingAction( bindingIndex, *pChannel );
        if ( pConflictingAction == nullptr ) {
            // FUN_00896fd0
            if ( bindingIndex >= 0 && bindingIndex < pBindings->NumEntries ) {
                pBindings->Entries[bindingIndex].setChannel(
                    pChannel, capture.bCapturedInverted,
                    capture.bCapturedCombination );
            }
            reset();
            return;
        }

        if ( gpMngFlashLocalize != nullptr ) {
            gpMngFlashLocalize->setSysVar( "ACTION", pConflictingAction );
            TestDrive::DisplayModalPrompt( false, kKeyConflictPrompt, 0,
                                           OnKeyConflictPromptClosed, nullptr,
                                           nullptr, 0, nullptr, true, false,
                                           MPT_Warning, gpFlashMessageBox );
            gpMngFlashLocalize->clearAllSysVar();
        }
    }

    reset();
}

void FlashListKeymap::writeDeviceLabel( const SavedInputBinding &binding,
                                        char *pOutLabel,
                                        const size_t outLabelLength ) const {
    // FUN_00896520
    uint64_t labelHashcode = kPadLabel;
    switch ( eDeviceType( binding.DeviceType ) ) {
    case eDeviceType::DT_Keyboard:
        labelHashcode = kKeyboardLabel;
        break;
    case eDeviceType::DT_Mouse:
        labelHashcode = kMouseLabel;
        break;
    case eDeviceType::DT_Other:
        if ( eDeviceSpecialization( binding.DeviceSubType ) ==
             eDeviceSpecialization::DS_None ) {
            labelHashcode = kJoystickLabel;
        } else if ( eDeviceSpecialization( binding.DeviceSubType ) ==
                    eDeviceSpecialization::DS_SteeringWheel ) {
            labelHashcode = kWheelLabel;
        }
        break;
    default:
        pOutLabel[0] = '\0';
        return;
    }

    const char *pLabel = GetDatabaseString( labelHashcode );
    strncpy( pOutLabel, ( pLabel != nullptr ) ? pLabel : "",
             outLabelLength - 1 );
    pOutLabel[outLabelLength - 1] = '\0';

    if ( binding.DeviceIndex != 0u ) {
        const size_t labelLength = strlen( pOutLabel );
        snprintf( pOutLabel + labelLength, outLabelLength - labelLength, " %d",
                  binding.DeviceIndex + 1 );
    }
}

const char *
FlashListKeymap::findConflictingAction( const int32_t bindingIndex,
                                        const InputChannel &channel ) const {
    // FUN_00896ab0
    const PCOptionMapping *pMapping =
        FindPCOptionMapping( pBindings->Entries[bindingIndex].ActionName );
    if ( pMapping == nullptr ) {
        return nullptr;
    }

    for ( int32_t i = 0; i < pBindings->NumEntries; i++ ) {
        const SavedInputBinding &other = pBindings->Entries[i];
        if ( i == bindingIndex || !other.isBoundTo( channel ) ) {
            continue;
        }

        const PCOptionMapping *pOtherMapping =
            FindPCOptionMapping( other.ActionName );
        if ( pOtherMapping == nullptr ) {
            continue;
        }

        const uint8_t group = pMapping->Unknown4;
        const uint8_t otherGroup = pOtherMapping->Unknown4;
        if ( otherGroup == group || ( gActionExclusions[otherGroup] &
                                      ( 1 << ( group & 0x1f ) ) ) != 0 ) {
            return GetActionLabel( other );
        }
    }

    return nullptr;
}
