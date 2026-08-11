#pragma once

#include "shared.h"

struct PhysicsConfig {
    float GravityCoeff;

    float MassCoef;
    float InertiaCoef;
    float CGHeightCoef;

    float WheelMaxSlipRatioFront;
    float WheelMaxSlipAngleFront;
    float SlipRatioStartLooseLateralFront;
    float MaxLateralLossFront;
    float FrontFrictionCoef;
    float FrontLateralFrictionCoef;
    float FrontLngBrakeFrictionCoef;
    float ResistiveCoefFront;
    float ResistiveLateralCoefFront;
    float LatBrakeFrictionSpeedSlowFront;
    float LatBrakeFrictionSpeedFastFront;
    float FrontLatBrakeFrictionCoefSlow;
    float FrontLatBrakeFrictionCoefFast;

    float WheelMaxSlipRatioRear;
    float WheelMaxSlipAngleRear;
    float SlipRatioStartLooseLateralRear;
    float MaxLateralLossRear;
    float RearFrictionCoef;
    float RearLateralFrictionCoef;
    float RearLngBrakeFrictionCoef;
    float ResistiveCoefRear;
    float ResistiveLateralCoefRear;
    float LatBrakeFrictionSpeedSlowRear;
    float LatBrakeFrictionSpeedFastRear;
    float RearLatBrakeFrictionCoefSlow;
    float RearLatBrakeFrictionCoefFast;

    float EngineBrakePower;
    float TorqueInertiaUp;
    float TorqueInertiaDown;
    float RedlinePurcent;

    float ReboundCoef;
    float SpringCoef;

    float SteeringFilterSpeed[5][3];
    float SteeringLimit[3];
    float SteeringSpeed[3];
    float SteeringCenterSpeed[3];
    float SteeringStraightSpeed[3];
    float SteeringCounterSpeed[3];
    float SteeringNormal[2] = { 1.25f, -0.5f };
    float SteeringHelpStraight[2];
    float SteeringHelpTurn[2];

    float AeroDensity[2];
    float AeroSpeeds[2];
    float AeroLength[2];
    float AeroWidth[3];

    float ActiveTCTrigger[3];
    float ActiveTCValue[3];
    float TCStraightTrigger[3];
    float TCStraightTriggerMid[3];
    float TCStraightTriggerAlt[3];
    float TCStraightValue[3];

    float ActiveABSSpeed[5];
    float ActiveABSValue[5];

    float ElectronicPowerCoef;
    float ElectronicPowerCoef3;
    float ElectronicPowerCoef2[4];

    char ElectronicParams[32];
    char ElectronicParamsValueName[9][32];
    float ElectronicParamsValue[9];
    int32_t NumElectronicParamsValues;
};

extern PhysicsConfig gPhysicsConfig;

extern float gSpringCoef;
extern float gSensitivity[6];
extern float gIgnitionFadeTime;
extern float gShiftTimeManual[2];
extern float gShiftTimeSequential[2];
extern float gShift2ndTo1st;
extern float gTorquePurcentToContact;
extern bool gUseSteerControl;

extern float gLeanSpeedRange[3];
extern float gLeanSpeedValue[3];
extern float gLeanCurveRange[3];
extern float gLeanCurveValue[3];
extern float gLeanHead[3];

extern bool gbGrassAsTarmac;