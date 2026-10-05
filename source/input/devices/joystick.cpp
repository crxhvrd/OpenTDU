#include "shared.h"
#include "joystick.h"

#include <SDL3/SDL.h>

// DInput8 to SDL3 helpers (would be nice to make those constants instead of
// macros...)
#define OTDU_CHANNEL_KIND_MASK 0x3u
#define OTDU_CHANNEL_KIND_BUTTON 0x0u
#define OTDU_CHANNEL_KIND_AXIS 0x1u
#define OTDU_CHANNEL_KIND_POV 0x2u

#define OTDU_CHANNEL_DIRECTION_MASK 0xCu
#define OTDU_CHANNEL_DIRECTION_SHIFT 2

#define OTDU_CHANNEL_AXIS_NEGATIVE 0x0u
#define OTDU_CHANNEL_AXIS_POSITIVE 0x4u

#define OTDU_CHANNEL_POV_UP 0x0u
#define OTDU_CHANNEL_POV_RIGHT 0x4u
#define OTDU_CHANNEL_POV_DOWN 0x8u
#define OTDU_CHANNEL_POV_LEFT 0xCu

static constexpr float kAxisScale = 1.0f / 32767.0f; // DAT_00f4167c
static constexpr int32_t kPOVCentered = -1;
static constexpr int32_t kNumPOVDirections = 4;

static constexpr const char *kPOVDirectionNames[kNumPOVDirections] = {
    "UP", "RIGHT", "DOWN", "LEFT" }; // NOTE: order matches the direction bits

struct JoystickAxisChannel {
    int32_t Axis;
    const char *pNegativeName;
    const char *pPositiveName;
};

static constexpr JoystickAxisChannel kGamepadAxes[] = {
    {SDL_GAMEPAD_AXIS_LEFTX,         "LEFT",         "RIGHT"        },
    {SDL_GAMEPAD_AXIS_LEFTY,         "UP",           "DOWN"         },
    {SDL_GAMEPAD_AXIS_RIGHTX,        "STICK_1_LEFT", "STICK_1_RIGHT"},
    {SDL_GAMEPAD_AXIS_RIGHTY,        "STICK_1_UP",   "STICK_1_DOWN" },

    // DirectInput merged both triggers into the Z axis, the left one driving it
    // positive and the right one negative.
    {SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, nullptr,        "WHEEL_DOWN"   },
    {SDL_GAMEPAD_AXIS_LEFT_TRIGGER,  nullptr,        "WHEEL_UP"     },
};

static constexpr JoystickAxisChannel kJoystickAxes[] = {
    {0, "LEFT",         "RIGHT"        },
    {1, "UP",           "DOWN"         },
    {2, "WHEEL_DOWN",   "WHEEL_UP"     },
    {3, "STICK_1_LEFT", "STICK_1_RIGHT"},
    {4, "STICK_1_UP",   "STICK_1_DOWN" },
};

// BUTTON_n numbering follows the order DirectInput reported for an XInput pad
static constexpr SDL_GamepadButton kGamepadButtons[] = {
    SDL_GAMEPAD_BUTTON_SOUTH,          // BUTTON_0
    SDL_GAMEPAD_BUTTON_EAST,           // BUTTON_1
    SDL_GAMEPAD_BUTTON_WEST,           // BUTTON_2
    SDL_GAMEPAD_BUTTON_NORTH,          // BUTTON_3
    SDL_GAMEPAD_BUTTON_LEFT_SHOULDER,  // BUTTON_4
    SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, // BUTTON_5
    SDL_GAMEPAD_BUTTON_BACK,           // BUTTON_6
    SDL_GAMEPAD_BUTTON_START,          // BUTTON_7
    SDL_GAMEPAD_BUTTON_LEFT_STICK,     // BUTTON_8
    SDL_GAMEPAD_BUTTON_RIGHT_STICK,    // BUTTON_9
};

static constexpr SDL_GamepadButton kGamepadDPad[kNumPOVDirections] = {
    SDL_GAMEPAD_BUTTON_DPAD_UP, SDL_GAMEPAD_BUTTON_DPAD_RIGHT,
    SDL_GAMEPAD_BUTTON_DPAD_DOWN, SDL_GAMEPAD_BUTTON_DPAD_LEFT };

static bool IsPOVDirectionPressed( const int32_t angle,
                                   const uint32_t direction ) {
    // FUN_004365f0
    if ( angle < 0 ) {
        return false;
    }

    switch ( direction ) {
    case 0:
        return angle <= 8999 || angle >= 27001;
    case 1:
        return angle >= 1 && angle <= 17999;
    case 2:
        return angle >= 9001 && angle <= 26999;
    case 3:
        return angle >= 18001 && angle <= 35999;
    default:
        return false;
    }
}

static int32_t SDLHatToPOVAngle( const uint8_t hatState ) {
    switch ( hatState ) {
    case SDL_HAT_UP:
        return 0;
    case SDL_HAT_RIGHTUP:
        return 4500;
    case SDL_HAT_RIGHT:
        return 9000;
    case SDL_HAT_RIGHTDOWN:
        return 13500;
    case SDL_HAT_DOWN:
        return 18000;
    case SDL_HAT_LEFTDOWN:
        return 22500;
    case SDL_HAT_LEFT:
        return 27000;
    case SDL_HAT_LEFTUP:
        return 31500;
    default:
        return kPOVCentered;
    }
}

DeviceJoystick::DeviceJoystick()
    : Device(),
      field_0x70( 0 ),
      field_0x74( 0 ),
      field_0x78( false ),
      field_0x7c( 0 ),
      field_0x80( 0 ),
      joystickID( 0 ),
      pJoystick( nullptr ),
      pGamepad( nullptr ) {}

DeviceJoystick::~DeviceJoystick() {
    SDL_CloseGamepad( pGamepad );
    SDL_CloseJoystick( pJoystick );
}

bool DeviceJoystick::initialize( SDL_JoystickID param_1 ) {
    // FUN_004363b0
    joystickID = param_1;

    pJoystick = SDL_OpenJoystick( joystickID );
    if ( pJoystick == nullptr ) {
        return false;
    }

    if ( SDL_IsGamepad( joystickID ) ) {
        pGamepad = SDL_OpenGamepad( joystickID );
    }

    SDL_JoystickType type = SDL_GetJoystickTypeForID( joystickID );
    switch ( type ) {
    case SDL_JOYSTICK_TYPE_GAMEPAD: {
        deviceType = eDeviceType::DT_Other;
        specializedType = eDeviceSpecialization::DS_Gamepad;
        setName( "Pad" ); // DAT_00f3ea9c
        break;
    }

    case SDL_JOYSTICK_TYPE_WHEEL: {
        deviceType = eDeviceType::DT_Other;
        specializedType = eDeviceSpecialization::DS_SteeringWheel;
        setName( "Wheel" ); // DAT_00f3ea94
        break;
    }

    default: {
        OTDU_LOG_WARN(
            "Unhandled joystick with type %u (assuming generic joystick)\n",
            type );

        deviceType = eDeviceType::DT_Other;
        specializedType = eDeviceSpecialization::DS_None;
        setName( "Joystick" ); // DAT_00f3eaa0
        break;
    }
    }

    if ( !createChannels() ) {
        return false;
    }

    flags = ( flags ^ ( flags ^ 0x01 ) ) & 1; // set connected bit
    flags |= 2;
    setEnabled();

    return true;
}

bool DeviceJoystick::createChannels() {
    // FUN_004354f0 + FUN_004368b0
    const bool bUseGamepadLayout = pGamepad != nullptr;

    const JoystickAxisChannel *pAxes =
        bUseGamepadLayout ? kGamepadAxes : kJoystickAxes;

    int32_t numButtons = 0;
    int32_t numHats = 0;
    int32_t numAxes = 0;

    if ( bUseGamepadLayout ) {
        numButtons = int32_t( std::size( kGamepadButtons ) );
        numHats = 1;
        numAxes = int32_t( std::size( kGamepadAxes ) );
    } else {
        numButtons = SDL_GetNumJoystickButtons( pJoystick );
        numHats = SDL_GetNumJoystickHats( pJoystick );
        numAxes = std::min( SDL_GetNumJoystickAxes( pJoystick ),
                            int32_t( std::size( kJoystickAxes ) ) );
    }

    int32_t numAxisChannels = 0;
    for ( int32_t i = 0; i < numAxes; i++ ) {
        numAxisChannels += pAxes[i].pNegativeName != nullptr ? 2 : 1;
    }

    numChannelButtons = uint32_t( numButtons + numHats * kNumPOVDirections );
    numChannelAxis = uint32_t( numAxisChannels );
    OTDU_UNIMPLEMENTED_ONCE; // TODO: FFB
    unknown = 0;
    channelCount = numChannelButtons + numChannelAxis;

    if ( allocateChannels( channelCount ) == nullptr ) {
        return false;
    }

    uint32_t cursor = 0;

    for ( int32_t i = 0; i < numButtons; i++ ) {
        InputChannel &channel = pChannels[cursor++];
        channel.Unknown = 0;
        ;
        channel.Index = bUseGamepadLayout ? int32_t( kGamepadButtons[i] ) : i;
        channel.Flags = OTDU_CHANNEL_KIND_BUTTON;
        snprintf( channel.pName, sizeof( channel.pName ), "BUTTON_%d", i );
    }

    for ( int32_t hat = 0; hat < numHats; hat++ ) {
        for ( int32_t direction = 0; direction < kNumPOVDirections;
              direction++ ) {
            InputChannel &channel = pChannels[cursor++];
            channel.Unknown = 0;
            ;
            channel.Index = hat;
            channel.Flags = uint16_t(
                OTDU_CHANNEL_KIND_POV |
                ( uint32_t( direction ) << OTDU_CHANNEL_DIRECTION_SHIFT ) );
            snprintf( channel.pName, sizeof( channel.pName ), "POV_%d_%s", hat,
                      kPOVDirectionNames[direction] );
        }
    }

    auto addAxisChannel = [&]( const int32_t axis, const char *pChannelName,
                               const uint32_t direction ) {
        InputChannel &channel = pChannels[cursor++];
        channel.Unknown = 1;
        ;
        channel.Index = axis;
        channel.Flags = uint16_t( OTDU_CHANNEL_KIND_AXIS | direction );
        strncpy( channel.pName, pChannelName, sizeof( channel.pName ) - 1 );
        channel.pName[sizeof( channel.pName ) - 1] = '\0';
    };

    for ( int32_t i = 0; i < numAxes; i++ ) {
        const JoystickAxisChannel &axis = pAxes[i];
        if ( axis.pNegativeName != nullptr ) {
            addAxisChannel( axis.Axis, axis.pNegativeName,
                            OTDU_CHANNEL_AXIS_NEGATIVE );
        }
        addAxisChannel( axis.Axis, axis.pPositiveName,
                        OTDU_CHANNEL_AXIS_POSITIVE );
    }

    OTDU_ASSERT( cursor == channelCount );

    for ( uint32_t i = 0; i < channelCount; i++ ) {
        InputChannel &channel = pChannels[i];
        if ( channel.Unknown == 0 ) {
            channel.Unknown4 = 10.0f;
            channel.Unknown5 = 10.0f;
        } else {
            channel.Unknown4 = 0.5f;
            channel.Unknown5 = 0.5f;
        }
    }

    return true;
}

bool DeviceJoystick::readButton( const int32_t buttonIndex ) const {
    if ( pGamepad != nullptr ) {
        return SDL_GetGamepadButton( pGamepad,
                                     SDL_GamepadButton( buttonIndex ) );
    }
    return SDL_GetJoystickButton( pJoystick, buttonIndex );
}

int16_t DeviceJoystick::readAxis( const int32_t axisIndex ) const {
    if ( pGamepad != nullptr ) {
        return SDL_GetGamepadAxis( pGamepad, SDL_GamepadAxis( axisIndex ) );
    }
    return SDL_GetJoystickAxis( pJoystick, axisIndex );
}

int32_t DeviceJoystick::readPOVAngle( const int32_t hatIndex ) const {
    uint8_t hatState = SDL_HAT_CENTERED;

    if ( pGamepad != nullptr ) {
        static constexpr uint8_t kHatBits[kNumPOVDirections] = {
            SDL_HAT_UP, SDL_HAT_RIGHT, SDL_HAT_DOWN, SDL_HAT_LEFT };

        for ( int32_t i = 0; i < kNumPOVDirections; i++ ) {
            if ( SDL_GetGamepadButton( pGamepad, kGamepadDPad[i] ) ) {
                hatState |= kHatBits[i];
            }
        }
    } else {
        hatState = SDL_GetJoystickHat( pJoystick, hatIndex );
    }

    return SDLHatToPOVAngle( hatState );
}

void DeviceJoystick::setEnabled() {
    // FUN_004350c0
    // NOTE: DirectInput Acquire has no SDL3 equivalent; only update the flag.
    if ( ( flags & 2 ) != 0 ) {
        flags = flags & 0xfd;
    }
}

void DeviceJoystick::setDisabled() {
    // FUN_004350a0
    // NOTE: DirectInput Unacquire has no SDL3 equivalent; only update the flag.
    if ( ( flags & 2 ) == 0 ) {
        flags |= 2;
    }
}

bool DeviceJoystick::FUN_0042df00() {
    // FUN_0042df00
    return true;
}

bool DeviceJoystick::tick() {
    // FUN_004365f0
    if ( ( flags & 2 ) != 0 ) {
        return true; // disabled
    }
    if ( ( flags & 1 ) == 0 ) {
        return false; // not connected
    }

    // Digital channels [0..numChannelButtons)
    for ( uint32_t i = 0; i < numChannelButtons; i++ ) {
        InputChannel &ch = pChannels[i];
        uint8_t bVar5 = static_cast<uint8_t>( ch.Flags );
        uint8_t bVar8;

        if ( ( bVar5 & OTDU_CHANNEL_KIND_MASK ) == OTDU_CHANNEL_KIND_POV ) {
            const int32_t povAngle = readPOVAngle( ch.Index );
            const uint32_t direction =
                ( bVar5 & OTDU_CHANNEL_DIRECTION_MASK ) >>
                OTDU_CHANNEL_DIRECTION_SHIFT;
            bVar8 =
                IsPOVDirectionPressed( povAngle, direction ) ? 0x10u : 0x00u;
        } else {
            bVar8 = readButton( ch.Index ) ? 0x10u : 0x00u;
        }

        ch.Flags = static_cast<uint16_t>(
            ( ch.Flags & 0xFF00u ) |
            ( static_cast<uint8_t>( ( ( bVar8 ^ bVar5 ) & 0x10u ) ^ bVar5 ) ) );
    }

    // Analog channels [numChannelButtons..numChannelButtons+numChannelAxis)
    uint32_t analogEnd = numChannelButtons + numChannelAxis;
    for ( uint32_t i = numChannelButtons; i < analogEnd; i++ ) {
        InputChannel &ch = pChannels[i];
        int16_t rawAxis = readAxis( ch.Index );
        float fVar13;

        if ( ( ch.Flags & OTDU_CHANNEL_KIND_MASK ) == OTDU_CHANNEL_KIND_AXIS ) {
            if ( ( ch.Flags & OTDU_CHANNEL_DIRECTION_MASK ) ==
                 OTDU_CHANNEL_AXIS_NEGATIVE ) {
                fVar13 = ( rawAxis < 0 ) ? (float)( -rawAxis ) : 0.0f;
            } else {
                fVar13 = ( rawAxis > 0 ) ? (float)rawAxis : 0.0f;
            }
        } else {
            fVar13 = (float)rawAxis;
        }

        float value = fVar13 * kAxisScale;
        ch.InputValue = value > 1.0f ? 1.0f : value;
    }

    return true;
}

edGUID DeviceJoystick::getInstanceGUID() const {
    // FUN_00435a30
    const SDL_GUID sdlGUID = SDL_GetJoystickGUIDForID( joystickID );

    static constexpr SDL_GUID kNullSDLGUID = {};
    if ( memcmp( &sdlGUID, &kNullSDLGUID, sizeof( SDL_GUID ) ) == 0 ) {
        return kGUIDJoystick;
    }

    static_assert( sizeof( SDL_GUID ) == sizeof( edGUID ) );
    edGUID guid;
    memcpy( &guid, &sdlGUID, sizeof( edGUID ) );
    return guid;
}

void DeviceJoystick::getChannelDisplayName( const InputChannel &channel,
                                            char *pOutName,
                                            const size_t outNameLength ) const {
    // FUN_00436890
    if ( ( channel.Flags & OTDU_CHANNEL_KIND_MASK ) ==
             OTDU_CHANNEL_KIND_BUTTON &&
         channel.Unknown == 0 ) {
        const ptrdiff_t buttonIndex = &channel - pChannels;
        snprintf( pOutName, outNameLength, "Button %d",
                  int32_t( buttonIndex ) );
        return;
    }

    Device::getChannelDisplayName( channel, pOutName, outNameLength );
}
