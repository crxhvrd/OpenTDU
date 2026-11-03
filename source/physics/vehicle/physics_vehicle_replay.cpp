#include "shared.h"
#include "physics_vehicle.h"

#include "core/arg_parser.h"

#include <Jolt/Jolt.h>
#include <Jolt/Physics/Body/Body.h>
#include <Jolt/Physics/Body/MotionProperties.h>
#include <Jolt/Physics/Collision/Shape/Shape.h>

#include <cstdio>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

// Replay snapshots from the original game (Havok) into Jolt (to evaluate the
// faithfulness of the physics model)
// (this throwaway tool is AI sloppa so dont expect high quality code)
namespace {
    constexpr int32_t kRecHeaderSize = 0x10;
    constexpr int32_t kRecVehSize = 0x43b0;
    constexpr int32_t kRecWheelStride = 0x450;
    constexpr int32_t kRecNumWheels = 4;
    constexpr int32_t kRecMotionSize = 0x140;
    constexpr int32_t kRecWheels = kRecHeaderSize + kRecVehSize;
    constexpr int32_t kRecMotion = kRecWheels + kRecWheelStride * kRecNumWheels;
    constexpr int32_t kRecSizeV1 = kRecMotion + kRecMotionSize;
    constexpr int32_t kRecEngine = kRecSizeV1;
    constexpr int32_t kRecDrivetrain = kRecEngine + 0x100;
    constexpr int32_t kRecSizeV2 = kRecDrivetrain + 0x100;
    constexpr uint32_t kRecFormatV2 = 2;

    struct RetailStepRecording {
        std::vector<uint8_t> Data;
        int32_t NumRecords = 0;
        int32_t RecordSize = kRecSizeV1;
        bool bHasEngine = false;
        FILE *pOutput = nullptr;
        const PhysicsVehicle *pTarget = nullptr;
        JPH::RVec3 Offset = JPH::RVec3::sZero();
        std::string Path;
        int32_t NextStep = -1; // record the next physics step will replay
        std::vector<int32_t> FreeStarts;
        int32_t FreeLength = 0;
        FILE *pFreeOutput = nullptr;

        int32_t freeWindow( const int32_t index ) const {
            for ( size_t k = 0; k < FreeStarts.size(); ++k ) {
                if ( index > FreeStarts[k] &&
                     index <= FreeStarts[k] + FreeLength ) {
                    return static_cast<int32_t>( k );
                }
            }
            return -1;
        }

        const uint8_t *record( const int32_t index ) const {
            return Data.data() + static_cast<size_t>( index ) * RecordSize;
        }
        static float f32( const uint8_t *pRecord, const int32_t offset ) {
            float value;
            memcpy( &value, pRecord + offset, sizeof( value ) );
            return value;
        }
        static JPH::Vec3 vec3( const uint8_t *pRecord, const int32_t offset ) {
            return JPH::Vec3( f32( pRecord, offset ),
                              f32( pRecord, offset + 4 ),
                              f32( pRecord, offset + 8 ) );
        }
        static float veh( const uint8_t *pRecord, const int32_t offset ) {
            return f32( pRecord, kRecHeaderSize + offset );
        }
        static int32_t vehInt( const uint8_t *pRecord, const int32_t offset ) {
            int32_t value;
            memcpy( &value, pRecord + kRecHeaderSize + offset,
                    sizeof( value ) );
            return value;
        }
        static float
        wheel( const uint8_t *pRecord, const int i, const int32_t offset ) {
            return f32( pRecord, kRecWheels + i * kRecWheelStride + offset );
        }
        static JPH::Vec3
        wheelVec( const uint8_t *pRecord, const int i, const int32_t offset ) {
            return vec3( pRecord, kRecWheels + i * kRecWheelStride + offset );
        }
        static bool wheelGrounded( const uint8_t *pRecord, const int i ) {
            return pRecord[kRecWheels + i * kRecWheelStride + 0x188] != 0;
        }
        static float engine( const uint8_t *pRecord, const int32_t offset ) {
            return f32( pRecord, kRecEngine + offset );
        }
        static JPH::Vec3 motionVec( const uint8_t *pRecord,
                                    const int32_t offset ) {
            return vec3( pRecord, kRecMotion + offset );
        }
        static float toPortLength( const uint8_t *pRecord,
                                   const int i,
                                   const float retailLength,
                                   const float droopStop ) {
            return droopStop - ( retailLength - wheel( pRecord, i, 0x248 ) );
        }
    };
    RetailStepRecording gRetailStepRecording;

    CmdLineArg CmdLineArgPhysicsReplay( "physreplay", []( const char *pArg ) {
        RetailStepRecording &rec = gRetailStepRecording;
        FILE *pFile = fopen( pArg, "rb" );
        if ( pFile == nullptr ) {
            OTDU_LOG_ERROR( "physreplay: cannot open '%s'\n", pArg );
            return;
        }
        fseek( pFile, 0, SEEK_END );
        const long size = ftell( pFile );
        fseek( pFile, 0, SEEK_SET );
        rec.Data.resize( static_cast<size_t>( size ) );
        const size_t numRead =
            fread( rec.Data.data(), 1, rec.Data.size(), pFile );
        fclose( pFile );
        uint32_t format = 0;
        if ( numRead >= kRecHeaderSize ) {
            memcpy( &format, rec.Data.data() + 0xc, sizeof( format ) );
        }
        rec.bHasEngine = ( format == kRecFormatV2 );
        rec.RecordSize = rec.bHasEngine ? kRecSizeV2 : kRecSizeV1;
        rec.NumRecords = static_cast<int32_t>( numRead / rec.RecordSize );
        if ( rec.NumRecords > 0 ) {
            gbSimuModeEnabled = rec.Data[kRecHeaderSize + 0x6f8] != 0;
        }

        rec.Path = pArg;
        const std::string outputPath = std::string( pArg ) + ".port.csv";
        rec.pOutput = fopen( outputPath.c_str(), "w" );
        if ( rec.pOutput != nullptr ) {
            fprintf(
                rec.pOutput,
                "step,dt,speed,steerPort,steerRetail,gearPort,"
                "gearRetail,engOmegaPort,engOmegaRetail,engTorquePort,"
                "engTorqueRetail,rollRatePort,rollRateRetail,pitchRatePort,"
                "pitchRateRetail,yawRatePort,yawRateRetail,velXPort,velXRetail,"
                "velYPort,velYRetail,velZPort,velZRetail" );
            static const char *kFields[] = {
                "len",    "susp",   "load",     "vLong",    "vLat",    "omega",
                "fLong",  "fLat",   "gripLo",   "gripLa",   "bGripLa", "brakeT",
                "driveT", "steerA", "slipping", "brakeOut", "inertia" };
            for ( int i = 0; i < kRecNumWheels; ++i ) {
                for ( const char *pField : kFields ) {
                    fprintf( rec.pOutput, ",w%d_%s_p,w%d_%s_r", i, pField, i,
                             pField );
                }
            }
            fprintf( rec.pOutput, "\n" );
        }
        OTDU_LOG_INFO( "physreplay: %d steps from '%s' -> '%s'\n",
                       rec.NumRecords, pArg, outputPath.c_str() );
    } );

    // -physreplayfree <start>[,<start>...]:<steps>
    CmdLineArg
        CmdLineArgPhysicsReplayFree( "physreplayfree", []( const char *pArg ) {
            RetailStepRecording &rec = gRetailStepRecording;
            const char *pLength = strchr( pArg, ':' );
            if ( pLength == nullptr ) {
                OTDU_LOG_ERROR(
                    "physreplayfree: expected <start>,...:<steps>\n" );
                return;
            }
            rec.FreeLength = atoi( pLength + 1 );
            for ( const char *pCursor = pArg; pCursor < pLength; ) {
                rec.FreeStarts.push_back( atoi( pCursor ) );
                const char *pComma = strchr( pCursor, ',' );
                if ( pComma == nullptr || pComma > pLength ) {
                    break;
                }
                pCursor = pComma + 1;
            }
            OTDU_LOG_INFO( "physreplayfree: %zu windows of %d steps\n",
                           rec.FreeStarts.size(), rec.FreeLength );
        } );
} // namespace

bool RetailReplayPeekDeltaTime( float &deltaTime ) {
    // OTDU_ADDITION
    const RetailStepRecording &rec = gRetailStepRecording;
    if ( rec.pTarget == nullptr || rec.NextStep < 0 ||
         rec.NextStep >= rec.NumRecords ) {
        return false;
    }
    deltaTime = RetailStepRecording::f32( rec.record( rec.NextStep ), 4 );
    return true;
}

bool PhysicsVehicle::retailReplayBeginStep( float &deltaTime ) {
    RetailStepRecording &rec = gRetailStepRecording;
    retailReplayIndex = -1;
    if ( rec.NumRecords < 2 || bIsAI || bIsNetwork ||
         pDrivingControl == nullptr || pChassisBody == nullptr ) {
        static int32_t sNumRejects = 0;
        if ( rec.NumRecords >= 2 && rec.pTarget == nullptr &&
             sNumRejects++ < 8 ) {
            OTDU_LOG_INFO(
                "physreplay: skip vehicle %p (ai %d net %d control %p)\n", this,
                bIsAI, bIsNetwork, pDrivingControl );
        }
        return false;
    }
    if ( rec.pTarget == nullptr ) {
        rec.pTarget = this;
        rec.Offset = pChassisBody->GetPosition() -
                     JPH::RVec3( RetailStepRecording::motionVec(
                         rec.record( 1 ), 0x40 ) );
        // The recorded path must not meet the port's world geometry around
        // the spawn point (hull contacts); the wheels use the recorded ground.
        rec.Offset += JPH::RVec3( 0.0f, 1500.0f, 0.0f );
        retailReplayStep = 1;
        rec.NextStep = 1;
        OTDU_LOG_INFO(
            "physreplay: replaying onto the local player vehicle "
            "(steer sens %.3f linearity %.3f control %d, driving "
            "mode %d, mass port %.1f retail %.1f, CoM y port %.4f "
            "retail %.4f)\n",
            steerSensitivity, steerAssistBlend, bUseSteerControl ? 1 : 0,
            static_cast<int>( drivingMode ),
            1.0f / pChassisBody->GetMotionProperties()->GetInverseMass(),
            1.0f /
                RetailStepRecording::f32( rec.record( 1 ), kRecMotion + 0xfc ),
            pChassisBody->GetShape()->GetCenterOfMass().GetY(),
            RetailStepRecording::f32( rec.record( 1 ), kRecMotion + 0x94 ) );
        for ( int i = 0; i < std::min( vehicle.NumWheels, kRecNumWheels );
              ++i ) {
            const uint8_t *pFirst = rec.record( 1 );
            const JPH::Vec3 d =
                RetailStepRecording::wheelVec( pFirst, i, 0x3c0 ) -
                RetailStepRecording::motionVec( pFirst, 0x40 );
            const JPH::Vec3 retailCentre(
                d.Dot( RetailStepRecording::motionVec( pFirst, 0x10 ) ),
                d.Dot( RetailStepRecording::motionVec( pFirst, 0x20 ) ),
                d.Dot( RetailStepRecording::motionVec( pFirst, 0x30 ) ) );
            OTDU_LOG_INFO( "physreplay: wheel %d attach port (%.4f %.4f %.4f) "
                           "retail centre (%.4f %.4f %.4f)\n",
                           i, wheelAttach[i].x(), wheelAttach[i].y(),
                           wheelAttach[i].z(), retailCentre.GetX(),
                           retailCentre.GetY(), retailCentre.GetZ() );
        }
    }
    if ( rec.pTarget != this || retailReplayStep >= rec.NumRecords ) {
        return false;
    }
    retailReplayIndex = retailReplayStep;

    const uint8_t *pCur = rec.record( retailReplayIndex );
    const uint8_t *pPrev = rec.record( retailReplayIndex - 1 );
    deltaTime = RetailStepRecording::f32( pCur, 4 );

    const int32_t freeWindow = rec.freeWindow( retailReplayIndex );
    bRetailReplayFree = freeWindow >= 0;
    if ( bRetailReplayFree ) {
        retailReplayLogFree( freeWindow );
        drivingMode = DM_Player;
        rawInputThrottle = RetailStepRecording::veh( pCur, 0xe04 );
        rawInputBrake = RetailStepRecording::veh( pCur, 0xe08 );
        inputHandbrake = RetailStepRecording::veh( pCur, 0xe0c );
        inputSteer = RetailStepRecording::veh( pCur, 0xe10 );
        clutchPedal = RetailStepRecording::veh( pCur, 0xe18 );
        setDrivingHelpLevel( RetailStepRecording::vehInt( pCur, 0x704 ) );
        return true;
    }

    retailReplayPreLinVel = pChassisBody->GetLinearVelocity();
    retailReplayPreAngVel = pChassisBody->GetAngularVelocity();

    const JPH::Mat44 rotation(
        JPH::Vec4( RetailStepRecording::motionVec( pCur, 0x10 ), 0.0f ),
        JPH::Vec4( RetailStepRecording::motionVec( pCur, 0x20 ), 0.0f ),
        JPH::Vec4( RetailStepRecording::motionVec( pCur, 0x30 ), 0.0f ),
        JPH::Vec4( 0.0f, 0.0f, 0.0f, 1.0f ) );
    const JPH::RVec3 position =
        JPH::RVec3( RetailStepRecording::motionVec( pCur, 0x40 ) ) + rec.Offset;
    // Inside the step the broadphase is locked for modification, so the
    // BodyInterface setters (which notify it) would self-deadlock; the bodies
    // are already locked for us, write the chassis directly.
    pChassisBody->SetPositionAndRotationInternal(
        position, rotation.GetQuaternion().Normalized() );
    pChassisBody->SetLinearVelocity(
        RetailStepRecording::motionVec( pCur, 0xd0 ) );
    pChassisBody->SetAngularVelocity(
        RetailStepRecording::motionVec( pCur, 0xe0 ) );

    // The recording was driven in the player mode; the game can force the
    // slow-down/parked modes on the port (GSPhysics::forwardInput), which
    // would rewrite the pedals and hand the clutch to manual control.
    drivingMode = DM_Player;
    vehicle.Clutch.bManualControl = false;

    // Driving struct (veh+0xe00); steer shares the port's sign (+X).
    rawInputThrottle = RetailStepRecording::veh( pCur, 0xe04 );
    rawInputBrake = RetailStepRecording::veh( pCur, 0xe08 );
    inputHandbrake = RetailStepRecording::veh( pCur, 0xe0c );
    inputSteer = RetailStepRecording::veh( pCur, 0xe10 );
    clutchPedal = RetailStepRecording::veh( pCur, 0xe18 );
    filteredSteer = RetailStepRecording::veh( pPrev, 0x4280 );

    for ( int i = 0; i < std::min( vehicle.NumWheels, kRecNumWheels ); ++i ) {
        PhysicsWheelState &w = vehicle.Wheels[i];
        w.AngularVel = RetailStepRecording::wheel( pPrev, i, 0x3e8 );
        prevSuspLength[i] = RetailStepRecording::toPortLength(
            pPrev, i, RetailStepRecording::wheel( pPrev, i, 0x3e0 ),
            suspDroopStop[i] );
        prevGroundHeight[i] = JPH::Clamp( prevSuspLength[i], suspMinLength[i],
                                          suspDroopStop[i] ) +
                              w.Radius;
        suspDamperLP[i] = RetailStepRecording::wheel( pPrev, i, 0x3e4 );
        // FUN_0041b920 only rewrites the rear wheels above 2 m/s; the fronts
        // keep the last low-speed value, so carry retail's.
        w.BrakeGripLat = RetailStepRecording::wheel( pPrev, i, 0x240 );
        w.NormLongClamped = RetailStepRecording::wheel( pPrev, i, 0x2e0 );
        w.NormLatClamped = RetailStepRecording::wheel( pPrev, i, 0x2e4 );
    }

    // Driving-help level as recorded (veh+0x704, FUN_008287d0).
    setDrivingHelpLevel( RetailStepRecording::vehInt( pCur, 0x704 ) );

    // Clutch coupling inputs carried across steps (sim+0x208 / sim+0x210,
    // written at the end of every FUN_0043aba0 substep).
    vehicle.ReflectedWheelSpeed = RetailStepRecording::veh( pPrev, 0x42f8 );
    vehicle.ReactionTorque = RetailStepRecording::veh( pPrev, 0x4300 );

    // Auto-gearbox pedal state machine (veh+0xe54, FUN_0041a210).
    autoGearboxState = RetailStepRecording::vehInt( pPrev, 0xe54 );

    // Electronics state carried across steps (FUN_0041b920).
    vehicle.EspMagFiltered = RetailStepRecording::veh( pPrev, 0xe98 );
    vehicle.bEspIntervening = RetailStepRecording::vehInt( pPrev, 0xeb0 ) != 0;
    vehicle.LaunchState = RetailStepRecording::vehInt( pPrev, 0xeb4 );
    vehicle.LaunchStartMag = RetailStepRecording::veh( pPrev, 0xeb8 );
    vehicle.LaunchTimer = RetailStepRecording::veh( pPrev, 0xebc );
    vehicle.AbsMagFront = RetailStepRecording::veh( pPrev, 0x4244 );
    vehicle.AbsMagRear = RetailStepRecording::veh( pPrev, 0x4248 );
    vehicle.EspMagFront = RetailStepRecording::veh( pPrev, 0x424c );
    vehicle.EspMagRear = RetailStepRecording::veh( pPrev, 0x4250 );

    if ( rec.bHasEngine ) {
        PhysicsEngineSim &e = vehicle.Engine;
        e.IgnitionOn = pPrev[kRecEngine + 0xcc] != 0;
        e.bIgnitionOnce = pPrev[kRecEngine + 0xcd] != 0;
        int32_t state = 0;
        memcpy( &state, pPrev + kRecEngine + 0x94, sizeof( state ) );
        e.State = static_cast<eEngineState>( state );
        e.AngAccel = rec.engine( pPrev, 0x9c );
        e.Omega = rec.engine( pPrev, 0xa4 );
        e.OmegaPrev = e.Omega;
        e.IgnTimer = rec.engine( pPrev, 0xb0 );
        e.TorqueOut = rec.engine( pPrev, 0xd0 );
        e.SmoothedTorque = rec.engine( pPrev, 0xd4 );
    }
    return true;
}

void PhysicsVehicle::retailReplayOverrideInputs() {
    // The port's auto-gearbox state machine is not seeded by the replay, so
    // retail's post-arbitration pedal, gear and clutch replace its outputs.
    if ( retailReplayIndex < 0 || bRetailReplayFree ) {
        return;
    }
    const RetailStepRecording &rec = gRetailStepRecording;
    const uint8_t *pPrev = rec.record( retailReplayIndex - 1 );
    const uint8_t *pCur = rec.record( retailReplayIndex );
    inputThrottle = RetailStepRecording::veh( pCur, 0xe04 );
    inputBrake = RetailStepRecording::veh( pCur, 0x4284 );
    // In reverse FUN_0041a210 swaps the pedals (brake drives, throttle
    // brakes); take the processed engine throttle instead of the raw pedal.
    if ( rec.bHasEngine && RetailStepRecording::vehInt( pPrev, 0x42ec ) == 0 ) {
        inputThrottle = RetailStepRecording::engine( pCur, 0xc4 );
    }
    // applyDrivingMode() zeroes these in the hold/park/slow-down modes.
    inputSteer = RetailStepRecording::veh( pCur, 0xe10 );
    inputHandbrake = RetailStepRecording::veh( pCur, 0xe0c );
    vehicle.GearBox.CurrentGear = RetailStepRecording::vehInt( pPrev, 0x42ec );
    vehicle.GearBox.ShiftState = RetailStepRecording::vehInt( pPrev, 0x4258 );
    vehicle.GearBox.ShiftSubstate =
        RetailStepRecording::vehInt( pPrev, 0x425c );
    vehicle.Clutch.Target = RetailStepRecording::veh( pPrev, 0x4318 );
    vehicle.Clutch.Engagement = RetailStepRecording::veh( pPrev, 0x431c );
    vehicle.Clutch.State = RetailStepRecording::vehInt( pPrev, 0x4320 );
}

bool PhysicsVehicle::retailReplayGroundHit( int i,
                                            const JPH::RVec3 &rayOrigin,
                                            const JPH::Vec3 &rayDirection,
                                            JPH::RVec3 &outPosition,
                                            JPH::Vec3 &outNormal ) const {
    const RetailStepRecording &rec = gRetailStepRecording;
    if ( i >= kRecNumWheels ) {
        return false;
    }
    int32_t groundIndex = retailReplayIndex;
    if ( bRetailReplayFree ) {
        // A free-running chassis no longer follows retail over kerbs and
        // jumps: keep the last ground retail saw under this wheel.
        const int32_t firstIndex = std::max( 1, retailReplayIndex - 400 );
        while ( groundIndex > firstIndex &&
                !RetailStepRecording::wheelGrounded( rec.record( groundIndex ),
                                                     i ) ) {
            --groundIndex;
        }
    }
    const uint8_t *pCur = rec.record( groundIndex );
    if ( !RetailStepRecording::wheelGrounded( pCur, i ) ) {
        return false;
    }
    const JPH::Vec3 normal = RetailStepRecording::wheelVec( pCur, i, 0x360 )
                                 .NormalizedOr( JPH::Vec3::sAxisY() );
    // +0x380 is derived along chassis up (attach - up * up.y * drop,
    // FUN_0083a560); the ground itself lies vertically below the attach
    // point at that drop, which only coincides on level ground.
    const JPH::Vec3 up = RetailStepRecording::motionVec( pCur, 0x20 );
    if ( up.GetY() < 1.0e-3f ) {
        return false;
    }
    // Retail's attach point (its own pose), so the plane stays right when the
    // port chassis is free-running away from the recorded one.
    const Eigen::Vector3f &a = wheelAttach[i];
    const JPH::RVec3 attach =
        JPH::RVec3( RetailStepRecording::motionVec( pCur, 0x40 ) +
                    RetailStepRecording::motionVec( pCur, 0x10 ) * a.x() +
                    up * a.y() +
                    RetailStepRecording::motionVec( pCur, 0x30 ) * a.z() ) +
        rec.Offset;
    const JPH::RVec3 contact =
        JPH::RVec3( RetailStepRecording::wheelVec( pCur, i, 0x380 ) ) +
        rec.Offset;
    const float alongUp = up.Dot( JPH::Vec3( attach - contact ) );
    const JPH::RVec3 point =
        attach - JPH::RVec3( 0.0f, alongUp / up.GetY(), 0.0f );
    const float denom = normal.Dot( rayDirection );
    if ( std::fabs( denom ) < 1.0e-6f ) {
        return false;
    }
    const float t = normal.Dot( JPH::Vec3( point - rayOrigin ) ) / denom;
    if ( t < 0.0f || t > 1.0f ) {
        return false;
    }
    outPosition = rayOrigin + rayDirection * t;
    outNormal = normal;
    return true;
}

void PhysicsVehicle::retailReplayEndStep() {
    RetailStepRecording &rec = gRetailStepRecording;
    if ( retailReplayIndex < 0 ) {
        return;
    }
    const uint8_t *pCur = rec.record( retailReplayIndex );
    if ( rec.pOutput != nullptr ) {
        const JPH::Vec3 fwd = RetailStepRecording::motionVec( pCur, 0x30 );
        const JPH::Vec3 side = RetailStepRecording::motionVec( pCur, 0x10 );
        fprintf( rec.pOutput, "%d,%.6f,%.4f,%.6f,%.6f,%d,%d", retailReplayIndex,
                 RetailStepRecording::f32( pCur, 4 ),
                 RetailStepRecording::motionVec( pCur, 0xd0 ).Length(),
                 steerCommand, RetailStepRecording::veh( pCur, 0x4280 ),
                 vehicle.GearBox.CurrentGear,
                 RetailStepRecording::vehInt( pCur, 0x42ec ) );
        const float nan = std::numeric_limits<float>::quiet_NaN();
        fprintf( rec.pOutput, ",%.6g,%.6g,%.6g,%.6g", vehicle.Engine.Omega,
                 rec.bHasEngine ? rec.engine( pCur, 0xa4 ) : nan,
                 vehicle.Engine.TorqueOut,
                 rec.bHasEngine ? rec.engine( pCur, 0xd0 ) : nan );
        {
            // Port chassis after its own integration of the previous step
            // (before the replay overwrote it) vs retail, body axes.
            const JPH::Vec3 up = RetailStepRecording::motionVec( pCur, 0x20 );
            const JPH::Vec3 w = RetailStepRecording::motionVec( pCur, 0xe0 );
            const JPH::Vec3 v = RetailStepRecording::motionVec( pCur, 0xd0 );
            fprintf(
                rec.pOutput,
                ",%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g",
                retailReplayPreAngVel.Dot( fwd ), w.Dot( fwd ),
                retailReplayPreAngVel.Dot( side ), w.Dot( side ),
                retailReplayPreAngVel.Dot( up ), w.Dot( up ),
                retailReplayPreLinVel.Dot( side ), v.Dot( side ),
                retailReplayPreLinVel.Dot( up ), v.Dot( up ),
                retailReplayPreLinVel.Dot( fwd ), v.Dot( fwd ) );
        }
        for ( int i = 0; i < kRecNumWheels; ++i ) {
            const bool bPortWheel = i < vehicle.NumWheels;
            const PhysicsWheelState &w = vehicle.Wheels[bPortWheel ? i : 0];
            const auto wr = [&]( const int32_t offset ) {
                return RetailStepRecording::wheel( pCur, i, offset );
            };
            const JPH::Vec3 longAxis =
                RetailStepRecording::wheelVec( pCur, i, 0x3b0 );
            const float retailSteer =
                std::atan2( longAxis.Dot( side ), longAxis.Dot( fwd ) );
            const float values[][2] = {
                { prevSuspLength[i],
                 RetailStepRecording::toPortLength( pCur, i, wr( 0x1b4 ),
                 suspDroopStop[i] ) },
                { w.SuspTotalForce, wr( 0x2fc ) },
                { w.NormalLoad, wr( 0x2f8 ) },
                { w.ContactVelLong, wr( 0x310 ) },
                { w.ContactVelLat, wr( 0x314 ) },
                { w.AngularVel, wr( 0x3e8 ) },
                { w.TireForceLong, wr( 0x330 ) },
                { w.TireForceLat, wr( 0x334 ) },
                { w.GripLong, wr( 0x234 ) },
                { w.GripLat, wr( 0x238 ) },
                { w.BrakeGripLat, wr( 0x240 ) },
                { w.BrakeCmdTorque, wr( 0x1d0 ) },
                { w.DriveTorque, wr( 0x184 ) },
                { computeWheelSteerAngle( w ) + w.ImpactSteerOffset,
                 retailSteer },
                { w.bTireSlipping ? 1.0f : 0.0f,
                 pCur[kRecWheels + i * kRecWheelStride + 0x2b4] ? 1.0f
                                                                 : 0.0f },
                { w.BrakeTorqueOut, wr( 0x2cc ) },
                { w.Inertia + w.ReflectedInertia, wr( 0x3f0 ) }
            };
            for ( const auto &pair : values ) {
                fprintf( rec.pOutput, ",%.6g,%.6g", pair[0], pair[1] );
            }
        }
        fprintf( rec.pOutput, "\n" );
        if ( ( retailReplayIndex % 100 ) == 0 ) {
            fflush( rec.pOutput );
        }
    }

    ++retailReplayStep;
    rec.NextStep = retailReplayStep;
    if ( retailReplayStep >= rec.NumRecords ) {
        rec.NextStep = -1;
        if ( rec.pOutput != nullptr ) {
            fclose( rec.pOutput );
            rec.pOutput = nullptr;
        }
        if ( rec.pFreeOutput != nullptr ) {
            fclose( rec.pFreeOutput );
            rec.pFreeOutput = nullptr;
        }
        OTDU_LOG_INFO( "physreplay: done (%d steps)\n", rec.NumRecords );
    }
}

void PhysicsVehicle::retailReplayLogFree( const int32_t window ) {
    // OTDU_ADDITION: port chassis (result of its own previous step) vs retail
    RetailStepRecording &rec = gRetailStepRecording;
    if ( rec.pFreeOutput == nullptr ) {
        rec.pFreeOutput = fopen( ( rec.Path + ".free.csv" ).c_str(), "w" );
        if ( rec.pFreeOutput == nullptr ) {
            return;
        }
        fprintf( rec.pFreeOutput,
                 "window,k,step,dist,dy,speedPort,speedRetail,yawRatePort,"
                 "yawRateRetail,headingErr,slipPort,slipRetail,steerPort,"
                 "steerRetail,engOmegaPort,engOmegaRetail,gearPort,gearRetail,"
                 "rearOmegaPort,rearOmegaRetail,clutchPort,clutchRetail,"
                 "absRearPort,absRearRetail,espRearPort,espRearRetail,"
                 "brakeOutPort,brakeOutRetail\n" );
    }
    const uint8_t *pCur = rec.record( retailReplayIndex );
    const uint8_t *pPrev = rec.record( retailReplayIndex - 1 );
    const JPH::RMat44 world = pChassisBody->GetWorldTransform();
    const JPH::Vec3 portFwd = world.GetColumn3( 2 );
    const JPH::Vec3 portSide = world.GetColumn3( 0 );
    const JPH::Vec3 portUp = world.GetColumn3( 1 );
    const JPH::Vec3 retailFwd = RetailStepRecording::motionVec( pCur, 0x30 );
    const JPH::Vec3 retailSide = RetailStepRecording::motionVec( pCur, 0x10 );
    const JPH::Vec3 retailUp = RetailStepRecording::motionVec( pCur, 0x20 );
    const JPH::Vec3 portVel = pChassisBody->GetLinearVelocity();
    const JPH::Vec3 retailVel = RetailStepRecording::motionVec( pCur, 0xd0 );
    const JPH::Vec3 delta(
        pChassisBody->GetPosition() - rec.Offset -
        JPH::RVec3( RetailStepRecording::motionVec( pCur, 0x40 ) ) );
    const float heading =
        std::atan2( portFwd.Dot( retailSide ), portFwd.Dot( retailFwd ) );
    const auto slip = []( const JPH::Vec3 &v, const JPH::Vec3 &fwd,
                          const JPH::Vec3 &side ) {
        return v.Length() > 1.0f ? std::atan2( v.Dot( side ), v.Dot( fwd ) )
                                 : 0.0f;
    };
    const int32_t start = rec.FreeStarts[window];
    const float nan = std::numeric_limits<float>::quiet_NaN();
    fprintf(
        rec.pFreeOutput,
        "%d,%d,%d,%.4f,%.4f,%.4f,%.4f,%.5f,%.5f,%.5f,%.5f,%.5f,%.5f,%.5f,"
        "%.3f,%.3f,%d,%d,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.1f,%.1f\n",
        window, retailReplayIndex - start, retailReplayIndex,
        JPH::Vec3( delta.GetX(), 0.0f, delta.GetZ() ).Length(), delta.GetY(),
        portVel.Length(), retailVel.Length(),
        pChassisBody->GetAngularVelocity().Dot( portUp ),
        RetailStepRecording::motionVec( pCur, 0xe0 ).Dot( retailUp ), heading,
        slip( portVel, portFwd, portSide ),
        slip( retailVel, retailFwd, retailSide ), filteredSteer,
        RetailStepRecording::veh( pPrev, 0x4280 ), vehicle.Engine.Omega,
        rec.bHasEngine ? rec.engine( pPrev, 0xa4 ) : nan,
        vehicle.GearBox.CurrentGear,
        RetailStepRecording::vehInt( pPrev, 0x42ec ),
        0.5f * ( vehicle.Wheels[2].AngularVel + vehicle.Wheels[3].AngularVel ),
        0.5f * ( RetailStepRecording::wheel( pPrev, 2, 0x3e8 ) +
                 RetailStepRecording::wheel( pPrev, 3, 0x3e8 ) ),
        vehicle.Clutch.Engagement, RetailStepRecording::veh( pPrev, 0x431c ),
        vehicle.AbsMagRear, RetailStepRecording::veh( pPrev, 0x4248 ),
        vehicle.EspMagRear, RetailStepRecording::veh( pPrev, 0x4250 ),
        vehicle.Wheels[2].BrakeTorqueOut,
        RetailStepRecording::wheel( pPrev, 2, 0x2cc ) );
    if ( ( retailReplayIndex % 50 ) == 0 ) {
        fflush( rec.pFreeOutput );
    }
}
