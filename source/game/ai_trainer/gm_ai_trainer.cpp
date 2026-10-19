#include "shared.h"
#include "gm_ai_trainer.h"

#include "ai/ai_config.h"
#include "ai/ai_debug_draw.h"
#include "ai/ai_veh_nav_trainer.h"
#include "ai/ai_vehicle.h"
#include "ai/gs_ai.h"
#include "world/gs_aircraft.h"
#include "world/gs_boat.h"
#include "config/gs_config.h"
#include "core/markup.h"
#include "core/time.h"
#include "database/db_aftermarket_pack.h"
#include "database/gs_database.h"
#include "debug/ed_debug_text.h"
#include "filesystem/bank.h"
#include "filesystem/gs_file.h"
#include "flash/gs_flash.h"
#include "game/avatar/gs_avatar.h"
#include "game/scenaric/gs_scenaric.h"
#include "game/vehicle/car_desc.h"
#include "game/vehicle/car_state.h"
#include "game/vehicle/gs_car_state.h"
#include "gs_timer.h"
#include "input/devices/keyboard.h"
#include "input/ed_dev.h"
#include "online/gs_online.h"
#include "physics/physics_config.h"
#include "physics/vehicle/physics_vehicle.h"
#include "render/camera/camera_focus_object.h"
#include "render/camera/camera_game.h"
#include "render/camera/gs_camera.h"
#include "render/draw_list.h"
#include "render/gs_render.h"
#include "render/postfx/gs_postfx.h"
#include "tdu_instance.h"
#include "world/details/gs_details.h"
#include "world/gs_world.h"
#include "world/gs_world_streaming.h"
#include "world/streaming_manager.h"

#include <cmath>
#include <string>

static constexpr float kStreamingCoordinate = 9200.0f;  // DAT_00f421f8
static constexpr float kBrakeTestTimeout = 30.0f;       // DAT_00f416e0
static constexpr float kBrakeTestMaxDistance = 1000.0f; // DAT_00fab8e4
static constexpr float kTurnTestTimeout = 90.0f;        // FLOAT_00f4180c
static constexpr float kTurnTestMinSpeed = -0.5f;       // DAT_00f4186c
static constexpr float kAccelTestMaxAccel = 100.0f;     // DAT_00f41708
static constexpr float kAccelGraphSampleStep = 0.1f;    // FLOAT_00f41668
static constexpr float kTurnRadiusMin = 10.0f;          // DAT_00f4166c
static constexpr float kTurnRadiusMax = 350.0f;         // DAT_00f41968
static constexpr float kKmhToMs = 1.0f / 3.6f;          // DAT_00f41868
static constexpr float kBrakeSpeedStepRatio = 0.04f;    // DAT_00f41980
static constexpr float kDefaultBrakeSpeedStep = 2.5f;   // DAT_00f416c0
static constexpr int32_t kNumBrakeSpeeds = 25;
static constexpr int32_t kBigVehicleLength = 5400;
static constexpr int32_t kExcludedCarID0 = 0x29a;
static constexpr int32_t kExcludedCarID1 = 999;
static constexpr int32_t kFocusCameraMode = 2;

static constexpr float kDefaultBrakeSpeeds[] = {
    5.0f,  7.5f,  10.0f, 12.5f, 15.0f, 17.5f,
    20.0f, 22.5f, 25.0f, 27.5f, 30.0f }; // DAT_00f41750 ... DAT_00f416e0

static const char *kCarFilterNames[] = {
    "All Cars",          "Only Traffics", "Only Brands",  "Only Bikes",
    "Only Big Vehicles", "Only Selected", "From Selected" };

static const Eigen::Vector4f kStartPosition0( 9216.0f, 0.0f, 9130.0f, 1.0f );
static const Eigen::Vector4f kStartPosition1( 8973.0f, 1.0f, 9281.0f, 1.0f );
static const Eigen::Vector4f kStartPosition2( 9216.0f, 0.0f, 8550.0f, 1.0f );

static const char *GetTrainerXmlPath( char *pBuffer, const size_t size ) {
    snprintf( pBuffer, size, "%sDataBase/AiTrainer.xml",
              gpConfig->getResRootPath() );
    return pBuffer;
}

static float ParseFloatAttrib( const CMarkup &xml, const char *pName ) {
    return static_cast<float>( atof( xml.getAttrib( pName ).c_str() ) );
}

static void
SetAttribFormat( CMarkup &xml, const char *pName, const char *pFormat, ... ) {
    char value[256];
    va_list args;
    va_start( args, pFormat );
    vsnprintf( value, sizeof( value ), pFormat, args );
    va_end( args );

    xml.setAttrib( pName, value );
}

static float FitQuadraticNoIntercept( const float *pX,
                                      const float *pY,
                                      const int32_t count,
                                      float &outLinear,
                                      float &outQuadratic ) {
    // FUN_00908210
    float sumX2 = 0.0f;
    float sumX3 = 0.0f;
    float sumX4 = 0.0f;
    float sumXY = 0.0f;
    float sumX2Y = 0.0f;
    for ( int32_t i = 0; i < count; i++ ) {
        const float x2 = pX[i] * pX[i];
        sumX2 += x2;
        sumX3 += pX[i] * x2;
        sumX4 += pX[i] * pX[i] * x2;
        sumXY += pY[i] * pX[i];
        sumX2Y += pY[i] * x2;
    }

    const float invDet = 1.0f / ( sumX4 * sumX2 - sumX3 * sumX3 );
    const float cross = 0.0f - invDet * sumX3;
    outQuadratic = invDet * sumX2 * sumX2Y + cross * sumXY;
    outLinear = cross * sumX2Y + invDet * sumX4 * sumXY;

    float sumSquaredErrors = 0.0f;
    for ( int32_t i = 0; i < count; i++ ) {
        const float error =
            ( pX[i] * pX[i] * outQuadratic + outLinear * pX[i] ) - pY[i];
        sumSquaredErrors += error * error;
    }
    return sqrtf( sumSquaredErrors / static_cast<float>( count ) );
}

static bool WasKeyPressed( Device *pKeyboard, const char *pChannelName ) {
    return pKeyboard != nullptr &&
           IsChannelJustPressed( pKeyboard->getChannel( pChannelName ) );
}

GMAITrainer::GMAITrainer()
    : GameMode(),
      AiFsm(),
      mngWorld(),
      mngNumber(),
      mngBackground(),
      pAiVehicle( nullptr ),
      pCarState( nullptr ),
      pCurrentRecord( nullptr ),
      carFilter( CF_AllCars ),
      bBrakeTest( false ),
      bTurnTest( false ),
      campaignStartTime( 0.0f ),
      carStartTime( 0.0f ),
      carEndTime( 0.0f ),
      numFrames( 0 ),
      nextFpsUpdateTime( 0.0f ),
      fps( 0 ),
      carList{},
      numCars( 0 ),
      currentCarIndex( 0 ),
      testValues{},
      numTestValues( 0 ),
      testValueIndex( 0 ),
      numRecords( 0 ),
      selectedRecord( 0 ),
      numStartMatrices( kNumStartMatrices ),
      testStartTime( 0.0f ),
      lastGraphSampleX( 0.0f ),
      bShowMenu( true ),
      graphMode( GRM_SpeedAccel ),
      pDrawList( nullptr ) {
    // FUN_007bb2e0
    gpConfig->TimerPriority |= 3;
    gpConfig->NbVBL = 2;

    Eigen::Vector4f position( kStreamingCoordinate, 0.0f, kStreamingCoordinate,
                              1.0f );
    gpScenaric->setDefaultStartPosition( position );
    gpWorld->setPlayerPosition( &position );
    if ( gpWorldStreaming->isEnabled() ) {
        gpWorldStreaming->setPosition( Eigen::Vector4f(
            kStreamingCoordinate, 0.0f, kStreamingCoordinate, 0.0f ) );
    }
    gpWorld->setStreamingPosition( &position );

    StreamingManager *pStreaming = gpWorld->getStreamingManager();
    pStreaming->sectorManager.bStreamSector = true;
    gpWorldStreaming->updateStreaming();
    pStreaming->initialize( gpWorld->getStreamingPositionPtr() );

    gbGrassAsTarmac = true;

    registerManager( &mngWorld );
    registerManager( &mngBackground );
    registerManager( &mngNumber );

    loadTrainerResults();
    buildCarList( carFilter );

    startMatrices[0] = Matrix4x4f_t::Identity();
    startMatrices[0].row( 3 ) = kStartPosition0.transpose();
    startMatrices[1] = Matrix4x4f_t::Identity();
    startMatrices[1].row( 3 ) = kStartPosition1.transpose();
    startMatrices[2] = Matrix4x4f_t::Identity();
    startMatrices[2].row( 3 ) = kStartPosition2.transpose();
}

GMAITrainer::~GMAITrainer() {
    // FUN_0042baf0
}

void GMAITrainer::initialize() {
    // FUN_007baf80
    gpAI->setTrafficCategories( true, 0 );

    CameraGame *pCamera =
        gpCamera->findCameraWithMode( 0, gpCamera->getBoundCarState() );
    pCamera->setPosition( kStartPosition0 );
    pCamera->setTarget( kStartPosition1 );
    pCamera->setUp( Eigen::Vector4f( 0.0f, 1.0f, 0.0f, 0.0f ) );
    pCamera->setVelocity( Eigen::Vector4f::Zero() );
    pCamera->updateMatrix( pCamera->getWorldPosition(),
                           Eigen::Vector3f( 0.0f, 1.0f, 0.0f ),
                           pCamera->getTarget() );
    gpCamera->setCameraMode( 0, 0, 1.0f );

    gpBoat->setPaused( true );
    gpAircraft->setPaused( true );
    gpFlash->setPaused( true );
    gpOnline->setPaused( true );
    gpAvatar->setPaused( true );
    gpAI->setPaused( true );
    gpScenaric->setPaused( true );
    gpDetails->setPaused( true );

    mngWorld.setDisableStreamingOnCar( true );
    Eigen::Vector4f streamingPosition = startMatrices[0].row( 3 ).transpose();
    gpWorld->setStreamingPosition( &streamingPosition );

    StreamingManager *pStreaming = gpWorld->getStreamingManager();
    pStreaming->sectorManager.bNeedStreamUpdate = true;
    pStreaming->sectorManager.bDrawSector = false;
    pStreaming->areaGrid.bNeedStreamUpdate = true;
    pStreaming->areaGrid.bDrawArea = false;
    pStreaming->setActiveRenderLayers( 1 );

    spawnCar( records[selectedRecord].CarID );

    DrawStreams streams;
    streams.Diffuse = '\1';
    streams.bIndex16 = '\1';

    pDrawList = new DrawList();
    pDrawList->initialize( 1000, 10000, 0, true, &streams, "TRAINER_DL",
                           false );

    graph.create( 3, pDrawList, 0.0f, 1.0f, 1000, 60.0f );
    graph.setAutoRange( true );
    graph.setShowNames( true );
    graph.setShowRange( true );
    graph.getCurve( 0 ).setColor( 0xff, 0x00, 0x00, 0xff );
    graph.getCurve( graph.getNumCurves() > 1 ? 1 : 0 )
        .setColor( 0x00, 0xff, 0x00, 0xff );
    graph.getCurve( 0 ).setName( "Speed" );
    graph.getCurve( graph.getNumCurves() > 1 ? 1 : 0 ).setName( "Accel" );
}

void GMAITrainer::reset() {
    // FUN_007b8360
    PostFXStep *pDesaturate =
        gpPostFX->getEffect<ePostFXStep::PFXS_Desaturation>();
    if ( !gpPostFX->isFXLocked() && pDesaturate != nullptr ) {
        pDesaturate->getInput()->setEnabled( true );
    }

    nextFpsUpdateTime = 0.0f;
    fps = 0;
    numFrames = 0;
}

void GMAITrainer::tick( float deltaTime, float totalTime ) {
    // FUN_007bace0
    OTDU_UNIMPLEMENTED_ONCE; // TODO: ONLINE (FUN_00879fb0 + remote channel
                             // tick at DAT_014d08c8)
    TickAiDebugOverlay( deltaTime, deltaTime );
    process( FE_Update );
    processDebugKeys();
    updateFpsCounter();

    if ( bShowMenu && bDraw ) {
        drawHelpText();
    }

    if ( pAiVehicle != nullptr ) {
        pAiVehicle->getTrainerNavigation().setShowDebug( bShowMenu );
    }

    PlayerConfiguration *pPlayerConfig = gpConfig->pPlayerConfig;
    if ( pPlayerConfig->pMenuQuit != nullptr &&
         IsActionPressed( pPlayerConfig->pMenuQuit ) ) {
        gpTestDriveInstance->requestExit();
        gpActiveGameMode->requestExit();
    }

    if ( deltaTime > 0.0f ) {
        if ( pAiVehicle != nullptr ) {
            pAiVehicle->tick( deltaTime, totalTime );
        }

        if ( pAiVehicle != nullptr ) {
            switch ( graphMode ) {
            case GRM_LateralSlip: {
                float rearRatio = 0.0f;
                float frontRatio = 0.0f;
                float maxSlip = 0.0f;
                pAiVehicle->getAxleSlipRatios( rearRatio, frontRatio, maxSlip );
                if ( !graph.isFrozen() ) {
                    if ( graph.getNumCurves() > 0 ) {
                        graph.getCurve( 0 ).addPoint( totalTime, rearRatio );
                    }
                    if ( !graph.isFrozen() && graph.getNumCurves() > 1 ) {
                        graph.getCurve( 1 ).addPoint( totalTime, frontRatio );
                    }
                }
                break;
            }
            case GRM_Acceleration: {
                CarState *pCar = pAiVehicle->getCarState();
                const float localAccel =
                    pCar->getPhysicsVehicle()->getLocalAcceleration().norm();
                const float worldAccel =
                    pCar->getPhysicsVehicle()->getWorldAcceleration().norm();
                const float bodySpeed = pCar->getBody()->SpeedInKmh;
                if ( !graph.isFrozen() ) {
                    if ( graph.getNumCurves() > 0 ) {
                        graph.getCurve( 0 ).addPoint( totalTime, localAccel );
                    }
                    if ( !graph.isFrozen() && graph.getNumCurves() > 1 ) {
                        graph.getCurve( 1 ).addPoint( totalTime, worldAccel );
                    }
                    if ( !graph.isFrozen() && graph.getNumCurves() > 2 ) {
                        graph.getCurve( 2 ).addPoint( totalTime, bodySpeed );
                    }
                }
                break;
            }
            case GRM_BrakeModel:
                plotBrakeModel();
                break;
            case GRM_TurnModel:
                plotTurnModel();
                break;
            default:
                break;
            }
        }
    }

    if ( pDrawList != nullptr ) {
        pDrawList->reset();
        if ( !bDraw ) {
            return;
        }
        graph.draw( deltaTime );
    }

    if ( bDraw ) {
        drawStats();
    }
}

void GMAITrainer::draw( float deltaTime, float totalTime ) {
    // FUN_007b8900
    if ( pDrawList != nullptr ) {
        gpRender->getRenderPass<eRenderPass::RP_2D>()
            .pScene->enqueueDynamicDrawList( pDrawList );
    }
    SubmitAiDebugDrawLists();
}

void GMAITrainer::destroy() {
    // FUN_007b7060
    // NOTE: the original leaks the trainer car, its AI vehicle and the draw
    // list; release them so the next game mode starts from a clean state.
    despawnCar();
    graph.release();
    if ( pDrawList != nullptr ) {
        pDrawList->destroy();
        delete pDrawList;
        pDrawList = nullptr;
    }
}

bool GMAITrainer::handle( const int32_t event, const int32_t state ) {
    // FUN_007ba3a0
    switch ( state ) {
    case S_Idle:
        if ( event == FE_Enter ) {
            selectedRecord = findRecordIndex(
                carList[std::min( currentCarIndex, kMaxNumCars - 1 )] );
            return true;
        }
        break;
    case S_StartCampaign:
        if ( event == FE_Enter ) {
            campaignStartTime = getElapsedSeconds();
            buildCarList( carFilter );
            currentCarIndex = -1;
            goTo( S_NextCar );
            return true;
        }
        break;
    case S_NextCar:
        if ( event == FE_Enter ) {
            carStartTime = getElapsedSeconds();
            bool bSpawned = false;
            while ( !bSpawned && currentCarIndex < numCars ) {
                currentCarIndex++;
                if ( currentCarIndex > 0 ) {
                    stampCarEnd();
                }
                if ( currentCarIndex < numCars ) {
                    pCurrentRecord = findRecord( carList[currentCarIndex] );
                    pCurrentRecord->NumBrakeSamples = 0;
                    despawnCar();
                    bSpawned = spawnCar( pCurrentRecord->CarID );
                }
            }

            if ( currentCarIndex >= numCars ) {
                goTo( S_CampaignOver );
            } else if ( bBrakeTest ) {
                goTo( S_BrakeSetup );
            } else {
                goTo( S_TurnSetup );
            }
            return true;
        }
        break;
    case S_CampaignOver:
        if ( event == FE_Enter ) {
            if ( bBrakeTest ) {
                fitAllBrakes();
                dumpBrakeResults();
            }
            if ( bTurnTest ) {
                dumpTurnResults();
            }
            goTo( S_Idle );
            return true;
        }
        if ( event == FE_Exit ) {
            StreamingManager *pStreaming = gpWorld->getStreamingManager();
            pStreaming->areaGrid.bStreamArea = true;
            pStreaming->sectorManager.bStreamSector = true;
            gpWorldStreaming->updateStreaming();
            return true;
        }
        break;
    case S_BrakeSetup:
        if ( event == FE_Enter ) {
            pCurrentRecord->BrakeSpeedStep =
                buildBrakeSpeeds( pCurrentRecord->CarID );
            testValueIndex = 0;
            goTo( S_BrakeRun );
            return true;
        }
        break;
    case S_BrakeRun:
        if ( event == FE_Enter ) {
            const int32_t matrixIndex = ( numStartMatrices > 2 ) ? 2 : 0;
            pAiVehicle->getTrainerNavigation().startBrakeTest(
                startMatrices[matrixIndex], testValues[testValueIndex], 1.0f );
            testStartTime = gpTimer->GameTotalTime;
            return true;
        }
        if ( event == FE_Update ) {
            AiVehNavTrainer &nav = pAiVehicle->getTrainerNavigation();
            BrakeSample &sample =
                pCurrentRecord->BrakeSamples[pCurrentRecord->NumBrakeSamples];
            if ( nav.getSequence() == AiVehNavTrainer::SEQ_Nothing ) {
                const float speed = testValues[testValueIndex];
                sample.bFailed = false;
                sample.Speed = speed;
                sample.NavTime = nav.getBrakeTime();
                sample.Distance = nav.getBrakeDistance();
                sample.TheoreticalDistance =
                    pAiVehicle->computeBrakingDistance( speed, 0.0f, 0.0f );
                sample.Unknown = 0.0f;
                pCurrentRecord->NumBrakeSamples++;
                goTo( S_BrakeNext );
            }

            const int32_t matrixIndex = ( numStartMatrices > 2 ) ? 2 : 0;
            const Eigen::Vector4f delta =
                startMatrices[matrixIndex].row( 3 ).transpose() -
                pCarState->getBody()->WorldMatrix.row( 3 ).transpose();
            if ( kBrakeTestTimeout < gpTimer->GameTotalTime - testStartTime ||
                 kBrakeTestMaxDistance <= delta.norm() ) {
                sample.bFailed = true;
                pCurrentRecord->NumBrakeSamples++;
                goTo( S_BrakeNext );
            }
            return true;
        }
        break;
    case S_BrakeNext:
        if ( event == FE_Enter ) {
            testValueIndex++;
            if ( testValueIndex < numTestValues ) {
                goTo( S_BrakeRun );
                return true;
            }
            if ( pCurrentRecord != nullptr ) {
                fitBrake( pCurrentRecord );
            }
            saveTrainerXml();
            goTo( bTurnTest ? S_TurnSetup : S_NextCar );
            return true;
        }
        break;
    case S_TurnSetup:
        if ( event == FE_Enter ) {
            buildTurnRadii();
            pCurrentRecord->NumTurnSamples = 0;
            testValueIndex = 0;
            goTo( S_TurnRun );
            return true;
        }
        break;
    case S_TurnRun:
        if ( event == FE_Enter ) {
            pAiVehicle->reset( true, false );
            const int32_t matrixIndex = ( numStartMatrices > 1 ) ? 1 : 0;
            pAiVehicle->getTrainerNavigation().startTurnTest(
                startMatrices[matrixIndex], testValues[testValueIndex] );
            testStartTime = gpTimer->GameTotalTime;
            return true;
        }
        if ( event == FE_Update ) {
            AiVehNavTrainer &nav = pAiVehicle->getTrainerNavigation();
            TurnSample &sample =
                pCurrentRecord->TurnSamples[pCurrentRecord->NumTurnSamples];
            if ( nav.getSequence() == AiVehNavTrainer::SEQ_Nothing ) {
                const float radius = testValues[testValueIndex];
                sample.bFailed = false;
                sample.Radius = radius;
                sample.MaxSpeed = nav.getTurnMaxSpeed();
                sample.TheoreticalSpeed =
                    pAiVehicle->computeTurningSpeed( radius );
                pCurrentRecord->NumTurnSamples++;
                goTo( S_TurnNext );
            }

            const float elapsed = gpTimer->GameTotalTime - testStartTime;
            if ( nav.getSequence() == AiVehNavTrainer::SEQ_Error ||
                 kTurnTestTimeout < elapsed ||
                 ( 1.0f < elapsed &&
                   pAiVehicle->getTrafficLocomotion()
                           .getSpeedControl()
                           .getErrorProportional() < kTurnTestMinSpeed ) ) {
                sample.bFailed = true;
                sample.Radius = testValues[testValueIndex];
                pCurrentRecord->NumTurnSamples++;
                goTo( S_TurnNext );
            }
            return true;
        }
        break;
    case S_TurnNext:
        if ( event == FE_Enter ) {
            testValueIndex++;
            if ( testValueIndex < numTestValues ) {
                goTo( S_TurnRun );
                return true;
            }
            if ( pCurrentRecord != nullptr ) {
                fitTurn( pCurrentRecord );
            }
            saveTrainerXml();
            goTo( S_NextCar );
            return true;
        }
        break;
    case S_AccelNextCar:
        if ( event == FE_Enter ) {
            bool bSpawned = false;
            while ( !bSpawned && currentCarIndex < numCars ) {
                currentCarIndex++;
                if ( currentCarIndex < numCars ) {
                    pCurrentRecord = findRecord( carList[currentCarIndex] );
                    despawnCar();
                    bSpawned = spawnCar( carList[currentCarIndex] );
                }
            }

            if ( currentCarIndex >= numCars ) {
                dumpAccelResults();
                StreamingManager *pStreaming = gpWorld->getStreamingManager();
                pStreaming->areaGrid.bStreamArea = true;
                pStreaming->sectorManager.bStreamSector = true;
                gpWorldStreaming->updateStreaming();
                goTo( S_Idle );
                return true;
            }

            testValueIndex = 0;
            goTo( S_AccelRun );
            return true;
        }
        break;
    case S_AccelRun:
        if ( event == FE_Enter ) {
            pAiVehicle->reset( true, false );
            const int32_t matrixIndex = ( numStartMatrices > 2 ) ? 2 : 0;
            pAiVehicle->getTrainerNavigation().startAccelTest(
                startMatrices[matrixIndex], 0.0f,
                pAiVehicle->getInfos().MaxSpeed );
            testStartTime = gpTimer->GameTotalTime;
            graph.clear();
            lastGraphSampleX = 0.0f;
            return true;
        }
        if ( event == FE_Update ) {
            const float speed = pCarState->getBody()->SpeedInKmh;
            float accel =
                pCarState->getPhysicsVehicle()->getLocalAcceleration().norm();
            if ( kAccelTestMaxAccel < accel || speed < 1.0f ) {
                accel = 0.0f;
            }

            if ( lastGraphSampleX + kAccelGraphSampleStep < speed ) {
                if ( !graph.isFrozen() && graph.getNumCurves() > 0 ) {
                    graph.getCurve( 0 ).addPoint( speed, accel );
                }
                lastGraphSampleX = speed;
            }

            AiVehNavTrainer &nav = pAiVehicle->getTrainerNavigation();
            AccelRun &run = pCurrentRecord->AccelRuns[0];
            if ( nav.getSequence() == AiVehNavTrainer::SEQ_Nothing ) {
                run.bFailed = false;
                run.Result1 = nav.getAccelMaxSpeed();
                run.Result0 = nav.getAccelMean();
                pCurrentRecord->NumAccelRuns++;
                goTo( S_AccelNextCar );
                return true;
            }
            if ( nav.getSequence() == AiVehNavTrainer::SEQ_Error ) {
                run.bFailed = true;
                goTo( S_AccelNextCar );
            }
            return true;
        }
        break;
    default:
        break;
    }

    return false;
}

float GMAITrainer::getElapsedSeconds() const { return GetTimePrecise(); }

GMAITrainer::TrainerRecord *GMAITrainer::findRecord( const int32_t carID ) {
    // FUN_007b70c0
    for ( int32_t i = 0; i < numRecords; i++ ) {
        if ( records[i].CarID == carID ) {
            return &records[i];
        }
    }
    return nullptr;
}

GMAITrainer::TrainerRecord *
GMAITrainer::findRecordOrFirst( const int32_t carID ) {
    TrainerRecord *pRecord = findRecord( carID );
    return ( pRecord != nullptr ) ? pRecord : &records[0];
}

int32_t GMAITrainer::findRecordIndex( const int32_t carID ) const {
    // FUN_007b7100
    for ( int32_t i = 0; i < numRecords; i++ ) {
        if ( records[i].CarID == carID ) {
            return i;
        }
    }
    return 0;
}

void GMAITrainer::loadTrainerXml() {
    // FUN_007b92b0
    numRecords = 0;

    char path[256];
    GetTrainerXmlPath( path, sizeof( path ) );

    void *pContent = nullptr;
    uint32_t contentSize = 0;
    if ( !gpFile->loadFile( path, &pContent, &contentSize ) ) {
        return;
    }

    CMarkup xml( static_cast<const char *>( pContent ) );
    if ( xml.findElem( "AITRAINER" ) ) {
        xml.intoElem();
        while ( numRecords < kMaxNumRecords && xml.findElem( "Result" ) ) {
            TrainerRecord &record = records[numRecords];
            record.CarID = atoi( xml.getAttrib( "CarId" ).c_str() );
            record.TurnCoef1 = ParseFloatAttrib( xml, "TurnCoef1" );
            record.TurnCoef2 = ParseFloatAttrib( xml, "TurnCoef2" );
            record.TurnStdErr = ParseFloatAttrib( xml, "TurnStdErr" );
            record.BrakeCoef1 = ParseFloatAttrib( xml, "BrakeCoef1" );
            record.BrakeCoef2 = ParseFloatAttrib( xml, "BrakeCoef2" );
            record.BrakeStdErr = ParseFloatAttrib( xml, "BrakeStdErr" );

            xml.intoElem();
            record.NumBrakeSamples = 0;
            while ( record.NumBrakeSamples < kMaxNumSamples &&
                    xml.findElem( "BrakeTest" ) ) {
                BrakeSample &sample =
                    record.BrakeSamples[record.NumBrakeSamples++];
                sample.Speed = ParseFloatAttrib( xml, "Speed" );
                sample.bFailed = false;
                sample.Distance = ParseFloatAttrib( xml, "Dist" );
            }

            record.NumTurnSamples = 0;
            while ( record.NumTurnSamples < kMaxNumSamples &&
                    xml.findElem( "TurnSpeedTest" ) ) {
                TurnSample &sample =
                    record.TurnSamples[record.NumTurnSamples++];
                sample.Radius = ParseFloatAttrib( xml, "Radius" );
                sample.bFailed = false;
                sample.MaxSpeed = ParseFloatAttrib( xml, "MaxSpeed" );
            }
            xml.outOfElem();
            numRecords++;
        }
    }

    TestDrive::Free( pContent );
}

void GMAITrainer::loadTrainerResults() {
    // FUN_007b9c80
    loadTrainerXml();

    const GameDatabase &gameDB = gpDatabase->getGameDB();
    for ( uint32_t i = 0; i < gpDatabase->getNumCars(); i++ ) {
        const DBEntryCarPhysicsData &car = gameDB.DBCarData[i];
        if ( ( car.BitField & 1 ) == 0 || car.Brand == nullptr ||
             ( car.Brand->Flags & 1 ) == 0 ) {
            continue;
        }

        const AIVehiclePhysics *pPhysics = nullptr;
        for ( int32_t j = 0; j < gAIConfig.NumVehiclePhysics; j++ ) {
            if ( gAIConfig.VehiclePhysics[j].DatabaseId == car.CarID ) {
                pPhysics = &gAIConfig.VehiclePhysics[j];
                break;
            }
        }

        TrainerRecord *pRecord = findRecord( car.CarID );
        if ( pRecord == nullptr ) {
            if ( numRecords >= kMaxNumRecords ) {
                continue;
            }
            pRecord = &records[numRecords++];
            pRecord->CarID = car.CarID;
            pRecord->NumTurnSamples = 0;
            pRecord->NumBrakeSamples = 0;
            pRecord->BrakeCoef1 = 0.0f;
            pRecord->BrakeCoef2 = 0.0f;
            pRecord->TurnCoef1 = 0.0f;
            pRecord->TurnCoef2 = 0.0f;
        }

        if ( pPhysics != nullptr ) {
            pRecord->BrakeCoef1 = pPhysics->BrakingDistC1;
            pRecord->BrakeCoef2 = pPhysics->BrakingDistC2;
            pRecord->TurnCoef1 = pPhysics->TurningSpeedC1;
            pRecord->TurnCoef2 = pPhysics->TurningSpeedC2;
        }
    }
}

void GMAITrainer::saveTrainerXml() {
    // FUN_007b8950
    CMarkup xml;
    xml.setDoc( "<?xml version=\"1.0\" encoding=\"ISO-8859-1\"?>\r\n" );
    xml.addElem( "AITRAINER" );
    xml.intoElem();
    for ( int32_t i = 0; i < numRecords; i++ ) {
        const TrainerRecord &record = records[i];
        xml.addElem( "Result" );
        SetAttribFormat( xml, "\n\tCarId", "%d", record.CarID );
        SetAttribFormat( xml, "\n\tTurnCoef1", "%.5f", record.TurnCoef1 );
        SetAttribFormat( xml, "\n\tTurnCoef2", "%.5f", record.TurnCoef2 );
        SetAttribFormat( xml, "\n\tTurnStdErr", "%.5f", record.TurnStdErr );
        SetAttribFormat( xml, "\n\tBrakeCoef1", "%.5f", record.BrakeCoef1 );
        SetAttribFormat( xml, "\n\tBrakeCoef2", "%.5f", record.BrakeCoef2 );
        SetAttribFormat( xml, "\n\tBrakeStdErr", "%.5f", record.BrakeStdErr );
        xml.intoElem();

        for ( int32_t j = 0; j < record.NumBrakeSamples; j++ ) {
            const BrakeSample &sample = record.BrakeSamples[j];
            if ( sample.bFailed ) {
                continue;
            }
            xml.addElem( "BrakeTest" );
            SetAttribFormat( xml, "Speed", "%.5f", sample.Speed );
            SetAttribFormat( xml, "Dist", "%.5f", sample.Distance );
        }
        for ( int32_t j = 0; j < record.NumTurnSamples; j++ ) {
            const TurnSample &sample = record.TurnSamples[j];
            if ( sample.bFailed ) {
                continue;
            }
            xml.addElem( "TurnSpeedTest" );
            SetAttribFormat( xml, "Radius", "%.5f", sample.Radius );
            SetAttribFormat( xml, "MaxSpeed", "%.5f", sample.MaxSpeed );
        }
        xml.outOfElem();
    }
    xml.outOfElem();

    // The original saves the terminating null character as well
    const std::string &doc = xml.getDoc();
    char path[256];
    GetTrainerXmlPath( path, sizeof( path ) );
    GSFile::SaveFile( path, doc.c_str(),
                      static_cast<uint32_t>( strlen( doc.c_str() ) + 1 ) );
}

void GMAITrainer::writeAIConfig() {
    // FUN_007b9a40
    static constexpr uint32_t kSerializeBufferSize = 0x10000;

    char path[256];
    snprintf( path, sizeof( path ), "%sDataBase/AIConfig.xml",
              gpConfig->getResRootPath() );

    AIConfig config;
    void *pContent = nullptr;
    uint32_t contentSize = 0;
    if ( gpFile->loadFile( path, &pContent, &contentSize ) ) {
        // NOTE: the original parses the file without decrypting it (so it
        // starts from defaults) and later saves it in plain text, which breaks
        // the next GSAI::loadAIConfig; keep the TEA encryption both ways.
        uint32_t remainingBytes = 0;
        Bank::DecryptTEA( pContent, contentSize, &remainingBytes );
        config.parse( static_cast<const char *>( pContent ) );
    }
    if ( pContent != nullptr ) {
        TestDrive::Free( pContent );
    }

    for ( int32_t i = 0; i < numRecords; i++ ) {
        const TrainerRecord &record = records[i];

        int32_t physicsIndex = 0;
        for ( ; physicsIndex < config.NumVehiclePhysics; physicsIndex++ ) {
            if ( config.VehiclePhysics[physicsIndex].DatabaseId ==
                 record.CarID ) {
                break;
            }
        }
        if ( physicsIndex == config.NumVehiclePhysics ) {
            if ( record.NumBrakeSamples == 0 && record.NumTurnSamples == 0 ) {
                continue;
            }
            config.VehiclePhysics[physicsIndex].DatabaseId = record.CarID;
            config.NumVehiclePhysics++;
        }

        AIVehiclePhysics &physics = config.VehiclePhysics[physicsIndex];
        physics.BrakingDistC1 = record.BrakeCoef1;
        physics.BrakingDistC2 = record.BrakeCoef2;
        physics.TurningSpeedC1 = record.TurnCoef1;
        physics.TurningSpeedC2 = record.TurnCoef2;
    }

    gAIHandlingReference.MaxTopSpeed = ComputeMaxTopSpeed();
    gAIHandlingReference.MinAcceleration = ComputeMinAcceleration();
    gAIHandlingReference.MinTurnRadius = config.computeMinTurnRadius();
    gAIHandlingReference.MinBrakingDistance =
        gAIConfig.computeMinBrakingDistance();

    ForEachAvailableCar( [&]( const int32_t carID ) {
        for ( int32_t i = 0; i < config.NumVehiclePhysics; i++ ) {
            if ( config.VehiclePhysics[i].DatabaseId == carID ) {
                config.VehiclePhysics[i].HandlingMark =
                    config.computeHandlingMark( carID, gAIHandlingReference );
                break;
            }
        }
    } );

    char *pBuffer =
        static_cast<char *>( TestDrive::AllocAligned( kSerializeBufferSize ) );
    memset( pBuffer, 0, kSerializeBufferSize );
    const int32_t size = config.serialize( pBuffer, kSerializeBufferSize - 16 );
    if ( size > 0 ) {
        const uint32_t paddedSize =
            ( static_cast<uint32_t>( size ) + 7u ) & ~7u;
        const uint64_t seed = GetTimeMonotonic();
        memcpy( pBuffer + paddedSize, &seed, sizeof( seed ) );
        const uint32_t encryptedSize = paddedSize + sizeof( seed );
        Bank::EncryptTEA( pBuffer, encryptedSize );
        GSFile::SaveFile( path, pBuffer, encryptedSize );
    }

    TestDrive::FreeAligned( pBuffer );
}

void GMAITrainer::dumpTurnResults() {
    // FUN_007b73d0
    for ( int32_t i = 0; i < numCars; i++ ) {
        const TrainerRecord *pRecord = findRecordOrFirst( carList[i] );
        std::string line;
        for ( int32_t j = 0; j < pRecord->NumTurnSamples; j++ ) {
            const TurnSample &sample = pRecord->TurnSamples[j];
            if ( sample.bFailed ) {
                continue;
            }
            char value[32];
            snprintf( value, sizeof( value ), "%.2f\t", sample.MaxSpeed );
            char *pSeparator = strchr( value, '.' );
            if ( pSeparator != nullptr ) {
                *pSeparator = ',';
            }
            line += value;
        }
        OTDU_LOG_DEBUG( "%d\t%s\n", pRecord->CarID, line.c_str() );
    }
}

void GMAITrainer::dumpBrakeResults() {
    // FUN_007b74d0
    for ( int32_t i = 0; i < numCars; i++ ) {
        const TrainerRecord *pRecord = findRecordOrFirst( carList[i] );
        std::string speeds;
        std::string distances;
        for ( int32_t j = 0; j < pRecord->NumBrakeSamples; j++ ) {
            const BrakeSample &sample = pRecord->BrakeSamples[j];
            if ( sample.bFailed ) {
                continue;
            }
            char value[32];
            snprintf( value, sizeof( value ), "%.2f, ", sample.Speed );
            speeds += value;
            snprintf( value, sizeof( value ), "%.2f, ", sample.Distance );
            distances += value;
        }
        OTDU_LOG_DEBUG( "%d\n%s\n%s\n", pRecord->CarID, speeds.c_str(),
                        distances.c_str() );
    }
}

void GMAITrainer::dumpAccelResults() {
    // FUN_007b7350
}

void GMAITrainer::buildCarList( const int32_t filter ) {
    // FUN_007b8ed0
    const int32_t currentCarID =
        ( pCarState != nullptr ) ? pCarState->getCarID() : 1;
    numCars = 0;

    bool bPassedCurrent = false;
    const GameDatabase &gameDB = gpDatabase->getGameDB();
    const char *pTrafficBrand = gpDatabase->getStringByHashcode( 6, 0x3540681 );
    const char *pBikeCategory = gpDatabase->getStringByHashcode( 1, 0x35da5b5 );
    for ( uint32_t i = 0; i < gpDatabase->getNumCars(); i++ ) {
        const DBEntryCarPhysicsData &car = gameDB.DBCarData[i];
        if ( ( car.BitField & 1 ) == 0 || car.Brand == nullptr ||
             ( car.Brand->Flags & 1 ) == 0 || car.CarID == kExcludedCarID0 ||
             car.CarID == kExcludedCarID1 ) {
            continue;
        }

        const bool bTraffic = pTrafficBrand == car.Brand->BrandID.pAsString;
        if ( !bTraffic ) {
            char bankPath[512];
            snprintf( bankPath, sizeof( bankPath ), "%sVehicules/%s.bnk",
                      gpConfig->getResRootPath(),
                      car.CarResourceFilename.pAsString );
            if ( !gpFile->fileExists( bankPath ) ) {
                continue;
            }
        }

        if ( car.CarID == currentCarID ) {
            bPassedCurrent = true;
        }

        bool bAccepted = false;
        switch ( filter ) {
        case CF_AllCars:
            bAccepted = true;
            break;
        case CF_OnlyTraffics:
            bAccepted = bTraffic;
            break;
        case CF_OnlyBrands:
            bAccepted = !bTraffic;
            break;
        case CF_OnlyBikes:
            bAccepted = pBikeCategory == car.Category.pAsString;
            break;
        case CF_OnlyBigVehicles:
            bAccepted = car.Length > kBigVehicleLength;
            break;
        case CF_OnlySelected:
            bAccepted = car.CarID == currentCarID;
            break;
        case CF_FromSelected:
            bAccepted = bPassedCurrent;
            break;
        default:
            break;
        }

        if ( bAccepted ) {
            carList[numCars++] = car.CarID;
            if ( numCars >= kMaxNumCars ) {
                return;
            }
        }
    }
}

void GMAITrainer::buildTurnRadii() {
    // FUN_007b71a0
    numTestValues = 0;
    const float ratio = powf( 35.0f, 0.1f );
    float radius = kTurnRadiusMin;
    do {
        testValues[numTestValues++] = radius;
        radius *= ratio;
    } while ( radius < kTurnRadiusMax );
}

float GMAITrainer::buildBrakeSpeeds( const int32_t carID ) {
    // FUN_007b7210
    numTestValues = kNumBrakeSpeeds;

    const DBEntryCarPhysicsData *pCar = gpDatabase->getCarDBEntry( carID );
    if ( pCar == nullptr ) {
        for ( int32_t i = 0; i < int32_t( std::size( kDefaultBrakeSpeeds ) );
              i++ ) {
            testValues[i] = kDefaultBrakeSpeeds[i];
        }
        return kDefaultBrakeSpeedStep;
    }

    const float step = pCar->TopSpeedKMH * kKmhToMs * kBrakeSpeedStepRatio;
    float speed = step;
    for ( int32_t i = 0; i < numTestValues; i++ ) {
        testValues[i] = speed;
        speed += step;
    }
    return step;
}

bool GMAITrainer::spawnCar( const int32_t carID ) {
    // FUN_007b9e10
    const DBEntryCarPhysicsData *pCar = gpDatabase->getCarDBEntry( carID );
    const eVehicleInstallType installType =
        gpDatabase->isTraffic( pCar ) ? VIT_Traffic : VIT_Opponent;

    VehicleSpawnParams params;
    params.Transform = startMatrices[0];
    params.bSetPosition = true;
    params.bCastToGround = true;
    params.bResetCar = true;
    params.PhantomMode = 1;
    pCarState = gpCarState->spawnVehicle( carID, -1, -1, kNoPackHashcode,
                                          params, -1, 0, installType );
    if ( pCarState == nullptr ) {
        return false;
    }

    gpScenaric->getLocalPlayer().setCarState( pCarState );
    pCarState->getBody()->WorldMatrix = startMatrices[0];

    pAiVehicle = new AiVehicle();
    pAiVehicle->initialize( eAiVehicleCategory::AVC_Opponent, pCarState,
                            AiRoadIt(), 0, AiVehicle::kBehaviourNone,
                            AiVehicle::kControllerNone, 3, 0, false );
    pAiVehicle->getLocomotion()->setPhysicsActive( true, false );
    SetDebuggedAiVehicle( pAiVehicle );
    pCarState->setAIController( pAiVehicle );

    gpCamera->setMouseCam( false );
    gpCamera->setBoundCarState( pCarState );
    gpCamera->setTargetPhysEntity( pCarState->getPhysicsVehicle() );
    CameraFocusObject *pFocusCamera = static_cast<CameraFocusObject *>(
        gpCamera->findCameraWithMode( kFocusCameraMode, pCarState ) );
    pFocusCamera->setObjectPosition(
        pCarState->getBody()->WorldMatrix.row( 3 ).data() );
    gpCamera->setCameraMode( kFocusCameraMode, 0, 1.0f );
    SetDebuggedAiVehicle( pAiVehicle );
    return true;
}

void GMAITrainer::despawnCar() {
    // FUN_007b9180
    if ( pCarState == nullptr ) {
        return;
    }

    SetDebuggedAiVehicle( nullptr );

    CarState *pTarget = gpCamera->getBoundCarState();
    CameraGame *pFocus =
        gpCamera->findCameraWithMode( kFocusCameraMode, pTarget );
    CameraGame *pFree = gpCamera->findCameraWithMode( 0, pTarget );
    const Eigen::Vector3f position = pFocus->getWorldPosition();
    const Eigen::Vector3f target = pFocus->getTarget();
    pFree->setPosition(
        Eigen::Vector4f( position.x(), position.y(), position.z(), 1.0f ) );
    pFree->setTarget(
        Eigen::Vector4f( target.x(), target.y(), target.z(), 1.0f ) );
    const Eigen::Vector3f up = pFocus->getUp();
    pFree->setUp( Eigen::Vector4f( up.x(), up.y(), up.z(), 0.0f ) );
    gpCamera->setCameraMode( 0, 0, 1.0f );

    pAiVehicle->release();
    delete pAiVehicle;
    pAiVehicle = nullptr;
    pCarState->setAIController( nullptr );

    gpScenaric->getLocalPlayer().setCarState( nullptr );

    VehicleBaseResources *pResource = pCarState->getResource();
    gpCarState->releaseCarState( pCarState, false );
    if ( pResource != nullptr ) {
        pResource->unload();
        gpCarState->releaseResourceEntry( pResource );
    }
    pCarState = nullptr;
}

void GMAITrainer::stampCarEnd() {
    // FUN_007b7000
    carEndTime = getElapsedSeconds();
}

void GMAITrainer::fitBrake( TrainerRecord *pRecord ) {
    // FUN_007b7b30
    if ( pRecord->NumBrakeSamples <= 0 ) {
        return;
    }

    float speeds[kMaxNumSamples];
    float distances[kMaxNumSamples];
    int32_t count = 0;
    for ( int32_t i = 0; i < pRecord->NumBrakeSamples; i++ ) {
        const BrakeSample &sample = pRecord->BrakeSamples[i];
        if ( !sample.bFailed ) {
            speeds[count] = sample.Speed;
            distances[count] = sample.Distance;
            count++;
        }
    }

    float linear = 0.0f;
    float quadratic = 0.0f;
    pRecord->BrakeStdErr =
        FitQuadraticNoIntercept( speeds, distances, count, linear, quadratic );
    pRecord->BrakeCoef1 = linear;
    pRecord->BrakeCoef2 = quadratic;
}

bool GMAITrainer::fitTurn( TrainerRecord *pRecord ) {
    // FUN_007b7a80
    if ( pRecord->NumTurnSamples <= 0 ) {
        return false;
    }

    float sqrtRadii[kMaxNumSamples];
    float speeds[kMaxNumSamples];
    int32_t count = 0;
    for ( int32_t i = 0; i < pRecord->NumTurnSamples; i++ ) {
        const TurnSample &sample = pRecord->TurnSamples[i];
        if ( !sample.bFailed ) {
            sqrtRadii[count] = sqrtf( sample.Radius );
            speeds[count] = sample.MaxSpeed;
            count++;
        }
    }

    float linear = 0.0f;
    float quadratic = 0.0f;
    pRecord->TurnStdErr =
        FitQuadraticNoIntercept( sqrtRadii, speeds, count, linear, quadratic );
    pRecord->TurnCoef1 = linear;
    pRecord->TurnCoef2 = quadratic;
    return true;
}

void GMAITrainer::fitAllBrakes() {
    // FUN_007b82f0
    for ( int32_t i = 0; i < numCars; i++ ) {
        fitBrake( findRecordOrFirst( carList[i] ) );
    }
}

void GMAITrainer::startTurnCampaign() {
    // FUN_007b76a0
    bBrakeTest = false;
    bTurnTest = true;
    goTo( S_StartCampaign );
}

void GMAITrainer::startBrakeCampaign() {
    // FUN_007b76c0
    bBrakeTest = true;
    bTurnTest = false;
    goTo( S_StartCampaign );
}

void GMAITrainer::startAccelCampaign() {
    // FUN_007b9120
    buildCarList( carFilter );
    currentCarIndex = -1;
    testValueIndex = 0;
    goTo( S_AccelNextCar );
}

void GMAITrainer::cycleCarFilter() {
    // FUN_007b9150
    carFilter++;
    if ( carFilter > CF_FromSelected ) {
        carFilter = CF_AllCars;
    }
    buildCarList( carFilter );
}

void GMAITrainer::setGraphMode( const int32_t mode ) {
    // FUN_007b83a0
    graphMode = mode;

    struct GraphSetup {
        int32_t NumCurves;
        float MinY;
        float MaxY;
        int32_t Capacity;
        float TimeWindow;
        float Right;
        float Bottom;
        bool bAutoRange;
        uint8_t Alpha;
        const char *pNames[3];
    };
    static constexpr GraphSetup kGraphSetups[GRM_Count] = {
        {3,
         0.0f,   1.0f,
         1000,                 60.0f,
         1.0f,                               1.0f,
         true,                                           0xff,
         { "Speed", "Accel", nullptr }                                                                       },
        {3,
         -10.0f,
         10.0f,          1000,
         20.0f,                        0.5f,
         0.5f,                                     true,
         0xff,                                                 { "Rear Lat", "Front Lat", nullptr }          },
        {3,
         -10.0f,
         10.0f,          1000,
         20.0f,                        1.0f,
         0.5f,                                     true,
         0x80,                                                 { "Local Accel", "World Accel", "Body Speed" }},
        {2,
         0.0f,   100.0f,
         500,                  100.0f,
         1.0f,                               0.5f,
         false,                                          0x80,
         { "Theoretical Brake Dist", "Empirical Brake Dist", nullptr }                                       },
        {2,
         0.0f,   75.0f,
         100,                  360.0f,
         1.0f,                               0.5f,
         false,                                          0x80,
         { "Theoretical Turn Speed", "Empirical Turn Speed", nullptr }                                       },
    };

    if ( mode < 0 || mode >= GRM_Count ) {
        return;
    }

    const GraphSetup &setup = kGraphSetups[mode];
    graph.release();
    graph.create( setup.NumCurves, pDrawList, setup.MinY, setup.MaxY,
                  setup.Capacity, setup.TimeWindow );
    graph.setScrolling( false );
    graph.setBounds( 0.0f, 0.0f, setup.Right, setup.Bottom );
    graph.setAutoRange( setup.bAutoRange );
    graph.setAutoScale( false );
    graph.setShowNames( true );
    graph.setShowRange( true );

    const int32_t numCurves = graph.getNumCurves();
    graph.getCurve( 0 ).setColor( 0xff, 0x00, 0x00, setup.Alpha );
    graph.getCurve( numCurves > 1 ? 1 : 0 )
        .setColor( 0x00, 0xff, 0x00, setup.Alpha );
    if ( mode == GRM_Acceleration ) {
        graph.getCurve( numCurves > 2 ? 2 : 0 )
            .setColor( 0x00, 0x00, 0xff, 0xff );
    }

    graph.getCurve( 0 ).setName( setup.pNames[0] );
    graph.getCurve( numCurves > 1 ? 1 : 0 ).setName( setup.pNames[1] );
    if ( setup.pNames[2] != nullptr ) {
        graph.getCurve( numCurves > 2 ? 2 : 0 ).setName( setup.pNames[2] );
    }
}

void GMAITrainer::plotBrakeModel() {
    // FUN_007b7800
    graph.clear();

    const TrainerRecord *pRecord = findRecord( pCarState->getCarID() );
    if ( pRecord == nullptr ) {
        return;
    }

    for ( int32_t i = 0; i < pRecord->NumBrakeSamples; i++ ) {
        const BrakeSample &sample = pRecord->BrakeSamples[i];
        if ( !sample.bFailed && !graph.isFrozen() &&
             graph.getNumCurves() > 1 ) {
            graph.getCurve( 1 ).addPoint( sample.Speed, sample.Distance );
        }

        float model = sample.Speed * sample.Speed * pRecord->BrakeCoef2 +
                      sample.Speed * pRecord->BrakeCoef1;
        if ( model <= 0.0f ) {
            model = 0.0f;
        }
        if ( !graph.isFrozen() && graph.getNumCurves() > 0 ) {
            graph.getCurve( 0 ).addPoint( sample.Speed, model );
        }
    }
}

void GMAITrainer::plotTurnModel() {
    // FUN_007b76e0
    graph.clear();

    // NOTE: the original dereferences a null record when the car has none.
    const TrainerRecord *pRecord = findRecord( pCarState->getCarID() );
    if ( pRecord == nullptr ) {
        return;
    }

    for ( int32_t i = 0; i < pRecord->NumTurnSamples; i++ ) {
        const TurnSample &sample = pRecord->TurnSamples[i];
        if ( !sample.bFailed && !graph.isFrozen() &&
             graph.getNumCurves() > 1 ) {
            graph.getCurve( 1 ).addPoint( sample.Radius, sample.MaxSpeed );
        }

        float model = pRecord->TurnCoef2 * sample.Radius +
                      sqrtf( sample.Radius ) * pRecord->TurnCoef1;
        if ( model <= 0.0f ) {
            model = 0.0f;
        }
        if ( !graph.isFrozen() && graph.getNumCurves() > 0 ) {
            graph.getCurve( 0 ).addPoint( sample.Radius, model );
        }
    }
}

void GMAITrainer::updateFpsCounter() {
    // FUN_007b7f00
    const float time = getElapsedSeconds();
    numFrames++;
    if ( time > nextFpsUpdateTime ) {
        nextFpsUpdateTime = time + 1.0f;
        fps = numFrames;
        numFrames = 0;
    }

    if ( gpMngNumber != nullptr && bDraw ) {
        const float screenWidth = static_cast<float>( gpRender->getWidth() );
        const float screenHeight = static_cast<float>( gpRender->getHeight() );
        Eigen::Vector3f position( ( screenWidth - 130.0f ) / screenWidth,
                                  50.0f / screenHeight, 0.0f );
        gpMngNumber->addNumber( position, 10, ColorRGBA( 0, 0xff, 0, 0xff ),
                                fps );
    }
}

void GMAITrainer::drawHelpText() {
    // FUN_007b7bd0
    gEdDebugText.setColor( DDC_Blue );
    gEdDebugText.setPosition( 10, 300 );
    gEdDebugText.drawText( "E: draw On/Off" );
    gEdDebugText.drawText( "Q: prev car" );
    gEdDebugText.drawText( "S: next car" );
    gEdDebugText.drawText( "O: WRITE CONFIG" );
    gEdDebugText.drawText( "1: sustain speed" );
    gEdDebugText.drawText( "2: MEASURE LONG+LAT" );
    gEdDebugText.drawText( "3: speed gradations (up)" );
    gEdDebugText.drawText( "4: speed gradations (down)" );
    gEdDebugText.drawText( "5: MEASURE ACCELERATION" );
    gEdDebugText.drawText( "6: steer alternates" );
    gEdDebugText.drawText( "7: MEASURE LONGITUDINAL FRICTION" );
    gEdDebugText.drawText( "8: MEASURE LATERAL FRICTION" );
    gEdDebugText.drawText( "9: steer on circle" );
    gEdDebugText.drawText( "]: acceleration 0 to max" );
    gEdDebugText.drawText( "=: compare locos" );
    gEdDebugText.drawText( "\xb2: stop distance" );
    gEdDebugText.drawText( "A: compare locos 2" );
    gEdDebugText.drawText( "Z: Change Graph Mode" );

    char buffer[0x100];
    snprintf( buffer, sizeof( buffer ), "^: Select what to test: " );
    if ( static_cast<uint32_t>( carFilter ) <= CF_FromSelected ) {
        strncat( buffer, kCarFilterNames[carFilter],
                 sizeof( buffer ) - strlen( buffer ) - 1 );
    }
    gEdDebugText.drawText( buffer );
}

void GMAITrainer::drawStats() {
    // FUN_007b8020
    if ( pAiVehicle == nullptr ) {
        return;
    }

    float rearRatio = 0.0f;
    float frontRatio = 0.0f;
    float maxSlip = 0.0f;
    pAiVehicle->getAxleSlipRatios( rearRatio, frontRatio, maxSlip );
    const bool bSliding = ( rearRatio > 1.0f && frontRatio > 1.0f ) ||
                          ( rearRatio < -1.0f && frontRatio < -1.0f );
    const bool bOverSteer = pAiVehicle->isOverSteering();
    const bool bUnderSteer = pAiVehicle->isUnderSteering();

    gEdDebugText.setPosition( gpRender->getWidth() - 0x104, 300 );
    if ( pCarState != nullptr ) {
        const int32_t carID = pCarState->getCarID();
        gEdDebugText.drawText( "Car ID: %d", carID );
        const CarConfig *pConfig = gpDatabase->getCarConfig( carID );
        if ( pConfig != nullptr ) {
            gEdDebugText.drawText( "%s", pConfig->FullName.c_str() );
        }
        gEdDebugText.drawText( "" );
    }

    if ( getCurrentState() != S_Idle ) {
        const float now = getElapsedSeconds();
        carEndTime = now;
        const int32_t numTested = currentCarIndex;
        const float carDuration = carStartTime - campaignStartTime;
        gEdDebugText.drawText( "TESTING started %.2f mins ago",
                               ( now - campaignStartTime ) * ( 1.0f / 60.0f ) );
        if ( numTested < 1 ) {
            gEdDebugText.drawText( "??? Estimated mins left" );
        } else {
            gEdDebugText.drawText( "%.2f estimated mins left",
                                   static_cast<float>( numCars ) * carDuration /
                                       numTested * ( 1.0f / 60.0f ) );
        }
        gEdDebugText.drawText(
            "%d / %d cars tested (%d %%)", numTested, numCars,
            static_cast<int32_t>( numTested * 100.0f / numCars ) );
    }

    gEdDebugText.drawText( bSliding ? "SLIDING" : "-------" );
    gEdDebugText.drawText( bOverSteer ? "OVER-STEER" : "----------" );
    gEdDebugText.drawText( bUnderSteer ? "UNDER-STEER" : "-----------" );

    char gear[32];
    snprintf( gear, sizeof( gear ), "GEAR: %d",
              pCarState->getPhysicsVehicle()->getCurrentGear() );
    gEdDebugText.drawText( gear );

    pAiVehicle->getTrainerNavigation().drawDebugText();
}

void GMAITrainer::processDebugKeys() {
    // NOTE: the retail key dispatch was stripped from the shipped exe; the
    // handlers it called survive with no caller. The bindings below follow the
    // help screen (FUN_007b7bd0) using the AZERTY-named DirectInput channels.
    Device *pKeyboard = gpEdDev->getDevice( eDeviceType::DT_Keyboard, 0 );
    if ( pKeyboard == nullptr ) {
        return;
    }

    if ( WasKeyPressed( pKeyboard, "E" ) ) {
        bShowMenu = !bShowMenu;
    }
    if ( WasKeyPressed( pKeyboard, "Q" ) ) {
        selectCar( currentCarIndex - 1 );
    }
    if ( WasKeyPressed( pKeyboard, "S" ) ) {
        selectCar( currentCarIndex + 1 );
    }
    if ( WasKeyPressed( pKeyboard, "O" ) ) {
        writeAIConfig();
    }
    if ( WasKeyPressed( pKeyboard, "2" ) ) {
        bBrakeTest = true;
        bTurnTest = true;
        goTo( S_StartCampaign );
    }
    if ( WasKeyPressed( pKeyboard, "5" ) ) {
        startAccelCampaign();
    }
    if ( WasKeyPressed( pKeyboard, "7" ) ) {
        startBrakeCampaign();
    }
    if ( WasKeyPressed( pKeyboard, "8" ) ) {
        startTurnCampaign();
    }
    if ( WasKeyPressed( pKeyboard, "Z" ) ) {
        setGraphMode( ( graphMode + 1 ) % GRM_Count );
    }
    if ( WasKeyPressed( pKeyboard, "TREMA" ) ) {
        cycleCarFilter();
    }
}

void GMAITrainer::selectCar( const int32_t carIndex ) {
    if ( numCars <= 0 || getCurrentState() != S_Idle ) {
        return;
    }

    currentCarIndex = ( carIndex + numCars ) % numCars;
    despawnCar();
    spawnCar( carList[currentCarIndex] );
    selectedRecord = findRecordIndex( carList[currentCarIndex] );
}
