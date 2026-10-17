#include "shared.h"
#include "physics_config.h"

PhysicsConfig gPhysicsConfig = {};                // DAT_0168d230
PhysicsConfig gBikePhysicsConfig = {};            // DAT_0168ce80
PhysicsConfig *gpPhysicsConfig = &gPhysicsConfig; // DAT_010e7814

float gSpringCoef = 1.0f;                     // DAT_00f8a1e4
float gSensitivity[6] = { 0, 0, 0, 0, 0, 0 }; // DAT_010e7834
float gIgnitionFadeTime = 1.0f;               // DAT_00f8a278
float gShiftTimeManual[2] = { 0, 0 };         // DAT_010e7818, DAT_010e7820
float gShiftTimeSequential[2] = { 0, 0 };     // DAT_010e781c, DAT_010e7824
float gShift2ndTo1st = 1.0f;                  // DAT_00f8a280
float gTorquePurcentToContact = 0.0f;         // DAT_00f8a298

float gLeanSpeedValueHigh = 0.7f;                  // DAT_00f8a29c
float gLeanSpeedRange[3] = { 5.0f, 30.0f, 60.0f }; // DAT_00f8a2a0
float gLeanSpeedValue[3] = { 0.75f, 1.2f, 0.75f }; // DAT_00f8a2ac
float gLeanCurveRange[2] = { 0.01f, 0.05f };       // DAT_00f8a2b8
float gLeanCurveValue[2] = { 0.35f, 0.85f };       // DAT_00f8a2c0
float gLeanHead[2] = { 0.5f, 0.5f };               // DAT_00f8a2c8

// TODO: Move To PhysicsConfig (this is a member of the global struct
// -> byte ptr [EDX + 0x1f8],CL)
bool gUseSteerControl = false;

bool gbGrassAsTarmac = false; // DAT_010e7830

void BeginBikePhysicsConfig() {
    // FUN_0098e960 (inlined)
    gBikePhysicsConfig = gPhysicsConfig;
    for ( char *pName : gBikePhysicsConfig.ElectronicParamsValueName ) {
        pName[0] = '\0';
    }
    memset( gBikePhysicsConfig.ElectronicParamsValue, 0,
            sizeof( gBikePhysicsConfig.ElectronicParamsValue ) );
    gBikePhysicsConfig.NumElectronicParamsValues = 0;
    gpPhysicsConfig = &gBikePhysicsConfig;
}

void EndBikePhysicsConfig() { gpPhysicsConfig = &gPhysicsConfig; }
