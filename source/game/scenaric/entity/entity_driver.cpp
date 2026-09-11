#include "shared.h"
#include "entity_driver.h"

#include "entity_bytecode.h"
#include "filesystem/binary.h"

#include "tdu_instance.h"
#include "core/maths_helper.h"
#include "database/gs_database.h"
#include "game/gm_base.h"
#include "game/avatar/gs_avatar.h"
#include "game/scenaric/gs_scenaric.h"
#include "game/vehicle/bank_vehicle.h"
#include "game/vehicle/car_state.h"
#include "game/vehicle/vehicle_base.h"
#include "game/vehicle/vehicle_controller.h"
#include "game/vehicle/vehicle_resource.h"
#include "render/hiearchy_node.h"
#include "render/instance.h"
#include "render/camera/camera_game.h"
#include "render/camera/gs_camera.h"

static constexpr uint64_t kBikeCategoryHashcode = 0x35da5b5;

static constexpr uint64_t kGearboxAutoHashcode = 0x84b2b;
static constexpr uint64_t kGearboxSemiAutoHashcode = 0x8723b;
static constexpr uint64_t kGearboxSequentialHashcode = 0x8994b;
static constexpr uint64_t kGearboxMechanicalHashcode = 0x8c05b;

static constexpr int32_t kDefaultBikeDBID = 0x29a;
static constexpr int32_t kDefaultCarDBID = 999;

static constexpr float kSeatedCarLODDistances[3] = { 20.0f, 10.0f,
                                                     0.0f }; // DAT_00f8b2a0
static constexpr float kSeatedBikeLODDistances[3] = { 150.0f, 15.0f,
                                                      0.0f }; // DAT_00f8b2ac

static constexpr uint32_t kShowFlagHideDriver = 1u << 16;
static constexpr uint32_t kShowFlagKeepHead = 1u << 17;
static constexpr uint32_t kShowFlagViewCockpit = 1u << 18;
static constexpr uint32_t kShowFlagViewHood = 1u << 19;
static constexpr uint32_t kShowFlagShowHead = 1u << 2;

static constexpr int32_t kCameraTypeShowcase = 6;
static constexpr int32_t kAvatarTypeDriver = 1;
static constexpr int32_t kStateProcessRun = 0xc0c1;

static const char *kCarIKPointNamesHR[kNumCarIKPoints] = { // DAT_00f8b0c8
    "DvrSeat_HR", "Hand_HR_R", "Hand_HR_L",    "Foot_HR_R",    "Foot_HR_L",
    "Radio_HR",   "Back_HR_L", "Gear_HR",      "HBK_HR",       "Wcd_HR",
    "PsgSeat_HR", "Back_HR_R", "PsgFoot_HR_R", "PsgFoot_HR_L", "PsgSeat_HR",
    "Seat_HR_L",  "Seat_HR_L", "Seat_HR_L",    "Seat_HR_L",    "Seat_HR_L",
    "Seat_HR_L",  "Seat_HR_R", "Seat_HR_R",    "Seat_HR_R",    "Seat_HR_R",
    "Seat_HR_R",  "Seat_HR_R" };
static const char *kCarIKPointNamesLR[kNumCarIKPoints] = { // DAT_00f8b138
    "DvrSeat_LR", "Hand_LR_R", "Hand_LR_L",    "Foot_LR_R",    "Foot_LR_L",
    "Radio_LR",   "Back_LR_L", "Gear_LR",      "HBK_LR",       "Wcd_LR",
    "PsgSeat_LR", "Back_LR_R", "PsgFoot_LR_R", "PsgFoot_LR_L", "PsgSeat_LR",
    "Seat_LR_L",  "Seat_LR_L", "Seat_LR_L",    "Seat_LR_L",    "Seat_LR_L",
    "Seat_LR_L",  "Seat_LR_R", "Seat_LR_R",    "Seat_LR_R",    "Seat_LR_R",
    "Seat_LR_R",  "Seat_LR_R" };
static const char *kBikeIKPointNames[kNumBikeIKPoints] = { // DAT_00f8b1a4
    "DvrSeat_HR", "Hand_R", "Hand_L", "Foot_R", "Foot_L" };

static void AccumulateNodeWorldMatrix( const HiearchyNode *pNode,
                                       Matrix4x4f_t &inOutMatrix ) {
    // FUN_008046a0
    if ( pNode == nullptr || pNode->pParent == nullptr ) {
        return;
    }

    inOutMatrix = pNode->TransformMatrix;

    for ( const HiearchyNode *pParent = pNode->pParent;
          pParent->pParent != nullptr; pParent = pParent->pParent ) {
        inOutMatrix = inOutMatrix * pParent->TransformMatrix;
    }
}

EntityDriver::EntityDriver()
    : EntityAvatar(),
      CarStateEventListener(),
      NumAnimRide( 0 ),
      NumAnimDrive( 0 ),
      CarIKPoints{},
      pCarHandSize( nullptr ),
      pCarSeatHeight( nullptr ),
      pCarSeatFrontBack( nullptr ),
      BikeIKPoints{},
      pBikeHandSize( nullptr ),
      pBikeBikerAngle( nullptr ),
      pBikeFrontLimit( nullptr ),
      pBikeBackLimit( nullptr ),
      SteerInit( Matrix4x4f_t::Identity() ),
      pSteer( nullptr ),
      GearLeverInit( Matrix4x4f_t::Identity() ),
      pGearLever( nullptr ),
      SeatModelInit( Matrix4x4f_t::Identity() ),
      pSeatModel( nullptr ),
      pHandLeft( nullptr ),
      pHandRight( nullptr ),
      VTranslation( Eigen::Vector4f::Zero() ),
      TransformRelativeToVehicle( Matrix4x4f_t::Identity() ),
      BikeLayersCoeff( 0.0f ),
      LastBikeLayersCoeff( 0.0f ),
      AnimationDeltaTime( 0.0f ),
      pCarState( nullptr ),
      DriveType( RT_Car ),
      GearboxType( GBT_Auto ),
      GearLeverAngleUp( 0.0f ),
      GearLeverAngleDown( 0.0f ),
      bHasInteriorHR( false ),
      bIsSeating( false ),
      bIsFreeMovement( false ),
      bIsUsedIK( true ),
      bMustPlayAnims( true ),
      bMustPlayRadioAnim( false ),
      bIsAccelerationAnimPlayed( false ),
      bMustRecalculateDriverSeatPos( true ),
      bRiderHasFalled( false ),
      pLinkedExternalHelmet( nullptr ) {
    // FUN_007eeac0
    setSeating( true );
    OTDU_UNIMPLEMENTED;
}

EntityDriver::~EntityDriver() {
    // FUN_00425230
    OTDU_UNIMPLEMENTED;
}

EntityDriver *EntityDriver::getDriver() { return this; }

void EntityDriver::onEvent( class CarState *param_1,
                            float param_2,
                            void *param_3 ) {
    // FUN_007ebc90
    // NOP
}

void EntityDriver::deserialize( BinaryReader *param_2 ) {
    // FUN_007f20f0
    EntityActor::deserialize( param_2 );
    const uint32_t version = gpScenaric->getVersion();

    if ( version < 0x1b ) {
        // Legacy: two animation references read and discarded.
        RenderAnm scratch;
        scratch.deserialize( param_2 );
        scratch.deserialize( param_2 );
    } else {
        NumAnimRide = SkipBytecodeAnimCarTable( param_2 );
    }

    if ( version < 0x17 ) {
        RenderAnm scratch;
        scratch.deserialize( param_2 );
        scratch.deserialize( param_2 );
    } else {
        SkipBytecodeAnimCarTable( param_2 );
    }

    if ( version < 0x19 ) {
        param_2->pCursor += 4;
    }

    if ( version > 0x1e ) {
        for ( int32_t i = 0; i < 7; ++i ) {
            AnimRide[i].deserialize( param_2 );
        }
    }
}

void EntityDriver::buildCarIKPoints() {
    // FUN_00800580
    for ( SCIKPoint &point : CarIKPoints ) {
        point.pNode = nullptr;
        point.pOffset = nullptr;
    }

    if ( pCarState == nullptr ) {
        return;
    }

    if ( gpScenaric != nullptr ) {
        SCIKCarOffsets *pOffsets = gpScenaric->getIKOffsetDB().findCarOffsets(
            pCarState->getCarID(), true );
        if ( pOffsets != nullptr ) {
            for ( int32_t i = 0; i < kNumCarIKPoints; i++ ) {
                CarIKPoints[i].pOffset = &pOffsets->Offsets[i];
            }
        }
    }

    VehicleBase *pDisplay = pCarState->getVehicleInstance();
    if ( pDisplay == nullptr ||
         pDisplay->getType() != VehicleBase::eType::VBT_Car ) {
        return;
    }

    HiearchyNode *pLowResRoot =
        pDisplay->shouldUnstream() ? nullptr : pDisplay->getNodeByIndex( 0 );
    HiearchyNode *pHighResRoot = static_cast<VehicleController *>( pDisplay )
                                     ->getInstantiatedInteriorNode();

    for ( int32_t i = 0; i < kNumCarIKPoints; i++ ) {
        if ( i == kCarIKPointWindscreen ) {
            continue;
        }

        HiearchyNode *pNode = nullptr;
        if ( pHighResRoot != nullptr ) {
            pNode = pHighResRoot->findChild(
                GetIdentifier64bit( kCarIKPointNamesHR[i] ) );
        }

        if ( pNode == nullptr && pLowResRoot != nullptr ) {
            pNode = pLowResRoot->findChild(
                GetIdentifier64bit( kCarIKPointNamesLR[i] ) );
        }

        CarIKPoints[i].pNode = pNode;
    }
}

void EntityDriver::buildBikeIKPoints() {
    // FUN_008004b0
    for ( SCIKPoint &point : BikeIKPoints ) {
        point.pNode = nullptr;
        point.pOffset = nullptr;
    }

    if ( pCarState == nullptr ) {
        return;
    }

    if ( gpScenaric != nullptr ) {
        SCIKBikeOffsets *pOffsets = gpScenaric->getIKOffsetDB().findBikeOffsets(
            pCarState->getCarID(), true );
        if ( pOffsets != nullptr ) {
            for ( int32_t i = 0; i < kNumBikeIKPoints; i++ ) {
                BikeIKPoints[i].pOffset = &pOffsets->Offsets[i];
            }
        }
    }

    VehicleBase *pDisplay = pCarState->getVehicleInstance();
    if ( pDisplay == nullptr ||
         pDisplay->getType() != VehicleBase::eType::VBT_Bike ||
         pDisplay->shouldUnstream() ) {
        return;
    }

    HiearchyNode *pRoot = pDisplay->getNodeByIndex( 0 );
    if ( pRoot == nullptr ) {
        return;
    }

    for ( int32_t i = 0; i < kNumBikeIKPoints; i++ ) {
        BikeIKPoints[i].pNode =
            pRoot->findChild( GetIdentifier64bit( kBikeIKPointNames[i] ) );
    }
}

void EntityDriver::setSeatModel( HiearchyNode *pNode ) {
    // FUN_007ed980
    pSeatModel = pNode;

    if ( pNode != nullptr ) {
        SeatModelInit = pNode->TransformMatrix;
        updateSeatTransform();
        return;
    }

    SeatModelInit = Matrix4x4f_t::Identity();
}

void EntityDriver::setSteer( HiearchyNode *pNode ) {
    // FUN_007ebf70
    pSteer = pNode;

    if ( pNode != nullptr ) {
        SteerInit = pNode->TransformMatrix;
        return;
    }

    SteerInit = Matrix4x4f_t::Identity();
}

void EntityDriver::setGearLever( HiearchyNode *pNode ) {
    // FUN_007ece90
    pGearLever = pNode;

    if ( pNode == nullptr ) {
        GearLeverInit = Matrix4x4f_t::Identity();
        return;
    }

    GearLeverInit = pNode->TransformMatrix;
    GearLeverAngleUp = 0.0f;

    OTDU_UNIMPLEMENTED_ONCE; // TODO: SCENARIC
}

void EntityDriver::updateSeatTransform() {
    // FUN_007ed780
    Matrix4x4f_t seatMatrix = SeatModelInit;

    if ( pSeatModel != nullptr ) {
        const bool bUseSeatOffsets = bIsUsedIK || !bIsFreeMovement;

        float seatHeight = 0.0f;
        if ( pCarSeatHeight != nullptr && bUseSeatOffsets ) {
            seatHeight = *pCarSeatHeight;
        }

        float seatFrontBack = 0.0f;
        if ( pCarSeatFrontBack != nullptr && bUseSeatOffsets ) {
            seatFrontBack = *pCarSeatFrontBack;
        }

        OTDU_UNIMPLEMENTED; // TODO: GMINTRO

        seatMatrix.row( 3 ) +=
            Eigen::Vector4f( 0.0f, seatHeight, seatFrontBack, 0.0f );

        pSeatModel->TransformMatrix = seatMatrix;
        if ( pSeatModel->pParent == nullptr ) {
            pSeatModel->pInstance->setModelMatrix(
                pSeatModel->TransformMatrix );
        } else {
            Matrix4x4f_t worldMatrix =
                pSeatModel->TransformMatrix *
                pSeatModel->pParent->pInstance->getModelMatrix();
            pSeatModel->pInstance->setModelMatrix( worldMatrix );
        }
        pSeatModel->propagateTransformMatricesNoScale();
    }

    if ( DriveType != RT_Car && DriveType != RT_Bike ) {
        bMustRecalculateDriverSeatPos = false;
        return;
    }

    const SCIKPoint &driverSeat = ( DriveType == RT_Car )
                                      ? CarIKPoints[kCarIKPointDriverSeat]
                                      : BikeIKPoints[kBikeIKPointDriverSeat];

    AccumulateNodeWorldMatrix( driverSeat.pNode, seatMatrix );

    if ( driverSeat.pOffset != nullptr && ( bIsUsedIK || !bIsFreeMovement ) ) {
        const Eigen::Vector4f offsetPosition =
            TDUMat4x4MulVec( seatMatrix, driverSeat.pOffset->Offset );
        seatMatrix( 3, 0 ) = offsetPosition.x();
        seatMatrix( 3, 1 ) = offsetPosition.y();
        seatMatrix( 3, 2 ) = offsetPosition.z();
    }

    VTranslation = seatMatrix.row( 3 );

    bMustRecalculateDriverSeatPos = false;
}

void EntityDriver::setCarState( CarState *pNewCarState ) {
    // FUN_007eda20
    if ( pCarState != nullptr ) {
        if ( pSeatModel != nullptr ) {
            pSeatModel->TransformMatrix = SeatModelInit;
            pSeatModel = nullptr;
        }
        if ( pSteer != nullptr ) {
            pSteer->TransformMatrix = SteerInit;
            pSteer = nullptr;
        }
        if ( pGearLever != nullptr ) {
            pGearLever->TransformMatrix = GearLeverInit;
            pGearLever = nullptr;
        }

        VehicleBase *pDisplay = pCarState->getVehicleInstance();
        if ( pDisplay != nullptr &&
             pDisplay->getType() == VehicleBase::eType::VBT_Car ) {
            OTDU_UNIMPLEMENTED_ONCE; // TODO: VEHICLE - DisplayVehicle +0x2b78
        }
    }

    pCarState = pNewCarState;

    pCarHandSize = nullptr;
    pCarSeatHeight = nullptr;
    pCarSeatFrontBack = nullptr;
    pBikeHandSize = nullptr;
    pBikeBikerAngle = nullptr;
    pBikeFrontLimit = nullptr;
    pBikeBackLimit = nullptr;

    setSeatModel( nullptr );
    setSteer( nullptr );
    setGearLever( nullptr );

    if ( pCarState != nullptr && gpDatabase != nullptr ) {
        DBEntryCarPhysicsData *pCarData =
            gpDatabase->getCarDBEntry( pCarState->getCarID() );

        VehicleBaseResources *pResource = pCarState->getResource();
        if ( pResource != nullptr &&
             pResource->Exterior.pBankInstance != nullptr &&
             pResource->Exterior.pBankInstance->isDefaultResource() ) {
            pCarData = gpDatabase->getCarDBEntry( gpDatabase->isBike( pCarData )
                                                      ? kDefaultBikeDBID
                                                      : kDefaultCarDBID );
        }

        bool bIsBike = false;
        if ( pCarData != nullptr ) {
            const char *pBikeCategory =
                gpDatabase->getStringByHashcode( 1, kBikeCategoryHashcode );
            bIsBike = ( pBikeCategory == pCarData->Category.pAsString );

            DriveType = bIsBike ? RT_Bike : RT_Car;

            const uint64_t gearboxHashcode =
                gpDatabase->getGameDB().Databases[1].getHashcodeForPointer(
                    pCarData->GearboxType.pAsRawPointer );
            switch ( gearboxHashcode ) {
            case kGearboxAutoHashcode:
                GearboxType = GBT_Auto;
                break;
            case kGearboxSemiAutoHashcode:
                GearboxType = GBT_SemiAuto;
                break;
            case kGearboxSequentialHashcode:
                GearboxType = GBT_Sequential;
                break;
            case kGearboxMechanicalHashcode:
                GearboxType = GBT_Mechanical;
                break;
            default:
                break;
            }
        }

        OTDU_UNIMPLEMENTED_ONCE; // TODO: SCENARIC - driver state machine

        if ( DriveType == RT_Car ) {
            buildCarIKPoints();
        } else {
            buildBikeIKPoints();
        }

        if ( gpScenaric != nullptr ) {
            SCIKOffsetDB &ikOffsetDB = gpScenaric->getIKOffsetDB();
            const int32_t carID = pCarState->getCarID();

            if ( bIsBike ) {
                SCIKBikeOffsets *pOffsets =
                    ikOffsetDB.findBikeOffsets( carID, true );
                if ( pOffsets != nullptr ) {
                    pBikeHandSize = &pOffsets->HandSize;
                    pBikeBikerAngle = &pOffsets->BikerAngle;
                    pBikeFrontLimit = &pOffsets->FrontLimit;
                    pBikeBackLimit = &pOffsets->BackLimit;
                }
            } else {
                SCIKCarOffsets *pOffsets =
                    ikOffsetDB.findCarOffsets( carID, true );
                if ( pOffsets != nullptr ) {
                    pCarHandSize = &pOffsets->HandSize;
                    pCarSeatHeight = &pOffsets->SeatHeight;
                    pCarSeatFrontBack = &pOffsets->SeatFrontBack;
                }
            }
        }

        VehicleBase *pDisplay = pCarState->getVehicleInstance();
        if ( bIsBike ) {
            updateSeatTransform();

            if ( pDisplay != nullptr &&
                 pDisplay->getType() == VehicleBase::eType::VBT_Bike ) {
                const VehicleResource &interior =
                    pDisplay->getInteriorResource();
                bHasInteriorHR = ( interior.pRender3DD != nullptr &&
                                   interior.pResourceBank != nullptr );
            }
        } else if ( pDisplay != nullptr &&
                    pDisplay->getType() == VehicleBase::eType::VBT_Car ) {
            const VehicleResource &interior = pDisplay->getInteriorResource();
            bHasInteriorHR = ( interior.pRender3DD != nullptr &&
                               interior.pResourceBank != nullptr );

            HiearchyNode *pLowResRoot = pDisplay->shouldUnstream()
                                            ? nullptr
                                            : pDisplay->getNodeByIndex( 0 );
            HiearchyNode *pHighResRoot =
                static_cast<VehicleController *>( pDisplay )
                    ->getInstantiatedInteriorNode();

            auto findNode = [pLowResRoot, pHighResRoot](
                                const char *pHighResName,
                                const char *pLowResName ) -> HiearchyNode * {
                if ( pHighResRoot != nullptr ) {
                    HiearchyNode *pNode = pHighResRoot->findChild(
                        GetIdentifier64bit( pHighResName ) );
                    if ( pNode != nullptr ) {
                        return pNode;
                    }
                }
                if ( pLowResRoot != nullptr && pLowResName != nullptr ) {
                    return pLowResRoot->findChild(
                        GetIdentifier64bit( pLowResName ) );
                }
                return nullptr;
            };

            setSeatModel( findNode( "Seat_HR_L", "Seat_LR_L" ) );
            setSteer( findNode( "Steer_HR", "Steer_LR" ) );
            setGearLever( findNode( "Gear", nullptr ) );

            OTDU_UNIMPLEMENTED_ONCE; // TODO: VEHICLE
        }
    }

    OTDU_UNIMPLEMENTED_ONCE; // TODO: SCENARIC/ANIM

    bIsAccelerationAnimPlayed = true;
    pLinkedExternalHelmet = nullptr;
    bMustPlayAnims = true;
}

void EntityDriver::setSeating( bool bState ) {
    // FUN_007ebe80
    bIsSeating = bState;

    bool bUseOutCarFactor = true;
    if ( bState && pCarState != nullptr && gpDatabase != nullptr ) {
        DBEntryCarPhysicsData *pCarData =
            gpDatabase->getCarDBEntry( pCarState->getCarID() );
        if ( pCarData != nullptr && !gpDatabase->isBike( pCarData ) ) {
            const uint64_t bodyHashcode =
                gpDatabase->getGameDB().Databases[1].getHashcodeForPointer(
                    pCarData->CarBody.pAsRawPointer );

            pLightingAttenuation =
                ( bodyHashcode == 0x56760d || bodyHashcode == 0x57fcad )
                    ? gAvatarConvertibleLightFactor
                    : gAvatarInCarLightFactor;
            bUseOutCarFactor = false;
        }
    }

    if ( bUseOutCarFactor ) {
        pLightingAttenuation = gAvatarOutCarLightFactor;
    }

    if ( !bState ) {
        OTDU_UNIMPLEMENTED_ONCE; // TODO: SCENARIC
    }

    if ( pCarState != nullptr ) {
        VehicleBase *pDisplay = pCarState->getVehicleInstance();
        if ( pDisplay != nullptr &&
             pDisplay->getType() == VehicleBase::eType::VBT_Car ) {
            OTDU_UNIMPLEMENTED_ONCE; // TODO: VEHICLE
        }
    }

    bMustPlayAnims = true;
}

void EntityDriver::tick( PlayerGameMech *pGameMech,
                         float deltaTime,
                         float totalTime ) {
    // FUN_007f29c0
    if ( pCarState != nullptr ) {
        VehicleBase *pDisplay = pCarState->getVehicleInstance();
        if ( pDisplay != nullptr ) {
            const VehicleResource &interior = pDisplay->getInteriorResource();
            const bool bInteriorHR = ( interior.pRender3DD != nullptr &&
                                       interior.pResourceBank != nullptr );

            if ( bHasInteriorHR != bInteriorHR ) {
                const bool bAccelerationAnimPlayed = bIsAccelerationAnimPlayed;
                Entity *pExternalHelmet = pLinkedExternalHelmet;
                setCarState( pCarState );
                bIsAccelerationAnimPlayed = bAccelerationAnimPlayed;
                pLinkedExternalHelmet = pExternalHelmet;
            }
        }

        if ( bMustRecalculateDriverSeatPos ) {
            updateSeatTransform();
        }

        const bool bHasBiker = pCarState->getBody()->bHasBikeAvatar;
        const bool bBikerMounted = bHasBiker && !bRiderHasFalled;
        const bool bBikerLost = !bHasBiker && bRiderHasFalled;
        bRiderHasFalled = bHasBiker;

        if ( bIsSeating ) {
            const Matrix4x4f_t &carMatrix =
                pCarState->getBody()->OffsetedWorldMatrix;
            Matrix4x4f_t driverMatrix = carMatrix;

            if ( DriveType == RT_Bike && bHasBiker ) {
                (void)bBikerMounted;
                OTDU_UNIMPLEMENTED_ONCE; // TODO: SCENARIC
            }

            if ( DriveType == RT_Bike && bBikerLost ) {
                OTDU_UNIMPLEMENTED_ONCE; // TODO: SCENARIC

                BikeLayersCoeff = 1.0f;
                LastBikeLayersCoeff = 1.0f;
                bIsUsedIK = true;
                bMustPlayAnims = true;
                bMustRecalculateDriverSeatPos = true;
            }

            Eigen::Vector4f seatOffset = VTranslation;
            seatOffset.w() = 0.0f;

            const Eigen::Vector4f seatPosition =
                Mat4x4fTransform( driverMatrix, seatOffset );
            driverMatrix( 3, 0 ) = seatPosition.x();
            driverMatrix( 3, 1 ) = seatPosition.y();
            driverMatrix( 3, 2 ) = seatPosition.z();

            if ( pBikeBikerAngle != nullptr &&
                 ( bIsUsedIK || !bIsFreeMovement ) ) {
                driverMatrix =
                    MakeRotationMatX( *pBikeBikerAngle ) * driverMatrix;
            }

            TransformRelativeToVehicle =
                driverMatrix * OrthoInvert( carMatrix );

            setTransform( driverMatrix, false );

            if ( pLinkedExternalHelmet != nullptr ) {
                pLinkedExternalHelmet->setTransform( driverMatrix, false );
            }
        }

        AnimationDeltaTime += deltaTime;

        if ( bRefreshAnimationThisFrame || bBikerLost || ( flags & 8 ) != 0 ) {
            deltaTime = AnimationDeltaTime;
            AnimationDeltaTime = 0.0f;

            if ( !bIsFreeMovement || bIsUsedIK || bIsSeating ) {
                OTDU_UNIMPLEMENTED_ONCE; // TODO: SCENARIC
            } else {
                OTDU_UNIMPLEMENTED_ONCE; // TODO: SCENARIC
            }
        }
    }

    const float *pLODDistances = gAvatarLODDistances;
    if ( bIsSeating ) {
        pLODDistances = ( DriveType == RT_Bike ) ? kSeatedBikeLODDistances
                                                 : kSeatedCarLODDistances;
    }
    LODDistances[0] = pLODDistances[0];
    LODDistances[1] = pLODDistances[1];
    LODDistances[2] = pLODDistances[2];

    if ( pPooledAvatarEntry != nullptr ) {
        pPooledAvatarEntry->CarBelt.setLowResVisible( bIsSeating );
    }

    EntityAvatar::tick( pGameMech, deltaTime, totalTime );
}

bool EntityDriver::draw( RenderScene *pScene ) {
    // FUN_007efe00
    CameraGame *pCamera =
        ( gpCamera != nullptr ) ? gpCamera->getActiveCamera() : nullptr;

    if ( pCamera != nullptr ) {
        const bool bIsPlayerDriver = ( avatarType == kAvatarTypeDriver );
        const bool bInGarage =
            ( gpTestDriveInstance != nullptr &&
              gpTestDriveInstance->getActiveGameMode() == GM_Garage );

        uint32_t showFlags = pCamera->getFlags();
        const bool bInteriorView =
            ( showFlags & ( kShowFlagViewCockpit | kShowFlagViewHood ) ) != 0;

        const bool bShowHead = !( bInteriorView && bIsPlayerDriver &&
                                  !bInGarage && !bRiderHasFalled );
        setHeadVisible( bShowHead );

        showFlags = pCamera->getFlags();
        if ( ( showFlags & kShowFlagKeepHead ) == 0 && bIsPlayerDriver &&
             !bInGarage && ( showFlags & kShowFlagShowHead ) == 0 ) {
            setHeadVisible( false );
        }

        showFlags = pCamera->getFlags();
        if ( ( showFlags & kShowFlagHideDriver ) != 0 &&
             ( showFlags & kShowFlagViewCockpit ) == 0 &&
             pCamera->getType() != kCameraTypeShowcase && bIsPlayerDriver ) {
            return true;
        }
    }

    EntityAvatar::draw( pScene );
    return true;
}

void EntityDriver::setHeadVisible( bool bVisible ) {
    // FUN_007ef7a0
    EntityAvatar::setHeadVisible( bVisible );

    if ( !bVisible || pPooledAvatarEntry == nullptr ) {
        return;
    }

    if ( bIsSeating && DriveType == RT_Bike ) {
        pPooledAvatarEntry->Slots[AC_Hair].setLowResVisible( false );
        pPooledAvatarEntry->Slots[AC_Helmet].setLowResVisible(
            pLinkedExternalHelmet == nullptr );
        return;
    }

    pPooledAvatarEntry->Slots[AC_Hair].setLowResVisible( true );
    pPooledAvatarEntry->Slots[AC_Helmet].setLowResVisible( false );
}
