#include "shared.h"
#include "gs_config.h"

#include "tdu_instance.h"
#include "physics/gs_physics.h"
#include "physics/physics_config.h"

#include <cstring>

static constexpr float kSpeedScale = 1.0f / 3.6f;
inline float ParseScaledFloat( std::string &argStr, size_t &it ) {
    return GSConfig::ParseFloat( argStr, it ) * kSpeedScale;
}

void GSConfig::registerPhysicsCommands() {
    registerCommand( "USESTEERCONTROL", [&]( const char *pArg ) {
        // FUN_00984740
        gUseSteerControl = atoi( pArg ) != 0;
    } );

    registerCommand( "REMOTEDEBUGGER", [&]( const char *pArg ) {
        // FUN_00984800
        gpPhysics->setDebugModeEnabled( atoi( pArg ) != 0 );
    } );

    registerCommand( "PHYSICSDEBUGINFO", [&]( const char *pArg ) {
        // FUN_009847e0
        gpPhysics->setDebugDrawEnabled( atoi( pArg ) != 0 );
    } );

    // Debug collision wireframe overlay (also toggleable from the GSPhysics
    // ImGui debug panel). COLLISIONVIS 1 / 2 (2 = also draw terrain).
    registerCommand( "COLLISIONVIS", []( const char *pArg ) {
        const int32_t mode = atoi( pArg );
        gDrawCollisionVis = mode != 0;
        gDrawCollisionVisTerrain = mode >= 2;
    } );

    registerCommand( "VEHICLEVIS", []( const char *pArg ) {
        const int32_t mode = atoi( pArg );
        gDrawVehicleCollisionVis = ( mode & 1 ) != 0;
        gDrawVehicleCoMVis = ( mode & 2 ) != 0;
    } );

    registerCommand( "GRASSASTARMAC", [&]( const char *pArg ) {
        // FUN_009847c0
        gbGrassAsTarmac = atoi( pArg ) != 0;
    } );

    registerCommand( "PHYSICSTHREAD", [&]( const char *pArg ) {
        // FUN_009878e0
        std::string argStr( pArg );
        size_t it = 0ull;

        std::string priority = ParseStringNoQuotes( argStr, it );
        int32_t frequency = ParseInteger( argStr, it );

        int32_t iVar1 = StringCmpI( priority.c_str(), "OFF" );
        if ( ( iVar1 != 0 ) &&
             ( gpTestDriveInstance->getWishedGameMode() != GM_AITrainer ) ) {
            iVar1 = StringCmpI( priority.c_str(), "NORMAL" );
            if ( iVar1 == 0 ) {
                gPhysicsThreadPriority = PTM_Normal;
            } else {
                iVar1 = StringCmpI( priority.c_str(), "REALTIME" );
                if ( iVar1 == 0 ) {
                    gPhysicsThreadPriority = PTM_Realtime;
                    gPhysicsThreadFrequency = frequency;
                    return;
                }
            }
            gPhysicsThreadFrequency = frequency;
            return;
        }
        gPhysicsThreadPriority = PTM_Off;
        gPhysicsThreadFrequency = frequency;
    } );

    registerCommand( "FULLVISIBILITYTEST", [&]( const char *pArg ) {
        // FUN_00984780
        int32_t bEnabled = atoi( pArg );
        if ( bEnabled != 0 ) {
            INT_00f87fec = INT_00f87fec + 0xe00;
            INT_00f87ff4 = INT_00f87ff4 + 0x400000;
            INT_00f87ff8 = 10;
        }
    } );

    registerCommand( "TORQUEPURCENTTOCONTACT", [&]( const char *pArg ) {
        // FUN_00984760
        std::string argStr( pArg );
        size_t it = 0ull;
        gTorquePurcentToContact = ParseFloat( argStr, it );
    } );

    registerCommand( "GRAVITYCOEF", [&]( const char *pArg ) {
        // FUN_009866e0
        std::string argStr( pArg );
        size_t it = 0ull;
        gpPhysicsConfig->GravityCoeff = ParseFloat( argStr, it );
    } );

    registerCommand( "FRONTFRICTIONCOEF", [&]( const char *pArg ) {
        std::string argStr( pArg );
        size_t it = 0ull;
        gpPhysicsConfig->FrontFrictionCoef = ParseFloat( argStr, it );
    } );

    registerCommand( "REARFRICTIONCOEF", [&]( const char *pArg ) {
        // FUN_00986590
        std::string argStr( pArg );
        size_t it = 0ull;
        gpPhysicsConfig->RearFrictionCoef = ParseFloat( argStr, it );
    } );

    registerCommand( "FRONTLATERALFRICTIONCOEF", [&]( const char *pArg ) {
        // FUN_009865d0
        std::string argStr( pArg );
        size_t it = 0ull;
        gpPhysicsConfig->FrontLateralFrictionCoef = ParseFloat( argStr, it );
    } );

    registerCommand( "REARLATERALFRICTIONCOEF", [&]( const char *pArg ) {
        // FUN_00986570
        std::string argStr( pArg );
        size_t it = 0ull;
        gpPhysicsConfig->RearLateralFrictionCoef = ParseFloat( argStr, it );
    } );

    registerCommand( "FRONTLNGBRAKEFRICTIONCOEF", [&]( const char *pArg ) {
        // FUN_009865b0
        std::string argStr( pArg );
        size_t it = 0ull;
        gpPhysicsConfig->FrontLngBrakeFrictionCoef = ParseFloat( argStr, it );
    } );

    registerCommand( "REARLNGBRAKEFRICTIONCOEF", [&]( const char *pArg ) {
        // FUN_00986550
        std::string argStr( pArg );
        size_t it = 0ull;
        gpPhysicsConfig->RearLngBrakeFrictionCoef = ParseFloat( argStr, it );
    } );

    registerCommand( "FRONTLATBRAKEFRICTIONCOEFSLOW", [&]( const char *pArg ) {
        // FUN_00986530
        std::string argStr( pArg );
        size_t it = 0ull;
        gpPhysicsConfig->FrontLatBrakeFrictionCoefSlow =
            ParseFloat( argStr, it );
    } );

    registerCommand( "REARLATBRAKEFRICTIONCOEFSLOW", [&]( const char *pArg ) {
        std::string argStr( pArg );
        size_t it = 0ull;
        gpPhysicsConfig->RearLatBrakeFrictionCoefSlow =
            ParseFloat( argStr, it );
    } );

    registerCommand( "FRONTLATBRAKEFRICTIONCOEFFAST", [&]( const char *pArg ) {
        std::string argStr( pArg );
        size_t it = 0ull;
        gpPhysicsConfig->FrontLatBrakeFrictionCoefFast =
            ParseFloat( argStr, it );
    } );

    registerCommand( "REARLATBRAKEFRICTIONCOEFFAST", [&]( const char *pArg ) {
        std::string argStr( pArg );
        size_t it = 0ull;
        gpPhysicsConfig->RearLatBrakeFrictionCoefFast =
            ParseFloat( argStr, it );
    } );

    registerCommand( "LATBRAKEFRICTIONSPEEDSLOW", [&]( const char *pArg ) {
        std::string argStr( pArg );
        size_t it = 0ull;
        const float speed = ParseScaledFloat( argStr, it );
        gpPhysicsConfig->LatBrakeFrictionSpeedSlowFront = speed;
        gpPhysicsConfig->LatBrakeFrictionSpeedSlowRear = speed;
    } );

    registerCommand( "LATBRAKEFRICTIONSPEEDFAST", [&]( const char *pArg ) {
        std::string argStr( pArg );
        size_t it = 0ull;
        const float speed = ParseScaledFloat( argStr, it );
        gpPhysicsConfig->LatBrakeFrictionSpeedFastFront = speed;
        gpPhysicsConfig->LatBrakeFrictionSpeedFastRear = speed;
    } );

    registerCommand( "RESISTIVECOEF", [&]( const char *pArg ) {
        std::string argStr( pArg );
        size_t it = 0ull;
        gpPhysicsConfig->ResistiveCoefFront = ParseFloat( argStr, it );
        gpPhysicsConfig->ResistiveCoefRear = ParseFloat( argStr, it );
    } );

    registerCommand( "RESISTIVELATERALCOEF", [&]( const char *pArg ) {
        std::string argStr( pArg );
        size_t it = 0ull;
        gpPhysicsConfig->ResistiveLateralCoefFront = ParseFloat( argStr, it );
        gpPhysicsConfig->ResistiveLateralCoefRear = ParseFloat( argStr, it );
    } );

    registerCommand( "INERTIACOEF", [&]( const char *pArg ) {
        std::string argStr( pArg );
        size_t it = 0ull;
        gpPhysicsConfig->InertiaCoef = ParseFloat( argStr, it );
    } );

    registerCommand( "MASSCOEF", [&]( const char *pArg ) {
        std::string argStr( pArg );
        size_t it = 0ull;
        gpPhysicsConfig->MassCoef = ParseFloat( argStr, it );
    } );

    registerCommand( "CGHEIGHTCOEF", [&]( const char *pArg ) {
        std::string argStr( pArg );
        size_t it = 0ull;
        gpPhysicsConfig->CGHeightCoef = ParseFloat( argStr, it );
    } );

    registerCommand( "REBOUNDCOEF", [&]( const char *pArg ) {
        std::string argStr( pArg );
        size_t it = 0ull;
        gpPhysicsConfig->ReboundCoef = ParseFloat( argStr, it );
    } );

    registerCommand( "SPRINGCOEF", [&]( const char *pArg ) {
        std::string argStr( pArg );
        size_t it = 0ull;
        gpPhysicsConfig->SpringCoef = ParseFloat( argStr, it );
        gSpringCoef = ParseFloat( argStr, it );
    } );

    registerCommand( "IGNITIONFADETIME", [&]( const char *pArg ) {
        std::string argStr( pArg );
        size_t it = 0ull;
        gIgnitionFadeTime = ParseFloat( argStr, it );
    } );

    registerCommand( "SHIFTTIMEMANUAL", [&]( const char *pArg ) {
        std::string argStr( pArg );
        size_t it = 0ull;
        gShiftTimeManual[0] = ParseFloat( argStr, it );
        gShiftTimeManual[1] = ParseFloat( argStr, it );
    } );

    registerCommand( "SHIFTTIMESEQUENTIAL", [&]( const char *pArg ) {
        std::string argStr( pArg );
        size_t it = 0ull;
        gShiftTimeSequential[0] = ParseFloat( argStr, it );
        gShiftTimeSequential[1] = ParseFloat( argStr, it );
    } );

    registerCommand( "SHIFT2NDTO1ST", [&]( const char *pArg ) {
        std::string argStr( pArg );
        size_t it = 0ull;
        gShift2ndTo1st = ParseFloat( argStr, it );
    } );

    registerCommand( "TORQUEINERTIAUP", [&]( const char *pArg ) {
        std::string argStr( pArg );
        size_t it = 0ull;
        gpPhysicsConfig->TorqueInertiaUp = ParseFloat( argStr, it );
    } );

    registerCommand( "TORQUEINERTIADOWN", [&]( const char *pArg ) {
        std::string argStr( pArg );
        size_t it = 0ull;
        gpPhysicsConfig->TorqueInertiaDown = ParseFloat( argStr, it );
    } );

    registerCommand( "ENGINEBRAKEPOWER", [&]( const char *pArg ) {
        std::string argStr( pArg );
        size_t it = 0ull;
        gpPhysicsConfig->EngineBrakePower = ParseFloat( argStr, it );
    } );

    registerCommand( "REDLINEPURCENT", [&]( const char *pArg ) {
        std::string argStr( pArg );
        size_t it = 0ull;
        gpPhysicsConfig->RedlinePurcent = ParseFloat( argStr, it );
    } );

    registerCommand( "WHEELMAXSLIPRATIO", [&]( const char *pArg ) {
        std::string argStr( pArg );
        size_t it = 0ull;
        gpPhysicsConfig->WheelMaxSlipRatioFront = ParseFloat( argStr, it );
        gpPhysicsConfig->WheelMaxSlipRatioRear = ParseFloat( argStr, it );
    } );

    registerCommand( "WHEELMAXSLIPANGLE", [&]( const char *pArg ) {
        std::string argStr( pArg );
        size_t it = 0ull;
        gpPhysicsConfig->WheelMaxSlipAngleFront = ParseFloat( argStr, it );
        gpPhysicsConfig->WheelMaxSlipAngleRear = ParseFloat( argStr, it );
    } );

    registerCommand( "SLIPRATIOSTARTLOOSELATERAL", [&]( const char *pArg ) {
        std::string argStr( pArg );
        size_t it = 0ull;
        gpPhysicsConfig->SlipRatioStartLooseLateralFront =
            ParseFloat( argStr, it );
        gpPhysicsConfig->SlipRatioStartLooseLateralRear =
            ParseFloat( argStr, it );
    } );

    registerCommand( "MAXLATERALLOSS", [&]( const char *pArg ) {
        std::string argStr( pArg );
        size_t it = 0ull;
        gpPhysicsConfig->MaxLateralLossFront = ParseFloat( argStr, it );
        gpPhysicsConfig->MaxLateralLossRear = ParseFloat( argStr, it );
    } );

    registerCommand( "STEERINGHELPSTRAIGHT", [&]( const char *pArg ) {
        std::string argStr( pArg );
        size_t it = 0ull;
        gpPhysicsConfig->SteeringHelpStraight[0] = ParseFloat( argStr, it );
        gpPhysicsConfig->SteeringHelpStraight[1] = ParseFloat( argStr, it );
    } );

    registerCommand( "STEERINGHELPTURN", [&]( const char *pArg ) {
        std::string argStr( pArg );
        size_t it = 0ull;
        gpPhysicsConfig->SteeringHelpTurn[0] = ParseFloat( argStr, it );
        gpPhysicsConfig->SteeringHelpTurn[1] = ParseFloat( argStr, it );
    } );

    registerCommand( "SENSITIVTY", [&]( const char *pArg ) {
        std::string argStr( pArg );
        size_t it = 0ull;
        gSensitivity[0] = 0.0f;
        gSensitivity[1] = 0.5f;
        gSensitivity[2] = 1.0f;
        gSensitivity[3] = ParseFloat( argStr, it );
        gSensitivity[4] = 1.0f;
        gSensitivity[5] = ParseFloat( argStr, it );
    } );

    registerCommand( "STEERINGFILTERSPEED", [&]( const char *pArg ) {
        std::string argStr( pArg );
        size_t it = 0ull;
        const float a = ParseScaledFloat( argStr, it );
        const float b = ParseScaledFloat( argStr, it );
        const float c = ParseScaledFloat( argStr, it );
        for ( int32_t i = 0; i < 5; i++ ) {
            gpPhysicsConfig->SteeringFilterSpeed[i][0] = a;
            gpPhysicsConfig->SteeringFilterSpeed[i][1] = b;
            gpPhysicsConfig->SteeringFilterSpeed[i][2] = c;
        }
    } );

    registerCommand( "STEERINGLIMIT", [&]( const char *pArg ) {
        std::string argStr( pArg );
        size_t it = 0ull;
        gpPhysicsConfig->SteeringLimit[0] = ParseFloat( argStr, it );
        gpPhysicsConfig->SteeringLimit[1] = ParseFloat( argStr, it );
        gpPhysicsConfig->SteeringLimit[2] = ParseFloat( argStr, it );
    } );

    registerCommand( "STEERINGCENTERSPEED", [&]( const char *pArg ) {
        std::string argStr( pArg );
        size_t it = 0ull;
        gpPhysicsConfig->SteeringCenterSpeed[0] = ParseFloat( argStr, it );
        gpPhysicsConfig->SteeringCenterSpeed[1] = ParseFloat( argStr, it );
        gpPhysicsConfig->SteeringCenterSpeed[2] = ParseFloat( argStr, it );
    } );

    registerCommand( "STEERINGSPEED", [&]( const char *pArg ) {
        // FUN_00986190
        std::string argStr( pArg );
        size_t it = 0ull;
        gpPhysicsConfig->SteeringSpeed[0] = ParseFloat( argStr, it );
        gpPhysicsConfig->SteeringSpeed[1] = ParseFloat( argStr, it );
        gpPhysicsConfig->SteeringSpeed[2] = ParseFloat( argStr, it );
    } );

    registerCommand( "STEERINGSTRAIGHTSPEED", [&]( const char *pArg ) {
        // FUN_00986110
        std::string argStr( pArg );
        size_t it = 0ull;
        gpPhysicsConfig->SteeringStraightSpeed[0] = ParseFloat( argStr, it );
        gpPhysicsConfig->SteeringStraightSpeed[1] = ParseFloat( argStr, it );
        gpPhysicsConfig->SteeringStraightSpeed[2] = ParseFloat( argStr, it );
    } );

    registerCommand( "STEERINGCOUNTERSPEED", [&]( const char *pArg ) {
        // FUN_009860d0
        std::string argStr( pArg );
        size_t it = 0ull;
        gpPhysicsConfig->SteeringCounterSpeed[0] = ParseFloat( argStr, it );
        gpPhysicsConfig->SteeringCounterSpeed[1] = ParseFloat( argStr, it );
        gpPhysicsConfig->SteeringCounterSpeed[2] = ParseFloat( argStr, it );
    } );

    registerCommand( "ACTIVEABSSPEED", [&]( const char *pArg ) {
        // FUN_00985eb0
        std::string argStr( pArg );
        size_t it = 0ull;
        gpPhysicsConfig->ActiveABSSpeed[0] = ParseScaledFloat( argStr, it );
        gpPhysicsConfig->ActiveABSSpeed[1] = ParseScaledFloat( argStr, it );
        gpPhysicsConfig->ActiveABSSpeed[2] = ParseScaledFloat( argStr, it );
        gpPhysicsConfig->ActiveABSSpeed[3] = ParseFloat( argStr, it );
        gpPhysicsConfig->ActiveABSSpeed[4] = ParseFloat( argStr, it );
    } );

    registerCommand( "ACTIVEABSVALUE", [&]( const char *pArg ) {
        // FUN_00985e60
        std::string argStr( pArg );
        size_t it = 0ull;
        gpPhysicsConfig->ActiveABSValue[0] = ParseFloat( argStr, it );
        gpPhysicsConfig->ActiveABSValue[1] = ParseFloat( argStr, it );
        gpPhysicsConfig->ActiveABSValue[2] = ParseFloat( argStr, it );
        gpPhysicsConfig->ActiveABSValue[3] = ParseFloat( argStr, it );
        gpPhysicsConfig->ActiveABSValue[4] = ParseFloat( argStr, it );
    } );

    registerCommand( "ELECTRONICPARAMS", [&]( const char *pArg ) {
        std::string argStr( pArg );
        size_t it = 0ull;
        std::string name = ParseStringNoQuotes( argStr, it );
        strncpy( gpPhysicsConfig->ElectronicParams, name.c_str(),
                 sizeof( gpPhysicsConfig->ElectronicParams ) - 1 );
        gPhysicsConfig
            .ElectronicParams[sizeof( gpPhysicsConfig->ElectronicParams ) - 1] =
            '\0';
    } );

    registerCommand( "ELECTRONICPARAMSVALUE", [&]( const char *pArg ) {
        // FUN_00985dc0
        std::string argStr( pArg );
        size_t it = 0ull;
        std::string name = ParseStringNoQuotes( argStr, it );
        float value = ParseFloat( argStr, it );

        int32_t idx = gpPhysicsConfig->NumElectronicParamsValues;
        if ( idx < 9 ) {
            char *pName = gpPhysicsConfig->ElectronicParamsValueName[idx];
            strncpy( pName, name.c_str(), 31 );
            pName[31] = '\0';
            gpPhysicsConfig->ElectronicParamsValue[idx] = value;
            gpPhysicsConfig->NumElectronicParamsValues = idx + 1;
        }
    } );

    registerCommand( "ELECTRONICPOWERCOEF", [&]( const char *pArg ) {
        // FUN_00985d60
        std::string argStr( pArg );
        size_t it = 0ull;
        for ( int32_t i = 0; i < 3; i++ ) {
            gpPhysicsConfig->ElectronicPowerCoefX[i] = ParseFloat( argStr, it );
            gpPhysicsConfig->ElectronicPowerCoefY[i] = ParseFloat( argStr, it );
        }
    } );

    registerCommand( "ACTIVETCTRIGGER", [&]( const char *pArg ) {
        // FUN_00985d20
        std::string argStr( pArg );
        size_t it = 0ull;
        gpPhysicsConfig->ActiveTCTrigger[0] = ParseFloat( argStr, it );
        gpPhysicsConfig->ActiveTCTrigger[1] = ParseFloat( argStr, it );
        gpPhysicsConfig->ActiveTCTrigger[2] = ParseFloat( argStr, it );
    } );

    registerCommand( "ACTIVETCVALUE", [&]( const char *pArg ) {
        // FUN_00985ce0
        std::string argStr( pArg );
        size_t it = 0ull;
        gpPhysicsConfig->ActiveTCValue[0] = ParseFloat( argStr, it );
        gpPhysicsConfig->ActiveTCValue[1] = ParseFloat( argStr, it );
        gpPhysicsConfig->ActiveTCValue[2] = ParseFloat( argStr, it );
    } );

    registerCommand( "TCSTRAIGHTTRIGGER", [&]( const char *pArg ) {
        // FUN_00985c10
        std::string argStr( pArg );
        size_t it = 0ull;
        const float a = ParseScaledFloat( argStr, it );
        const float b = ParseScaledFloat( argStr, it );
        const float c = ParseScaledFloat( argStr, it );
        gpPhysicsConfig->TCStraightTrigger[0] = a;
        gpPhysicsConfig->TCStraightTrigger[1] = b;
        gpPhysicsConfig->TCStraightTrigger[2] = c;
        gpPhysicsConfig->TCStraightTriggerMid[0] = a;
        gpPhysicsConfig->TCStraightTriggerMid[1] = b;
        gpPhysicsConfig->TCStraightTriggerMid[2] = c;
        gpPhysicsConfig->TCStraightTriggerAlt[0] = a;
        gpPhysicsConfig->TCStraightTriggerAlt[1] = b;
        gpPhysicsConfig->TCStraightTriggerAlt[2] = c;
    } );

    registerCommand( "TCSTRAIGHTVALUE", [&]( const char *pArg ) {
        // FUN_00985bd0
        std::string argStr( pArg );
        size_t it = 0ull;
        gpPhysicsConfig->TCStraightValue[0] = ParseFloat( argStr, it );
        gpPhysicsConfig->TCStraightValue[1] = ParseFloat( argStr, it );
        gpPhysicsConfig->TCStraightValue[2] = ParseFloat( argStr, it );
    } );

    registerCommand( "AEROSPEEDS", [&]( const char *pArg ) {
        // FUN_00985b90
        std::string argStr( pArg );
        size_t it = 0ull;
        const float v0 = ParseFloat( argStr, it );
        const float v1 = ParseFloat( argStr, it );
        gpPhysicsConfig->AeroSpeeds[0] = v0 * v0;
        gpPhysicsConfig->AeroSpeeds[1] = v1 * v1;
    } );

    registerCommand( "AEROLENGTH", [&]( const char *pArg ) {
        // FUN_00985b60
        std::string argStr( pArg );
        size_t it = 0ull;
        gpPhysicsConfig->AeroLength[0] = ParseFloat( argStr, it );
        gpPhysicsConfig->AeroLength[1] = ParseFloat( argStr, it );
    } );

    registerCommand( "AEROWIDTH", [&]( const char *pArg ) {
        // FUN_00985b20
        std::string argStr( pArg );
        size_t it = 0ull;
        gpPhysicsConfig->AeroWidth[0] = ParseFloat( argStr, it );
        gpPhysicsConfig->AeroWidth[1] = ParseFloat( argStr, it );
        gpPhysicsConfig->AeroWidth[2] = ParseFloat( argStr, it );
    } );

    registerCommand( "AERODENSITY", [&]( const char *pArg ) {
        // FUN_00985af0
        std::string argStr( pArg );
        size_t it = 0ull;
        gpPhysicsConfig->AeroDensity[0] = ParseFloat( argStr, it );
        gpPhysicsConfig->AeroDensity[1] = ParseFloat( argStr, it );
    } );

    registerCommand( "LEAN_SPEED_RANGE", [&]( const char *pArg ) {
        // FUN_00985aa0
        std::string argStr( pArg );
        size_t it = 0ull;
        gLeanSpeedRange[0] = ParseScaledFloat( argStr, it );
        gLeanSpeedRange[1] = ParseScaledFloat( argStr, it );
        gLeanSpeedRange[2] = ParseScaledFloat( argStr, it );
    } );

    registerCommand( "LEAN_SPEED_VAL", [&]( const char *pArg ) {
        // FUN_00985a50
        std::string argStr( pArg );
        size_t it = 0ull;
        gLeanSpeedValue[0] = ParseFloat( argStr, it );
        gLeanSpeedValue[1] = ParseFloat( argStr, it );
        gLeanSpeedValue[2] = ParseFloat( argStr, it );
        gLeanSpeedValueHigh = ParseFloat( argStr, it );
    } );

    registerCommand( "LEAN_CURVE_RANGE", [&]( const char *pArg ) {
        // FUN_00985a30
        std::string argStr( pArg );
        size_t it = 0ull;
        // The original stores the first value in both slots.
        gLeanCurveRange[0] = ParseFloat( argStr, it );
        gLeanCurveRange[1] = gLeanCurveRange[0];
    } );

    registerCommand( "LEAN_CURVE_VAL", [&]( const char *pArg ) {
        // FUN_00985a00
        std::string argStr( pArg );
        size_t it = 0ull;
        gLeanCurveValue[0] = ParseFloat( argStr, it );
        gLeanCurveValue[1] = ParseFloat( argStr, it );
    } );

    registerCommand( "LEAN_HEAD", [&]( const char *pArg ) {
        // FUN_009859d0
        std::string argStr( pArg );
        size_t it = 0ull;
        gLeanHead[0] = ParseFloat( argStr, it );
        gLeanHead[1] = ParseFloat( argStr, it );
    } );
}
