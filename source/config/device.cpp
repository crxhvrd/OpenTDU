#include "shared.h"
#include "gs_config.h"

#include "core/arg_parser.h"
#include "core/logger.h"

#include "input/ed_dev.h"
#include "input/gs_device.h"
#include "input/input_device.h"

#include "player_data/gs_playerdata.h"
#include "render/camera/gs_camera.h"

// TODO: REMOVE ME: Remove when control mapping is implemented in game.
static bool gUseDefaultPadMapping = false;
static CmdLineArg CmdLineGamepad( "gamepad", []( const char *pArg ) {
    gUseDefaultPadMapping = true;
} );

struct DefaultPadBinding {
    const char *pActionName;
    int32_t Category;
    char ExclusionGroup;
    const char *pChannelName;
};

static constexpr DefaultPadBinding kDefaultPadBindings[] = {
    {"CAR_ACCEL",          1, 'A', "WHEEL_DOWN"   },
    {"CAR_BRAKE",          1, 'A', "WHEEL_UP"     },
    {"CAR_LEFT",           1, 'A', "LEFT"         },
    {"CAR_RIGHT",          1, 'A', "RIGHT"        },
    {"CAR_HBRAKE",         1, 'A', "BUTTON_0"     },

    {"BIKE_ACCEL",         1, 'B', "WHEEL_DOWN"   },
    {"BIKE_BRAKE",         1, 'B', "WHEEL_UP"     },
    {"BIKE_LEFT",          1, 'B', "LEFT"         },
    {"BIKE_RIGHT",         1, 'B', "RIGHT"        },

    {"CAR_GUP",            2, 'A', "BUTTON_5"     },
    {"CAR_GDOWN",          2, 'A', "BUTTON_4"     },
    {"BIKE_GUP",           2, 'B', "BUTTON_5"     },
    {"BIKE_GDOWN",         2, 'B', "BUTTON_4"     },

    {"CAR_CAMERA",         5, 'C', "BUTTON_3"     },
    {"CAR_BACKVIEW",       5, 'C', "BUTTON_1"     },
    {"CAR_HORN",           5, 'C', "BUTTON_8"     },
    {"BOT_PAD",            5, 'C', "BUTTON_2"     },
    {"MAP",                5, 'D', "BUTTON_6"     },

    {"CAR_HEAD_LEFT",      5, 'C', "STICK_1_LEFT" },
    {"CAR_HEAD_RIGHT",     5, 'C', "STICK_1_RIGHT"},
    {"CAR_HEAD_UP",        5, 'C', "STICK_1_UP"   },
    {"CAR_HEAD_DOWN",      5, 'C', "STICK_1_DOWN" },
    {"CAR_HEAD_CENTER",    5, 'C', "BUTTON_9"     },

    {"IN_GAME_MENU_UP",    5, 'E', "POV_0_UP"     },
    {"IN_GAME_MENU_DOWN",  5, 'E', "POV_0_DOWN"   },
    {"IN_GAME_MENU_LEFT",  5, 'E', "POV_0_LEFT"   },
    {"IN_GAME_MENU_RIGHT", 5, 'E', "POV_0_RIGHT"  },

    {"MENU_UP",            3, 'G', "POV_0_UP"     },
    {"MENU_DOWN",          3, 'G', "POV_0_DOWN"   },
    {"MENU_LEFT",          3, 'G', "POV_0_LEFT"   },
    {"MENU_RIGHT",         3, 'G', "POV_0_RIGHT"  },
    {"MENU_VALIDATE",      3, 'I', "BUTTON_0"     },
    {"MENU_BACK",          3, 'I', "BUTTON_1"     },
    {"MENU_PAUSE",         3, 'F', "BUTTON_7"     },
    {"MENU_SORT_LEFT",     3, 'H', "BUTTON_4"     },
    {"MENU_SORT_RIGHT",    3, 'H', "BUTTON_5"     },

    {"MENU_UP",            3, 'G', "UP"           },
    {"MENU_DOWN",          3, 'G', "DOWN"         },
    {"MENU_LEFT",          3, 'G', "LEFT"         },
    {"MENU_RIGHT",         3, 'G', "RIGHT"        },
};

void GSConfig::applyDefaultPadMapping() {
    if ( !gUseDefaultPadMapping ) {
        return;
    }

    Device *pDevice = gEdDev.getDevice( eDeviceType::DT_Other, 0 );
    if ( pDevice == nullptr ) {
        OTDU_LOG_WARN( "-gamepad in cmdline but no controller found...\n" );
        return;
    }

    OTDU_LOG_INFO( "Applying default pad mapping to device '%s'\n",
                   pDevice->getName() );

    for ( const DefaultPadBinding &binding : kDefaultPadBindings ) {
        char actionArgs[192];
        snprintf( actionArgs, sizeof( actionArgs ),
                  "\"%s\" \"ALL\" %d \"%c\" \"%s\" \"0\" \"%s\"",
                  binding.pActionName, binding.Category, binding.ExclusionGroup,
                  pDevice->getName(), binding.pChannelName );

        if ( !executeCommand( "ADDACTION", actionArgs ) ) {
            OTDU_LOG_ERROR( "ADDACTION is not a registered config command\n" );
            return;
        }
    }
}

void GSConfig::registerDeviceCommands() {
    registerCommand( "USEAUTOMATIC", [&]( const char *pArg ) {
        // FUN_009878b0
        if ( pPlayerConfig == nullptr || NbMaxPlayers < 1 ) {
            return;
        }
        pPlayerConfig[0].bUseAutomatic = atoi( pArg ) != 0;
    } );

    registerCommand( "ADDACTION", [&]( const char *pArg ) {
        // FUN_00988c30
        // 0            1                       2    3. 4.       5           6
        // 7     8   9
        // AddAction	"CAR_ACCEL"				"ALL"	 1
        // "A"		"Keyboard"	"0"		"UP"  ??? ???
        std::string argStr( pArg );
        size_t it = 0;

        std::string actionName = ParseString( argStr, it );
        std::string playerName = ParseString( argStr, it );
        int32_t iVar11 = ParseInteger( argStr, it );
        std::string actionChannelName = ParseString( argStr, it );
        std::string deviceName = ParseString( argStr, it );
        std::string deviceIndexName = ParseString( argStr, it );
        std::string unk2 = ParseString( argStr, it );

        int32_t playerIndex = 0;
        int32_t numPlayers = gpConfig->NbMaxPlayers - 1;
        if ( playerName != "ALL" ) {
            numPlayers = std::stoi( playerName );
            playerIndex = numPlayers;
        }

        bool bAllDevice = true;
        uint32_t deviceIndex = 0;
        if ( deviceIndexName != "ALL" ) {
            bAllDevice = false;
            deviceIndex = std::stoi( deviceIndexName );
        }

        // TODO: Looks like actions mapping can take two extra args? This is
        // unused on PC so this will remain unimplemented for now
        uint8_t uVar6 = 0x0;
        bool bVar15 = false;
        //   if (5 < iVar11) {
        //     pPlayerName = pArgs + 9;
        //     uVar6 = true;
        //     if (pArgs[8].Number == 0) {
        //       uVar6 = false;
        //     }
        //   }
        //   if (6 < iVar11) {
        //     bVar16 = pPlayerName->Number != 0;
        //   }

        if ( playerIndex <= numPlayers ) {
            int32_t local_134 = -1;
            int32_t iVar13 = local_134;
            do {
                if ( bAllDevice ) {
                    deviceIndex = playerIndex;
                }
                PlayerConfiguration &pPVar4 = pPlayerConfig[playerIndex];
                int32_t iVar10 = pPVar4.NumActions;

                for ( int32_t iVar12 = 0; iVar12 < iVar10; iVar12++ ) {
                    DeviceAction &piVar14 = pPVar4.pActions[iVar12];
                    if ( ( local_134 == -1 ) &&
                         strncmp( piVar14.pName, "Unamed", 32 ) == 0 ) {
                        local_134 = iVar12;
                    }

                    iVar13 = iVar12;
                    if ( actionName == piVar14.pName ) {
                        break;
                    }

                    iVar13 = local_134;
                }

                if ( actionName.length() < 0x20 ) {
                    strncpy( pPVar4.pActions[iVar13].pName, actionName.c_str(),
                             actionName.length() + 1 );
                }

                iVar10 = gEdDev.getDeviceID( deviceName, deviceIndex );
                Device *peVar5 = nullptr;
                if ( ( iVar10 < 0 ) || ( peVar5 = gEdDev.ppDevices[iVar10],
                                         peVar5 == nullptr ) ) {
                    if ( actionName.length() < 0x20 ) {
                        strncpy( pPVar4.pActions[local_134].pName,
                                 actionName.c_str(), actionName.length() + 1 );
                    }
                } else {
                    // (**(code **)(peVar5->lpVtbl + 0x14))(local_100); //
                    // DInput8 GetDeviceInfo
                    iVar10 = peVar5->getChannelIndex( unk2 );

                    InputChannel *peVar9 = nullptr;
                    if ( ( -1 < iVar10 ) &&
                         iVar10 < (int32_t)peVar5->getNumChannels() ) {
                        peVar9 = &peVar5->getChannels()[iVar10];
                        // (**(code **)(peVar5->lpVtbl +
                        // 0x18))(local_100,peVar9); // DInput8 Retrieves
                        // something for the active channel??
                        pPVar4.pActions[iVar13].addChannel( uVar6, bVar15,
                                                            peVar9 );
                    }
                }
                playerIndex++;
            } while ( playerIndex <= numPlayers );
        }

        uint8_t cVar3 = actionChannelName[0];
        FUN_008963e0( unk2.c_str(), deviceName.c_str(),
                      uint8_t( ( bAllDevice - 1U ) & deviceIndex ), uVar6 != 0,
                      bVar15, int8_t( iVar11 ), uint8_t( cVar3 - 0x41 ),
                      actionName.c_str() );
    } );

    registerCommand( "ADDACTIONAND", [&]( const char *pArg ) {
        // FUN_009889d0
        OTDU_UNIMPLEMENTED
    } );

    registerCommand( "SETINVERTMOUSE", [&]( const char *pArg ) {
        // FUN_00985800
        bInvertMouse = ( strncmp( pArg, "TRUE", strlen( "TRUE" ) ) == 0 );
    } );

    registerCommand( "ACTIONSEXCLUSIONS", [&]( const char *pArg ) {
        // FUN_00985840
        OTDU_ASSERT( pArg );

        const char *pcVar2 = pArg;
        while ( *pcVar2 != '\0' ) {
            int32_t iVar4 = *pcVar2 + -0x41;
            int32_t iVar5 = pcVar2[1] + -0x41;
            pcVar2 += 2;

            while ( *pcVar2 == ' ' ) {
                pcVar2++;
            }
            if ( ( ( ( -1 < iVar4 ) && ( iVar4 < 0x20 ) ) && ( -1 < iVar5 ) ) &&
                 ( iVar5 < 0x20 ) ) {
                gActionExclusions[iVar4] =
                    gActionExclusions[iVar4] | 1 << ( (int8_t)iVar5 & 0x1f );
                gActionExclusions[iVar5] =
                    gActionExclusions[iVar5] | 1 << ( (int8_t)iVar4 & 0x1f );
            }
        }
    } );

    registerCommandFloat( "FFB_FACTOR", &gFFBSlipAngle[1][0] ); // FUN_009859b0

    registerCommand( "FFB_SLIPANGLE", [&]( const char *pArg ) {
        // FUN_00985980
        std::string argStr( pArg );
        size_t it = 0;

        int32_t iVar1 = ParseInteger( argStr, it );
        float fVar1 = ParseFloat( argStr, it );
        float fVar2 = ParseFloat( argStr, it );

        gFFBSlipAngle[iVar1][0] = fVar1;
        gFFBSlipAngle[iVar1][1] = fVar2;
    } );

    registerCommand( "FFB_SPEEDFACTOR", [&]( const char *pArg ) {
        // FUN_00985940
        OTDU_UNIMPLEMENTED
    } );

    registerCommand( "FFB_LOADFROMACCEL", [&]( const char *pArg ) {
        // FUN_00985900
        std::string argStr( pArg );
        size_t it = 0;

        gFFBLoadFromAccel[0] = ParseFloat( argStr, it );
        gFFBLoadFromAccel[1] = ParseFloat( argStr, it );
        gFFBLoadFromAccel[2] = ParseFloat( argStr, it );
    } );

    registerCommand( "FFB_LOADFACTOR", [&]( const char *pArg ) {
        // FUN_009858c0
        std::string argStr( pArg );
        size_t it = 0;

        gFFBLoadFactor[0] = ParseFloat( argStr, it );
        gFFBLoadFactor[1] = ParseFloat( argStr, it );
        gFFBLoadFactor[2] = ParseFloat( argStr, it );
    } );

    registerCommandFloat( "STEERINGDEADZONE",
                          &gSteeringDeadZone );               // FUN_009857e0
    registerCommandFloat( "BRAKEDEADZONE", &gBrakeDeadZone ); // FUN_009857a0
    registerCommandFloat( "STEERINGTURN", &gSteeringTurn );   // FUN_00985700
    registerCommandFloat( "STEERINGLINEARITY",
                          &gSteeringLinearity ); // FUN_00985720
    registerCommand( "CAMERADEADZONE", [&]( const char *pArg ) {
        // FUN_00985760
        gpCamera->setDeadZone( static_cast<float>( atof( pArg ) ) );
    } );

    registerCommand( "INVERTACCEL", [&]( const char *pArg ) {
        // FUN_00985790
        // NOP
    } );

    registerCommand( "INVERTBRAKE", [&]( const char *pArg ) {
        // FUN_00985780
        // NOP
    } );

    registerCommand( "PEDALSSEPARATED", [&]( const char *pArg ) {
        // FUN_00985740
        gPedalsSeparated = atoi( pArg ) != 0;
    } );

    registerCommand( "DEFAULTSENSITIVITY", [&]( const char *pArg ) {
        // FUN_00984720
        gDefaultSensitivity = atoi( pArg );
    } );

    registerCommand( "DEFAULTSEATPOSITION", [&]( const char *pArg ) {
        // FUN_009846f0
        std::string argStr( pArg );
        size_t it = 0;

        gSeatPosition[0] = ParseFloat( argStr, it );
        gSeatPosition[1] = ParseFloat( argStr, it );
    } );

    registerCommand( "ACCELDEADZONE", [&]( const char *pArg ) {
        // FUN_009857c0
        gAccelDeadZone = static_cast<float>( atof( pArg ) );
    } );
}
