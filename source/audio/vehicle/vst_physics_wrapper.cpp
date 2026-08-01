#include "shared.h"
#include "vst_physics_wrapper.h"

#include "game/gm_scene.h"
#include "game/car_showcase/mng_showcase_car_control.h"
#include "game/vehicle/car_state.h"
#include "game/vehicle/vehicle_car.h"
#include "physics/vehicle/physics_vehicle.h"
#include "render/camera/camera_game.h"
#include "render/camera/gs_camera.h"

#include <cmath>

namespace {
    constexpr float kDoorApertureMean = 0.5f;    // FLOAT_00f41674
    constexpr float kWindowApertureScale = 0.7f; // DAT_00f41828
    constexpr float kGravity = 9.81f;            // DAT_00f419bc
    constexpr float kAeroDensityNominal = 1.7f;  // DAT_00f417d4
    constexpr float kAeroDensityFloor = 0.7f;    // DAT_00f41828
    constexpr uint32_t kShowFlagInteriorShift = 0x12;

    uint32_t SurfaceStateFlagFor( eMaterialType material ) {
        // FUN_007d4920
        switch ( material ) {
        case eMaterialType::MT_Asphalt:
        case eMaterialType::MT_Rock:
        case eMaterialType::MT_Concrete:
        case eMaterialType::MT_Pavement:
        case eMaterialType::MT_Rubber:
        case eMaterialType::MT_Tires:
            return VSTSF_SurfaceRoad; // returns 3
        case eMaterialType::MT_Dirt:
        case eMaterialType::MT_Wood:
        case eMaterialType::MT_Vegetation1:
        case eMaterialType::MT_Vegetation2:
        case eMaterialType::MT_Vegetation3:
            return VSTSF_SurfaceDirt; // returns 5
        case eMaterialType::MT_Grass:
        case eMaterialType::MT_Foliage3:
        case eMaterialType::MT_Foliage4:
            return VSTSF_SurfaceGrass; // returns 4
        default:
            return VSTSF_SurfaceOther; // returns 2
        }
    }

    int32_t TrackingModeForCameraType( int32_t cameraType, bool &bIsPlayer ) {
        // FUN_007d4c20
        switch ( cameraType ) {
        case 0x16:
        case 0x2a:
            return 0;
        case 0x18:
        case 0x2c:
            return 1;
        case 6:
        case 7:
        case 0x17:
        case 0x2b:
            return 2;
        case 0x19:
        case 0x2d:
            return 3;
        case 0x14:
        case 0x28:
            return 4;
        case 0x15:
        case 0x29:
            return 5;
        case 9:
        case 10:
            return 6;
        default:
            bIsPlayer = false;
            return 0;
        }
    }
} // namespace

void VSTPhysicsWrapper::refresh( const CarState &carState ) {
    // FUN_007d4c20
    const CarBodyState *pBody = carState.getBody();
    const PhysicsVehicle *pVehicle = carState.getPhysicsVehicle();
    if ( pBody == nullptr || pVehicle == nullptr ) {
        return;
    }

    const VehicleBase *pVehicleBase =
        const_cast<CarState &>( carState ).getVehicleInstance();
    const VehicleCar *pCar =
        ( pVehicleBase != nullptr &&
          pVehicleBase->getType() == VehicleBase::eType::VBT_Car )
            ? static_cast<const VehicleCar *>( pVehicleBase )
            : nullptr;

    bool bIsPlayer =
        carState.getOwner() == CarState::eOwner::CSO_LocalPlayer &&
        carState.getSimulationType() == CarState::eSimulationType::CSST_Local;
    if ( !bIsPlayer && carState.isInShowcaseMode() &&
         gpActiveGMScene != nullptr ) {
        const VehicleBase *pFocused =
            gpActiveGMScene->getShowcaseFocusedVehicle();
        if ( pFocused != nullptr && pFocused->getCarState() == &carState ) {
            bIsPlayer = true;
        }
    }

    CameraGame *pCamera =
        ( gpCamera != nullptr ) ? gpCamera->getActiveCamera() : nullptr;
    bool bInteriorCamera = false;
    if ( !bIsPlayer || pCamera == nullptr ) {
        settings.TrackingMode = 0;
    } else {
        settings.TrackingMode =
            TrackingModeForCameraType( pCamera->getType(), bIsPlayer );
        bInteriorCamera =
            ( ( pCamera->getFlags() >> kShowFlagInteriorShift ) & 1u ) != 0;
    }
    settings.bIsPlayerState = bIsPlayer;

    int32_t gear = pVehicle->getCurrentGear() + 1;
    const PhysicsVehicle::DrivingGearbox gearbox =
        pVehicle->getDrivingGearbox();
    gear += gearbox.ShiftDirection;
    settings.CurrentGear = gear;

    settings.VolumeFactor = 1.0f; // rewritten every frame in the original too
    settings.Speed = pBody->LinearVelocity.head<3>();
    settings.Transform = pBody->WorldMatrix;

    const bool bHorn = pBody->bHonking != 0;
    const bool bCranking = pVehicle->getEngineState() == eEngineState::ES_Crank;
    const bool bViewInternal = bIsPlayer && bInteriorCamera;

    const float windowAperture =
        ( pCar != nullptr ) ? pCar->getWindowAperture() : 0.0f;
    const float doorLeft =
        ( pCar != nullptr ) ? pCar->getDoorApertureLeft() : 0.0f;
    const float doorRight =
        ( pCar != nullptr ) ? pCar->getDoorApertureRight() : 0.0f;

    settings.bCurrentHornDown = bHorn;
    settings.bCurrentEngineIgnition = bCranking;
    settings.bCurrentViewInternal = bViewInternal;
    settings.bCurrentDoorLeftOpened = doorLeft != 0.0f;
    settings.bCurrentDoorRightOpened = doorRight != 0.0f;

    float *pValues = settings.PhysicsValues;

    pValues[VSTPC_EngineRPM] = pBody->EngineRPM;
    pValues[VSTPC_SpeedKmh] = pBody->AudioSpeedKmh;
    pValues[VSTPC_EnginePowerPS] = pBody->EnginePowerPS;
    pValues[VSTPC_EngineTorqueNm] = pBody->EngineTorqueNm;

    pValues[VSTPC_AirDensityNorm] =
        ( kAeroDensityNominal - kAeroDensityFloor ) /
        ( kAeroDensityNominal - kAeroDensityFloor );

    if ( pCar != nullptr ) {
        pValues[VSTPC_WindowAperture] = windowAperture * kWindowApertureScale;
        pValues[VSTPC_DoorAperture] =
            ( std::fabs( doorLeft ) + std::fabs( doorRight ) ) *
            kDoorApertureMean;
    } else {
        pValues[VSTPC_WindowAperture] = 0.0f;
        pValues[VSTPC_DoorAperture] = 0.0f;
    }

    pValues[VSTPC_Zero0] = 0.0f;
    pValues[VSTPC_Zero1] = 0.0f;

    pValues[VSTPC_ReverseRPM] =
        ( settings.CurrentGear == 0 ) ? pValues[VSTPC_EngineRPM] : 0.0f;

    pValues[VSTPC_TurboPressureBar] = 0.0f;
    pValues[VSTPC_TurboRPM] = 0.0f;
    pValues[VSTPC_TurboAccelRPM] = 0.0f;
    pValues[VSTPC_LandingImpact] = 0.0f;

    refreshWheels( carState, bIsPlayer );
}

void VSTPhysicsWrapper::refreshWheels( const CarState &carState,
                                       bool bIsPlayer ) {
    // FUN_007d4b60
    const PhysicsVehicle *pVehicle = carState.getPhysicsVehicle();
    const uint32_t numWheels =
        Min( carState.getNumWheels(),
             static_cast<uint32_t>( VSTPhysicsSettings::kStateFlagCount ) );
    const float weight = Max( pVehicle->getVehicleMass() * kGravity, 1.0f );

    float *pValues = settings.PhysicsValues;

    for ( uint32_t i = 0; i < numWheels; i++ ) {
        const WheelState &wheel = carState.getWheel( i );

        const float groundSpeed = wheel.AudioGroundSpeedKmh;
        const float latSpeed = wheel.AudioLatSpeedKmh;
        const float slipLong = wheel.AudioSlipLong;
        const float slipLat = wheel.AudioSlipLat;
        const float loadNorm = wheel.Load / weight;

        if ( bIsPlayer || i == 0 ) {
            pValues[VSTPC_WheelSpeedKmh + i] = groundSpeed;
            pValues[VSTPC_WheelLoadNorm + i] = loadNorm;
            pValues[VSTPC_WheelSlipLong + i] = slipLong;
            pValues[VSTPC_WheelSlipLat + i] = std::fabs( slipLat );
            pValues[VSTPC_WheelSpeedLatKmh + i] = std::fabs( latSpeed );
            pValues[VSTPC_WheelSlipLatRatio + i] = 0.0f;
        } else {
            float &speedSlot = pValues[VSTPC_WheelSpeedKmh];
            if ( std::fabs( speedSlot ) < std::fabs( groundSpeed ) ) {
                speedSlot = groundSpeed;
            }
            float &loadSlot = pValues[VSTPC_WheelLoadNorm];
            if ( loadSlot < loadNorm ) {
                loadSlot = loadNorm;
            }
            float &slipLongSlot = pValues[VSTPC_WheelSlipLong];
            if ( std::fabs( slipLongSlot ) < std::fabs( slipLong ) ) {
                slipLongSlot = slipLong;
            }
            float &slipLatSlot = pValues[VSTPC_WheelSlipLat];
            if ( slipLatSlot < std::fabs( slipLat ) ) {
                slipLatSlot = std::fabs( slipLat );
            }
            float &latSpeedSlot = pValues[VSTPC_WheelSpeedLatKmh];
            if ( latSpeedSlot < std::fabs( latSpeed ) ) {
                latSpeedSlot = std::fabs( latSpeed );
            }
        }

        uint32_t flags = wheel.bGrounded
                             ? SurfaceStateFlagFor( wheel.ContactMaterial )
                             : VSTSF_SurfaceOther;
        flags |= ( bIsPlayer && settings.bCurrentViewInternal )
                     ? VSTSF_ViewInterior
                     : VSTSF_ViewExterior;
        settings.StateFlags[i] = flags;
    }
}

void VSTPhysicsWrapper::computeEvents() {
    // FUN_0042a120
    settings.bEventHornPushed = false;
    settings.bEventHornReleased = false;
    settings.bEventStartEnginePushed = false;
    settings.bEventStartEngineReleased = false;
    settings.bEventGearUpChanged = false;
    settings.bEventGearDownChanged = false;
    settings.bEventViewChanged = false;
    settings.bEventDoorLeftClose = false;
    settings.bEventDoorLeftOpen = false;
    settings.bEventDoorRightClose = false;
    settings.bEventDoorRightOpen = false;
    settings.bEventWindowMovingStart = false;
    settings.bEventWindowMovingStop = false;

    const int32_t gear = settings.CurrentGear;
    if ( previousGear < gear ) {
        settings.bEventGearUpChanged = true;
    }
    if ( gear < previousGear ) {
        settings.bEventGearDownChanged = true;
    }

    if ( settings.bCurrentHornDown != bPreviousHornDown ) {
        if ( settings.bCurrentHornDown ) {
            settings.bEventHornPushed = true;
        } else {
            settings.bEventHornReleased = true;
        }
    }
    if ( settings.bCurrentEngineIgnition != bPreviousEngineIgnition ) {
        if ( settings.bCurrentEngineIgnition ) {
            settings.bEventStartEnginePushed = true;
        } else {
            settings.bEventStartEngineReleased = true;
        }
    }

    if ( settings.bCurrentViewInternal != bPreviousViewInternal ) {
        settings.bEventViewChanged = true;
    }

    if ( !bPreviousDoorLeftOpened ) {
        if ( settings.bCurrentDoorLeftOpened ) {
            settings.bEventDoorLeftOpen = true;
        }
    } else if ( !settings.bCurrentDoorLeftOpened ) {
        settings.bEventDoorLeftClose = true;
    }
    if ( !bPreviousDoorRightOpened ) {
        if ( settings.bCurrentDoorRightOpened ) {
            settings.bEventDoorRightOpen = true;
        }
    } else if ( !settings.bCurrentDoorRightOpened ) {
        settings.bEventDoorRightClose = true;
    }

    const float windowAperture = settings.PhysicsValues[VSTPC_WindowAperture];
    const bool bWindowMoving = previousWindowAperture != windowAperture;
    settings.bCurrentWindowMoving = bWindowMoving;
    if ( !bPreviousWindowMoving ) {
        if ( bWindowMoving ) {
            settings.bEventWindowMovingStart = true;
        }
    } else if ( !bWindowMoving ) {
        settings.bEventWindowMovingStop = true;
    }

    bPreviousHornDown = settings.bCurrentHornDown;
    bPreviousEngineIgnition = settings.bCurrentEngineIgnition;
    previousGear = gear;
    bPreviousViewInternal = settings.bCurrentViewInternal;
    previousWindowAperture = windowAperture;
    bPreviousDoorLeftOpened = settings.bCurrentDoorLeftOpened;
    bPreviousDoorRightOpened = settings.bCurrentDoorRightOpened;
    bPreviousWindowMoving = bWindowMoving;
}
