#include "shared.h"
#include "replay.h"

#include "ai/ai_pool.h"
#include "ai/ai_vehicle.h"
#include "ai/gs_ai.h"

#include "gs_physics.h"
#include "vehicle/physics_vehicle.h"

#include "audio/gs_audio.h"
#include "flash/entities/flash_entity.h"
#include "flash/implementation/flash_player.h"
#include "flash/mng_fade.h"
#include "game/cinematic/mng_cinematics.h"
#include "game/game_mechanism.h"
#include "game/game_object.h"
#include "game/gm_world.h"
#include "game/mission/gm_mission.h"
#include "game/scenaric/entity/entity_cinematic.h"
#include "game/scenaric/gs_scenaric.h"
#include "game/vehicle/car_state.h"
#include "game/vehicle/gs_car_state.h"
#include "game/vehicle/mng_car_tire_skid.h"
#include "gs_timer.h"
#include "player_data/gs_playerdata.h"
#include "render/camera/camera_vehicle.h"
#include "render/camera/gs_camera.h"
#include "tdu_instance.h"
#include "world/gs_world.h"

Replay gReplay;
int32_t gReplayModeDebugIndex = 0; // DAT_010e7c18

static constexpr int32_t kPoseRecordSize = 0x41;
static constexpr int32_t kInputRecordSize = 9;
static constexpr int32_t kSnapshotRecordSize =
    sizeof( PhysicsVehicle::ReplaySnapshot );

static constexpr float kPoseRecordQuantize = 32767.0f;          // DAT_00f419b8
static constexpr float kPoseRecordDequantize = 1.0f / 32767.0f; // DAT_00f41714
static constexpr float kCurveTangentScale = 1.0f / 300.0f;      // DAT_00f4216c
static constexpr float kBlendStartPercent = 0.01f;              // DAT_00f416d4
static constexpr float kFadeDuration = 0.5f;                    // DAT_00f8a660
static constexpr float kMinStreamedHeight = -2.0f;              // DAT_00f41c1c

static bool gbReplayEnterPending = false; // DAT_010e7c20
static bool gbFadeStepStarted = false;    // DAT_010e7c1c

static constexpr FlashMessage kMsgRequestReplay = FlashMessage( "rqre" );
static constexpr FlashMessage kMsgReplayIn = FlashMessage( "rpin" );
static constexpr FlashMessage kMsgReplayOut = FlashMessage( "rpou" );
static constexpr FlashMessage kMsgPlay = FlashMessage( "play" );
static constexpr FlashMessage kMsgQuit = FlashMessage( "quit" );
static constexpr FlashMessage kMsgSlow = FlashMessage( "slow" );
static constexpr FlashMessage kMsgStart = FlashMessage( "strt" );
static constexpr FlashMessage kMsgSwitchCamera = FlashMessage( "swca" );

static bool IsStreamedUnder( const PhysicsVehicle &vehicle ) {
    // FUN_00829ee0
    if ( gpWorld == nullptr ) {
        return false;
    }
    const Eigen::Vector4f position =
        vehicle.getPhysicsMatrix().row( 3 ).transpose() +
        GSPhysics::GetShiftOrigin();
    return gpWorld->isStreamedAround( &position, true, false ) &&
           kMinStreamedHeight < position.y();
}

static void ReleasePlayerAutopilot() {
    // FUN_0090dcc0
    CarState *pCar = gpScenaric->getLocalPlayer().getCarState();
    if ( pCar == nullptr ||
         pCar->getSimulationType() != CarState::eSimulationType::CSST_Local ||
         pCar->getOwner() != CarState::eOwner::CSO_LocalPlayer ) {
        return;
    }
    AiVehicle *pAutopilot = static_cast<AiVehicle *>( pCar->getAIController() );
    if ( pAutopilot == nullptr ) {
        return;
    }
    if ( pAutopilot->getCategory() != eAiVehicleCategory::AVC_Network ) {
        gpAI->getAIPool()->togglePlayerAutopilot( false );
        return;
    }
    if ( pAutopilot->getNavigationType() == 1 ) {
        pAutopilot->getOpponentNavigation().requestState(
            AiVehNavOpponent::NS_Idle );
    }
}

static PhysicsVehicle *GetLocalPlayerVehicle() {
    CarState *pCar = gpScenaric->getLocalPlayer().getCarState();
    return ( pCar != nullptr ) ? pCar->getPhysicsVehicle() : nullptr;
}

struct PoseRecord {
    Matrix4x4f_t World;
    Eigen::Vector4f LinearVelocity;
    Eigen::Vector4f AngularVelocity;
    uint8_t Flag;
};

static void PackPose( const PoseRecord &pose, uint8_t *pRecord ) {
    // FUN_0081aca0
    int16_t rotation[9];
    for ( int32_t row = 0; row < 3; row++ ) {
        for ( int32_t column = 0; column < 3; column++ ) {
            rotation[row * 3 + column] =
                static_cast<int16_t>( static_cast<int32_t>(
                    pose.World( row, column ) * kPoseRecordQuantize ) );
        }
    }
    const float position[3] = { pose.World( 3, 0 ), pose.World( 3, 1 ),
                                pose.World( 3, 2 ) };
    memcpy( pRecord, rotation, sizeof( rotation ) );
    memcpy( pRecord + 0x14, position, sizeof( position ) );
    memcpy( pRecord + 0x20, pose.LinearVelocity.data(), sizeof( float ) * 4 );
    memcpy( pRecord + 0x30, pose.AngularVelocity.data(), sizeof( float ) * 4 );
    pRecord[0x40] = pose.Flag;
}

static void UnpackPose( const uint8_t *pRecord, PoseRecord &pose ) {
    // FUN_0081adf0
    int16_t rotation[9];
    float position[3];
    memcpy( rotation, pRecord, sizeof( rotation ) );
    memcpy( position, pRecord + 0x14, sizeof( position ) );
    pose.World = Matrix4x4f_t::Zero();
    for ( int32_t row = 0; row < 3; row++ ) {
        for ( int32_t column = 0; column < 3; column++ ) {
            pose.World( row, column ) =
                static_cast<float>( rotation[row * 3 + column] ) *
                kPoseRecordDequantize;
        }
    }
    pose.World.row( 3 ) =
        Eigen::Vector4f( position[0], position[1], position[2], 1.0f );
    memcpy( pose.LinearVelocity.data(), pRecord + 0x20, sizeof( float ) * 4 );
    memcpy( pose.AngularVelocity.data(), pRecord + 0x30, sizeof( float ) * 4 );
    pose.LinearVelocity.w() = 0.0f;
    pose.AngularVelocity.w() = 0.0f;
    pose.Flag = pRecord[0x40];
}

Replay::Replay()
    : bufferSize( 0 ),
      pBuffer( nullptr ),
      pSnapshotRing( nullptr ),
      pPoseRing( nullptr ),
      pInputRing( nullptr ),
      snapshotRecordSize( 0 ),
      poseRecordSize( 0 ),
      inputRecordSize( 0 ),
      inputRingSize( 0 ),
      poseRingSize( 0 ),
      snapshotRingSize( 0 ),
      snapshotFrequency( 0 ),
      poseFrequency( 0 ),
      inputFrequency( 0 ),
      numSnapshotRecords( 0 ),
      numPoseRecords( 0 ),
      numInputRecords( 0 ),
      numVehicles( 0 ),
      numFrames( 0 ),
      cursor( 0 ),
      numRecordedFrames( 0 ),
      firstFrame( 0 ),
      writeFrame( 0 ),
      field_0x5c( 0 ),
      lastPoseFrame( 0 ),
      lastSnapshotFrame( 0 ),
      lastInputFrame( 0 ),
      state( eReplayState::RS_Off ),
      field_0x70( 0 ),
      slowDownDisplayValue( 0 ),
      slowDownFactor( 0 ),
      speedUpFactor( 0 ),
      field_0x80( 0 ),
      field_0x84( 0 ),
      slowDownSubStep( 0 ),
      savedCameraMode( 0 ),
      field_0x90( 0 ),
      field_0x94( 0 ),
      bPlaybackStarted( 0 ),
      bExitRequested( 0 ),
      bPaused( 0 ),
      bStopRequested( 0 ),
      bInitialized( 0 ),
      bReplayRequestHidden( 0 ),
      bRewound( 0 ),
      bRestoreCamera( 0 ),
      bForceCameraSwitch( 0 ),
      replayExitRequest( 0 ),
      pRemotePlayer( nullptr ) {}

void Replay::initialize( const uint32_t size ) {
    // FUN_0081ac20
    inputFrequency = gRPLConfig[3];
    poseFrequency = gRPLConfig[3] * gRPLConfig[2];
    snapshotFrequency = poseFrequency * gRPLConfig[1];
    snapshotRecordSize = kSnapshotRecordSize;
    poseRecordSize = kPoseRecordSize;
    inputRecordSize = kInputRecordSize;
    bufferSize = size;
    pBuffer = static_cast<uint8_t *>( TestDrive::Alloc( size ) );
    reset();
    bInitialized = 1;
}

void Replay::release() {
    // FUN_00846cf0 (inlined)
    if ( !bInitialized ) {
        return;
    }
    reset();
    if ( pBuffer != nullptr ) {
        TestDrive::Free( pBuffer );
    }
    pBuffer = nullptr;
    bPlaybackStarted = 0;
    bExitRequested = 0;
    bPaused = 0;
    bStopRequested = 0;
}

void Replay::reset() {
    // FUN_0041f8f0
    slowDownDisplayValue = 1;
    slowDownFactor = 1;
    speedUpFactor = 1;
    firstFrame = 0;
    writeFrame = 0;
    field_0x5c = 0;
    cursor = 0;
    numRecordedFrames = 0;
    replayExitRequest = 0;
    field_0x80 = 8;
    field_0x84 = 2;
    field_0x90 = -1;
    field_0x94 = -1;
    if ( pBuffer != nullptr ) {
        memset( pBuffer, 0, bufferSize );
    }
    numVehicles = 0;
    state = eReplayState::RS_Off;
    field_0x70 = 0;
    bPaused = 0;
    bPlaybackStarted = 0;
    bReplayRequestHidden = 0;
    GSPhysics::ClearFadeRequests();
}

void Replay::resetSession() {
    // FUN_00740440 (inlined)
    pRemotePlayer = nullptr;
    reset();
    numVehicles = 0;
    numFrames = 0;
    gpAI->setLocationsFromCarState( false );
}

void Replay::setupRings( const int32_t vehicleCount ) {
    // FUN_0081a770
    reset();
    numVehicles = vehicleCount;
    numFrames = 0;
    if ( vehicleCount == 0 || !bInitialized ) {
        return;
    }

    const auto computeSize = [&]( const int32_t frame ) {
        numInputRecords = frame / inputFrequency;
        numPoseRecords = frame / poseFrequency;
        numSnapshotRecords = frame / snapshotFrequency;
        return static_cast<uint32_t>(
            ( poseRecordSize * numPoseRecords +
              inputRecordSize * numInputRecords +
              snapshotRecordSize * numSnapshotRecords ) *
            vehicleCount );
    };

    int32_t frame = 0;
    while ( computeSize( frame++ ) < bufferSize ) {
    }
    while ( computeSize( frame-- ) > bufferSize ) {
    }

    numFrames = std::min( { inputFrequency * numInputRecords,
                            poseFrequency * numPoseRecords,
                            snapshotFrequency * numSnapshotRecords } );

    inputRingSize = inputRecordSize * numInputRecords * vehicleCount;
    poseRingSize = poseRecordSize * numPoseRecords * vehicleCount;
    snapshotRingSize = snapshotRecordSize * numSnapshotRecords * vehicleCount;
    pInputRing = pBuffer;
    pPoseRing = pBuffer + inputRingSize;
    pSnapshotRing = pPoseRing + poseRingSize;
}

void Replay::PackDriving( const ReplayDriving &driving, uint8_t *pRecord ) {
    // FUN_00827910
    const auto toUnsigned = []( const float value ) {
        return static_cast<uint8_t>( static_cast<uint32_t>( value * 255.0f ) );
    };
    const auto toSigned = []( const float value ) {
        return static_cast<uint8_t>( static_cast<int32_t>( value * 127.0f ) );
    };
    pRecord[0] = toUnsigned( driving.Accelerate );
    pRecord[1] = toUnsigned( driving.Brake );
    pRecord[2] = toUnsigned( driving.Handbrake );
    pRecord[3] = toSigned( driving.Steer );
    pRecord[4] = toSigned( driving.Lean );
    pRecord[5] = toUnsigned( driving.Clutch );
    pRecord[6] = static_cast<uint8_t>(
        ( driving.bGearUp != 0 ) | ( ( driving.bGearDown != 0 ) << 1 ) |
        ( ( driving.bReset != 0 ) << 2 ) |
        ( ( driving.BackOnTrack & 3 ) << 3 ) |
        ( ( driving.bFlashLights != 0 ) << 5 ) |
        ( ( driving.bFlashLightsOnce != 0 ) << 6 ) |
        ( ( driving.bBackView != 0 ) << 7 ) );
    pRecord[7] = static_cast<uint8_t>(
        ( driving.bChangeView != 0 ) | ( ( driving.bHonk != 0 ) << 1 ) |
        ( ( ( driving.OpenWindows + 1 ) & 3 ) << 2 ) |
        ( driving.DrivingMode << 4 ) );
    pRecord[8] = static_cast<uint8_t>( ( driving.bIgnition != 0 ) |
                                       ( ( driving.bIgnitionOnce != 0 ) << 1 ) |
                                       ( ( driving.GearSelect & 0xf ) << 2 ) );
}

void Replay::UnpackDriving( const uint8_t *pRecord, ReplayDriving &driving ) {
    // FUN_00827800
    driving.Accelerate = static_cast<float>( pRecord[0] ) / 255.0f;
    driving.Brake = static_cast<float>( pRecord[1] ) / 255.0f;
    driving.Handbrake = static_cast<float>( pRecord[2] ) / 255.0f;
    driving.Steer =
        static_cast<float>( static_cast<int8_t>( pRecord[3] ) ) / 127.0f;
    driving.Lean =
        static_cast<float>( static_cast<int8_t>( pRecord[4] ) ) / 127.0f;
    driving.Clutch = static_cast<float>( pRecord[5] ) / 255.0f;
    driving.bGearUp = pRecord[6] & 1;
    driving.bGearDown = ( pRecord[6] >> 1 ) & 1;
    driving.bReset = ( pRecord[6] >> 2 ) & 1;
    driving.BackOnTrack = ( pRecord[6] >> 3 ) & 3;
    driving.bFlashLights = ( pRecord[6] >> 5 ) & 1;
    driving.bFlashLightsOnce = ( pRecord[6] >> 6 ) & 1;
    driving.bBackView = pRecord[6] >> 7;
    driving.bChangeView = pRecord[7] & 1;
    driving.bHonk = ( pRecord[7] >> 1 ) & 1;
    driving.OpenWindows =
        static_cast<int8_t>( ( ( pRecord[7] >> 2 ) & 3 ) - 1 );
    driving.DrivingMode = pRecord[7] >> 4;
    driving.bIgnition = pRecord[8] & 1;
    driving.bIgnitionOnce = ( pRecord[8] >> 1 ) & 1;
    driving.GearSelect = ( pRecord[8] >> 2 ) & 0xf;
}

template <typename Fn> void Replay::forEachRecordedVehicle( Fn &&fn ) {
    int32_t index = 0;
    for ( PhysicsVehicle *it = gpPhysics->getVehiclesBegin();
          it < gpPhysics->getVehiclesEnd(); ++it ) {
        if ( it->isActive() && it->isInInstantReplay() ) {
            fn( *it, index );
            index++;
        }
    }
}

void Replay::updatePendingFlags() {
    for ( PhysicsVehicle *it = gpPhysics->getVehiclesBegin();
          it < gpPhysics->getVehiclesEnd(); ++it ) {
        if ( !it->isInInstantReplay() ) {
            continue;
        }
        PhysicsVehicle::ReplayPlayback &replay = it->getReplayPlayback();
        if ( it->isPaused() ) {
            replay.PendingFlag = 1;
        }
        if ( replay.bTeleported ) {
            replay.PendingFlag = 2;
        }
    }
}

void Replay::record( const bool bStep ) {
    // FUN_0081af70
    if ( !bInitialized ) {
        return;
    }

    if ( bStopRequested ) {
        state = eReplayState::RS_Stop;
        bStopRequested = 0;
    }
    if ( state == eReplayState::RS_StartPlayback ) {
        requestPlayback();
        field_0x94 = -1;
        field_0x90 = -1;
    }
    if ( state == eReplayState::RS_Stop ) {
        field_0x94 = -1;
        field_0x90 = -1;
    }

    if ( state == eReplayState::RS_WaitForRecording ) {
        PhysicsVehicle *pFirstVehicle = gpPhysics->getVehiclesBegin();
        if ( pFirstVehicle != nullptr && IsStreamedUnder( *pFirstVehicle ) ) {
            state = eReplayState::RS_Recording;
        }
        gpAI->setLocationsFromCarState( false );
    } else if ( ( state == eReplayState::RS_Recording ||
                  state == eReplayState::RS_StartPlayback ||
                  state == eReplayState::RS_Stop ) &&
                bStep ) {
        updatePendingFlags();

        const bool bSnapshotFrame = cursor % snapshotFrequency == 0;
        if ( bSnapshotFrame ) {
            forEachRecordedVehicle( [&]( PhysicsVehicle &vehicle,
                                         const int32_t index ) {
                vehicle.saveReplaySnapshot(
                    pSnapshotRing +
                    ( ( cursor / snapshotFrequency ) * numVehicles + index ) *
                        snapshotRecordSize );
            } );
        }

        const bool bPoseFrame = cursor % poseFrequency == 0;
        if ( bPoseFrame ) {
            forEachRecordedVehicle( [&]( PhysicsVehicle &vehicle,
                                         const int32_t index ) {
                PhysicsVehicle::ReplayPlayback &replay =
                    vehicle.getReplayPlayback();
                PoseRecord pose;
                pose.World = vehicle.getChassisWorldMatrix();
                pose.LinearVelocity = vehicle.getChassisVelocity();
                pose.AngularVelocity = vehicle.getChassisAngularVelocity();
                pose.Flag = replay.PendingFlag;
                PackPose( pose, pPoseRing +
                                    ( ( cursor / poseFrequency ) * numVehicles +
                                      index ) *
                                        poseRecordSize );
                replay.PendingFlag = 0;
            } );
        }

        const bool bInputFrame = cursor % inputFrequency == 0;
        if ( bInputFrame ) {
            forEachRecordedVehicle(
                [&]( PhysicsVehicle &vehicle, const int32_t index ) {
                    vehicle.getReplayPlayback().pInputSlot =
                        pInputRing +
                        ( ( cursor / inputFrequency ) * numVehicles + index ) *
                            inputRecordSize;
                } );
        }

        if ( bSnapshotFrame ) {
            lastSnapshotFrame = cursor;
        }
        if ( bPoseFrame ) {
            lastPoseFrame = cursor;
        }
        if ( bInputFrame ) {
            lastInputFrame = cursor;
        }

        writeFrame++;
        numRecordedFrames++;
        if ( numFrames <= writeFrame ) {
            writeFrame = 0;
        }
        if ( writeFrame == firstFrame ) {
            firstFrame += numVehicles * snapshotFrequency;
            if ( numFrames <= firstFrame ) {
                firstFrame = 0;
            }
        }
        cursor = writeFrame;
    }

    if ( state == eReplayState::RS_StartPlayback ) {
        state = eReplayState::RS_Playback;
        rewind();
        bPlaybackStarted = 1;
        cursor = 0;
    }
    if ( state == eReplayState::RS_Stop ) {
        state = eReplayState::RS_Off;
        field_0x94 = -1;
        field_0x90 = -1;
    }
}

void Replay::applyPoseRecords( PhysicsVehicle &vehicle, const int32_t index ) {
    // FUN_0081b820 (inlined)
    PhysicsVehicle::ReplayPlayback &replay = vehicle.getReplayPlayback();
    const auto getRecord = [&]( const int32_t frame ) {
        return pPoseRing + ( ( frame / poseFrequency ) * numVehicles + index ) *
                               poseRecordSize;
    };
    const float tangentScale =
        static_cast<float>( poseFrequency ) * kCurveTangentScale;
    const auto buildCurve = [&]( Eigen::Vector4f *pCurve, const int32_t from,
                                 const int32_t to ) {
        const Eigen::Vector4f fromPosition =
            replay.Poses[from].row( 3 ).transpose();
        const Eigen::Vector4f toPosition =
            replay.Poses[to].row( 3 ).transpose();
        pCurve[0] = fromPosition;
        pCurve[1] = fromPosition + replay.Velocities[from] * tangentScale;
        pCurve[2] = toPosition - replay.Velocities[to] * tangentScale;
        pCurve[3] = toPosition;
    };

    PoseRecord pose;
    UnpackPose( getRecord( cursor ), pose );
    vehicle.setPostponedMatrix( pose.World );
    replay.Poses[0] = pose.World;
    replay.Velocities[0] = pose.LinearVelocity;
    vehicle.setReplayBodyVelocities( pose.LinearVelocity,
                                     pose.AngularVelocity );
    replay.PoseFlags[0] = pose.Flag;

    if ( cursor == lastPoseFrame ) {
        replay.Elapsed = 0.0f;
        return;
    }

    replay.Period = static_cast<float>( poseFrequency );
    replay.BlendStart =
        static_cast<float>( static_cast<int32_t>( gRPLConfig[4] ) *
                            poseFrequency ) *
        kBlendStartPercent;
    replay.Elapsed = 0.0f;

    UnpackPose( getRecord( cursor + poseFrequency ), pose );
    replay.Poses[1] = pose.World;
    replay.Velocities[1] = pose.LinearVelocity;
    buildCurve( replay.Curves[0], 0, 1 );
    replay.PoseFlags[1] = pose.Flag;

    UnpackPose( getRecord( cursor + poseFrequency * 2 ), pose );
    replay.Poses[2] = pose.World;
    replay.Velocities[2] = pose.LinearVelocity;
    buildCurve( replay.Curves[1], 1, 2 );
    replay.PoseFlags[2] = pose.Flag;
}

bool Replay::playback( const bool bPaused ) {
    // FUN_0081b820
    if ( !bInitialized ) {
        return true;
    }
    if ( state != eReplayState::RS_Playback || bPaused ) {
        return true;
    }

    const bool bStepBoundary =
        ( slowDownFactor == 0 ) ? speedUpFactor == 0 : slowDownSubStep == 0;
    const eFadeRequest fadeRequest = GSPhysics::peekFadeRequest();
    if ( !bPlaybackStarted || !bStepBoundary ||
         ( fadeRequest != eFadeRequest::FR_FadeIn &&
           fadeRequest != eFadeRequest::FR_Idle &&
           fadeRequest != eFadeRequest::FR_ReplayPlayback ) ) {
        for ( PhysicsVehicle *it = gpPhysics->getVehiclesBegin();
              it < gpPhysics->getVehiclesEnd(); ++it ) {
            PhysicsVehicle::ReplayPlayback &replay = it->getReplayPlayback();
            if ( !it->isInInstantReplay() ) {
                replay.Driving = it->captureReplayDriving();
            }
            replay.Driving.bGearUp = 0;
            replay.Driving.bGearDown = 0;
            replay.bDrivingOverride = true;
        }
    } else {
        if ( cursor == firstFrame ) {
            if ( gpMngCarTireSkid != nullptr ) {
                gpMngCarTireSkid->clearSkids();
            }
            if ( gpTestDriveInstance->getActiveGameMode() == GM_Mission ) {
                static_cast<GMMission *>( gpActiveGM )
                    ->getBlockedRoads()
                    .requestRelease();
            }
            for ( PhysicsVehicle *it = gpPhysics->getVehiclesBegin();
                  it < gpPhysics->getVehiclesEnd(); ++it ) {
                if ( !it->isInInstantReplay() ) {
                    continue;
                }
                it->resetEjectedBikeRider();
                CarState *pCar = gpCarState->findCarState( it );
                if ( pCar != nullptr ) {
                    pCar->resetBackOnTrack();
                }
            }
        }

        const int32_t playedFrame = cursor;
        const int32_t lastRecordedFrame = writeFrame;
        updatePendingFlags();

        bool bSnapshotFrame = false;
        if ( cursor % snapshotFrequency == 0 ) {
            bSnapshotFrame = true;
            forEachRecordedVehicle( [&]( PhysicsVehicle &vehicle,
                                         const int32_t index ) {
                vehicle.restoreReplaySnapshot(
                    pSnapshotRing +
                    ( ( cursor / snapshotFrequency ) * numVehicles + index ) *
                        snapshotRecordSize );
            } );
        }

        if ( cursor % poseFrequency == 0 ) {
            forEachRecordedVehicle(
                [&]( PhysicsVehicle &vehicle, const int32_t index ) {
                    applyPoseRecords( vehicle, index );
                } );
        }

        if ( cursor % inputFrequency == 0 ) {
            forEachRecordedVehicle( [&]( PhysicsVehicle &vehicle,
                                         const int32_t index ) {
                PhysicsVehicle::ReplayPlayback &replay =
                    vehicle.getReplayPlayback();
                const uint8_t backOnTrack = replay.Driving.BackOnTrack;
                UnpackDriving(
                    pInputRing +
                        ( ( cursor / inputFrequency ) * numVehicles + index ) *
                            inputRecordSize,
                    replay.Driving );
                if ( backOnTrack != 0 ) {
                    replay.Driving.BackOnTrack = backOnTrack;
                }
                if ( bSnapshotFrame ) {
                    replay.Driving.bGearUp = 0;
                    replay.Driving.bGearDown = 0;
                }
                replay.bDrivingOverride = true;
            } );
        } else {
            forEachRecordedVehicle(
                [&]( PhysicsVehicle &vehicle, const int32_t ) {
                    PhysicsVehicle::ReplayPlayback &replay =
                        vehicle.getReplayPlayback();
                    replay.Driving.bGearUp = 0;
                    replay.Driving.bGearDown = 0;
                    replay.bDrivingOverride = true;
                } );
        }

        if ( bRewound ) {
            bRewound = 0;
            bForceCameraSwitch = 1;
            GSPhysics::pushFadeRequest( eFadeRequest::FR_ReplayPlayback );
        }

        if ( playedFrame == lastRecordedFrame ) {
            rewind();
            if ( bExitRequested ) {
                state = eReplayState::RS_Off;
                speedUpFactor = 1;
                slowDownDisplayValue = 1;
                GSPhysics::pushFadeRequest( eFadeRequest::FR_ReplayPlayback );
                field_0x94 = -1;
                field_0x90 = -1;
                bExitRequested = 0;
                replayExitRequest = 1;
            }
        } else {
            cursor++;
        }
        if ( numFrames <= cursor ) {
            cursor = 0;
        }
    }

    PhysicsVehicle *pLocalVehicle = GetLocalPlayerVehicle();
    if ( pLocalVehicle != nullptr && !IsStreamedUnder( *pLocalVehicle ) ) {
        bPlaybackStarted = 0;
        return false;
    }
    bPlaybackStarted = 1;
    return true;
}

void Replay::advanceSlowDownSubStep() {
    // FUN_0081dfd0 (inlined)
    if ( slowDownFactor == 0 ) {
        slowDownFactor = slowDownDisplayValue;
    } else if ( slowDownSubStep == 0 ) {
        slowDownFactor = slowDownDisplayValue;
        slowDownSubStep = slowDownDisplayValue - 1;
    } else {
        slowDownSubStep--;
    }
}

void Replay::requestPlayback() {
    // FUN_0081aaa0
    GSPhysics::pushFadeRequest( eFadeRequest::FR_FadeOut );
    CameraVehicle *pCamera = gpCamera->getActiveVehicleCamera();
    savedCameraMode = ( pCamera != nullptr ) ? pCamera->getType() : -1;
    gbReplayEnterPending = true;
    gpAI->setLocationsFromCarState( true );
}

void Replay::rewind() {
    // FUN_0081a6f0
    cursor = firstFrame;
    bRewound = 1;
    GSPhysics::pushFadeRequest( eFadeRequest::FR_FadeOut );
}

void Replay::exitPlayback() {
    // FUN_0081a9c0
    bExitRequested = 1;
    cursor = lastPoseFrame;
    bPaused = 0;
    bRestoreCamera = 1;
    for ( PhysicsVehicle *it = gpPhysics->getVehiclesBegin();
          it < gpPhysics->getVehiclesEnd(); ++it ) {
        if ( !it->isActive() ) {
            continue;
        }
        if ( it->isInInstantReplay() && !it->isPaused() ) {
            PhysicsLock lock;
            it->setPostponedPause( 1 );
        }
        const CarState *pCar = gpCarState->findCarState( it );
        AiVehicle *pAI =
            ( pCar != nullptr )
                ? static_cast<AiVehicle *>( pCar->getAIController() )
                : nullptr;
        if ( pAI != nullptr && pAI->getLocomotion() != nullptr ) {
            pAI->getLocomotion()->setPhysicsActive( false, true );
        }
    }
    gpAI->setLocationsFromCarState( false );
}

void Replay::restoreCamera() {
    // FUN_0081ab30
    if ( !bRestoreCamera ) {
        return;
    }
    gpCamera->resetVehicleCameraSelection();
    gpCamera->requestCameraMode( savedCameraMode, 1.0f );
    PhysicsVehicle *pLocalVehicle = GetLocalPlayerVehicle();
    if ( pLocalVehicle != nullptr && pLocalVehicle->isActive() &&
         pLocalVehicle->isInInstantReplay() && pLocalVehicle->isPaused() ) {
        PhysicsLock lock;
        pLocalVehicle->setPostponedPause( 2 );
    }
    bRestoreCamera = 0;
    gpAI->setLocationsFromCarState( false );
}

void Replay::applyCameraSwitchRequest() {
    // FUN_0081a5d0
    if ( bForceCameraSwitch ) {
        gpCamera->forceReplayCameraSwitch();
        bForceCameraSwitch = 0;
    }
}

void Replay::tick() {
    // FUN_0081c330
    if ( gpTestDriveInstance->getActiveGameMode() == GM_DriveIn ) {
        return;
    }

    Eigen::Vector4f position = Eigen::Vector4f::Zero();
    if ( gpWorld->getStreamingPositionPtr() != nullptr ) {
        position = *gpWorld->getStreamingPositionPtr();
    }
    PhysicsVehicle *pTargetVehicle = nullptr;
    CarState *pTarget = gpCamera->getBoundCarState();
    if ( pTarget != nullptr ) {
        position = pTarget->getBody()->WorldMatrix.row( 3 ).transpose();
        pTargetVehicle = pTarget->getPhysicsVehicle();
        if ( pTargetVehicle != nullptr ) {
            position = pTargetVehicle->getPhysicsMatrix().row( 3 ).transpose() +
                       GSPhysics::GetShiftOrigin();
        }
    }

    eFadeRequest request = GSPhysics::peekFadeRequest();
    if ( gpMngFade != nullptr &&
         ( gpMngFade->getTransitionFlags() & 1 ) == 0 ) {
        if ( replayExitRequest == 1 ) {
            if ( gpActiveGM != nullptr &&
                 gpActiveGM->getGameMech() != nullptr ) {
                gpActiveGM->getGameMech()->requestReplayExit();
            }
            replayExitRequest = 0;
        }
        if ( gbFadeStepStarted ) {
            if ( request == eFadeRequest::FR_FadeOut ) {
                if ( gbReplayEnterPending ) {
                    gbReplayEnterPending = false;
                    gpCamera->setReplayMode( true );
                    if ( gpMngCinematics != nullptr &&
                         gpMngCinematics->getActiveCinematic() != nullptr ) {
                        gpMngCinematics->getActiveCinematic()->stop( true );
                    }
                    ReleasePlayerAutopilot();
                    replayExitRequest = 0;
                    rewind();
                }
                if ( bRestoreCamera ) {
                    gpCamera->setReplayMode( false );
                    gpCamera->requestCameraMode( savedCameraMode, 1.0f );
                }
                gpMngFade->setTransitionFlags( gpMngFade->getTransitionFlags() |
                                               8 );
            } else if ( request == eFadeRequest::FR_FadeIn ) {
                bReplayRequestHidden = 0;
            }
            request = GSPhysics::popFadeRequest();
            gbFadeStepStarted = false;
        }

        if ( request == eFadeRequest::FR_ReplayPlayback ) {
            if ( gpWorld->isStreamedAround( &position, true, false ) ) {
                gbFadeStepStarted = true;
                restoreCamera();
                applyCameraSwitchRequest();
                GSPhysics::pushFadeRequest( eFadeRequest::FR_FadeIn );
            }
        } else if ( request == eFadeRequest::FR_FadeIn ) {
            if ( !gbFadeStepStarted ) {
                gpMngFade->restartTransition(
                    ( gpMngFade->getTransitionFlags() & ~8u ) | 5,
                    kFadeDuration );
                gpMngFade->setRenderLogo( false );
                gbFadeStepStarted = true;
            }
        } else if ( request == eFadeRequest::FR_FadeOut &&
                    !gbFadeStepStarted ) {
            gpMngFade->restartTransition(
                ( gpMngFade->getTransitionFlags() & ~0xcu ) | 1,
                kFadeDuration );
            gpMngFade->setRenderLogo( true );
            gbFadeStepStarted = true;
            bReplayRequestHidden = 1;
        }
    }

    if ( state != eReplayState::RS_Playback ) {
        return;
    }
    if ( gpAI->areLocationsFromCarState() ) {
        gGameObjectDico.updateLocations( gpTimer->GameDeltaTime, nullptr );
    }
    if ( state == eReplayState::RS_Playback && pRemotePlayer != nullptr &&
         pTargetVehicle != nullptr ) {
        updateRemoteVariables( *pTargetVehicle );
    }
}

void Replay::updateRemoteVariables( PhysicsVehicle &vehicle ) {
    // FUN_0081c330 (inlined)
    static constexpr float kMsToKmh = 3.6f;       // DAT_00f41768
    static constexpr float kMsToMph = 2.2374156f; // DAT_00f419f4

    const int32_t gear = vehicle.getDrivingGearbox().Gear;
    if ( gear == 0 ) {
        pRemotePlayer->setVariableValueNonLinked( "/:Gear", "R" );
    } else if ( gear == 1 ) {
        pRemotePlayer->setVariableValueNonLinked( "/:Gear", "N" );
    } else if ( gear >= 2 ) {
        pRemotePlayer->setVariableValue( "/:Gear", gear - 1 );
    }

    const bool bImperial = gpPlayerData->isUsingImperialUnits();
    const float speed =
        vehicle.getReportedSpeed() * ( bImperial ? kMsToMph : kMsToKmh );
    pRemotePlayer->setVariableValueNonLinked( "/:SpeedUnit",
                                              bImperial ? "Mph" : "Kmh" );
    const int32_t hundreds = static_cast<int32_t>( speed * 0.01f );
    const float remainder = speed - static_cast<float>( hundreds ) * 100.0f;
    const int32_t tens = static_cast<int32_t>( remainder * 0.1f );
    pRemotePlayer->setVariableValue( "/:Speed_c", hundreds );
    pRemotePlayer->setVariableValue( "/:Speed_d", tens );
    pRemotePlayer->setVariableValue(
        "/:Speed_u", static_cast<int32_t>(
                         remainder - static_cast<float>( tens ) * 10.0f ) );
}

void Replay::getCameraLabel( char *pLabel ) const {
    // FUN_0081a5f0
    static constexpr const char *kOverrideLabels[] = {
        "zoom", "large",  "travelling",
        "fixe", "helico", "car" }; // DAT_00f8a400

    const int32_t modeOverride = gpCamera->getActiveModeOverride();
    if ( modeOverride == -1 ) {
        strcpy( pLabel, "auto" );
        return;
    }
    if ( modeOverride != 5 ) {
        strcpy( pLabel, kOverrideLabels[modeOverride] );
        return;
    }
    switch ( gpCamera->getActiveModeVehicleOverride() ) {
    case 0:
        strcpy( pLabel, "wheel" );
        break;
    case 1:
        strcpy( pLabel, "car" );
        break;
    case 2:
        strcpy( pLabel, "ingame" );
        break;
    case 3:
        strcpy( pLabel, "cockpit" );
        break;
    default:
        break;
    }
}

bool Replay::onFlashMessage( const FlashMessage &message ) {
    // FUN_0081b490
    PhysicsLock lock;

    bool bHandled = false;
    if ( message == kMsgRequestReplay ) {
        if ( pRemotePlayer != nullptr ) {
            pRemotePlayer->setVariableValue( "/:IsRequestReplay",
                                             bReplayRequestHidden ? 0 : 1 );
        }
    } else if ( message == kMsgReplayIn ) {
        GSPhysics::pushFadeRequest( eFadeRequest::FR_FadeOut );
        if ( state == eReplayState::RS_Recording ) {
            state = eReplayState::RS_StartPlayback;
            bStopRequested = 0;
            requestPlayback();
        } else {
            state = eReplayState::RS_Playback;
            requestPlayback();
            bHandled = true;
            bPlaybackStarted = 1;
        }
        gpAudio->requestMixStateResync();
    } else if ( message == kMsgPlay ) {
        if ( !bPaused ) {
            gpAudio->changeMixState( gpAudio->getMixFlags( "MixReplayPause" ) );
            if ( gpTimer->PreviousGameSpeed != gpTimer->GameSpeed &&
                 gpTimer->GameSpeed != 0.0f ) {
                gpTimer->PreviousGameSpeed = gpTimer->GameSpeed;
            }
            gpTimer->GameSpeed = 0.0f;
            bPaused = 1;
        } else {
            gpAudio->requestMixStateResync();
            gpTimer->GameSpeed = gpTimer->PreviousGameSpeed;
            bPaused = 0;
        }
        bHandled = true;
    } else if ( message == kMsgQuit ) {
        if ( numRecordedFrames != 0 ) {
            gpAudio->changeMixState( gpAudio->getMixFlags( "MixMenu" ) );
            gpTimer->GameSpeed = gpTimer->PreviousGameSpeed;
        }
    } else if ( message == kMsgReplayOut ) {
        gpAudio->changeMixState( gpAudio->getMixFlags( "MixMenu" ) );
        gpTimer->GameSpeed = gpTimer->PreviousGameSpeed;
        bPaused = 0;
        GSPhysics::pushFadeRequest( eFadeRequest::FR_FadeOut );
        exitPlayback();
        gpCamera->setMouseCam( true );
        CarState *pLocalCar = gpScenaric->getLocalPlayer().getCarState();
        gpCamera->setBoundCarState( pLocalCar );
        if ( pLocalCar != nullptr ) {
            gpCamera->setTargetPhysEntity( pLocalCar->getPhysicsVehicle() );
        }
        bHandled = true;
    } else if ( message == kMsgSlow ) {
        speedUpFactor = 1;
        if ( slowDownFactor == 1 ) {
            slowDownDisplayValue = 2;
        } else if ( slowDownDisplayValue == 2 ) {
            slowDownDisplayValue = 4;
        } else if ( slowDownFactor == 4 ) {
            slowDownDisplayValue = 1;
        }
        if ( pRemotePlayer != nullptr ) {
            char value[32];
            snprintf( value, sizeof( value ), "/%d", slowDownDisplayValue );
            pRemotePlayer->setVariableValueNonLinked( "/:SlowValue", value );
        }
        bHandled = true;
    } else if ( message == kMsgStart ) {
        rewind();
        gpTimer->GameSpeed = gpTimer->PreviousGameSpeed;
        bPaused = 0;
        bHandled = true;
    } else if ( message == kMsgSwitchCamera ) {
        if ( !gpCamera->cycleReplayModeOverride() ) {
            gpCamera->lockReplayModeOverride( false );
        }
    } else {
        return false;
    }

    if ( pRemotePlayer != nullptr ) {
        char label[128] = {};
        getCameraLabel( label );
        pRemotePlayer->showFrame( label, "/remote/cam_ic", false );
    }
    return bHandled;
}
