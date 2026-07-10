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

    registerCommand( "GRASSASTARMAC", [&]( const char *pArg ) {
        // FUN_009847c0
        gbGrassAsTarmac = atoi( pArg ) != 0;
    } );

    registerCommand( "PHYSICSTHREAD", [&]( const char *pArg ) {
        // FUN_009878e0
        std::string argStr( pArg );
        size_t it = 0ull;

        std::string priority = ParseString( argStr, it );
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
        gPhysicsConfig.GravityCoeff = ParseFloat( argStr, it );
    } );

    registerCommand( "FRONTFRICTIONCOEF", [&]( const char *pArg ) {
        std::string argStr( pArg );
        size_t it = 0ull;
        gPhysicsConfig.FrontFrictionCoef = ParseFloat( argStr, it );
    } );

    registerCommand( "REARFRICTIONCOEF", [&]( const char *pArg ) {
        // FUN_00986590
        std::string argStr( pArg );
        size_t it = 0ull;
        gPhysicsConfig.RearFrictionCoef = ParseFloat( argStr, it );
    } );

    registerCommand( "FRONTLATERALFRICTIONCOEF", [&]( const char *pArg ) {
        // FUN_009865d0
        std::string argStr( pArg );
        size_t it = 0ull;
        gPhysicsConfig.FrontLateralFrictionCoef = ParseFloat( argStr, it );
    } );

    registerCommand( "REARLATERALFRICTIONCOEF", [&]( const char *pArg ) {
        // FUN_00986570
        std::string argStr( pArg );
        size_t it = 0ull;
        gPhysicsConfig.RearLateralFrictionCoef = ParseFloat( argStr, it );
    } );

    registerCommand( "FRONTLNGBRAKEFRICTIONCOEF", [&]( const char *pArg ) {
        // FUN_009865b0
        std::string argStr( pArg );
        size_t it = 0ull;
        gPhysicsConfig.FrontLngBrakeFrictionCoef = ParseFloat( argStr, it );
    } );

    registerCommand( "REARLNGBRAKEFRICTIONCOEF", [&]( const char *pArg ) {
        // FUN_00986550
        std::string argStr( pArg );
        size_t it = 0ull;
        gPhysicsConfig.RearLngBrakeFrictionCoef = ParseFloat( argStr, it );
    } );

    registerCommand( "FRONTLATBRAKEFRICTIONCOEFSLOW", [&]( const char *pArg ) {
        // FUN_00986530
        std::string argStr( pArg );
        size_t it = 0ull;
        gPhysicsConfig.FrontLatBrakeFrictionCoefSlow = ParseFloat( argStr, it );
    } );

    registerCommand( "REARLATBRAKEFRICTIONCOEFSLOW", [&]( const char *pArg ) {
        std::string argStr( pArg );
        size_t it = 0ull;
        gPhysicsConfig.RearLatBrakeFrictionCoefSlow = ParseFloat( argStr, it );
    } );

    registerCommand( "FRONTLATBRAKEFRICTIONCOEFFAST", [&]( const char *pArg ) {
        std::string argStr( pArg );
        size_t it = 0ull;
        gPhysicsConfig.FrontLatBrakeFrictionCoefFast = ParseFloat( argStr, it );
    } );

    registerCommand( "REARLATBRAKEFRICTIONCOEFFAST", [&]( const char *pArg ) {
        std::string argStr( pArg );
        size_t it = 0ull;
        gPhysicsConfig.RearLatBrakeFrictionCoefFast = ParseFloat( argStr, it );
    } );

    registerCommand( "LATBRAKEFRICTIONSPEEDSLOW", [&]( const char *pArg ) {
        std::string argStr( pArg );
        size_t it = 0ull;
        const float speed = ParseScaledFloat( argStr, it );
        gPhysicsConfig.LatBrakeFrictionSpeedSlowFront = speed;
        gPhysicsConfig.LatBrakeFrictionSpeedSlowRear = speed;
    } );

    registerCommand( "LATBRAKEFRICTIONSPEEDFAST", [&]( const char *pArg ) {
        std::string argStr( pArg );
        size_t it = 0ull;
        const float speed = ParseScaledFloat( argStr, it );
        gPhysicsConfig.LatBrakeFrictionSpeedFastFront = speed;
        gPhysicsConfig.LatBrakeFrictionSpeedFastRear = speed;
    } );

    registerCommand( "RESISTIVECOEF", [&]( const char *pArg ) {
        std::string argStr( pArg );
        size_t it = 0ull;
        gPhysicsConfig.ResistiveCoefFront = ParseFloat( argStr, it );
        gPhysicsConfig.ResistiveCoefRear = ParseFloat( argStr, it );
    } );

    registerCommand( "RESISTIVELATERALCOEF", [&]( const char *pArg ) {
        std::string argStr( pArg );
        size_t it = 0ull;
        gPhysicsConfig.ResistiveLateralCoefFront = ParseFloat( argStr, it );
        gPhysicsConfig.ResistiveLateralCoefRear = ParseFloat( argStr, it );
    } );

    registerCommand( "INERTIACOEF", [&]( const char *pArg ) {
        std::string argStr( pArg );
        size_t it = 0ull;
        gPhysicsConfig.InertiaCoef = ParseFloat( argStr, it );
    } );

    registerCommand( "MASSCOEF", [&]( const char *pArg ) {
        std::string argStr( pArg );
        size_t it = 0ull;
        gPhysicsConfig.MassCoef = ParseFloat( argStr, it );
    } );

    registerCommand( "CGHEIGHTCOEF", [&]( const char *pArg ) {
        std::string argStr( pArg );
        size_t it = 0ull;
        gPhysicsConfig.CGHeightCoef = ParseFloat( argStr, it );
    } );

    registerCommand( "REBOUNDCOEF", [&]( const char *pArg ) {
        std::string argStr( pArg );
        size_t it = 0ull;
        gPhysicsConfig.ReboundCoef = ParseFloat( argStr, it );
    } );

    registerCommand( "SPRINGCOEF", [&]( const char *pArg ) {
        std::string argStr( pArg );
        size_t it = 0ull;
        gPhysicsConfig.SpringCoef = ParseFloat( argStr, it );
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
        gPhysicsConfig.TorqueInertiaUp = ParseFloat( argStr, it );
    } );

    registerCommand( "TORQUEINERTIADOWN", [&]( const char *pArg ) {
        std::string argStr( pArg );
        size_t it = 0ull;
        gPhysicsConfig.TorqueInertiaDown = ParseFloat( argStr, it );
    } );

    registerCommand( "ENGINEBRAKEPOWER", [&]( const char *pArg ) {
        std::string argStr( pArg );
        size_t it = 0ull;
        gPhysicsConfig.EngineBrakePower = ParseFloat( argStr, it );
    } );

    registerCommand( "REDLINEPURCENT", [&]( const char *pArg ) {
        std::string argStr( pArg );
        size_t it = 0ull;
        gPhysicsConfig.RedlinePurcent = ParseFloat( argStr, it );
    } );

    registerCommand( "WHEELMAXSLIPRATIO", [&]( const char *pArg ) {
        std::string argStr( pArg );
        size_t it = 0ull;
        gPhysicsConfig.WheelMaxSlipRatioFront = ParseFloat( argStr, it );
        gPhysicsConfig.WheelMaxSlipRatioRear = ParseFloat( argStr, it );
    } );

    registerCommand( "WHEELMAXSLIPANGLE", [&]( const char *pArg ) {
        std::string argStr( pArg );
        size_t it = 0ull;
        gPhysicsConfig.WheelMaxSlipAngleFront = ParseFloat( argStr, it );
        gPhysicsConfig.WheelMaxSlipAngleRear = ParseFloat( argStr, it );
    } );

    registerCommand( "SLIPRATIOSTARTLOOSELATERAL", [&]( const char *pArg ) {
        std::string argStr( pArg );
        size_t it = 0ull;
        gPhysicsConfig.SlipRatioStartLooseLateralFront =
            ParseFloat( argStr, it );
        gPhysicsConfig.SlipRatioStartLooseLateralRear =
            ParseFloat( argStr, it );
    } );

    registerCommand( "MAXLATERALLOSS", [&]( const char *pArg ) {
        std::string argStr( pArg );
        size_t it = 0ull;
        gPhysicsConfig.MaxLateralLossFront = ParseFloat( argStr, it );
        gPhysicsConfig.MaxLateralLossRear = ParseFloat( argStr, it );
    } );

    registerCommand( "STEERINGHELPSTRAIGHT", [&]( const char *pArg ) {
        std::string argStr( pArg );
        size_t it = 0ull;
        gPhysicsConfig.SteeringNormal[0] = 1.25f;
        gPhysicsConfig.SteeringNormal[1] = -0.5f;
        gPhysicsConfig.SteeringHelpStraight[0] = ParseFloat( argStr, it );
        gPhysicsConfig.SteeringHelpStraight[1] = ParseFloat( argStr, it );
    } );

    registerCommand( "STEERINGHELPTURN", [&]( const char *pArg ) {
        std::string argStr( pArg );
        size_t it = 0ull;
        gPhysicsConfig.SteeringHelpTurn[0] = ParseFloat( argStr, it );
        gPhysicsConfig.SteeringHelpTurn[1] = ParseFloat( argStr, it );
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
            gPhysicsConfig.SteeringFilterSpeed[i][0] = a;
            gPhysicsConfig.SteeringFilterSpeed[i][1] = b;
            gPhysicsConfig.SteeringFilterSpeed[i][2] = c;
        }
    } );

    registerCommand( "STEERINGLIMIT", [&]( const char *pArg ) {
        std::string argStr( pArg );
        size_t it = 0ull;
        gPhysicsConfig.SteeringLimit[0] = ParseFloat( argStr, it );
        gPhysicsConfig.SteeringLimit[1] = ParseFloat( argStr, it );
        gPhysicsConfig.SteeringLimit[2] = ParseFloat( argStr, it );
    } );

    registerCommand( "STEERINGCENTERSPEED", [&]( const char *pArg ) {
        std::string argStr( pArg );
        size_t it = 0ull;
        gPhysicsConfig.SteeringCenterSpeed[0] = ParseFloat( argStr, it );
        gPhysicsConfig.SteeringCenterSpeed[1] = ParseFloat( argStr, it );
        gPhysicsConfig.SteeringCenterSpeed[2] = ParseFloat( argStr, it );
    } );

    registerCommand( "STEERINGSPEED", [&]( const char *pArg ) {
        // FUN_00986190
        std::string argStr( pArg );
        size_t it = 0ull;
        gPhysicsConfig.SteeringSpeed[0] = ParseFloat( argStr, it );
        gPhysicsConfig.SteeringSpeed[1] = ParseFloat( argStr, it );
        gPhysicsConfig.SteeringSpeed[2] = ParseFloat( argStr, it );
    } );

    registerCommand( "STEERINGSTRAIGHTSPEED", [&]( const char *pArg ) {
        // FUN_00986110
        std::string argStr( pArg );
        size_t it = 0ull;
        gPhysicsConfig.SteeringStraightSpeed[0] = ParseFloat( argStr, it );
        gPhysicsConfig.SteeringStraightSpeed[1] = ParseFloat( argStr, it );
        gPhysicsConfig.SteeringStraightSpeed[2] = ParseFloat( argStr, it );
    } );

    registerCommand( "STEERINGCOUNTERSPEED", [&]( const char *pArg ) {
        // FUN_009860d0
        std::string argStr( pArg );
        size_t it = 0ull;
        gPhysicsConfig.SteeringCounterSpeed[0] = ParseFloat( argStr, it );
        gPhysicsConfig.SteeringCounterSpeed[1] = ParseFloat( argStr, it );
        gPhysicsConfig.SteeringCounterSpeed[2] = ParseFloat( argStr, it );
    } );

    registerCommand( "ACTIVEABSSPEED", [&]( const char *pArg ) {
        // FUN_00985eb0
        std::string argStr( pArg );
        size_t it = 0ull;
        gPhysicsConfig.ActiveABSSpeed[0] = ParseScaledFloat( argStr, it );
        gPhysicsConfig.ActiveABSSpeed[1] = ParseScaledFloat( argStr, it );
        gPhysicsConfig.ActiveABSSpeed[2] = ParseScaledFloat( argStr, it );
        gPhysicsConfig.ActiveABSSpeed[3] = ParseFloat( argStr, it );
        gPhysicsConfig.ActiveABSSpeed[4] = ParseFloat( argStr, it );
    } );

    registerCommand( "ACTIVEABSVALUE", [&]( const char *pArg ) {
        // FUN_00985e60
        std::string argStr( pArg );
        size_t it = 0ull;
        gPhysicsConfig.ActiveABSValue[0] = ParseFloat( argStr, it );
        gPhysicsConfig.ActiveABSValue[1] = ParseFloat( argStr, it );
        gPhysicsConfig.ActiveABSValue[2] = ParseFloat( argStr, it );
        gPhysicsConfig.ActiveABSValue[3] = ParseFloat( argStr, it );
        gPhysicsConfig.ActiveABSValue[4] = ParseFloat( argStr, it );
    } );

    registerCommand( "ELECTRONICPARAMS", [&]( const char *pArg ) {
        std::string argStr( pArg );
        size_t it = 0ull;
        std::string name = ParseStringNoQuotes( argStr, it );
        strncpy( gPhysicsConfig.ElectronicParams, name.c_str(),
                 sizeof( gPhysicsConfig.ElectronicParams ) - 1 );
        gPhysicsConfig
            .ElectronicParams[sizeof( gPhysicsConfig.ElectronicParams ) - 1] =
            '\0';
    } );

    registerCommand( "ELECTRONICPARAMSVALUE", [&]( const char *pArg ) {
        // FUN_00985dc0
        std::string argStr( pArg );
        size_t it = 0ull;
        std::string name = ParseStringNoQuotes( argStr, it );
        float value = ParseFloat( argStr, it );

        int32_t idx = gPhysicsConfig.NumElectronicParamsValues;
        if ( idx < 9 ) {
            char *pName = gPhysicsConfig.ElectronicParamsValueName[idx];
            strncpy( pName, name.c_str(), 31 );
            pName[31] = '\0';
            gPhysicsConfig.ElectronicParamsValue[idx] = value;
            gPhysicsConfig.NumElectronicParamsValues = idx + 1;
        }
    } );

    registerCommand( "ELECTRONICPOWERCOEF", [&]( const char *pArg ) {
        // FUN_00985d60
        std::string argStr( pArg );
        size_t it = 0ull;
        gPhysicsConfig.ElectronicPowerCoef = ParseFloat( argStr, it );
        gPhysicsConfig.ElectronicPowerCoef2[1] = ParseFloat( argStr, it );
        gPhysicsConfig.ElectronicPowerCoef3 = ParseFloat( argStr, it );
        gPhysicsConfig.ElectronicPowerCoef2[2] = ParseFloat( argStr, it );
        gPhysicsConfig.ElectronicPowerCoef2[0] = ParseFloat( argStr, it );
        gPhysicsConfig.ElectronicPowerCoef2[3] = ParseFloat( argStr, it );
    } );

    registerCommand( "ACTIVETCTRIGGER", [&]( const char *pArg ) {
        // FUN_00985d20
        std::string argStr( pArg );
        size_t it = 0ull;
        gPhysicsConfig.ActiveTCTrigger[0] = ParseFloat( argStr, it );
        gPhysicsConfig.ActiveTCTrigger[1] = ParseFloat( argStr, it );
        gPhysicsConfig.ActiveTCTrigger[2] = ParseFloat( argStr, it );
    } );

    registerCommand( "ACTIVETCVALUE", [&]( const char *pArg ) {
        // FUN_00985ce0
        std::string argStr( pArg );
        size_t it = 0ull;
        gPhysicsConfig.ActiveTCValue[0] = ParseFloat( argStr, it );
        gPhysicsConfig.ActiveTCValue[1] = ParseFloat( argStr, it );
        gPhysicsConfig.ActiveTCValue[2] = ParseFloat( argStr, it );
    } );

    registerCommand( "TCSTRAIGHTTRIGGER", [&]( const char *pArg ) {
        // FUN_00985c10
        std::string argStr( pArg );
        size_t it = 0ull;
        const float a = ParseScaledFloat( argStr, it );
        const float b = ParseScaledFloat( argStr, it );
        const float c = ParseScaledFloat( argStr, it );
        gPhysicsConfig.TCStraightTrigger[0] = a;
        gPhysicsConfig.TCStraightTrigger[1] = b;
        gPhysicsConfig.TCStraightTrigger[2] = c;
        gPhysicsConfig.TCStraightTriggerMid[0] = a;
        gPhysicsConfig.TCStraightTriggerMid[1] = b;
        gPhysicsConfig.TCStraightTriggerMid[2] = c;
        gPhysicsConfig.TCStraightTriggerAlt[0] = a;
        gPhysicsConfig.TCStraightTriggerAlt[1] = b;
        gPhysicsConfig.TCStraightTriggerAlt[2] = c;
    } );

    registerCommand( "TCSTRAIGHTVALUE", [&]( const char *pArg ) {
        // FUN_00985bd0
        std::string argStr( pArg );
        size_t it = 0ull;
        gPhysicsConfig.TCStraightValue[0] = ParseFloat( argStr, it );
        gPhysicsConfig.TCStraightValue[1] = ParseFloat( argStr, it );
        gPhysicsConfig.TCStraightValue[2] = ParseFloat( argStr, it );
    } );

    registerCommand( "AEROSPEEDS", [&]( const char *pArg ) {
        // FUN_00985b90
        std::string argStr( pArg );
        size_t it = 0ull;
        const float v0 = ParseFloat( argStr, it );
        const float v1 = ParseFloat( argStr, it );
        gPhysicsConfig.AeroSpeeds[0] = v0 * v0;
        gPhysicsConfig.AeroSpeeds[1] = v1 * v1;
    } );

    registerCommand( "AEROLENGTH", [&]( const char *pArg ) {
        // FUN_00985b60
        std::string argStr( pArg );
        size_t it = 0ull;
        gPhysicsConfig.AeroLength[0] = ParseFloat( argStr, it );
        gPhysicsConfig.AeroLength[1] = ParseFloat( argStr, it );
    } );

    registerCommand( "AEROWIDTH", [&]( const char *pArg ) {
        // FUN_00985b20
        std::string argStr( pArg );
        size_t it = 0ull;
        gPhysicsConfig.AeroWidth[0] = ParseFloat( argStr, it );
        gPhysicsConfig.AeroWidth[1] = ParseFloat( argStr, it );
        gPhysicsConfig.AeroWidth[2] = ParseFloat( argStr, it );
    } );

    registerCommand( "AERODENSITY", [&]( const char *pArg ) {
        // FUN_00985af0
        std::string argStr( pArg );
        size_t it = 0ull;
        gPhysicsConfig.AeroDensity[0] = ParseFloat( argStr, it );
        gPhysicsConfig.AeroDensity[1] = ParseFloat( argStr, it );
    } );

    registerCommand( "LEAN_SPEED_RANGE", [&]( const char *pArg ) {
        // FUN_00985aa0
        std::string argStr( pArg );
        size_t it = 0ull;
        gLeanSpeedRange[0] = ParseFloat( argStr, it );
        gLeanSpeedRange[1] = ParseFloat( argStr, it );
        gLeanSpeedRange[2] = ParseFloat( argStr, it );
    } );

    registerCommand( "LEAN_SPEED_VAL", [&]( const char *pArg ) {
        // FUN_00985a50
        std::string argStr( pArg );
        size_t it = 0ull;
        gLeanSpeedValue[0] = ParseFloat( argStr, it );
        gLeanSpeedValue[1] = ParseFloat( argStr, it );
        gLeanSpeedValue[2] = ParseFloat( argStr, it );
    } );

    registerCommand( "LEAN_CURVE_RANGE", [&]( const char *pArg ) {
        // FUN_00985a30
        std::string argStr( pArg );
        size_t it = 0ull;
        gLeanCurveRange[0] = ParseFloat( argStr, it );
        gLeanCurveRange[1] = ParseFloat( argStr, it );
        gLeanCurveRange[2] = ParseFloat( argStr, it );
    } );

    registerCommand( "LEAN_CURVE_VAL", [&]( const char *pArg ) {
        // FUN_00985a00
        std::string argStr( pArg );
        size_t it = 0ull;
        gLeanCurveValue[0] = ParseFloat( argStr, it );
        gLeanCurveValue[1] = ParseFloat( argStr, it );
        gLeanCurveValue[2] = ParseFloat( argStr, it );
    } );

    registerCommand( "LEAN_HEAD", [&]( const char *pArg ) {
        // FUN_009859d0
        std::string argStr( pArg );
        size_t it = 0ull;
        gLeanHead[0] = ParseFloat( argStr, it );
        gLeanHead[1] = ParseFloat( argStr, it );
        gLeanHead[2] = ParseFloat( argStr, it );
    } );
}
