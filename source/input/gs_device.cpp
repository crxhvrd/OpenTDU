#include "shared.h"
#include "gs_device.h"

#include "ed_dev.h"
#include "input_device.h"
#include "devices/joystick.h"
#include "input_templates.h"
#include "head_tracking.h"
#include "system/ed_sys.h"

#include "debug/mng_debug_hud.h"

#include "flash/modal_message_box.h"
#include "flash/gs_flash.h"

#include "config/gs_config.h"

#include "game/scenaric/gs_scenaric.h"
#include "game/vehicle/car_state.h"
#include "physics/vehicle/physics_vehicle.h"
#include "player_data/gs_playerdata.h"

#include "tdu_instance.h"

#include <cmath>

#ifdef OTDU_USE_IMGUI
#include "debug/gs_imgui.h"
#endif

// TODO: Move those globals to the device service instance
uint32_t gDefaultSensitivity = 8;

float gFFBFactor = 0.0f;
float gSteeringDeadZone = 0.20f;
float gSteeringLinearity = 50.0f;
float gSteeringTurn = 270.0f;
float gBrakeDeadZone = 0.20f;
float gAccelDeadZone = 0.20f; // DAT_00f8a28c
float gFFBLoadFactor[3] = { 0.0f, 0.0f, 0.0f };
float gFFBLoadFromAccel[3] = { 0.0f, 0.0f, 0.0f };
float gFFBSlipAngle[3][2] = {
    // DAT_010d6ec0
    {0.0f, 0.0f},
    {0.0f, 0.0f},
    {0.0f, 0.0f}
};
float gFFBSpeedFactor[4][2] = {
    // DAT_010d6ed8
    {0.0f, 0.0f},
    {0.0f, 0.0f},
    {0.0f, 0.0f},
    {0.0f, 0.0f}
};
bool gPedalsSeparated = false;
ForceFeedbackOutput gForceFeedbackOutput = {};

static constexpr float kFFBUnitsScale = 10000.0f; // DAT_00f416c4
static constexpr uint32_t kRumbleDurationMs = 100;

static DeviceJoystick *gpFFBDevice = nullptr;
static int32_t gFFBDamperEffect = -1;    // DAT_010e729c
static int32_t gFFBConstantEffect = -1;  // DAT_010e72a0
static bool gbFFBEffectsCreated = false; // BOOL_010e72ac
static bool gbFFBEffectsRunning = false;
static Uint16 gRumbleLevels[2] = { 0, 0 };
static uint64_t gRumbleHoldEndMs[2] = { 0, 0 };

static Uint16 HoldRumbleLevel( const int32_t motor,
                               const Uint16 level,
                               const uint64_t nowMs ) {
    if ( level >= gRumbleLevels[motor] || nowMs >= gRumbleHoldEndMs[motor] ) {
        gRumbleLevels[motor] = level;
        gRumbleHoldEndMs[motor] = nowMs + kRumbleDurationMs;
    }
    return gRumbleLevels[motor];
}

static Sint16 ToSDLHapticLevel( const float dinputValue ) {
    const float level = dinputValue * ( 32767.0f / kFFBUnitsScale );
    return Sint16( std::clamp( level, -32767.0f, 32767.0f ) );
}

static Uint16 ToSDLRumbleStrength( const float value ) {
    return Uint16( std::clamp( value, 0.0f, 1.0f ) * 65535.0f );
}

static void SetAxisChannelThresholds( Device *pDevice ) {
    // FUN_00980af0 (inlined)
    float fVar19 = 0.8f;
    for ( uint32_t iVar2 = 0; iVar2 < pDevice->getNumAxis(); iVar2++ ) {
        InputChannel *peVar10 = pDevice->getChannels();

        uint32_t iVar17 = 0;
        for ( uint32_t iVar15 = 0; iVar15 < pDevice->getNumChannels();
              iVar15++, peVar10++ ) {
            if ( peVar10 != nullptr && peVar10->Unknown == 1 ) {
                if ( iVar17 == iVar2 ) {
                    peVar10 = &pDevice->getChannels()[iVar15];
                    peVar10->Unknown4 = fVar19;
                    peVar10->Unknown5 = fVar19;
                    break;
                }
                iVar17++;
            }
        }
    }
}

static void UnbindDeviceChannels( DeviceAction &action,
                                  const Device *pDevice ) {
    int32_t numKeptChannels = 0;
    for ( int32_t i = 0; i < action.NumAddedChannels; i++ ) {
        const InputChannel *pChannel = action.pChannel[i].pChannel;
        if ( pChannel != nullptr && pChannel->pParent != pDevice ) {
            action.pChannel[numKeptChannels++] = action.pChannel[i];
        }
    }

    for ( int32_t i = numKeptChannels; i < action.NumAddedChannels; i++ ) {
        action.pChannel[i] = InputChanneMapping();
    }
    action.NumAddedChannels = numKeptChannels;
}

static void UnbindDeviceChannels( const Device *pDevice ) {
    for ( uint32_t i = 0; i < gEdDev.MaxNumActions; i++ ) {
        if ( gEdDev.pActions[i].pChannel != nullptr ) {
            UnbindDeviceChannels( gEdDev.pActions[i], pDevice );
        }
    }

    if ( gpConfig == nullptr || gpConfig->pPlayerConfig == nullptr ) {
        return;
    }

    for ( int32_t i = 0; i < gpConfig->NbMaxPlayers; i++ ) {
        PlayerConfiguration &player = gpConfig->pPlayerConfig[i];
        for ( uint32_t j = 0; j < player.NumActions; j++ ) {
            if ( player.pActions[j].pChannel != nullptr ) {
                UnbindDeviceChannels( player.pActions[j], pDevice );
            }
        }
    }
}

GSDevice gDevice = {}; /* FUN_009ddeb0 */ // DAT_00fa3ff0
GSDevice *gpDevice = nullptr;

GSDevice::GSDevice()
    : GameSystem(),
      replayFilename{},
      replaySize( 0 ),
      bReplayLoading( false ),
      bReplaySaving( false ),
      headTrackingYaw( 0.0f ),
      headTrackingPitch( 0.0f ),
      bUseNaturalPointHeadTracking( false ),
      field14_0x54( true ),
      headTrackingReceiver(),
      headTrackingIdleTime( 0.0f ) {
    gpDevice = this;
}

GSDevice::~GSDevice() { gpDevice = nullptr; }

bool GSDevice::initialize( TestDriveGameInstance *param_1 ) {
    // FUN_00980af0
    pGameInstance = param_1;
    bPaused = false;

    gEdDev.ReplayBufferSize = 0;

    // TODO: Memory pooling
    // gEdDev.MemoryPoolSize =
    //    iVar2 + 0x100000 + gEdDev.MaxNumChannel * 8 +
    //    gEdDev.MaxNumActions * 0x3c;
    // gEdDev.pMemoryPool = (TestDrive::AlignedMalloc)(gEdDev.MemoryPoolSize,0);

    gEdDev.initialize();
    gEdDev.pReplayBufferPtr = gEdDev.pReplayBuffer;

    for ( uint32_t iVar13 = 0; iVar13 < gEdDev.MaxNumJoystick; iVar13++ ) {
        Device *peVar3 = gEdDev.getDevice( eDeviceType::DT_Other, iVar13 );
        if ( peVar3 != nullptr ) {
            SetAxisChannelThresholds( peVar3 );
        }
    }

    if ( gpConfig != nullptr && gpConfig->NbMaxPlayers > 0 &&
         gpConfig->NbMaxChannelPerActions > 0 ) {
        for ( int32_t i = 0; i < gpConfig->NbMaxPlayers; i++ ) {
            PlayerConfiguration &player = gpConfig->pPlayerConfig[i];
            for ( uint32_t j = 0; j < player.NumActions; j++ ) {
                DeviceAction &action = player.pActions[j];
                action.NumChannels = gpConfig->NbMaxChannelPerActions;
                action.pChannel =
                    new ( TestDrive::Alloc( sizeof( InputChanneMapping ) *
                                            gpConfig->NbMaxChannelPerActions ) )
                        InputChanneMapping[gpConfig->NbMaxChannelPerActions]();
                action.NumAddedChannels = 0;
            }
        }
    }

    headTrackingYaw = 0.0f;
    headTrackingPitch = 0.0f;
    headTrackingIdleTime = 0.0f;
    bUseNaturalPointHeadTracking = headTrackingReceiver.open();

    mouseState.initialize();

    // OTDU_ADDITION: per-device input templates
    InputTemplates::Load();
    for ( uint32_t i = 0; i < gEdDev.MaxNumJoystick; i++ ) {
        DeviceJoystick *pJoystick = gEdDev.getConnectedJoystick( i );
        if ( pJoystick != nullptr ) {
            InputTemplates::OnDeviceConnected( *pJoystick );
        }
    }

    return true;
}

void GSDevice::processJoystickHotplug() {
    // OTDU_ADDITION
    uint32_t joystickID = 0;
    bool bConnected = false;
    while ( gEdDev.popJoystickEvent( joystickID, bConnected ) ) {
        if ( bConnected ) {
            DeviceJoystick *pJoystick = gEdDev.connectJoystick( joystickID );
            if ( pJoystick == nullptr ) {
                continue;
            }

            SetAxisChannelThresholds( pJoystick );
            InputTemplates::OnDeviceConnected( *pJoystick );
        } else {
            DeviceJoystick *pJoystick = gEdDev.findJoystick( joystickID );
            if ( pJoystick == nullptr ) {
                continue;
            }

            if ( gbFFBEffectsCreated && gpFFBDevice == pJoystick ) {
                releaseForceFeedbackEffects();
            }

            UnbindDeviceChannels( pJoystick );
            InputTemplates::OnDeviceDisconnected( *pJoystick );
            gEdDev.disconnectJoystick( pJoystick );
        }

        InputTemplates::RequestRebind();
    }

    if ( InputTemplates::ConsumeRebindRequest() && gpPlayerData != nullptr ) {
        gpPlayerData->reapplyInputBindings();
    }

    InputTemplates::TickPrompt();
}

void GSDevice::tick( float totalTime, float deltaTime ) {
    // FUN_00980700
    processJoystickHotplug();

    bool bSuppressInput = !gEdSys.bWindowActivated;
#ifdef OTDU_USE_IMGUI
    // Keep device input from reaching the game while the imgui debug overlay is
    // visible (same handling as the window losing focus): disabled devices skip
    // sampling in Device::tick()/sampleData() below, so no channels update.
    bSuppressInput |= ( gpImgui != nullptr && gpImgui->hasFocus() );
#endif
    if ( bSuppressInput ) {
        gEdDev.disableAllDevices();
    } else {
        uint32_t totalDevCount = ( gEdDev.MaxNumMouse + gEdDev.MaxNumKeyboard +
                                   gEdDev.MaxNumJoystick );
        for ( uint32_t param_5 = 0; param_5 < totalDevCount; param_5++ ) {
            if ( gEdDev.isEnabled() &&
                 !gpTestDriveInstance->hasRequestedExit() ) {
                Device *peVar2 = gEdDev.ppDevices[param_5];
                if ( peVar2 != nullptr ) {
                    if ( peVar2->getType() == eDeviceType::DT_Keyboard &&
                         field14_0x54 == false ) {
                        peVar2->setDisabled();
                    } else {
                        peVar2->setEnabled();
                    }
                }
            }
        }
    }

    updateHeadTracking( deltaTime );

    for ( uint32_t i = 0; i < gEdDev.NumDevices; i++ ) {
        if ( gEdDev.ppDevices[i] != nullptr ) {
            gEdDev.ppDevices[i]->tick();
        }
    }
    if ( deltaTime <= 0.0f ) {
        deltaTime = 0.011111111f;
    }
    gEdDev.tick( deltaTime );

    if ( gpConfig != nullptr && gpConfig->pPlayerConfig != nullptr ) {
        for ( int32_t i = 0; i < gpConfig->NbMaxPlayers; i++ ) {
            PlayerConfiguration &player = gpConfig->pPlayerConfig[i];
            for ( uint32_t j = 0; j < player.NumActions; j++ ) {
                if ( player.pActions[j].NumChannels != 0 ) {
                    player.pActions[j].tick( deltaTime );
                }
            }
        }
    }

    CarState *pCarState = gpScenaric != nullptr
                              ? gpScenaric->getLocalPlayer().getCarState()
                              : nullptr;
    PhysicsVehicle *pVehicle =
        pCarState != nullptr ? pCarState->getPhysicsVehicle() : nullptr;
    if ( pVehicle != nullptr && gpConfig->pPlayerConfig != nullptr ) {
        DeviceAction *pSteerAction = pVehicle->isBike()
                                         ? gpConfig->pPlayerConfig->pBikeRight
                                         : gpConfig->pPlayerConfig->pCarRight;
        if ( pSteerAction != nullptr && pSteerAction->NumAddedChannels != 0 ) {
            InputChannel *pChannel = pSteerAction->pChannel->pChannel;
            Device *pDevice = pChannel != nullptr ? pChannel->pParent : nullptr;
            if ( pDevice != nullptr && pDevice->getNumEffects() != 0 ) {
                // Only joysticks expose effect channels
                DeviceJoystick *pJoystick =
                    static_cast<DeviceJoystick *>( pDevice );
                if ( pJoystick->getSDLJoystick() == nullptr ) {
                    return;
                }
                if ( gbFFBEffectsCreated ) {
                    updateForceFeedbackEffects();
                } else {
                    createForceFeedbackEffects( pJoystick );
                }
                return;
            }
        }
    }

    if ( gbFFBEffectsCreated ) {
        releaseForceFeedbackEffects();
    }
}

void GSDevice::createForceFeedbackEffects( DeviceJoystick *pDevice ) {
    // FUN_00980380
    gpFFBDevice = pDevice;

    SDL_Haptic *pHaptic = pDevice->getSDLHaptic();
    if ( pHaptic != nullptr ) {
        SDL_HapticEffect effect = {};
        effect.type = SDL_HAPTIC_CONSTANT;
        effect.constant.direction.type = SDL_HAPTIC_STEERING_AXIS;
        effect.constant.length = SDL_HAPTIC_INFINITY;
        effect.constant.level = 0;
        gFFBConstantEffect = SDL_CreateHapticEffect( pHaptic, &effect );

        if ( ( SDL_GetHapticFeatures( pHaptic ) & SDL_HAPTIC_DAMPER ) != 0 ) {
            effect = {};
            effect.type = SDL_HAPTIC_DAMPER;
            effect.condition.direction.type = SDL_HAPTIC_STEERING_AXIS;
            effect.condition.length = SDL_HAPTIC_INFINITY;
            gFFBDamperEffect = SDL_CreateHapticEffect( pHaptic, &effect );
        }
    }

    gbFFBEffectsCreated = true;
}

void GSDevice::updateForceFeedbackEffects() {
    // FUN_009801e0
    ForceFeedbackOutput &output = gForceFeedbackOutput;
    const float peakConstantForce = output.PeakConstantForce.exchange( 0.0f );
    const float peakSecondaryForce = output.PeakSecondaryForce.exchange( 0.0f );

    SDL_Haptic *pHaptic = gpFFBDevice->getSDLHaptic();
    if ( pHaptic == nullptr ) {
        // NOTE: DirectInput pads without FF actuators never rumbled; SDL
        // rumble motors stand in for the two pad effect channels. Each motor
        // holds a raised level for the rumble duration so short impact
        // pulses can be felt.
        Uint16 lowFrequency = 0;
        Uint16 highFrequency = 0;
        if ( output.Mode == eForceFeedbackMode::FFBM_Pad ) {
            lowFrequency = ToSDLRumbleStrength(
                std::max( output.ConstantForce, peakConstantForce ) );
            highFrequency = ToSDLRumbleStrength(
                std::max( output.SecondaryForce, peakSecondaryForce ) );
        }
        const uint64_t nowMs = SDL_GetTicks();
        SDL_RumbleJoystick( gpFFBDevice->getSDLJoystick(),
                            HoldRumbleLevel( 0, lowFrequency, nowMs ),
                            HoldRumbleLevel( 1, highFrequency, nowMs ),
                            kRumbleDurationMs );
        return;
    }

    if ( gFFBDamperEffect >= 0 ) {
        const Sint16 coefficient =
            ToSDLHapticLevel( output.Damper * kFFBUnitsScale );

        SDL_HapticEffect effect = {};
        effect.type = SDL_HAPTIC_DAMPER;
        effect.condition.direction.type = SDL_HAPTIC_STEERING_AXIS;
        effect.condition.length = SDL_HAPTIC_INFINITY;
        effect.condition.right_coeff[0] = coefficient;
        effect.condition.left_coeff[0] = coefficient;
        effect.condition.right_sat[0] = 0xffff;
        effect.condition.left_sat[0] = 0xffff;
        SDL_UpdateHapticEffect( pHaptic, gFFBDamperEffect, &effect );
    }

    if ( gFFBConstantEffect >= 0 &&
         output.Mode != eForceFeedbackMode::FFBM_None ) {
        float magnitude = output.ConstantForce * kFFBUnitsScale;
        if ( output.Mode == eForceFeedbackMode::FFBM_Pad ) {
            const float secondary = output.SecondaryForce * kFFBUnitsScale;
            magnitude =
                std::sqrt( secondary * secondary + magnitude * magnitude );
        }

        SDL_HapticEffect effect = {};
        effect.type = SDL_HAPTIC_CONSTANT;
        effect.constant.direction.type = SDL_HAPTIC_STEERING_AXIS;
        effect.constant.length = SDL_HAPTIC_INFINITY;
        effect.constant.level = ToSDLHapticLevel( magnitude );
        SDL_UpdateHapticEffect( pHaptic, gFFBConstantEffect, &effect );
    }

    // NOTE: DirectInput restarted both effects on every update (DIEP_START);
    // SDL only needs them started once.
    if ( !gbFFBEffectsRunning ) {
        if ( gFFBDamperEffect >= 0 ) {
            SDL_RunHapticEffect( pHaptic, gFFBDamperEffect, 1 );
        }
        if ( gFFBConstantEffect >= 0 ) {
            SDL_RunHapticEffect( pHaptic, gFFBConstantEffect, 1 );
        }
        gbFFBEffectsRunning = true;
    }
}

void GSDevice::releaseForceFeedbackEffects() {
    // FUN_00980700 (inlined)
    SDL_Haptic *pHaptic = gpFFBDevice->getSDLHaptic();
    if ( pHaptic != nullptr ) {
        if ( gFFBDamperEffect >= 0 ) {
            SDL_DestroyHapticEffect( pHaptic, gFFBDamperEffect );
        }
        if ( gFFBConstantEffect >= 0 ) {
            SDL_DestroyHapticEffect( pHaptic, gFFBConstantEffect );
        }
    } else {
        SDL_RumbleJoystick( gpFFBDevice->getSDLJoystick(), 0, 0, 0 );
    }

    gFFBDamperEffect = -1;
    gFFBConstantEffect = -1;
    gpFFBDevice = nullptr;
    gRumbleLevels[0] = gRumbleLevels[1] = 0;
    gRumbleHoldEndMs[0] = gRumbleHoldEndMs[1] = 0;
    gbFFBEffectsRunning = false;
    gbFFBEffectsCreated = false;
}

void GSDevice::terminate() {
    // FUN_00980670
    bUseNaturalPointHeadTracking = false;
    headTrackingReceiver.close();

    // NOTE: DirectInput effects were released with their device; SDL effects
    // must go before the haptic device is closed.
    if ( gbFFBEffectsCreated ) {
        releaseForceFeedbackEffects();
    }

    gEdDev.destroy();
}

void GSDevice::retrieveFlashNavKeyStates( bool *param_1,
                                          bool *param_2,
                                          const bool param_3,
                                          const bool param_4,
                                          const int32_t param_5 ) {
    // FUN_009808c0
    *param_1 = false;
    MngDebugHUD *pMVar1 = gpMngDebugHUD;
    bool bVar8 = ( gpMngDebugHUD == nullptr );
    *param_2 = false;
    if ( ( ( ( bVar8 ) || ( pMVar1->isVisible() != false ) ) ||
           ( ( gpFlashMessageBox->getNumOpenMsgBox() == 0 &&
               ( ( gpFlashMessageBox->getUnknownFlag() == false &&
                   ( bVar8 = gpFlash->FUN_0099b1e0( 0, param_5 ),
                     bVar8 ) ) ) ) ) ) &&
         ( gpConfig->pPlayerConfig != nullptr ) ) {
        DeviceAction *local_8 = gpConfig->pPlayerConfig->pMenuUp;
        DeviceAction *local_4 = nullptr;
        DeviceAction *peVar2 = nullptr;
        DeviceAction *peVar3 = nullptr;
        DeviceAction *peVar4 = nullptr;
        DeviceAction *peVar5 = nullptr;
        DeviceAction *peVar6 = nullptr;
        DeviceAction *peVar7 = nullptr;
        if ( param_4 ) {
            local_4 = gpConfig->pPlayerConfig->pMapCursorRight;
            peVar5 = gpConfig->pPlayerConfig->pMenuLeft;
            peVar4 = gpConfig->pPlayerConfig->pMapCursorLeft;
            peVar7 = gpConfig->pPlayerConfig->pMapCursorDown;
            peVar6 = gpConfig->pPlayerConfig->pMapCursorUp;
            peVar2 = local_8;
            peVar3 = gpConfig->pPlayerConfig->pMenuDown;
            local_8 = gpConfig->pPlayerConfig->pMenuRight;
        } else {
            local_4 = gpConfig->pPlayerConfig->pMapCursorUp;
            peVar5 = gpConfig->pPlayerConfig->pMenuDown;
            peVar4 = gpConfig->pPlayerConfig->pMapCursorDown;
            peVar7 = gpConfig->pPlayerConfig->pMapCursorRight;
            peVar2 = gpConfig->pPlayerConfig->pMenuLeft;
            peVar6 = gpConfig->pPlayerConfig->pMapCursorLeft;
            peVar3 = gpConfig->pPlayerConfig->pMenuRight;
        }
        if ( param_3 ) {
            if ( ( ( ( ( peVar5 != nullptr ) && ( local_8 != nullptr ) ) &&
                     ( ( peVar2 != nullptr &&
                         ( ( ( peVar3 != nullptr && ( peVar4 != nullptr ) ) &&
                             ( local_4 != nullptr ) ) ) ) ) ) &&
                   ( ( ( ( peVar6 != nullptr && ( peVar7 != nullptr ) ) &&
                         ( ( peVar2->Flags & 1 ) == 0 ) ) &&
                       ( ( ( peVar3->Flags & 1 ) == 0 &&
                           ( peVar6->FUN_00434bc0( 0.8f, 1.0f ) <=
                             0.0f ) ) ) ) ) ) &&
                 ( peVar7->FUN_00434bc0( 0.8f, 1.0f ) <= 0.0f ) ) {
                if ( ( ( peVar5->Flags & 1 ) != 0 ) ||
                     ( 0.0f < peVar4->FUN_00434bc0( 0.8f, 1.0f ) ) ) {
                    bVar8 = true;
                } else {
                    bVar8 = false;
                }
                *param_1 = bVar8;
                if ( ( ( local_8->Flags & 1 ) == 0 ) &&
                     ( local_4->FUN_00434bc0( 0.8f, 1.0f ) <= 0.0f ) ) {
                    *param_2 = false;
                    return;
                }
                *param_2 = true;
                return;
            }
        } else if ( ( ( ( ( peVar5 != nullptr ) && ( local_8 != nullptr ) ) &&
                        ( peVar2 != nullptr ) ) &&
                      ( ( peVar3 != nullptr &&
                          ( ( peVar2->Flags & 1 ) == 0 ) ) ) ) &&
                    ( ( peVar3->Flags & 1 ) == 0 ) ) {
            *param_1 = (bool)( peVar5->Flags & 1 );
            *param_2 = (bool)( local_8->Flags & 1 );
        }
    }
}

void MouseState::initialize() {
    // FUN_00980550
    Device *peVar1 = gEdDev.getDevice( eDeviceType::DT_Mouse, 0 );
    pInstance = peVar1;
    if ( peVar1 == nullptr ) {
        pChannelLeft = nullptr;
        pChannelUp = nullptr;
        pChannelWheelUp = nullptr;
        pChannelWheelDown = nullptr;
        pChannelButton0 = nullptr;
        pChannelButton2 = nullptr;
        pChannelButton1 = nullptr;
        return;
    }

    pChannelLeft = peVar1->getChannel( "LEFT" );
    pChannelUp = peVar1->getChannel( "UP" );
    pChannelWheelUp = peVar1->getChannel( "WHEEL_UP" );
    pChannelWheelDown = peVar1->getChannel( "WHEEL_DOWN" );
    pChannelButton0 = peVar1->getChannel( "BUTTON_0" );
    pChannelButton1 = peVar1->getChannel( "BUTTON_1" );
    pChannelButton2 = peVar1->getChannel( "BUTTON_2" );
}

bool MouseState::FUN_00980000() const {
    // FUN_00980000
    if ( pChannelLeft != nullptr && pChannelUp != nullptr ) {
        if ( pChannelUp->InputValue > 0.0f )
            return true;
    }

    if ( pChannelWheelUp != nullptr && (int8_t)pChannelWheelUp->Flags < 0 )
        return true;
    if ( pChannelWheelDown != nullptr && (int8_t)pChannelWheelDown->Flags < 0 )
        return true;

    if ( pChannelLeft != nullptr && pChannelUp != nullptr ) {
        if ( pChannelLeft->InputValue != 0.0f )
            return true;
    }

    return false;
}

void GSDevice::setHeadTrackingEndpoint( const char *pAddress,
                                        const uint16_t port ) {
    headTrackingReceiver.setEndpoint( pAddress, port );
    if ( bUseNaturalPointHeadTracking ) {
        bUseNaturalPointHeadTracking = headTrackingReceiver.open();
    }
}

void GSDevice::updateHeadTracking( const float deltaTime ) {
    static constexpr float kIdleTimeout = 0.5f;
    static constexpr float kRecenterRate = 4.0f;

    if ( !bUseNaturalPointHeadTracking ) {
        return;
    }

    float yaw = 0.0f;
    float pitch = 0.0f;
    if ( headTrackingReceiver.poll( yaw, pitch ) ) {
        headTrackingYaw = yaw;
        headTrackingPitch = pitch;
        headTrackingIdleTime = 0.0f;
        return;
    }

    headTrackingIdleTime += deltaTime;
    if ( headTrackingIdleTime > kIdleTimeout ) {
        const float decay = std::exp( -kRecenterRate * deltaTime );
        headTrackingYaw *= decay;
        headTrackingPitch *= decay;
    }
}
