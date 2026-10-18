#include "shared.h"
#include "gs_config.h"

#include "input/gs_device.h"
#include "render/camera/camera_replay_road.h"

uint32_t gRPLConfig[5] = {}; // DAT_010d6f38

static constexpr float kMphToMs = 0.44694445f; // DAT_00f41860

static int32_t
ParseFloats( const char *pArg, float *pValues, const int32_t count ) {
    int32_t numParsed = 0;
    char *pEnd = nullptr;
    for ( ; numParsed < count; numParsed++ ) {
        pValues[numParsed] = strtof( pArg, &pEnd );
        if ( pEnd == pArg ) {
            break;
        }
        pArg = pEnd;
    }
    return numParsed;
}

void GSConfig::registerReplayCommands() {
    registerCommand( "SAVEREPLAY", [&]( const char *pArg ) {
        // FUN_009856c0
        char filename[32] = {};
        uint32_t size = 0;
        sscanf( pArg, "%31s %u", filename, &size );
        gDevice.requestReplay( filename, size, true );
    } );

    registerCommand( "LOADREPLAY", [&]( const char *pArg ) {
        // FUN_00985680
        char filename[32] = {};
        uint32_t size = 0;
        sscanf( pArg, "%31s %u", filename, &size );
        gDevice.requestReplay( filename, size, false );
    } );

    registerCommand( "RPL_CONFIG", [&]( const char *pArg ) {
        // FUN_009835d0
        sscanf( pArg, "%u %u %u %u %u", &gRPLConfig[0], &gRPLConfig[1],
                &gRPLConfig[2], &gRPLConfig[3], &gRPLConfig[4] );
    } );

    registerCommand( "RPL_FREQUENCY", [&]( const char *pArg ) {
        // FUN_009835b0
        sscanf( pArg, "%d %d", &gRPLRatioCamOutCamOnCar,
                &gRPLRatioCamCarOutInGame );
    } );

    registerCommand( "RPL_SWAPLENGTH", [&]( const char *pArg ) {
        // FUN_00983590
        gRPLSwapLength = static_cast<float>( atof( pArg ) );
    } );

    registerCommand( "RPL_SWAPTIME", [&]( const char *pArg ) {
        // FUN_00983560
        float values[2] = {};
        ParseFloats( pArg, values, 2 );
        gRPLMinTimeToSwap = values[0];
        gRPLMaxTimeToSwap = values[1];
    } );

    registerCommand( "RPL_HEIGHTS", [&]( const char *pArg ) {
        // FUN_00983520
        float values[3] = {};
        ParseFloats( pArg, values, 3 );
        gRPLMinHeight = values[0];
        gRPLMaxHeight = values[1];
        gRPLMaxHeightModeLarge = values[2];
    } );

    registerCommand( "RPL_WIDTH", [&]( const char *pArg ) {
        // FUN_00983500
        gRPLMaxWidthFromCenterOfRoad = static_cast<float>( atof( pArg ) );
    } );

    registerCommand( "RPL_SPEEDRATIO", [&]( const char *pArg ) {
        // FUN_009834e0
        gRPLSpeedRatio = static_cast<float>( atof( pArg ) );
    } );

    registerCommand( "RPL_CARWHEELOFFSET", [&]( const char *pArg ) {
        // FUN_009871a0
        float values[4] = {};
        ParseFloats( pArg, values, 4 );
        gRPLCarWheelOffset.x() = values[0];
        gRPLCarWheelOffset.y() = values[1];
        gRPLCarWheelOffset.z() = values[2];
        gRPLCarWheelOffset.w() = values[3];
    } );

    registerCommand( "RPL_CAROFFSETPOS", [&]( const char *pArg ) {
        // FUN_00987150
        float values[3] = {};
        ParseFloats( pArg, values, 3 );
        gRPLCarOffsetPos.x() = values[0];
        gRPLCarOffsetPos.y() = values[1];
        gRPLCarOffsetPos.z() = values[2];
        gRPLCarOffsetPos.w() = 1.0f;
    } );

    registerCommand( "RPL_CAROFFSETTRG", [&]( const char *pArg ) {
        // FUN_00987100
        float values[3] = {};
        ParseFloats( pArg, values, 3 );
        gRPLCarOffsetTRG.x() = values[0];
        gRPLCarOffsetTRG.y() = values[1];
        gRPLCarOffsetTRG.z() = values[2];
        gRPLCarOffsetTRG.w() = 1.0f;
    } );

    registerCommand( "RPL_FOVNORMAL", [&]( const char *pArg ) {
        // FUN_009834b0
        float values[2] = {};
        ParseFloats( pArg, values, 2 );
        gRPLFOVNormalMin = values[0];
        gRPLFOVNormalMax = values[1];
    } );

    registerCommand( "RPL_FOVZOOM", [&]( const char *pArg ) {
        // FUN_00983460
        float values[4] = {};
        ParseFloats( pArg, values, 4 );
        gRPLFOVZoomK1 = values[0];
        gRPLFOVZoomK2 = values[1];
        gRPLFOVZoomMin = values[2];
        gRPLFOVZoomMax = values[3];
    } );

    registerCommand( "RPL_FOVLARGE", [&]( const char *pArg ) {
        // FUN_00983430
        float values[2] = {};
        ParseFloats( pArg, values, 2 );
        gRPLFOVLargeMin = values[0];
        gRPLFOVLargeMax = values[1];
    } );

    registerCommand( "RPL_FOVCAR", [&]( const char *pArg ) {
        // FUN_00983410
        gRPLFOVCar = static_cast<float>( atof( pArg ) );
    } );

    registerCommand( "RPL_DOFCAMNORMAL", [&]( const char *pArg ) {
        // FUN_009833e0
        float values[2] = {};
        ParseFloats( pArg, values, 2 );
        gRPLDOFCamNormalMin = values[0];
        gRPLDOFCamNormalMax = values[1];
    } );

    registerCommand( "RPL_DOFCAMZOOM", [&]( const char *pArg ) {
        // FUN_009833b0
        float values[2] = {};
        ParseFloats( pArg, values, 2 );
        gRPLDOFCamZoomMin = values[0];
        gRPLDOFCamZoomMax = values[1];
    } );

    registerCommand( "RPL_DOFCAMLARGE", [&]( const char *pArg ) {
        // FUN_00983380
        float values[2] = {};
        ParseFloats( pArg, values, 2 );
        gRPLDOFCamLargeMin = values[0];
        gRPLDOFCamLargeMax = values[1];
    } );

    registerCommand( "RPL_DOFCAMFIXED", [&]( const char *pArg ) {
        // FUN_00983350
        float values[2] = {};
        ParseFloats( pArg, values, 2 );
        gRPLDOFCamFixedMin = values[0];
        gRPLDOFCamFixedMax = values[1];
    } );

    registerCommand( "RPL_SPRINGMINPOS", [&]( const char *pArg ) {
        // FUN_009870c0
        float values[3] = {};
        ParseFloats( pArg, values, 3 );
        gRPLSpringMinPos.x() = values[0];
        gRPLSpringMinPos.y() = values[1];
        gRPLSpringMinPos.z() = values[2];
    } );

    registerCommand( "RPL_SPRINGMAXPOS", [&]( const char *pArg ) {
        // FUN_00987080
        float values[3] = {};
        ParseFloats( pArg, values, 3 );
        gRPLSpringMaxPos.x() = values[0];
        gRPLSpringMaxPos.y() = values[1];
        gRPLSpringMaxPos.z() = values[2];
    } );

    registerCommand( "RPL_SPRINGRATE", [&]( const char *pArg ) {
        // FUN_00987040
        float values[3] = {};
        ParseFloats( pArg, values, 3 );
        gRPLSpringRate.x() = values[0];
        gRPLSpringRate.y() = values[1];
        gRPLSpringRate.z() = values[2];
    } );

    registerCommand( "RPL_SPRINGDAMPING", [&]( const char *pArg ) {
        // FUN_00987000
        float values[3] = {};
        ParseFloats( pArg, values, 3 );
        gRPLSpringDamping.x() = values[0];
        gRPLSpringDamping.y() = values[1];
        gRPLSpringDamping.z() = values[2];
    } );

    registerCommand( "RPL_SPRINGFORCESCALE", [&]( const char *pArg ) {
        // FUN_00986fc0
        float values[3] = {};
        ParseFloats( pArg, values, 3 );
        gRPLSpringForceScale.x() = values[0];
        gRPLSpringForceScale.y() = values[1];
        gRPLSpringForceScale.z() = values[2];
    } );

    registerCommand( "RPL_SPRINGFORCEDEADZONE", [&]( const char *pArg ) {
        // FUN_00986f80
        float values[3] = {};
        ParseFloats( pArg, values, 3 );
        gRPLSpringForceDeadZone.x() = values[0];
        gRPLSpringForceDeadZone.y() = values[1];
        gRPLSpringForceDeadZone.z() = values[2];
    } );

    registerCommand( "RPL_SPRINGFORCECLAMPING", [&]( const char *pArg ) {
        // FUN_00986f40
        float values[3] = {};
        ParseFloats( pArg, values, 3 );
        gRPLSpringForceClamping.x() = values[0];
        gRPLSpringForceClamping.y() = values[1];
        gRPLSpringForceClamping.z() = values[2];
    } );

    registerCommand( "RPL_SPRINGTIMESCALE", [&]( const char *pArg ) {
        // FUN_00983330
        gRPLSpringTimescale = static_cast<float>( atof( pArg ) );
    } );

    registerCommand( "RPL_HELICO_SPEEDS", [&]( const char *pArg ) {
        // FUN_009832b0
        float values[5] = {};
        ParseFloats( pArg, values, 5 );
        gRPLHelicoMaxAccelLongitudinal = values[0] * kMphToMs;
        gRPLHelicoMaxAccelLateral = values[1] * kMphToMs;
        gRPLHelicoMaxSpeedLongitudinal = values[2] * kMphToMs;
        gRPLHelicoMaxSpeedLateral = values[3] * kMphToMs;
        gRPLDampingMotion = values[4] * kMphToMs;
    } );

    registerCommand( "RPL_HELICO_HEIGHT", [&]( const char *pArg ) {
        // FUN_00983290
        gRPLHelicoHeight = static_cast<float>( atof( pArg ) );
    } );

    registerCommand( "RPL_HELICO_FOV", [&]( const char *pArg ) {
        // FUN_00983270
        gRPLHelicoFOV = static_cast<float>( atof( pArg ) );
    } );

    registerCommand( "RPL_HELICO_AIM", [&]( const char *pArg ) {
        // FUN_00986ef0
        float values[3] = {};
        ParseFloats( pArg, values, 3 );
        gRPLHelicoAim.x() = values[0];
        gRPLHelicoAim.y() = values[1];
        gRPLHelicoAim.z() = values[2];
        gRPLHelicoAim.w() = 0.0f;
    } );
}
