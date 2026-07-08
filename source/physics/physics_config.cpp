#include "shared.h"
#include "physics_config.h"

PhysicsConfig gPhysicsConfig = {}; // DAT_010e7814

float gSpringCoef = 1.0f;                     // DAT_00f8a1e4
float gSensitivity[6] = { 0, 0, 0, 0, 0, 0 }; // DAT_010e7834
float gIgnitionFadeTime = 1.0f;               // DAT_00f8a278
float gShiftTimeManual[2] = { 0, 0 };         // DAT_010e7818, DAT_010e7820
float gShiftTimeSequential[2] = { 0, 0 };     // DAT_010e781c, DAT_010e7824
float gShift2ndTo1st = 1.0f;                  // DAT_00f8a280
float gTorquePurcentToContact = 0.0f;         // DAT_00f8a298

float gLeanSpeedRange[3] = { 5.0f, 30.0f, 60.0f }; // DAT_00f8a2a0
float gLeanSpeedValue[3] = { 0.75f, 1.2f, 0.75f }; // DAT_00f8a2ac
float gLeanCurveRange[3] = { 0.0099999998f, 0.05f,
                             0.34999999f };              // DAT_00f8a2b8
float gLeanCurveValue[3] = { 0.34999999f, 0.85f, 0.5f }; // DAT_00f8a2c0
float gLeanHead[3] = { 0.5f, 0.5f, 50.0f };              // DAT_00f8a2c8

// TODO: Move To PhysicsConfig (this is a member of the global struct
// -> byte ptr [EDX + 0x1f8],CL)
bool gUseSteerControl = false;

bool gbGrassAsTarmac = false; // DAT_010e7830
