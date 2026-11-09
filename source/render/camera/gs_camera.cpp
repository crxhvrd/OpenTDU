#include "shared.h"
#include "gs_camera.h"

#include "camera_base.h"
#include "tdu_instance.h"

#include "config/gs_config.h"
#include "filesystem/gs_file.h"

#include "world/gs_world.h"
#include "world/gs_world_streaming.h"
#include "world/streaming_manager.h"

#include "camera_base.h"
#include "camera_mouse.h"
#include "camera_focus_object.h"
#include "camera_gps.h"
#include "camera_steady_cam.h"
#include "camera_gme.h"
#include "camera_showcase.h"
#include "camera_path.h"
#include "camera_road_follow.h"
#include "camera_replay_road.h"
#include "camera_bike_driver.h"
#include "camera_cinematic.h"
#include "camera_vehicle.h"

#include "database/gs_database.h"
#include "game/vehicle/car_state.h"
#include "game/vehicle/gs_car_state.h"
#include "game/scenaric/gs_scenaric.h"

#include "game/mng_hud.h"
#include "flash/implementation/flash_player.h"
#include "game/gm_world.h"

#include "core/maths_helper.h"
#include "render/postfx/gs_postfx.h"

#include "input/input_device.h"
#include "input/ed_dev.h"

#include "physics/gs_physics.h"
#include "physics/replay.h"

#include "flash/gs_flash.h"

#include "gs_timer.h"

GSCamera gCamera = {};        // DAT_013a6750
GSCamera *gpCamera = nullptr; // DAT_010e747c

float gCamViewLockHoldTime = 0.0f; // DAT_010e761c

// DAT_00f7c718
static constexpr uint32_t DAT_00f7c718[9][2] = {
    {0x102, 0x5DC},
    {0x413, 0x5DD},
    {0x103, 0x5DE},
    {0x11A, 0x5DF},
    {0x11B, 0x5E0},
    {0x110, 0x5E1},
    {0xF7,  0x5E2},
    {0x10F, 0x5E3},
    {0x10E, 0x5E4}
};

// DAT_00fab290
static int32_t DAT_00fab290[4] = { -1, -1, 0x270E, 0x270D };
static constexpr float FLOAT_00fab2a0 = 0.0099999998f; // DAT_00fab2a0
static int32_t DAT_00fab2ac = 0xffffffff;              // DAT_00fab2ac
static constexpr float FLOAT_00fab2a8 = 0.2f;          // DAT_00fab2a8
static constexpr float FLOAT_00fab2a4 = 1.0f;          // DAT_00fab2a4

GSCamera::GSCamera()
    : GameSystem(),
      nearPlane( 0.5f ),
      farPlane( 40000.0f ),
      nearPlaneInterior( 0.5f ),
      farPlaneInterior( 40000.0f ),
      deadZone( 0.0f ),
      unknownFloat( 0.5f ),
      leftView( 0.0f ),
      rightView( 0.0f ),
      downView( 0.0f ),
      upView( 0.0f ),
      bDepthAware( false ),
      bDepthAwareInterior( false ),
      bLookingBack( false ),
      bInterior( false ),
      bUnknownBool3( false ),
      bLocked( false ),
      cameraShake( 0.0f ),
      radialBlurIntensity( 0.0f ),
      bEditing( false ),
      bPaused( false ),
      bLookAtPlayerPos( true ),
      bFinishedCamTransition( true ),
      bMouseCam( 1 ),
      bViewChangeAllowed( 0 ),
      field_0x932( 0 ),
      bUseBlur( false ),
      field_0x955( false ),
      bReplayMode( false ),
      bViewBackOnTrigger( false ),
      bViewBackOffTrigger( false ),
      bChangeCamTrigger( false ),
      bForceSwitch( false ),
      bUpdatedView( false ),
      field_0x954( false ),
      bHeadTracking( false ),
      currentCameraIndex( 0 ),
      numCameraRegistered( 0 ),
      defaultCamera(),
      activeCamera(),
      tmpCamera(),
      frustumNear(),
      frustumFar(),
      nextCameraMode( -1 ),
      pPrevCamera( nullptr ),
      pNextCamera( nullptr ),
      pActiveVehicleCamera( nullptr ),
      transitionType( 0 ),
      transitionTime( 0.0f ),
      transition(),
      pTarget( nullptr ),
      currentTarget( 0 ),
      currentTransitionIndex( 0 ),
      numTransition( 0 ),
      numVehicleEntries( 0 ),
      numVehicleCamera( 0 ),
      pTargetPhysEntity( nullptr ),
      seatPos{ 0.0f, 0.0f },
      seatMinPos{ 0.0f, 0.0f },
      seatMaxPos{ 0.0f, 0.0f },
      pCameraDBBinary( nullptr ),
      pTransitionIndices( nullptr ),
      pCameraVehicleEntries( nullptr ),
      pVehicleMem( nullptr ),
      pCameraVehicles( nullptr ),
      pVehicleCameraPtrs( nullptr ),
      field_0x96c( 0 ),
      field83_0x92c( 0 ),
      field82_0x928( 0 ),
      field_0x958( 0.5f ) {
    // FUN_008e3450
    OTDU_ASSERT( gpCamera == nullptr );

    for ( int32_t i = 0; i < kNumCamerasCameraRegister; i++ ) {
        cameraRegister[i] = nullptr;
    }

#ifdef OTDU_DEVBUILD
    bFlyCam = false;
#endif

    gpCamera = this;
}

GSCamera::~GSCamera() {
    // FUN_00411ec0
    pActiveVehicleCamera = nullptr;
    gpCamera = nullptr;
}

bool GSCamera::initialize( TestDriveGameInstance * ) {
    // FUN_008e4d50
    if ( gpTestDriveInstance->isUsingDVDVFS() == false ) {
        registerCamera<CameraMouse>( "MOUSE", 1 );              // DAT_00f143a4
        registerCamera<CameraFocusObject>( "FOCUS_OBJECT", 2 ); // DAT_00f14388
    }

    registerCamera<CameraGame>( "BASE", 0 );               // DAT_00f143ac
    registerCamera<CameraGPS>( "GPS", 3 );                 // DAT_00f14384
    registerCamera<CameraGME>( "GME", 5 );                 // DAT_00f14380
    registerCamera<CameraShowcase>( "SHOW_CASE", 6 );      // DAT_00f14374
    registerCamera<CameraPath>( "PATH", 7 );               // DAT_00f1436c
    registerCamera<CameraRoadFollow>( "ROADFOLLOW", 8 );   // DAT_00f14360
    registerCamera<CameraReplayRoad>( "REPLAYROAD", 9 );   // DAT_00f14354
    registerCamera<CameraCinematic>( "CINEMATIC", 0xa );   // DAT_00f143b4
    registerCamera<CameraBikeDriver>( "BIKEDRIVER", 0xb ); // DAT_00f14348
    registerCamera<CameraSteadyCam>( "STEADYCAM", 0xc );   // DAT_00f14398

    std::string local_80 = gpConfig->getResRootPath();
    local_80 += "DataBase/Cameras.bin";

    uint32_t uStack_84 = 0u;
    gpFile->loadFile( local_80.c_str(), &pCameraDBBinary, &uStack_84 );

    loadCameraDB( (uint8_t *)pCameraDBBinary );

    if ( 0 < numCameraRegistered ) {
        currentCameraIndex = 0;
        findAndSetCameraMode( *pTransitionIndices, 0, 1.0f );
    }

    pNextCamera = nullptr;
    nextCameraMode = -1;
    bLookingBack = false;
    seatMinPos = { -0.15f, -0.05f };
    seatMaxPos = { 0.05f, 0.01f };
    seatPos = { 0.0f, 0.0f };
    field_0x96c = 0;
    bViewChangeAllowed = 1;
    bLocked = false;
    bUpdatedView = false;

    return true;
}

void GSCamera::tick( float totalTime, float deltaTime ) {
    // FUN_008e2ac0
    if ( bLocked ) {
        return;
    }

    bool bBackViewRequested = false;
    bool bChangeViewRequested = false;
    if ( pTarget != nullptr && pTarget->getPhysicsVehicle() != nullptr ) {
        const PhysicsVehicle *pTargetVehicle = pTarget->getPhysicsVehicle();
        bBackViewRequested = pTargetVehicle->isBackViewRequested();
        bChangeViewRequested = pTargetVehicle->isChangeViewRequested();
    }

    uint8_t bVar8 = 0;
    uint32_t uVar12 = 0;
    PostFXNode *iVar11 = nullptr;
    if ( ( gpTestDriveInstance->getActiveGameMode() == GM_FreeRide ) ||
         ( gpTestDriveInstance->getActiveGameMode() == GM_Mission ) ) {
        if ( bUseBlur == false ) {
            if ( ( pActiveVehicleCamera->getFlags() >> 0x11 & 1 ) == 0 ) {
                if ( gpPostFX->shouldUpdateEffects() ||
                     ( bVar8 =
                           ( pActiveVehicleCamera->getVehicleFlags() >> 16 ),
                       gpPostFX->getEffect<ePostFXStep::PFXS_RadialBlur>() ==
                           nullptr ) )
                    goto LAB_008e2c4f;
                iVar11 = gpPostFX->getEffect<ePostFXStep::PFXS_RadialBlur>()
                             ->getInput();
                iVar11->setEnabled( bVar8 & 1 );
                goto LAB_008e2b3f;
            }
            if ( gpPostFX->shouldUpdateEffects() ||
                 gpPostFX->getEffect<ePostFXStep::PFXS_RadialBlur>() ==
                     nullptr )
                goto LAB_008e2c4f;

            gpPostFX->getEffect<ePostFXStep::PFXS_RadialBlur>()
                ->getInput()
                ->setEnabled( false );
        } else {
            if ( gpPostFX->shouldUpdateEffects() ||
                 ( gpPostFX->getEffect<ePostFXStep::PFXS_RadialBlur>() ==
                   nullptr ) )
                goto LAB_008e2c4f;
            gpPostFX->getEffect<ePostFXStep::PFXS_RadialBlur>()
                ->getInput()
                ->setEnabled( true );
        }
        if ( ( gpPostFX->getEffect<ePostFXStep::PFXS_RadialBlur>() ==
               nullptr ) ||
             ( iVar11 = gpPostFX->getEffect<ePostFXStep::PFXS_RadialBlur>()
                            ->getInput(),
               iVar11->isEnabled() == '\0' ) ) {
            if ( gpPostFX->getEffect<ePostFXStep::PFXS_FakeDOF>() != nullptr ) {
                iVar11 = gpPostFX->getEffect<ePostFXStep::PFXS_FakeDOF>()
                             ->getInput();
                goto LAB_008e2c2a;
            }
            goto LAB_008e2c34;
        }
    LAB_008e2c41:
        uVar12 =
            gpRender->getRenderPass<eRenderPass::RP_Sun>().pScene->getFlags() &
            0xffffffdf;
    } else {
        if ( gpPostFX->shouldUpdateEffects() ||
             ( gpPostFX->getEffect<ePostFXStep::PFXS_RadialBlur>() ==
               nullptr ) )
            goto LAB_008e2c4f;
        gpPostFX->getEffect<ePostFXStep::PFXS_RadialBlur>()
            ->getInput()
            ->setEnabled( false );
    LAB_008e2b3f:
        if ( ( gpPostFX->getEffect<ePostFXStep::PFXS_RadialBlur>() !=
               nullptr ) &&
             gpPostFX->getEffect<ePostFXStep::PFXS_RadialBlur>()
                 ->getInput()
                 ->isEnabled() )
            goto LAB_008e2c41;
        if ( gpPostFX->getEffect<ePostFXStep::PFXS_FakeDOF>() != nullptr ) {
            iVar11 =
                gpPostFX->getEffect<ePostFXStep::PFXS_FakeDOF>()->getInput();
        LAB_008e2c2a:
            if ( iVar11->isEnabled() )
                goto LAB_008e2c41;
        }
    LAB_008e2c34:
        uVar12 =
            gpRender->getRenderPass<eRenderPass::RP_Sun>().pScene->getFlags() |
            0x20;
    }
    gpRender->getRenderPass<eRenderPass::RP_Sun>().pScene->setFlags( uVar12 );

LAB_008e2c4f:
    PlayerConfiguration *pPVar7 = gpConfig->pPlayerConfig;
    if ( bEditing == false ) {
        if ( kCamViewLockHoldThreshold < gCamViewLockHoldTime ) {
            if ( pActiveVehicleCamera != nullptr &&
                 ( ( pActiveVehicleCamera->getFlags() >> 0x11 ) & 1 ) == 0 ) {
                ToggleFreeLookLock( pActiveVehicleCamera );
                FlashPlayer *pHudPlayer = ( gpMngHud != nullptr )
                                              ? gpMngHud->getHudPlayer()
                                              : nullptr;
                if ( pHudPlayer != nullptr ) {
                    const char *pFrame = nullptr;
                    if ( ( pActiveVehicleCamera->getVehicleFlags() & 1 ) ==
                         0 ) {
                        pFrame = gbFreeLookUnlocked ? "unlocked" : "locked";
                    } else {
                        pFrame = gbFixedCamLookSnap ? "speed_up" : "speed_down";
                    }
                    pHudPlayer->showFrame( pFrame, "/HUD/cam_ic", false );
                }
            }
            gCamViewLockHoldTime = -1.0f;
        }

        updateCameraInput( pPVar7->pCarHeadLeft, leftView, false );
        updateCameraInput( pPVar7->pCarHeadRight, rightView, true );
        updateCameraInput( pPVar7->pCarHeadUp, upView, true );
        updateCameraInput( pPVar7->pCarHeadDown, downView, false );

        float fVar18 = 0.0f;
        DeviceAction *peVar5 = pPVar7->pCarHeadCenter;
        float fVar24 = field_0x958 * FLOAT_00fab2a8;
        float fVar23 = field_0x958 * FLOAT_00fab2a4;
        if ( ( ( peVar5 != nullptr ) && ( peVar5->NumAddedChannels != 0 ) ) &&
             ( ( peVar5->Flags & 8 ) != 0 ) ) {
            field_0x954 = true;
            leftView = 0.0f;
            rightView = 0.0f;
            upView = 0.0f;
            downView = 0.0f;
            bUpdatedView = true;
        }

        int32_t channelIndex = 0;
        Device *peVar9 = gEdDev.getDevice( eDeviceType::DT_Mouse, 0 );

        float fVar19 = 0.0f;
        float fVar20 = 0.0f;
        float fVar21 = 0.0f;
        float fVar22 = 0.0f;
        bool bVar8 = false;
        if ( peVar9 != nullptr ) {
            fVar19 = peVar9->getChannelValue( "RIGHT" );
            fVar20 = peVar9->getChannelValue( "UP" );
            fVar21 = peVar9->getChannelValue( "LEFT" );
            fVar22 = peVar9->getChannelValue( "DOWN" );

            bVar8 = peVar9->getChannelValueBinary( "BUTTON_0" );
        }

        if ( ( ( ( ( FLOAT_00fab2a0 < fVar21 ) ||
                   ( FLOAT_00fab2a0 < fVar19 ) ) ||
                 ( FLOAT_00fab2a0 < fVar20 ) ) ||
               ( FLOAT_00fab2a0 < fVar22 ) ) &&
             ( ( ( gpActiveGM != nullptr ||
                   ( gpFlash->getMouse()->ActiveCursorType ==
                     eFlashCursorType::FCT_Camera ) ) &&
                 ( bVar8 != 0 ) ) ) ) {
            bUpdatedView = true;
        }

        if ( ( bUpdatedView != false ) && ( bVar8 != 0 ) ) {
            if ( fVar18 <= rightView ) {
                leftView = ( fVar21 - fVar19 ) * fVar24 + leftView;
            }
            if ( leftView <= fVar18 ) {
                rightView = ( fVar21 - fVar19 ) * fVar24 + rightView;
            }
            if ( field_0x955 == '\0' ) {
                if ( fVar18 <= downView ) {
                    upView = upView - ( fVar20 - fVar22 ) * fVar23;
                }
                if ( upView <= fVar18 ) {
                    downView = downView - ( fVar20 - fVar22 ) * fVar23;
                }
            } else {
                if ( fVar18 <= downView ) {
                    upView = ( fVar20 - fVar22 ) * fVar23 + upView;
                }
                if ( upView <= fVar18 ) {
                    downView = ( fVar20 - fVar22 ) * fVar23 + downView;
                }
            }
            fVar23 = 1.0f - deadZone;
            if ( fVar23 < leftView ) {
                leftView = fVar23;
            }
            if ( leftView <= fVar18 && fVar18 != leftView ) {
                leftView = fVar18;
            }
            fVar24 = fVar18 - fVar23;
            if ( rightView <= fVar24 && fVar24 != rightView ) {
                rightView = fVar24;
            }
            if ( fVar18 < rightView ) {
                rightView = fVar18;
            }
            if ( fVar23 < upView ) {
                upView = fVar23;
            }
            if ( upView <= fVar18 && fVar18 != upView ) {
                upView = fVar18;
            }
            if ( fVar18 < downView ) {
                downView = fVar18;
            }
            if ( downView <= fVar24 && fVar24 != downView ) {
                downView = fVar24;
            }
        }

        if ( ( pActiveVehicleCamera->getType() != 5 ) &&
             ( bInterior == false ) && bChangeViewRequested &&
             ( bLookingBack == false ) ) {
            bChangeCamTrigger = true;
        }

        peVar5 = pPVar7->pCarHeadLeft;
        DeviceAction *peVar6 = pPVar7->pCarHeadRight;
        const bool bHeadLookBack =
            ( peVar5 != nullptr ) && ( peVar5->NumAddedChannels != 0 ) &&
            ( ( peVar5->Flags & 1 ) != 0 ) && ( peVar6 != nullptr ) &&
            ( peVar6->NumAddedChannels != 0 ) && ( ( peVar6->Flags & 1 ) != 0 );
        if ( ( bHeadLookBack == false ) && ( bBackViewRequested == false ) ) {
            if ( ( bLookingBack != false ) && ( bChangeCamTrigger == false ) ) {
                bViewBackOffTrigger = true;
            }
        } else if ( ( bLookingBack == false ) &&
                    ( bChangeCamTrigger == false ) ) {
            bViewBackOnTrigger = true;
        }

        int32_t pauseStep = field_0x96c;
        if ( pauseStep != 0 ) {
            bPaused = (bool)( bPaused ^ 1 );
            if ( pauseStep == 1 ) {
                bPaused = false;
            } else if ( pauseStep == 2 ) {
                bPaused = true;
            }
            if ( bPaused == false ) {
                gpTimer->GameSpeed = gpTimer->PreviousGameSpeed;
                fVar18 = gpTimer->GameSpeed;
            } else if ( ( ( gpTimer->PreviousGameSpeed ==
                            gpTimer->GameSpeed ) ) &&
                        ( ( gpTimer->GameSpeed == fVar18 ) ) ) {
                gpTimer->PreviousGameSpeed = gpTimer->GameSpeed;
            }
            gpTimer->GameSpeed = fVar18;
            if ( field_0x96c == 1 ) {
                field_0x96c = 2;
            } else if ( field_0x96c == 2 ) {
                field_0x96c = 0;
            }
        }
    }

    if ( bEditing && pActiveVehicleCamera != nullptr ) {
        editActiveCamera();
    }

    const Matrix4x4f_t &camToWorld = pActiveVehicleCamera->getCamToWorld();

    float fVar17 = atan2f( camToWorld( 1, 2 ), camToWorld( 2, 2 ) );
    angleX.x() = fVar17;
    float fVar18;
    if ( fVar17 < (float)-EIGEN_HALF_PI ) {
        float fVar18 = angleX.x();
        do {
            fVar18 = fVar18 + (float)EIGEN_PI;
        } while ( fVar18 < (float)-EIGEN_HALF_PI );
        angleX.x() = fVar18;
    }
    fVar18 = angleX.x();
    if ( (float)EIGEN_HALF_PI < fVar18 ) {
        do {
            fVar18 = fVar18 - (float)EIGEN_PI;
        } while ( (float)EIGEN_HALF_PI < fVar18 );
        angleX.x() = fVar18;
    }

    angleX.y() = abs( angleX.x() );

    if ( seatPos.x() <= seatMinPos.x() && seatMinPos.x() != seatPos.x() ) {
        seatPos.x() = seatMinPos.x();
    }

    if ( seatPos.y() <= seatMinPos.y() && seatMinPos.y() != seatPos.y() ) {
        seatPos.y() = seatMinPos.y();
    }

    if ( seatMaxPos.x() <= seatPos.x() && seatPos.x() != seatMaxPos.x() ) {
        seatPos.x() = seatMaxPos.x();
    }

    if ( seatMaxPos.y() <= seatPos.y() && seatPos.y() != seatMaxPos.y() ) {
        seatPos.y() = seatMaxPos.y();
    }
}

void GSCamera::terminate() {
    // FUN_008e2310
    if ( pActiveVehicleCamera != nullptr ) {
        pActiveVehicleCamera->destroy();
        pActiveVehicleCamera = nullptr;
    }

    for ( int32_t i = 0; i < kNumCamerasCameraRegister; i++ ) {
        CameraGame *pCamera = cameraRegister[i];
        if ( pCamera != nullptr ) {
            pCamera->destroy();
            pCamera->~CameraGame();
            // OTDU_ADDITION: registerCamera() placement-news the cameras into
            // TestDrive::AllocAligned memory; free() on that pointer corrupts
            // the heap (0xC0000374 on quit).
            TestDrive::FreeAligned( pCamera );
            pCamera = nullptr;
        }
    }

    if ( pCameraVehicles != nullptr ) {
        TestDrive::FreeAligned( pVehicleMem );
    }

    // Native allocations replacing the former on-disk-overlaid camera arrays.
    if ( pCameraVehicleEntries != nullptr ) {
        TestDrive::Free( pCameraVehicleEntries );
        pCameraVehicleEntries = nullptr;
    }
    if ( pVehicleCameraPtrs != nullptr ) {
        TestDrive::Free( pVehicleCameraPtrs );
        pVehicleCameraPtrs = nullptr;
    }

    pVehicleMem = nullptr;
    pCameraVehicles = nullptr;
    numVehicleCamera = 0;
    numCameraRegistered = 0;
    if ( pCameraDBBinary != nullptr ) {
        TestDrive::Free( pCameraDBBinary );
    }
}

void GSCamera::pause() {
    // FUN_008e1f10
    bMouseCam = true;
}

void GSCamera::reset() {
    // FUN_008e4cb0
    if ( 0 < numCameraRegistered ) {
        field82_0x928 = 0;
        findAndSetCameraMode( *pTransitionIndices, 0, 1.0f );
    }
    pNextCamera = nullptr;
    bLookingBack = false;
    nextCameraMode = -1;
    seatMinPos = { -0.15f, -0.05f };
    seatMaxPos = { 0.05f, 0.01f };
    seatPos = Eigen::Vector2f::Zero();

    bLocked = false;
}

void GSCamera::setSeatPosition( float seatHeight, float seatPosition ) {
    // FUN_008e1c90
    float fVar2 = 0.0f;
    if ( seatPosition <= 0.0f ) {
        seatPos.x() = 0.0f - seatMinPos.x() * seatPosition;
    } else {
        seatPos.x() = seatMaxPos.x() * seatPosition;
    }

    if ( seatHeight <= 0.0f ) {
        fVar2 = 0.0f - seatMinPos.y() * seatHeight;
    } else {
        fVar2 = seatMaxPos.y() * seatHeight;
    }

    seatPos.y() = fVar2;
    fVar2 = seatMinPos.x();
    if ( seatPos.x() <= fVar2 && fVar2 != seatPos.x() ) {
        seatPos.x() = seatMinPos.x();
    }
    fVar2 = seatMinPos.y();
    if ( seatPos.y() <= fVar2 && fVar2 != seatPos.y() ) {
        seatPos.y() = seatMinPos.y();
    }

    fVar2 = seatPos.x();
    if ( seatMaxPos.x() <= fVar2 && fVar2 != seatMaxPos.x() ) {
        seatPos.x() = seatMaxPos.x();
    }

    fVar2 = seatPos.y();
    if ( seatMaxPos.y() <= fVar2 && fVar2 != seatMaxPos.y() ) {
        seatPos.y() = seatMaxPos.y();
    }
}

void GSCamera::setCameraMode( const int32_t mode,
                              const int32_t transition,
                              const float time ) {
    nextCameraMode = mode;
    transitionType = transition;
    transitionTime = time;
    pNextCamera = nullptr;
}

CameraGame *GSCamera::findCameraWithMode( const int32_t param_3,
                                          CarState *param_2 ) {
    // FUN_008e25e0
    DAT_00fab290[0] = -1;
    DAT_00fab290[1] = -1;

    if ( param_2 != nullptr ) {
        DBEntryCarPhysicsData *pDVar5 =
            gpDatabase->getCarDBEntry( param_2->getCarID() );
        if ( pDVar5 != nullptr ) {
            DAT_00fab290[0] = pDVar5->CameraID;
            DAT_00fab290[1] = ( param_2->getNumWheels() == 2 ) + 9999;
        }
    }

    CameraDBVehicleEntry *pCVar2 = pCameraVehicleEntries;
    int32_t iVar3 = numVehicleEntries;
    CameraDBVehicleEntry *pCVar4 = pCameraVehicleEntries;
    int32_t piVar7 = 0; // DAT_00fab290;

    do {
        while ( pCVar2 + iVar3 <= pCVar4 ) {
            piVar7++;
            pCVar4 = pCameraVehicleEntries;
            if ( piVar7 > 3 ) {
                return nullptr;
            }
        }

        CameraGame **ppBVar6 = pCVar4->pCameras;
        if ( ppBVar6 != nullptr && pCVar4->CarIndex == DAT_00fab290[piVar7] ) {
            int32_t iVar7 = 0;
            for ( int32_t iVar5 = 0; iVar5 < pCVar4->NumCameras; iVar5++ ) {
                CameraGame *pCameraIt = ppBVar6[iVar5];
                // Null-check BEFORE the deref (the original checked after, at
                // the return). A camera slot can resolve to null now that the
                // camera DB's x86Pointer entries are no longer masked by a
                // densely populated gPointerRegister.
                if ( pCameraIt != nullptr &&
                     pCameraIt->cameraType == param_3 ) {
                    if ( iVar7 == 0 ) {
                        return pCameraIt;
                    }
                    iVar7++;
                }
            }
        }

        pCVar4 = pCVar4 + 1;
    } while ( true );
}

void GSCamera::findAndSetCameraMode( int param_1,
                                     int32_t param_2,
                                     float param_3 ) {
    // FUN_008e41e0
    CameraGame *pBVar1 = findCameraWithMode( param_1, pTarget );
    if ( pBVar1 != nullptr ) {
        bindCamera( pBVar1, param_2, param_3 );
        pActiveVehicleCamera->tick( 0.016666668f, true, true );
    }
}

CameraGame *GSCamera::findCameraByCurrentTransition() {
    int32_t index = ( field83_0x92c < numTransition )
                        ? pTransitionIndices[field83_0x92c]
                        : -1;
    return findCameraWithMode( index, pTarget );
}

static constexpr uint32_t kSeatAdjustableFlag = 0x40000u;

int32_t GSCamera::findFirstVisibleTransitionMode() {
    // FUN_008e3f00
    for ( uint32_t i = 0; i < numTransition; i++ ) {
        CameraGame *pCamera =
            findCameraWithMode( pTransitionIndices[i], pTarget );
        if ( ( pCamera != nullptr ) &&
             ( ( pCamera->getFlags() & 0xd0000 ) == 0 ) ) {
            return pTransitionIndices[i];
        }
    }
    return -1;
}

int32_t GSCamera::findFirstHoodTransitionMode() {
    // FUN_008e3f50
    for ( uint32_t i = 0; i < numTransition; i++ ) {
        CameraGame *pCamera =
            findCameraWithMode( pTransitionIndices[i], pTarget );
        if ( ( pCamera != nullptr ) &&
             ( ( pCamera->getFlags() >> 0x13 & 1 ) != 0 ) ) {
            return pTransitionIndices[i];
        }
    }
    return -1;
}

int32_t GSCamera::findFirstSeatAdjustableTransitionMode() {
    // FUN_008e3fa0
    for ( uint32_t i = 0; i < numTransition; i++ ) {
        CameraGame *pCamera =
            findCameraWithMode( pTransitionIndices[i], pTarget );
        if ( ( pCamera != nullptr ) &&
             ( ( pCamera->getFlags() & kSeatAdjustableFlag ) != 0 ) ) {
            return pTransitionIndices[i];
        }
    }
    return -1;
}

void GSCamera::setTransitionCameraMode( const int32_t mode ) {
    // FUN_008e2030
    for ( uint32_t i = 0; i < numTransition; i++ ) {
        if ( pTransitionIndices[i] != mode ) {
            continue;
        }

        field83_0x92c = i;
        nextCameraMode = mode;
        pNextCamera = nullptr;
        transitionType = 0;
        transitionTime = 1.0f;
    }
}

CameraMouse *GSCamera::getMouseCamera() {
    CameraMouse *pFlyCam = nullptr;
    for ( uint32_t i = 0; i < numCameraRegistered; i++ ) {
        if ( cameraRegister[i] != nullptr &&
             cameraRegister[i]->getType() == 1 ) {
            pFlyCam = (CameraMouse *)cameraRegister[i];
            break;
        }
    }
    return pFlyCam;
}

CameraGame *GSCamera::findCamera( CarState *pState ) {
    // FUN_008e3a50
    for ( uint32_t iVar2 = 0; iVar2 < numTransition; iVar2++ ) {
        CameraGame *pBVar1 =
            findCameraWithMode( pTransitionIndices[iVar2], pState );
        if ( ( pBVar1 != nullptr ) &&
             ( ( pBVar1->getFlags() >> 0x12 & 1 ) != 0 ) ) {
            return pBVar1;
        }
    }
    return nullptr;
}

void GSCamera::restoreDefaultVehicleCamera( CarState *pCar ) {
    CameraGame *pChase = findCamera( pCar );
    if ( pChase != nullptr ) {
        setCameraMode( pChase->getType(), 0, 0.0f );
    }
}

void GSCamera::restoreCamera() {
    // FUN_008e2200
    if ( pTransitionIndices != nullptr && field83_0x92c < numTransition ) {
        nextCameraMode = pTransitionIndices[field83_0x92c];
    }
    pNextCamera = nullptr;
    transitionType = 0;
    transitionTime = 1.0f;
    bLookingBack = false;
    if ( pActiveVehicleCamera != nullptr &&
         ( pActiveVehicleCamera->getFlags() >> 0x11 & 1 ) == 0 ) {
        pActiveVehicleCamera->resetTransform();
    }
}

void GSCamera::bindCamera( CameraGame *param_1,
                           int32_t param_2,
                           float param_3 ) {
    // FUN_008e4120
    CameraGame *pBVar1 = pActiveVehicleCamera;
    pPrevCamera = pBVar1;
    if ( pBVar1 != nullptr ) {
        pBVar1->destroy();
    }

    if ( param_2 == 0 ) {
        pActiveVehicleCamera = (CameraVehicle *)param_1;
        bFinishedCamTransition = true;
        param_1->initialize();
        pActiveVehicleCamera->markAsInitialized();
        bUpdatedView = false;
        return;
    }

    CameraGame local_180( pActiveVehicleCamera );
    tmpCamera = local_180;

    transition.setCameras( &tmpCamera, param_1 );
    transition.initialize();
    transition.begin( param_3, param_2 );

    pActiveVehicleCamera = (CameraVehicle *)&transition;
    bUpdatedView = false;
}

bool GSCamera::updateActiveCamera( float deltaTime, float accTime ) {
    // FUN_008e45d0
    if ( pActiveVehicleCamera == nullptr ) {
        return false;
    }

    updateReplayCameraSwitch( deltaTime );
    (void)accTime;

    if ( pActiveVehicleCamera == nullptr || bLocked ) {
        return true;
    }

    Eigen::Vector3f previousPosition =
        pActiveVehicleCamera->getWorldPositionOffset();

    const bool bJumped = pActiveVehicleCamera->tick( deltaTime, false, false );

    const Eigen::Vector3f currentPosition =
        pActiveVehicleCamera->getWorldPositionOffset();

    const bool bInterpolationReset =
        deltaTime == 0.0f || pActiveVehicleCamera->bInitialized;
    if ( deltaTime == 0.0f || bJumped || pActiveVehicleCamera->bInitialized ) {
        previousPosition = currentPosition;
        pActiveVehicleCamera->bInitialized = false;
    }
    latchRenderInterpolation( bInterpolationReset );

    if ( deltaTime <= 0.0f ) {
        pActiveVehicleCamera->setVelocity( Eigen::Vector4f::Zero() );
    } else {
        const Eigen::Vector3f delta =
            ( currentPosition - previousPosition ) / deltaTime;
        pActiveVehicleCamera->setVelocity(
            Eigen::Vector4f( delta.x(), delta.y(), delta.z(), 0.0f ) );
    }

    if ( ( ( pActiveVehicleCamera->getFlags() >> 0x11 ) & 1 ) == 0 &&
         pTargetPhysEntity != nullptr && pTargetPhysEntity->isActive() ) {
        pActiveVehicleCamera->setVelocity(
            pTargetPhysEntity->getChassisVelocity() );
    }

    return true;
}

void GSCamera::latchRenderInterpolation( bool bSnap ) {
    static constexpr float kMaxStepDistance = 5.0f;

    RenderInterpolationPose pose;
    pose.Position = pActiveVehicleCamera->getWorldPositionOffset();
    pose.Target = pActiveVehicleCamera->getTargetOffset();
    pose.Up = pActiveVehicleCamera->getUp();

    if ( !bSnap && pInterpolationCamera == pActiveVehicleCamera &&
         ( pose.Position - interpolationPoses[1].Position ).squaredNorm() <
             kMaxStepDistance * kMaxStepDistance ) {
        interpolationPoses[0] = interpolationPoses[1];
    } else {
        interpolationPoses[0] = pose;
    }
    interpolationPoses[1] = pose;
    pInterpolationCamera = pActiveVehicleCamera;
    bInterpolationValid = true;
}

void GSCamera::applyRenderInterpolation( float alpha ) {
    static constexpr float kMatchTolerance = 1.0e-3f;

    if ( alpha >= 1.0f || !bInterpolationValid || bFlyCam ||
         bCinematicCameraOverride ||
         pInterpolationCamera != pActiveVehicleCamera ) {
        return;
    }

    const RenderInterpolationPose &previous = interpolationPoses[0];
    const RenderInterpolationPose &current = interpolationPoses[1];
    if ( ( defaultCamera.getWorldPositionOffset() - current.Position )
                 .squaredNorm() > kMatchTolerance * kMatchTolerance ||
         ( defaultCamera.getTargetOffset() - current.Target ).squaredNorm() >
             kMatchTolerance * kMatchTolerance ) {
        bInterpolationValid = false;
        return;
    }

    const Eigen::Vector3f position =
        previous.Position + ( current.Position - previous.Position ) * alpha;
    const Eigen::Vector3f target =
        previous.Target + ( current.Target - previous.Target ) * alpha;
    Eigen::Vector3f up = previous.Up + ( current.Up - previous.Up ) * alpha;
    if ( up.squaredNorm() > 0.0f ) {
        up.normalize();
    } else {
        up = current.Up;
    }

    defaultCamera.setPositionOffseted(
        Eigen::Vector4f( position.x(), position.y(), position.z(), 1.0f ) );
    defaultCamera.setTargetOffseted(
        Eigen::Vector4f( target.x(), target.y(), target.z(), 1.0f ) );
    defaultCamera.setUp( Eigen::Vector4f( up.x(), up.y(), up.z(), 0.0f ) );
}

void GSCamera::tickActiveCamera( float param_2 ) {
    // FUN_008e48a0
    CarState *local_4 = ( gpScenaric != nullptr )
                            ? gpScenaric->getLocalPlayer().getCarState()
                            : nullptr;
    if ( local_4 == nullptr ) {
        local_4 = gpCarState->getCarStates();
    }

    CarState *pCVar4 = pTarget;
    if ( bMouseCam ) {
        pCVar4 = local_4;
    }

    if ( ( pCVar4 != pTarget ) ||
         ( ( ( pCVar4 != nullptr && ( pTarget != nullptr ) ) &&
             ( pCVar4->getCarID() != carID ) ) ) ) {
        if ( pCVar4->getResource() == nullptr ) {
            pTarget = nullptr;
            bLookingBack = false;
        } else {
            carID = pCVar4->getCarID();
            pTarget = pCVar4;
            pTargetPhysEntity = pCVar4->getPhysicsVehicle();

            uint32_t uVar1 = 0;
            if ( bReplayMode != false ) {
                if ( ( ( pCVar4 != local_4 ) &&
                       ( uVar1 = pActiveVehicleCamera->getFlags(),
                         ( uVar1 >> 0x11 & 1 ) == 0 ) ) &&
                     ( ( uVar1 >> 0x12 & 1 ) != 0 ) ) {
                    DAT_00fab2ac = 1;
                    findAndSetCameraMode( pTransitionIndices[2], 0, 1.0f );
                }
                if ( ( -1 < DAT_00fab2ac ) && ( pCVar4 == local_4 ) ) {
                    findAndSetCameraMode( pTransitionIndices[DAT_00fab2ac], 0,
                                          1.0f );
                    DAT_00fab2ac = -1;
                }
            }
            findAndSetCameraMode( pActiveVehicleCamera->getType(), 0, 1.0f );
        }
    }
    if ( ( nextCameraMode < 0 ) && ( pNextCamera == nullptr ) ) {
        if ( bForcePlayerCamCockpit ) {
            CameraGame *pBVar3 = nullptr;
            uint32_t uVar1 = pActiveVehicleCamera->getFlags();
            if ( ( ( ( ( uVar1 >> 0x11 & 1 ) == 0 ) &&
                     ( ( uVar1 >> 0x12 & 1 ) == 0 ) ) &&
                   ( pTarget != nullptr ) ) &&
                 ( pBVar3 = findCamera( pTarget ), pBVar3 != nullptr ) ) {
                bindCamera( pBVar3, 0, 1.0f );
            }
            bChangeCamTrigger = false;
        }

        int32_t iVar2 = 0;
        if ( ( bViewChangeAllowed != '\0' ) &&
             ( ( ( ( pActiveVehicleCamera->getFlags() >> 0x11 & 1 ) == 0 ||
                   ( iVar2 = pActiveVehicleCamera->getType(), iVar2 == 1 ) ) ||
                 ( ( iVar2 == 2 || ( iVar2 == 0xc ) ) ) ) ) ) {
            if ( ( bChangeCamTrigger != false ) && ( bLookingBack == false ) ) {
                if ( gpMngHud != nullptr ) {
                    gpMngHud->FUN_006e6a00( pTarget );
                }
                FUN_008e4840( 0, 1.0f );

                if ( ( pActiveVehicleCamera->getFlags() >> 0x11 & 1 ) == 0 ) {
                    pActiveVehicleCamera->resetTransform();
                }
                pActiveVehicleCamera->tick( param_2, true, true );

                bFinishedCamTransition = true;
                bLookingBack = false;
                FUN_008e1e80( false, false );
                bChangeCamTrigger = false;
            }

            CameraGame *pBVar3 = nullptr;
            if ( ( ( bViewBackOnTrigger ) && ( bChangeCamTrigger == false ) ) &&
                 ( pBVar3 = findCameraWithMode(
                       pActiveVehicleCamera->getBackivewType(), pTarget ),
                   pBVar3 != nullptr ) ) {
                pBVar3->initialize();
                bindCamera( pBVar3, 0, 1.0f );
                pActiveVehicleCamera->tick( param_2, true, true );
                bFinishedCamTransition = true;
                bLookingBack = true;
                FUN_008e1e80( false, false );
                bViewBackOnTrigger = false;
            }
            if ( ( ( bViewBackOffTrigger != false ) &&
                   ( bChangeCamTrigger == false ) ) &&
                 ( pBVar3 = findCameraWithMode(
                       pActiveVehicleCamera->getBackivewType(), pTarget ),
                   pBVar3 != nullptr ) ) {
                bindCamera( pBVar3, 0, 1.0f );
                pActiveVehicleCamera->tick( param_2, true, true );
                bFinishedCamTransition = true;
                bLookingBack = false;
                FUN_008e1e80( false, false );
                bViewBackOffTrigger = false;
                return;
            }
        }
    } else {
        timer = 99.0f;
        bReplayMode = false;
        bDisableInteriorView = false;
        bNeedSwitch = true;
        activeModeVehicle = -1;
        activeMode = -1;
        timerToSwitch = 0.0f;
        activeModeOverride = -1;
        activeModeVehicleOverride = -1;
        if ( pNextCamera == nullptr ) {
            findAndSetCameraMode( nextCameraMode, transitionType,
                                  transitionTime );
        } else {
            bindCamera( pNextCamera, transitionType, transitionTime );

            pActiveVehicleCamera->tick( 0.016666668f, true, true );
        }

        if ( ( pActiveVehicleCamera->getFlags() >> 0x11 & 1 ) == 0 ) {
            pActiveVehicleCamera->resetTransform();
        }

        nextCameraMode = -1;
        pNextCamera = nullptr;
        bChangeCamTrigger = false;
        bLookingBack = false;
        bViewBackOnTrigger = false;
        bViewBackOffTrigger = false;
    }
}

void GSCamera::onWorldOriginStepped() {
    if ( pActiveVehicleCamera != nullptr ) {
        pActiveVehicleCamera->rebaseToStreamOrigin();
    }
    defaultCamera.rebaseToStreamOrigin();
    activeCamera.rebaseToStreamOrigin();
}

void GSCamera::bindToRender() {
    // FUN_008e3b10
    float fVar1 = (float)gWindowWidth;
    if ( fVar1 < 0 ) {
        fVar1 = fVar1 + 4.2949673e+09f;
    }

    float fVar2 = (float)gWindowHeight;
    if ( fVar2 < 0 ) {
        fVar2 = fVar2 + 4.2949673e+09f;
    }

    defaultCamera.setAspectRatio( fVar1, fVar2 );
    if ( bDepthAware ) {
        gpRender->setupScenes( nearPlane, farPlane );
    }
    if ( bDepthAwareInterior ) {
        gpRender->setupInteriorScenes( nearPlaneInterior, farPlaneInterior );
    }

    frustumNear = gpRender->getRenderPass<eRenderPass::RP_Near>()
                      .pScene->getFrustumWrite();
    frustumFar = gpRender->getRenderPass<eRenderPass::RP_Back>()
                     .pScene->getFrustumWrite();

    activeCamera = defaultCamera;
    activeCamera.updateMatrix( activeCamera.getWorldPositionOffset(),
                               activeCamera.getUp(),
                               activeCamera.getTargetOffset() );

    applyCinematicOverride( activeCamera );

    // Copy activeCamera into cameraMain so all render scenes that reference
    // &cameraMain receive the correct per-frame camera transform.
    // (Original: edCamera::operator=(gGSRender.pScenes[0x2b].pCamera,
    // &activeCamera))
    *gpRender->getRenderPass<eRenderPass::RP_Near>().pScene->getCamera() =
        &activeCamera;
    gpRender->updateFovVsLOD( gpRender->getRenderPass<eRenderPass::RP_Near>()
                                  .pScene->getCamera()
                                  ->getTanHalfFov() );
}

void GSCamera::applyCinematicOverride( CameraGame &camera ) {
    if ( !bCinematicCameraOverride ) {
        return;
    }

    camera.setPositionOffseted(
        Eigen::Vector4f( cinematicCamPosition.x(), cinematicCamPosition.y(),
                         cinematicCamPosition.z(), 1.0f ) );
    camera.setTargetOffseted( Eigen::Vector4f( cinematicCamTarget.x(),
                                               cinematicCamTarget.y(),
                                               cinematicCamTarget.z(), 1.0f ) );
    camera.setUp( Eigen::Vector4f( cinematicCamUp.x(), cinematicCamUp.y(),
                                   cinematicCamUp.z(), 0.0f ) );
    camera.setFOV( cinematicCamFov );
    camera.updateMatrix( cinematicCamPosition, cinematicCamUp,
                         cinematicCamTarget );
}

void GSCamera::setCinematicCameraOverride( const Eigen::Vector3f &position,
                                           const Eigen::Vector3f &up,
                                           const Eigen::Vector3f &target,
                                           float fov ) {
    cinematicCamPosition = position;
    cinematicCamUp = up;
    cinematicCamTarget = target;
    cinematicCamFov = fov;
    bCinematicCameraOverride = true;
}

void GSCamera::clearCinematicCameraOverride() {
    bCinematicCameraOverride = false;
}

#ifdef OTDU_USE_IMGUI
void GSCamera::drawDebug( float deltaTime ) {
    ImGui::DragFloat( "Near Plane", &nearPlane, 0.5f, 0.05f, 5000.0f );
    ImGui::DragFloat( "Far Plane", &farPlane, 5.0f, 0.05f, 50000.0f );

    ImGui::DragFloat( "Indoor Near Plane", &nearPlaneInterior, 0.5f, 0.05f,
                      5000.0f );
    ImGui::DragFloat( "Indoor Far Plane", &farPlaneInterior, 5.0f, 0.05f,
                      50000.0f );

#ifdef OTDU_DEVBUILD
    bool bFlyCamUI = bFlyCam;
    if ( ImGui::Checkbox( "Free-fly camera (debug)", &bFlyCamUI ) ) {
        toggleFreeCam();
    }
#endif

    if ( numCameraRegistered > 0 ) {
        ImGui::SeparatorText( "Force Camera (debug)" );
        static uint32_t sSelectedCamera = 0;
        if ( sSelectedCamera >= numCameraRegistered ) {
            sSelectedCamera = 0;
        }

        CameraGame *pSelected = cameraRegister[sSelectedCamera];
        if ( ImGui::BeginCombo( "Camera Override",
                                pSelected ? pSelected->getName() : "-" ) ) {
            for ( uint32_t i = 0; i < numCameraRegistered; i++ ) {
                CameraGame *pCamera = cameraRegister[i];
                if ( pCamera == nullptr ) {
                    continue;
                }
                char label[64];
                snprintf( label, sizeof( label ), "%s (type %d)",
                          pCamera->getName(), pCamera->getType() );
                if ( ImGui::Selectable( label, i == sSelectedCamera ) ) {
                    sSelectedCamera = i;
                }
            }
            ImGui::EndCombo();
        }

        ImGui::BeginDisabled( pSelected == nullptr || bFlyCam );
        if ( ImGui::Button( "Bind" ) ) {
            forceDebugCamera( pSelected );
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled( pPreDebugCamera == nullptr );
        if ( ImGui::Button( "Restore" ) ) {
            restoreDebugCamera();
        }
        ImGui::EndDisabled();
    }

    if ( pActiveVehicleCamera != nullptr ) {
        ImGui::SeparatorText( "Active Camera" );
        ImGui::Text( "Name: '%s'", pActiveVehicleCamera->name.c_str() );
        const uint32_t showFlags = pActiveVehicleCamera->getFlags();
        ImGui::Text( "type %d  ShowFlags 0x%05x  vehFlags 0x%05x  fov %.1fdeg  "
                     "near %.2f far %.1f",
                     pActiveVehicleCamera->getType(), showFlags,
                     pActiveVehicleCamera->getVehicleFlags(),
                     pActiveVehicleCamera->getFOV() * 57.29578f,
                     pActiveVehicleCamera->getNearPlane(),
                     pActiveVehicleCamera->getFarPlane() );
        const bool bit18 = ( showFlags >> 0x12 ) & 1;
        const bool bit19 = ( showFlags >> 0x13 ) & 1;
        const bool bCockpitScene = bit18 || bit19 || ( ( showFlags & 1 ) == 0 );
        ImGui::Text( "  env-view: %s   car-scene: %s",
                     bit19 ? "Hood(bit19)"
                           : ( bit18 ? "Cockpit(bit18)" : "Exterior" ),
                     bCockpitScene ? "RP_Cockpit (near 0.05)"
                                   : "RP_CarPlayer/Near (near 0.80)" );

        if ( ( pActiveVehicleCamera->getVehicleFlags() & 1 ) != 0 ) {
            const Eigen::Vector4f &pos =
                pActiveVehicleCamera->getFixedPosIncar();
            const Eigen::Vector4f &trg =
                pActiveVehicleCamera->getFixedTrgInCar();
            ImGui::Text( "FIXED PosIncar(%.2f %.2f %.2f w%.2f) "
                         "TrgInCar(%.2f %.2f %.2f)",
                         pos.x(), pos.y(), pos.z(), pos.w(), trg.x(), trg.y(),
                         trg.z() );
        }
        if ( pTransitionIndices != nullptr && numTransition > 0 ) {
            ImGui::Text( "view-cycle slot %u / %d  (mode %d)", field83_0x92c,
                         numTransition, pTransitionIndices[field83_0x92c] );
            if ( ImGui::Button( "Cycle camera view (next)" ) ) {
                FUN_008e4840( 0, 1.0f );
            }
        }

        Eigen::Vector4f &worldPos = pActiveVehicleCamera->worldPosition;
        if ( ImGui::InputFloat3( "Position", worldPos.data() ) ) {
            pActiveVehicleCamera->setPosition( worldPos );
        }

        if ( pActiveVehicleCamera->getType() == 6 ) {
            CameraShowcase *pShowcaseCam =
                (CameraShowcase *)pActiveVehicleCamera;

            ImGui::DragFloat3( "Translation", pShowcaseCam->translation.data(),
                               0.01f );

            static bool bClampAngleRotation = true;
            static bool bShowcaseAnim = false;
            ImGui::Checkbox( "bClampAngleRotation", &bClampAngleRotation );
            ImGui::SameLine();
            ImGui::Checkbox( "bShowcaseAnim", &bShowcaseAnim );

            if ( bShowcaseAnim ) {
                pShowcaseCam->angles[1] += deltaTime * 0.5f;
                if ( pShowcaseCam->angles[1] >= (float)EIGEN_PI ) {
                    pShowcaseCam->angles[1] = (float)-EIGEN_PI;
                }
            }

            if ( bClampAngleRotation ) {
                ImGui::DragFloat2( "Angles", pShowcaseCam->angles.data(), 0.01f,
                                   pShowcaseCam->minRotAngle,
                                   pShowcaseCam->maxRotAngle );
            } else {
                ImGui::DragFloat2( "Angles", pShowcaseCam->angles.data(), 0.01f,
                                   (float)-EIGEN_PI, (float)EIGEN_PI, "%.3f",
                                   ImGuiSliderFlags_WrapAround );
            }

            ImGui::DragFloat( "distanceToTarget",
                              &pShowcaseCam->distanceToTarget, 0.01f );
        }
    }
}

void GSCamera::forceDebugCamera( CameraGame *pCamera ) {
    if ( pCamera == nullptr || pCamera == pActiveVehicleCamera ) {
        return;
    }

    if ( pPreDebugCamera == nullptr ) {
        pPreDebugCamera = pActiveVehicleCamera;
        if ( gpWorld != nullptr ) {
            pPreDebugStreamPos = gpWorld->getStreamPosition();
        }
    }
    bindCamera( pCamera, 0, 0.0f );
}

void GSCamera::restoreDebugCamera() {
    if ( pPreDebugCamera == nullptr ) {
        return;
    }

    bindCamera( pPreDebugCamera, 0, 0.0f );
    pPreDebugCamera = nullptr;

    if ( gpWorld != nullptr && pPreDebugStreamPos != nullptr ) {
        gpWorld->setStreamingPositionPtr( pPreDebugStreamPos );
    }
    pPreDebugStreamPos = nullptr;
}

void GSCamera::focusOnPoint( const Eigen::Vector4f &worldPos, float distance ) {
    CameraGame *pCam = ( pActiveVehicleCamera != nullptr )
                           ? (CameraGame *)pActiveVehicleCamera
                           : &defaultCamera;

    if ( bFlyCam ) {
        if ( CameraMouse *pFlyCam = getMouseCamera() ) {
            pCam = pFlyCam;
        }
    }

    const Eigen::Vector3f target = worldPos.head<3>();
    // Pull the camera back and slightly up from the target.
    const Eigen::Vector3f camPos =
        target + Eigen::Vector3f( 0.0f, distance * 0.4f, distance );

    pCam->setPosition(
        Eigen::Vector4f( camPos.x(), camPos.y(), camPos.z(), 1.0f ) );
    pCam->setTarget(
        Eigen::Vector4f( target.x(), target.y(), target.z(), 1.0f ) );
    pCam->updateMatrix( camPos, Eigen::Vector3f( 0.0f, 1.0f, 0.0f ), target );
}
#endif
#ifdef OTDU_DEVBUILD
void GSCamera::toggleFreeCam() {
    CameraMouse *pFlyCam = getMouseCamera();
    if ( pFlyCam != nullptr ) {
        // TODO: FIXME: Hacky debug camera implementation (should be done
        // cleanly in tick; like the original game did...)
        static CameraGame *spPreFlyCamera = nullptr;
        static Eigen::Vector4f *spPreFlyStreamPos = nullptr;

        bFlyCam = !bFlyCam;

        if ( bFlyCam ) {
            // Seed the fly cam from the current view so it doesn't
            // jump.
            CameraGame *pCur = ( pActiveVehicleCamera != nullptr )
                                   ? (CameraGame *)pActiveVehicleCamera
                                   : &defaultCamera;
            const Eigen::Vector3f pos = pCur->getWorldPosition();
            const Eigen::Vector3f tgt = pCur->getTarget();
            pFlyCam->setPosition(
                Eigen::Vector4f( pos.x(), pos.y(), pos.z(), 1.0f ) );
            pFlyCam->setTarget(
                Eigen::Vector4f( tgt.x(), tgt.y(), tgt.z(), 1.0f ) );
            // Populate worldToCam so initialize() derives the starting
            // yaw/pitch from the seeded view, and disable the unported
            // follow-player path (CameraMouse::tick) that would assert.
            pFlyCam->updateMatrix( pos, pFlyCam->getUp(), tgt );
            pFlyCam->setFollowPlayer( false );

            spPreFlyCamera = (CameraGame *)pActiveVehicleCamera;
            bindCamera( pFlyCam, 0, 0.0f );

            if ( gpWorld != nullptr ) {
                spPreFlyStreamPos = gpWorld->getStreamPosition();
                gpWorld->setStreamingPositionPtr(
                    pFlyCam->getWorldPositionRW() );
                // The fly cam can move far faster than the streaming
                // freeze-debounce allows; force responsive re-streaming
                // while it drives the centre.
                if ( gpWorld->getStreamingManager() != nullptr ) {
                    gpWorld->getStreamingManager()->setForceStreamUpdate(
                        true );
                }
            }
        } else if ( spPreFlyCamera != nullptr ) {
            bindCamera( spPreFlyCamera, 0, 0.0f );
            spPreFlyCamera = nullptr;

            // Hand the stream center back to whatever drove it before.
            if ( gpWorld != nullptr ) {
                gpWorld->setStreamingPositionPtr( spPreFlyStreamPos );
                spPreFlyStreamPos = nullptr;
                if ( gpWorld->getStreamingManager() != nullptr ) {
                    gpWorld->getStreamingManager()->setForceStreamUpdate(
                        false );
                }
            }
        }
    }
}
#endif

void GSCamera::loadCameraDB( uint8_t *param_1 ) {
    // FUN_008e3550
    numTransition = *(int32_t *)param_1;
    pTransitionIndices = (int32_t *)( param_1 + *(int32_t *)( param_1 + 4 ) );
    int32_t *piVar4 = (int32_t *)( param_1 + numTransition * 4 + 8 );
    numVehicleEntries = *piVar4;
    uint32_t entriesOffset = piVar4[1];

    // On-disk entry headers: numVehicleEntries x { CarIndex, NumCameras, 4-byte
    // pointer placeholder } (0xc each). Read for CarIndex/NumCameras only — the
    // camera-pointer arrays are now allocated natively (8-byte pointers don't
    // fit the on-disk 4-byte slots) instead of overlaid on the binary.
    const int32_t *pDiskEntries = (const int32_t *)( param_1 + entriesOffset );

    // Start of the on-disk 4-byte camera-pointer-array region (right after the
    // entry headers). No longer used for storage; only skipped to reach the
    // camera definition data.
    uint8_t *pDiskArrays = (uint8_t *)( piVar4 + numVehicleEntries * 3 + 2 );

    // Count vehicle cameras (entries 1+; entry 0 uses the registered cameras).
    numVehicleCamera = 0;
    for ( uint32_t i = 1; i < numVehicleEntries; i++ ) {
        int32_t numCameras = pDiskEntries[i * 3 + 1];
        if ( numCameras > 0 ) {
            numVehicleCamera += numCameras;
        }
    }

    // Native entry table + native camera-pointer pool (replace the on-disk
    // overlay and the x86Pointer arrays).
    pCameraVehicleEntries = (CameraDBVehicleEntry *)TestDrive::Alloc(
        numVehicleEntries * sizeof( CameraDBVehicleEntry ) );
    pVehicleCameraPtrs = ( numVehicleCamera != 0 )
                             ? (CameraGame **)TestDrive::Alloc(
                                   numVehicleCamera * sizeof( CameraGame * ) )
                             : nullptr;

    pCameraVehicleEntries[0].CarIndex = pDiskEntries[0];
    pCameraVehicleEntries[0].NumCameras = numCameraRegistered;
    pCameraVehicleEntries[0].pCameras = cameraRegister;

    CameraGame **pPtrIt = pVehicleCameraPtrs;
    for ( uint32_t i = 1; i < numVehicleEntries; i++ ) {
        int32_t numCameras = pDiskEntries[i * 3 + 1];
        pCameraVehicleEntries[i].CarIndex = pDiskEntries[i * 3];
        pCameraVehicleEntries[i].NumCameras = numCameras;
        pCameraVehicleEntries[i].pCameras =
            ( numCameras > 0 ) ? pPtrIt : nullptr;
        if ( numCameras > 0 ) {
            pPtrIt += numCameras;
        }
    }

    // Allocate + construct the CameraVehicle objects. The pool holds one object
    // per loaded camera (numVehicleCamera), NOT one per entry: the original
    // FUN_008e3550 sizes, headers, and constructs the pool off numVehicleCamera
    // (ESI = [this+0x5d8]). Constructing only numVehicleEntries leaves the tail
    // slots memset-zeroed, so their vtable pointer is NULL — a later virtual
    // call (e.g. bindCamera -> initialize()) then faults dereferencing
    // vptr+0x10.
    void *pVehicleMem = TestDrive::AllocAligned(
        numVehicleCamera * sizeof( CameraVehicle ) + 0x10,
        alignof( CameraVehicle ) );
    OTDU_ASSERT( pVehicleMem );
    memset( pVehicleMem, 0, numVehicleCamera * sizeof( CameraVehicle ) + 0x10 );

    int32_t *piVehicleHeader = (int32_t *)pVehicleMem;
    CameraVehicle *pCVar10 = (CameraVehicle *)( piVehicleHeader + 4 );
    pCVar10 = (CameraVehicle *)( Align( (uintptr_t)pCVar10,
                                        alignof( CameraVehicle ) ) );
    int32_t iVar7 = (int32_t)numVehicleCamera;
    *piVehicleHeader = iVar7;
    int32_t pCVar4 = ( iVar7 + -1 );
    CameraVehicle *pCVar8 = pCVar10;
    if ( -1 < pCVar4 ) {
        do {
            new ( pCVar8 ) CameraVehicle( pCVar4 );
            iVar7 = iVar7 + -1;
            pCVar8 = pCVar8 + 1;
        } while ( iVar7 != 0 );
    }
    pCameraVehicles = pCVar10;

    // Camera definition data follows the (skipped) on-disk camera-pointer-array
    // region. Load each camera and store it in the native arrays.
    uint8_t *pBufferIt = pDiskArrays + numVehicleCamera * sizeof( uint32_t );
    for ( uint32_t i = 1; i < numVehicleEntries; i++ ) {
        int32_t numCameras = pCameraVehicleEntries[i].NumCameras;
        CameraGame **pCameraArray = pCameraVehicleEntries[i].pCameras;
        for ( int32_t j = 0; j < numCameras; j++ ) {
            pBufferIt = pCVar10->load( pBufferIt );

            // TODO: What's the point of this? Is the pcode incorrect? Check
            // disam
            for ( int32_t k = 0; k < 9; k++ ) {
                if ( DAT_00f7c718[k][0] == pCVar10->carID ) {
                    break;
                }
            }

            uint32_t uVar1 = pCVar10->flags;
            if ( ( ( uVar1 >> 3 & 1 ) != 0 ) && ( ( uVar1 >> 1 & 1 ) != 0 ) ) {
                pCVar10->flags = uVar1 & 0xfffffffd;
            }

            pCameraArray[j] = pCVar10;
            pCVar10++;
        }
    }
}

void GSCamera::FUN_008e4840( int32_t param_1, float param_2 ) {
    // FUN_008e4840
    uint32_t iVar1 = field83_0x92c + 1;
    field83_0x92c = iVar1;
    if ( numTransition <= iVar1 ) {
        field83_0x92c = 0;
    }

    if ( ( pActiveVehicleCamera->getFlags() >> 0x11 & 1 ) == 0 ) {
        pActiveVehicleCamera->resetTransform();
    }

    findAndSetCameraMode( pTransitionIndices[field83_0x92c], param_1, param_2 );
}

bool GSCamera::cycleReplayModeOverride() {
    // FUN_008e1d90
    const float switchTime = timerToSwitch + 1.0f;
    bool bCycled = true;
    do {
        bCycled = true;
        if ( activeModeOverride == 5 ) {
            activeModeVehicleOverride++;
            if ( activeModeVehicleOverride >= 4 ) {
                activeModeVehicleOverride = 0;
                activeModeOverride = 0;
                bCycled = false;
            }
        } else {
            activeModeOverride++;
            activeModeVehicleOverride = 0;
            if ( activeModeOverride > 5 ) {
                activeModeOverride = 0;
                bCycled = false;
            }
        }
        timer = switchTime;
        bForceSwitch = true;
    } while ( activeModeOverride == 2 );
    return bCycled;
}

void GSCamera::lockReplayModeOverride( const bool bLock ) {
    // FUN_008e1e20
    if ( bLock ) {
        activeModeVehicleOverride = activeModeVehicle;
        bForceSwitch = true;
        activeModeOverride = activeMode;
        return;
    }
    bForceSwitch = false;
    activeModeVehicleOverride = -1;
    activeModeOverride = -1;
}

static inline int32_t CRTRand() {
    return static_cast<int32_t>( rand() & 0x7fff );
}

static int32_t gReplayRoadShotCount = 0; // DAT_016a2c64
static int32_t gReplayCarShotCount = 0;  // DAT_016a2c60

void GSCamera::updateReplayCameraSwitch( const float deltaTime ) {
    // FUN_008e4230
    if ( !bReplayMode && pActiveVehicleCamera->getType() != 9 ) {
        return;
    }

    CameraGame *pActiveCamera = pActiveVehicleCamera;
    CameraReplayRoad *pRoadCamera =
        static_cast<CameraReplayRoad *>( findCameraWithMode( 9, pTarget ) );

    bNeedSwitch = bForceSwitch;
    if ( pActiveCamera->getType() == 9 ) {
        const bool bSwapRequested =
            pRoadCamera == pActiveCamera && pRoadCamera->isSwapRequested();
        const bool bTimedShot =
            activeMode == 5 || ( activeMode == 4 && activeModeOverride != 4 ) ||
            ( activeMode == 2 && activeModeOverride != 2 );
        if ( bSwapRequested || ( bTimedShot && timerToSwitch < timer ) ) {
            bNeedSwitch = true;
        }
    } else if ( timerToSwitch < timer ) {
        bNeedSwitch = true;
    }

    timer += gReplay.getTimeScale() * deltaTime;
    if ( !bNeedSwitch ) {
        return;
    }

    const int32_t previousMode = activeMode;
    bForceSwitch = false;
    timer = 0.0f;
    timerToSwitch =
        ( gRPLMaxTimeToSwap - gRPLMinTimeToSwap ) * CameraRandom01() +
        gRPLMinTimeToSwap;
    do {
        activeMode = CRTRand() % 6;
    } while ( activeMode == previousMode );
    while ( activeMode == 2 ) {
        activeMode = CRTRand() % 6;
    }
    if ( activeModeOverride >= 0 ) {
        activeMode = activeModeOverride;
        gReplayRoadShotCount = 0;
    }

    bool bRoadShot = false;
    if ( activeMode != 5 && gReplayRoadShotCount <= gRPLRatioCamOutCamOnCar ) {
        gReplayRoadShotCount++;
        bRoadShot = true;
    } else {
        const int32_t previousModeVehicle = activeModeVehicle;
        gReplayRoadShotCount = 0;
        do {
            activeModeVehicle = CRTRand() % 4;
        } while ( activeModeVehicle == previousModeVehicle );
        if ( activeModeVehicleOverride >= 0 ) {
            activeModeVehicle = activeModeVehicleOverride;
            gReplayCarShotCount = 0;
        }
        if ( bDisableInteriorView ) {
            while ( activeModeVehicle == 3 ) {
                activeModeVehicle = CRTRand() % 4;
            }
        }
        if ( activeModeVehicle != 2 && activeModeVehicle != 3 &&
             gReplayCarShotCount <= gRPLRatioCamCarOutInGame ) {
            gReplayCarShotCount++;
            bRoadShot = true;
        }
    }

    if ( bRoadShot ) {
        if ( pRoadCamera == nullptr ) {
            return;
        }
        pRoadCamera->setShot(
            static_cast<eReplayRoadShot>( activeMode ),
            static_cast<eReplayRoadCarShot>( activeModeVehicle ) );
        if ( pActiveCamera->getType() != 9 ) {
            findAndSetCameraMode( 9, 0, 1.0f );
        }
        return;
    }

    if ( activeModeVehicle == 3 ) {
        gReplayCarShotCount = 0;
        const int32_t mode = findFirstSeatAdjustableTransitionMode();
        if ( findCameraWithMode( mode, pTarget ) != nullptr ) {
            findAndSetCameraMode( mode, 0, 1.0f );
        }
        return;
    }

    activeModeVehicle = 2;
    gReplayCarShotCount = 0;
    if ( numTransition == 0 ) {
        return;
    }

    int32_t mode = -1;
    CameraGame *pCamera = nullptr;
    do {
        const int32_t index = CRTRand() % static_cast<int32_t>( numTransition );
        mode = pTransitionIndices[index];
        pCamera = findCameraWithMode( mode, pTarget );
    } while ( pCamera == nullptr ||
              ( bDisableInteriorView &&
                ( pCamera->getFlags() & kSeatAdjustableFlag ) != 0 ) );

    if ( CRTRand() % 100 > 50 ) {
        mode = static_cast<CameraVehicle *>( pCamera )->getBackivewType();
    }
    findAndSetCameraMode( mode, 0, 1.0f );
}

bool GSCamera::FUN_008e1e80( bool param_1, bool param_3 ) {
    // FUN_008e1e80
    bDisableInteriorView = param_1;
    activeModeVehicle = -1;
    activeMode = -1;
    activeModeOverride = -1;
    activeModeVehicleOverride = -1;
    timer = 99.0f;
    bReplayMode = param_3;
    bNeedSwitch = true;
    timerToSwitch = 0.0;
    if ( param_3 ) {
        bViewBackOffTrigger = false;
        bViewBackOnTrigger = false;
        bChangeCamTrigger = false;
    }
    return param_3;
}

template <typename T>
bool GSCamera::registerCamera( const char *pName, const int32_t type ) {
    if ( 0xf < numCameraRegistered ) {
        return false;
    }

    T *pcVar2 =
        new ( TestDrive::AllocAligned( sizeof( T ), alignof( T ) ) ) T();
    cameraRegister[numCameraRegistered] = pcVar2;

    pcVar2->setName( pName );
    pcVar2->flags |= 0x20000;
    pcVar2->velocity.x() = DAT_00fac350.x();
    pcVar2->velocity.y() = DAT_00fac350.y();
    pcVar2->velocity.z() = DAT_00fac350.z();
    pcVar2->cameraType = type;
    pcVar2->bInitialized = true;

    numCameraRegistered++;

    return true;
}

void GSCamera::editActiveCamera() {
    // FUN_008e26c0
    Device *pDevice = gEdDev.getDevice( eDeviceType::DT_Other, 0 );
    if ( pDevice == nullptr )
        return;

    auto applyDeadzone = []( float value ) -> float {
        return ( value <= -0.3f || value >= 0.3f ) ? value : 0.0f;
    };

    EditCamDevState state = {};

    state.stick0.X =
        applyDeadzone( pDevice->getChannelValue( "STICK_0_RIGHT" ) -
                       pDevice->getChannelValue( "STICK_0_LEFT" ) );
    state.stick0.Y =
        applyDeadzone( pDevice->getChannelValue( "STICK_0_UP" ) -
                       pDevice->getChannelValue( "STICK_0_DOWN" ) );
    state.stick1.X =
        applyDeadzone( pDevice->getChannelValue( "STICK_1_RIGHT" ) -
                       pDevice->getChannelValue( "STICK_1_LEFT" ) );
    state.stick1.Y =
        applyDeadzone( pDevice->getChannelValue( "STICK_1_UP" ) -
                       pDevice->getChannelValue( "STICK_1_DOWN" ) );

    state.trigger0 = pDevice->getChannelValue( "STICK_0_FORWARD" );
    state.trigger1 = pDevice->getChannelValue( "STICK_1_FORWARD" );
    state.bTrigger0Digital = state.trigger0 > 0.8f;
    state.bTrigger1Digital = state.trigger1 > 0.8f;

    state.bPovLeft = pDevice->getChannelValueBinary( "POV_0_LEFT" );
    state.bPovRight = pDevice->getChannelValueBinary( "POV_0_RIGHT" );
    state.bPovUp = pDevice->getChannelValueBinary( "POV_0_UP" );
    state.bPovDown = pDevice->getChannelValueBinary( "POV_0_DOWN" );

    state.bButton0 = pDevice->getChannelValueBinary( "BUTTON_0" );
    state.bButton1 = pDevice->getChannelValueBinary( "BUTTON_1" );
    state.bButton2 = pDevice->getChannelValueBinary( "BUTTON_2" );
    state.bButton3 = pDevice->getChannelValueBinary( "BUTTON_3" );
    state.bButton4 = pDevice->getChannelValueBinary( "BUTTON_4" );
    state.bButton5 = pDevice->getChannelValueBinary( "BUTTON_5" );
    state.bButton6 = pDevice->getChannelValueBinary( "BUTTON_6" );
    state.bButton7 = pDevice->getChannelValueBinary( "BUTTON_7" );
    state.bButton8 = pDevice->getChannelValueBinary( "BUTTON_8" );
    state.bButton9 = pDevice->getChannelValueBinary( "BUTTON_9" );

    PhysicsLock lock;
    pActiveVehicleCamera->unknownFunc( reinterpret_cast<float *>( &state ) );
}

void GSCamera::updateCameraInput( DeviceAction *peVar5,
                                  float &input,
                                  bool bNegate ) {
    float fVar18 = gCamViewLockHoldTime;
    if ( ( ( peVar5 == nullptr ) || ( peVar5->NumAddedChannels == 0 ) ) ||
         ( fVar18 = peVar5->FUN_00434e40( gCamViewLockHoldTime ),
           fVar18 < deadZone || fVar18 == deadZone ) ) {
        if ( bUpdatedView == false ) {
            input = 0.0f;
        }
    } else {
        fVar18 = peVar5->FUN_00434e40( fVar18 );
        fVar18 = fVar18 - deadZone;
        input = bNegate ? ( 0.0f - fVar18 ) : fVar18;
        bUpdatedView = false;
    }
}
