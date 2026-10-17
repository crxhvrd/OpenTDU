#include "shared.h"
#include "entity_driver.h"

#include "entity_bytecode.h"
#include "filesystem/binary.h"

#include "tdu_instance.h"
#include "game/gm_scene.h"
#include "core/maths_helper.h"
#include "database/gs_database.h"
#include "game/gm_base.h"
#include "game/avatar/gs_avatar.h"
#include "game/scenaric/gs_scenaric.h"
#include "game/vehicle/bank_vehicle.h"
#include "game/vehicle/car_state.h"
#include "game/vehicle/vehicle_base.h"
#include "game/vehicle/vehicle_car.h"
#include "physics/vehicle/physics_vehicle.h"
#include "game/vehicle/vehicle_controller.h"
#include "game/vehicle/vehicle_resource.h"
#include "render/animation/anim_state.h"
#include "render/animation/clip.h"
#include "render/hiearchy_node.h"
#include "render/gs_render.h"
#include "render/frustum.h"
#include "world/gs_world_streaming.h"
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

static constexpr int32_t kIntroCarDBID0 = 0x75;
static constexpr int32_t kIntroCarDBID1 = 0xf5;
static constexpr float kIntroSeatFrontBack = 0.08f; // DAT_00f418cc

static constexpr int32_t kExpressionsMatchID = 7;
static constexpr int32_t kFreeMovementExpressionsMatchID = 1;

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

static constexpr float kRadioAnimTotalTimeInv =
    0.5875440835952759f;                                 // DAT_00f41fcc
static constexpr float kAlmostOne = 0.9999899864196777f; // DAT_00f41fd0

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

EntityDriver::EntityDriver()
    : EntityAvatar(),
      CarStateEventListener(),
      AnimRide{},
      NumAnimRide( 0 ),
      AnimDrive{},
      NumAnimDrive( 0 ),
      AnimFalls{},
      StandDriveRatio{},
      BikeStandDriveRatio{},
      BikeHeadTurnRatio{},
      BikeHeadHeightRatio{},
      BikeLeftRightTorso{},
      BikeHandSizeRatio{},
      BikeAccelerateRatio{},
      BikeFootGearingRatio{},
      LastTurnWeight( 0.0f ),
      LastStandWeight( 1.0f ),
      LastRatioLean( 0.0f ),
      LastWheelingAngle( 0.0f ),
      LastLeanFrontBack( 0.0f ),
      WheelingRandomNoise( 0.0f ),
      LastWheelingRandomNoise( 0.0f ),
      LastFBAccelInfluence( 0.0f ),
      LastAccelBrakeRatio( 0.0f ),
      CurrentBikeGearAnimRatio( 0.0f ),
      bLastIsInBikeStand( false ),
      bIsPlayingBikeGearingAnim( false ),
      bRideRatiosLinked{},
      BikeFallSide{},
      LastSlideRatio( 0.0f ),
      TimeSinceFalled( 0.0f ),
      TimeSinceOnGround( 0.0f ),
      TotalTimeToGetUp( 0.0f ),
      bMustCalculateTotalTimeToGetUp( false ),
      RiderFallState( RFS_OnBike ),
      LastPosOnBike( Matrix4x4f_t::Identity() ),
      LastStandDriveWeight( 1.0f ),
      KeepAccelRatio( 0.0f ),
      CurrentBodyPosition( DBP_2HandsOnWheel ),
      HandsTarget( HT_Wheel ),
      PrevHandsTarget( HT_Wheel ),
      HandsTargetTransitionTime( 0.3f ),
      LastGear( 0 ),
      TimeInLeverNeutralPos( 0.0f ),
      IKRatioGear( 0.0f ),
      LastIKRatioGear( 0.0f ),
      RatioGearUp( 0.0f ),
      PrevRatioGearUp( 0.0f ),
      RatioGearDown( 0.0f ),
      PrevRatioGearDown( 0.0f ),
      RatioRadioAnim( 0.0f ),
      PrevRatioRadioAnim( 0.0f ),
      RadioAnimTotalTimeInv( kRadioAnimTotalTimeInv ),
      HandOpenRatio{},
      HandOpenLayerWeight( 0.0f ),
      GearLeverRatio{},
      GearLeverLayerWeight( 0.0f ),
      RadioTurnRatio{},
      RadioTurnLayerWeight( 0.0f ),
      GearingHandOpenResponseCurve(),
      RatioHandOpenResponseCurve(),
      GearingLayerResponseCurve(),
      RadioTurnRatioResponseCurve(),
      RadioTurnWeightResponseCurve(),
      LayerRadioBlendRatioResponseCurve(),
      LeanResponseCurve(),
      BikeFrontBackResponseCurve(),
      CenterFBResponseCurve(),
      BikeFallSlideRatioResponseCurve(),
      SteerSM( 0.0f ),
      SteerStack{},
      SteerStackIndex( 0 ),
      LastWheelRatio( 0.0f ),
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
      HeadMatrix( Matrix4x4f_t::Identity() ),
      BikeLayersCoeff( 1.0f ),
      LastBikeLayersCoeff( 1.0f ),
      AnimationDeltaTime( 0.0f ),
      pCarState( nullptr ),
      DriverState( DS_Invalid ),
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
      bMustPlayGearUpAnim( false ),
      bMustPlayGearDownAnim( false ),
      bMustRecalculateDriverSeatPos( true ),
      bRiderHasFalled( false ),
      pLinkedExternalHelmet( nullptr ) {
    // FUN_007eeac0
    NumMaxLayers = kAnimChannelPoolSize;

    setSeating( true );

    GearingHandOpenResponseCurve.addPoint( 0.0f, 0.0f );
    GearingHandOpenResponseCurve.addPoint( 0.2f, kAlmostOne );
    GearingHandOpenResponseCurve.addPoint( 1.0f, 0.0f );

    const float radioTime = RadioAnimTotalTimeInv;
    RatioHandOpenResponseCurve.addPoint( 0.0f, 0.0f );
    RatioHandOpenResponseCurve.addPoint( radioTime * 0.2f, kAlmostOne );
    RatioHandOpenResponseCurve.addPoint( radioTime * 0.702f, 0.0f );
    RatioHandOpenResponseCurve.addPoint( radioTime * 1.002f, 0.0f );
    RatioHandOpenResponseCurve.addPoint( radioTime * 1.502f, kAlmostOne );
    RatioHandOpenResponseCurve.addPoint( 1.0f, 0.0f );

    GearingLayerResponseCurve.addPoint( 0.2f, 0.0f );
    GearingLayerResponseCurve.addPoint( 1.0f, kAlmostOne );

    RadioTurnRatioResponseCurve.addPoint( 0.4124559462070465f, 0.0f );
    RadioTurnRatioResponseCurve.addPoint( 0.5887191295623779f, kAlmostOne );

    RadioTurnWeightResponseCurve.addPoint( radioTime * 0.2f, 0.0f );
    RadioTurnWeightResponseCurve.addPoint( radioTime * 0.702f, 1.0f );
    RadioTurnWeightResponseCurve.addPoint( radioTime * 1.002f, 1.0f );
    RadioTurnWeightResponseCurve.addPoint( radioTime * 1.502f, 0.0f );

    LayerRadioBlendRatioResponseCurve.addPoint( 0.0f, 0.0f );
    LayerRadioBlendRatioResponseCurve.addPoint( radioTime * 0.2f, 1.0f );
    LayerRadioBlendRatioResponseCurve.addPoint( 1.0f - radioTime * 0.2f, 1.0f );
    LayerRadioBlendRatioResponseCurve.addPoint( 1.0f, 0.0f );

    LeanResponseCurve.addPoint( 13.0f, 0.0f );
    LeanResponseCurve.addPoint( 30.0f, 1.0f );

    BikeFrontBackResponseCurve.addPoint( 0.0f, 0.0f );
    BikeFrontBackResponseCurve.addPoint( 0.5f, 0.2f );
    BikeFrontBackResponseCurve.addPoint( 1.0f, 1.0f );

    CenterFBResponseCurve.addPoint( 13.0f, 0.2f );
    CenterFBResponseCurve.addPoint( 30.0f, 0.6f );

    BikeFallSlideRatioResponseCurve.addPoint( 0.0f, 1.0f );
    BikeFallSlideRatioResponseCurve.addPoint( 10.0f, 1.0f );
    BikeFallSlideRatioResponseCurve.addPoint( 30.0f, 0.0f );
}

EntityDriver::~EntityDriver() {
    // FUN_00425230
}

EntityDriver *EntityDriver::getDriver() { return this; }

void EntityDriver::getCarAlphaScreenRect( Eigen::Vector4f &outRect ) {
    // FUN_007ed6f0
    if ( pCarState == nullptr || pCarState->getVehicleInstance() == nullptr ) {
        outRect = Eigen::Vector4f( 0.0f, 0.0f, 1.0f, 1.0f );
        return;
    }

    const DBEntryCarPhysicsData *pCarData =
        ( gpDatabase != nullptr )
            ? gpDatabase->getCarDBEntry( pCarState->getCarID() )
            : nullptr;
    if ( pCarData != nullptr && gpDatabase->isBike( pCarData ) ) {
        computeRiderScreenRect( outRect );
        return;
    }

    pCarState->getVehicleInstance()->computeScreenRect( outRect );
}

void EntityDriver::computeRiderScreenRect( Eigen::Vector4f &outRect ) {
    // FUN_007ecb10
    const Eigen::Vector4f &dimensions = pCarState->getBody()->BodyDimensions;
    Eigen::Vector3f scale( dimensions.x(), dimensions.y() * 2.0f,
                           dimensions.z() );
    Matrix4x4f_t boundingBox = MakeScaleMatrix( scale );
    boundingBox.row( 3 ) += Eigen::Vector4f( 0.0f, dimensions.y(), 0.0f, 0.0f );
    boundingBox = boundingBox * pCarState->getWorldMatrix();

    const Eigen::Vector4f shift = gpWorldStreaming->isEnabled()
                                      ? gpWorldStreaming->getPosition()
                                      : Eigen::Vector4f::Zero();
    RenderScene *pScene =
        gpRender->getRenderPassAtIndex( getRenderSceneID() ).pScene;

    Matrix4x4f_t worldToView = Matrix4x4f_t::Identity();
    worldToView.row( 3 ) +=
        Eigen::Vector4f( -shift.x(), -shift.y(), -shift.z(), 0.0f );
    worldToView = worldToView * pScene->getCamera()->getWorldToCam();

    ProjectBoundingBoxToScreenRect( outRect, boundingBox, worldToView,
                                    pScene->getFrustumWrite() );
}

void EntityDriver::onHeadUpdate( Matrix4x4f_t &headTransform ) {
    // FUN_007f2200
    EntityAvatar::onHeadUpdate( headTransform );
    if ( pCarState != nullptr ) {
        HeadMatrix = headTransform *
                     OrthoInvert( pCarState->getBody()->OffsetedWorldMatrix );
    }
}

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
        NumAnimRide =
            DecodeBytecodeAnimCarTable( param_2, AnimRide, kNumDriverAnims );
    }

    if ( version < 0x17 ) {
        RenderAnm scratch;
        scratch.deserialize( param_2 );
        scratch.deserialize( param_2 );
    } else {
        NumAnimDrive = static_cast<int32_t>(
            DecodeBytecodeAnimCarTable( param_2, AnimDrive, kNumDriverAnims ) );
    }

    if ( version < 0x19 ) {
        param_2->pCursor += 4;
    }

    if ( version > 0x1e ) {
        for ( int32_t i = 0; i < kNumDriverAnims; ++i ) {
            AnimFalls[i].deserialize( param_2 );
        }
    }
}

void EntityDriver::loadResources( ScenaricBank *param_2 ) {
    // FUN_007f2250
    EntityActor::loadResources( param_2 );

    const auto resolve = [param_2]( RenderAnm &anim ) {
        if ( anim.File != 0 ) {
            anim.pAnimState =
                gpScenaric->resolveAnimState( param_2, anim.pRefBas );
            anim.pClip = gpScenaric->resolveClip( param_2, anim.pRefBas );
        }
    };

    const int32_t numRide =
        std::min( static_cast<int32_t>( NumAnimRide ), kNumDriverAnims );
    for ( int32_t i = 0; i < numRide; ++i ) {
        resolve( AnimRide[i] );
    }

    const int32_t numDrive = std::min( NumAnimDrive, kNumDriverAnims );
    for ( int32_t i = 0; i < numDrive; ++i ) {
        resolve( AnimDrive[i] );
    }

    // The original unrolls this loop, one inlined resolve per slot.
    for ( int32_t i = 0; i < kNumDriverAnims; ++i ) {
        resolve( AnimFalls[i] );
    }
}

void EntityDriver::instanciate( MngActor *pMngActor, Entity *param_1 ) {
    // FUN_007efc80
    EntityAvatar::instanciate( pMngActor, param_1 );

    EntityDriver *pInstance = static_cast<EntityDriver *>( param_1 );

    pInstance->NumAnimRide = NumAnimRide;

    const int32_t numCopied = std::min( NumAnimDrive, kNumDriverAnims );
    for ( int32_t i = 0; i < numCopied; ++i ) {
        pInstance->AnimRide[i] = AnimRide[i];
    }

    pInstance->NumAnimDrive = NumAnimDrive;
    for ( int32_t i = 0; i < numCopied; ++i ) {
        pInstance->AnimDrive[i] = AnimDrive[i];
    }

    for ( int32_t i = 0; i < kNumDriverAnims; ++i ) {
        pInstance->AnimFalls[i] = AnimFalls[i];
    }
}

void BuildCarIKPoints( SCIKPoint ( &points )[kNumCarIKPoints],
                       CarState *pCarState ) {
    // FUN_00800580
    for ( SCIKPoint &point : points ) {
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
                points[i].pOffset = &pOffsets->Offsets[i];
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

        points[i].pNode = pNode;
    }
}

void EntityDriver::buildCarIKPoints() {
    // FUN_00800580
    BuildCarIKPoints( CarIKPoints, pCarState );
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

    static constexpr float kLeverAngleNumerator = 0.1f; // FLOAT_00f41668
    static constexpr float kLeverAngleClamp = -0.3f;    // DAT_00f41a34

    Matrix4x4f_t gearWorld = Matrix4x4f_t::Identity();
    const SCIKPoint &gearPoint = CarIKPoints[kCarIKPointGearLever];
    AccumulateNodeWorldMatrix( gearPoint.pNode, gearWorld );

    Eigen::Vector3f gearPosition( gearWorld( 3, 0 ), gearWorld( 3, 1 ),
                                  gearWorld( 3, 2 ) );
    if ( gearPoint.pOffset != nullptr ) {
        gearPosition = TDUMat4x4MulVec(
            gearWorld, Eigen::Vector3f( gearPoint.pOffset->Offset.x(),
                                        gearPoint.pOffset->Offset.y(),
                                        gearPoint.pOffset->Offset.z() ) );
    }

    const Eigen::Vector3f leverPosition(
        GearLeverInit( 3, 0 ), GearLeverInit( 3, 1 ), GearLeverInit( 3, 2 ) );
    const float distance = ( leverPosition - gearPosition ).norm();
    GearLeverAngleDown = std::max(
        kLeverAngleClamp, -std::atan( kLeverAngleNumerator / distance ) );
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

        if ( gpTestDriveInstance != nullptr &&
             gpTestDriveInstance->getActiveGameMode() == GM_Intro &&
             pCarState != nullptr &&
             ( pCarState->getCarID() == kIntroCarDBID0 ||
               pCarState->getCarID() == kIntroCarDBID1 ) ) {
            seatFrontBack = kIntroSeatFrontBack;
        }

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
        const Eigen::Vector3f offsetPosition = TDUMat4x4MulVec(
            seatMatrix, Eigen::Vector3f( driverSeat.pOffset->Offset.x(),
                                         driverSeat.pOffset->Offset.y(),
                                         driverSeat.pOffset->Offset.z() ) );
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
            static_cast<VehicleCar *>( pDisplay )
                ->setSeatBeltVisible( 0, true );
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

        DriverState = bIsBike ? DS_Bike : DS_Car;

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

            static_cast<VehicleCar *>( pDisplay )
                ->setSeatBeltVisible( 0, !bIsSeating );
        }
    }

    if ( DriveType == RT_Car ) {
        ExpressionList.clear();
        ExpressionList.append( &IKBack );
        ExpressionList.append( &IKRightHand );
        ExpressionList.append( &IKLeftHand );
        ExpressionList.append( &IKRightFoot );
        ExpressionList.append( &IKLeftFoot );
        ExpressionList.append( &IKRightShoulderBelt1 );
        ExpressionList.append( &IKRightShoulderBelt2 );
        ExpressionList.append( &IKLeftShoulderBelt1 );
        ExpressionList.append( &IKLeftShoulderBelt2 );
        ExpressionList.append( &IKRightHipsBelt );
        ExpressionList.append( &IKLeftHipsBelt );
    } else {
        BikeExpressionList.clear();
        BikeExpressionList.append( &IKRightHand );
        BikeExpressionList.append( &IKRightHandAlignY );
        BikeExpressionList.append( &IKRightHand2 );
        BikeExpressionList.append( &IKLeftHand );
        BikeExpressionList.append( &IKLeftHandAlignY );
        BikeExpressionList.append( &IKLeftHand2 );
        BikeExpressionList.append( &IKRightFoot );
        BikeExpressionList.append( &IKLeftFoot );
    }

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
        resetAnimationChannels();
    }

    if ( pCarState != nullptr ) {
        VehicleBase *pDisplay = pCarState->getVehicleInstance();
        if ( pDisplay != nullptr &&
             pDisplay->getType() == VehicleBase::eType::VBT_Car ) {
            static_cast<VehicleCar *>( pDisplay )
                ->setSeatBeltVisible( 0, !bIsSeating );
        }
    }

    bMustPlayAnims = true;
}

void EntityDriver::resetAnimationChannels() {
    // FUN_007ebe50
    for ( int32_t i = 0; i < animation.getNumChannels(); i++ ) {
        animation.resetChannel( i );
    }
}

void EntityDriver::leaveSeatForCinematic() {
    // FUN_007f7b60 (inlined)
    setSeating( false );

    bIsFreeMovement = true;
    resetAnimationChannels();
    bMustPlayAnims = true;
    bMustRecalculateDriverSeatPos = true;

    bIsUsedIK = false;
    resetAnimationChannels();
    bMustPlayAnims = true;
    bMustRecalculateDriverSeatPos = true;
}

void EntityDriver::returnToSeatAfterCinematic() {
    // FUN_007e5f70 (inlined)
    setSeating( true );
    bIsFreeMovement = false;
    bIsUsedIK = true;
    bMustPlayAnims = true;
    bMustRecalculateDriverSeatPos = true;

    if ( DriveType == RT_Bike && pPooledAvatarEntry != nullptr ) {
        pPooledAvatarEntry->Slots[AC_Helmet].setLowResVisible(
            pLinkedExternalHelmet == nullptr );
    }
}

bool EntityDriver::onMessage( FlashMessage *pMessage ) {
    // FUN_007ef8e0
    if ( EntityAvatar::onMessage( pMessage ) ) {
        return true;
    }

    const bool bCarInstalled =
        gpScenaric != nullptr &&
        gpScenaric->getLocalPlayer().getCarState() != nullptr;

    if ( *pMessage == FlashMessage( "avm4" ) ) {
        if ( bCarInstalled ) {
            placeInLocalPlayerCar();
        }
        if ( getCineAvatarMode() != 4 ) {
            setSeating( true );
            bIsFreeMovement = false;
            bIsUsedIK = true;
            requestSeatRefresh();
        }
        setCineAvatarMode( 4 );
        return true;
    }

    if ( *pMessage == FlashMessage( "avm1" ) ) {
        leaveSeatForCinematic();
        setCineAvatarMode( 1 );
        return true;
    }

    if ( *pMessage == FlashMessage( "avm2" ) ) {
        if ( bCarInstalled ) {
            placeInLocalPlayerCar();
        }
        setSeating( true );
        bIsFreeMovement = true;
        resetAnimationChannels();
        requestSeatRefresh();
        bIsUsedIK = false;
        resetAnimationChannels();
        requestSeatRefresh();
        setCineAvatarMode( 2 );
        return true;
    }

    if ( *pMessage == FlashMessage( "avm3" ) ) {
        if ( bCarInstalled ) {
            placeInLocalPlayerCar();
        }
        setSeating( true );
        bIsUsedIK = true;
        requestSeatRefresh();
        if ( DriveType == RT_Bike ) {
            requestSeatRefresh();
            bIsFreeMovement = false;
            setCineAvatarMode( 4 );
            return true;
        }
        bIsFreeMovement = true;
        resetAnimationChannels();
        requestSeatRefresh();
        setCineAvatarMode( 3 );
        return true;
    }

    if ( *pMessage == FlashMessage( "heof" ) ) {
        if ( DriveType == RT_Bike && pPooledAvatarEntry != nullptr ) {
            pPooledAvatarEntry->Slots[AC_Helmet].setLowResVisible( false );
        }
        return true;
    }

    if ( *pMessage == FlashMessage( "heon" ) ) {
        if ( DriveType == RT_Bike && pPooledAvatarEntry != nullptr ) {
            pPooledAvatarEntry->Slots[AC_Helmet].setLowResVisible(
                pLinkedExternalHelmet == nullptr );
        }
        return true;
    }

    return false;
}

bool EntityDriver::placeInLocalPlayerCar() {
    // FUN_007e4e60
    CarState *pLocalCar = ( gpScenaric != nullptr )
                              ? gpScenaric->getLocalPlayer().getCarState()
                              : nullptr;
    if ( pLocalCar == nullptr ) {
        return false;
    }

    if ( pLocalCar->getDriver() != nullptr ) {
        if ( pLocalCar->getDriver() == this ) {
            return true;
        }
        pLocalCar->unloadDriver();
        if ( animation.getNumChannels() > 0 ) {
            animation.resetChannel( 0 );
        }
    }

    if ( pLocalCar->getDriver() == nullptr ) {
        return gpScenaric->placeDriver( pLocalCar, this );
    }
    return false;
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
        const bool bRiderJustThrown = bHasBiker && !bRiderHasFalled;
        const bool bBikerLost = !bHasBiker && bRiderHasFalled;
        bRiderHasFalled = bHasBiker;

        if ( bIsSeating ) {
            const Matrix4x4f_t &carMatrix =
                pCarState->getBody()->OffsetedWorldMatrix;
            Matrix4x4f_t driverMatrix = carMatrix;

            if ( DriveType == RT_Bike && bHasBiker ) {
                updateRiderFall( deltaTime, bRiderJustThrown, driverMatrix );
            } else {
                if ( DriveType == RT_Bike && bBikerLost ) {
                    animation.stopChannelsWithClipID( 0 );
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
                processDriverState( deltaTime );
            } else {
                updateIKTargets();
                updateBikeIKTargets();
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

    if ( pCarState != nullptr ) {
        if ( DriverState == DS_Car ) {
            applyDriveBaseAnim();
        } else if ( DriverState == DS_Bike ) {
            applyRideBaseAnim();
        }
    }
}

void EntityDriver::processDriverState( float deltaTime ) {
    // FUN_007efef0
    switch ( DriverState ) {
    case DS_Car:
        updateStandDriveRatio( deltaTime );
        updateBodyPositionFSM( deltaTime );
        updateGearRatios( deltaTime );
        updateAnimChannels( deltaTime );
        updateIKTargets();
        break;
    case DS_Bike:
        updateBikeRideState( deltaTime );
        updateBikeIKTargets();
        break;
    default:
        break;
    }
}

void EntityDriver::playAnimState( RenderAnm &anim ) {
    if ( anim.pAnimState == nullptr && anim.pRefBas != nullptr &&
         gpScenaric != nullptr ) {
        anim.pAnimState = gpScenaric->resolveAnimState( nullptr, anim.pRefBas );
    }

    const AnimStateInstance *pState = anim.pAnimState;
    if ( anim.File == 0 || pState == nullptr ) {
        return;
    }

    std::vector<AnimStateBlendEntry> blendSet;
    pState->getBlendSet( blendSet );
    blendSet.erase( std::remove_if( blendSet.begin(), blendSet.end(),
                                    []( const AnimStateBlendEntry &entry ) {
                                        return entry.pClip == nullptr;
                                    } ),
                    blendSet.end() );
    if ( blendSet.empty() ) {
        return;
    }

    animation.playChannel( static_cast<int32_t>( pState->getMatchID() ),
                           anim.ID, blendSet.data(),
                           static_cast<int32_t>( blendSet.size() ) );
}

void EntityDriver::applyDriveBaseAnim() {

    // FUN_007efef0
    if ( !bMustPlayAnims || !animation.isValid() ) {
        return;
    }

    if ( bIsFreeMovement ) {
        if ( ( bIsUsedIK || bIsSeating ) && AnimDrive[6].File != 0 ) {
            animation.playChannel( kFreeMovementExpressionsMatchID,
                                   AnimDrive[6].ID, nullptr, 0 );
            buildIKChains( kFreeMovementExpressionsMatchID );
        }
        bMustPlayAnims = false;
        return;
    }

    static constexpr int32_t kPlayOrder[kNumDriverAnims] = { 0, 1, 3, 2,
                                                             4, 5, 6 };
    for ( int32_t i = 0; i < kNumDriverAnims; i++ ) {
        playAnimState( AnimDrive[kPlayOrder[i]] );
    }

    bMustPlayAnims = false;

    buildIKChains( kExpressionsMatchID );
}

void EntityDriver::applyRideBaseAnim() {
    // FUN_007efef0
    if ( !bMustPlayAnims || !animation.isValid() ) {
        return;
    }

    if ( !bIsFreeMovement ) {
        resetAnimationChannels();
        for ( RenderAnm &anim : AnimRide ) {
            playAnimState( anim );
        }
        for ( bool &bLinked : bRideRatiosLinked ) {
            bLinked = true;
        }
        if ( AnimRide[6].File != 0 ) {
            buildIKChains( kExpressionsMatchID );
        }
    } else if ( bIsUsedIK && AnimRide[6].File != 0 ) {
        animation.playChannel( kFreeMovementExpressionsMatchID, AnimRide[6].ID,
                               nullptr, 0 );
        buildIKChains( kFreeMovementExpressionsMatchID );
    }

    bMustPlayAnims = false;
}

static inline float SlewLimit( float target, float last, float step ) {
    if ( step < target - last ) {
        target = last + step;
    }
    if ( target - last < -step ) {
        target = last - step;
    }
    return target;
}

void EntityDriver::updateBikeRideState( float deltaTime ) {
    // FUN_007efef0
    static constexpr float kToNormalised =
        0.49999749660491943f;                                // _DAT_00f420c0
    static constexpr float kMaxWheelingAngle = 1.5f;         // DAT_00f416b8
    static constexpr float kAccelInfluence = 0.2f;           // DAT_00f41730
    static constexpr float kFrontBackCenter = 0.5f;          // FLOAT_00f41674
    static constexpr float kEpsilon = 1e-5f;                 // DAT_00f41710
    static constexpr float kSteerDeadZone = 0.01f;           // DAT_00f416d4
    static constexpr float kTurnScale = 0.5050504803657532f; // DAT_00f420b8
    static constexpr float kTurnRate = 2.0f;                 // DAT_00f416b0
    static constexpr float kStandExitSpeed = 0.8f;           // DAT_00f416d0
    static constexpr float kStandEnterSpeed = 0.4f;          // DAT_00f4175c
    static constexpr float kStandEnterRate = 4.0f;           // DAT_00f418d4
    static constexpr float kStandExitRate = 2.5f;            // DAT_00f416c0
    static constexpr float kNoiseMaxWheelingRate = 0.2f;     // DAT_00f41730
    static constexpr float kNoiseMax = 0.1f;                 // 0x3dcccccd
    static constexpr float kNoiseRate = 0.1f;                // FLOAT_00f41668
    static constexpr float kLeanRate = 3.0f;                 // DAT_00f416b4
    static constexpr float kHeadHeightScale = 0.3333311f;    // _DAT_00f420b4
    static constexpr float kAccelRate = 4.0f;                // DAT_00f418d4

    if ( pCarState == nullptr || !animation.isValid() ) {
        return;
    }

    const CarBodyState *pBody = pCarState->getBody();

    const float steer = updateSteerValue( deltaTime );
    const float steerNormalised = ( steer + 1.0f ) * kToNormalised;
    const float headSteer = ( pBody->SteeringRaw + 1.0f ) * kToNormalised;

    const float wheeling = std::min(
        std::max( -pBody->BikeLeanLongitudinal, 0.0f ), kMaxWheelingAngle );
    const float speed = pBody->SpeedInKmh;

    const float accelBrake =
        std::min( std::max( pBody->Throttle - pBody->Brake, -1.0f ), 1.0f ) *
        kAccelInfluence;
    LastFBAccelInfluence =
        SlewLimit( accelBrake, LastFBAccelInfluence, deltaTime );
    BikeFrontBackResponseCurve.setPointValue(
        kFrontBackCenter,
        CenterFBResponseCurve.evaluate( speed ) + LastFBAccelInfluence );

    const float wheelingRate =
        ( deltaTime > kEpsilon ) ? ( wheeling - LastWheelingAngle ) / deltaTime
                                 : 0.0f;
    LastWheelingAngle = wheeling;

    float turnTarget = 0.0f;
    if ( steer > kSteerDeadZone || steer < -kSteerDeadZone ) {
        const float steerPastDeadZone = ( steer > kSteerDeadZone )
                                            ? steer - kSteerDeadZone
                                            : steer + kSteerDeadZone;
        turnTarget = ( steerPastDeadZone * kTurnScale != 0.0f ) ? 1.0f : 0.0f;
    }
    const float turnStep = deltaTime * kTurnRate;
    const float turn = SlewLimit( turnTarget, LastTurnWeight, turnStep );
    LastTurnWeight = turn;

    const float standThreshold =
        bLastIsInBikeStand ? kStandExitSpeed : kStandEnterSpeed;
    bLastIsInBikeStand = ( speed < standThreshold );
    const float standRate =
        bLastIsInBikeStand ? kStandEnterRate : kStandExitRate;
    const float stand = SlewLimit( bLastIsInBikeStand ? 1.0f : 0.0f,
                                   LastStandWeight, deltaTime * standRate );
    LastStandWeight = stand;

    const float lean =
        std::min( std::max( pBody->BikeLeanLateral, -1.0f ), 1.0f );

    CameraVehicle *pCamera =
        ( gpCamera != nullptr ) ? gpCamera->getActiveVehicleCamera() : nullptr;
    const bool bHoodView =
        pCamera != nullptr && ( pCamera->getFlags() & kShowFlagViewHood ) != 0;
    const float frontBackInput =
        bHoodView ? kFrontBackCenter
                  : ( pBody->BikeLeanInput + 1.0f ) * kFrontBackCenter;
    const float frontBackCurve =
        BikeFrontBackResponseCurve.evaluate( frontBackInput );

    if ( wheeling <= kSteerDeadZone ||
         std::fabs( wheelingRate ) > kNoiseMaxWheelingRate ) {
        WheelingRandomNoise = 0.0f;
    } else if ( LastWheelingRandomNoise == WheelingRandomNoise &&
                gpScenaric != nullptr ) {
        WheelingRandomNoise =
            RandomUnit( gpScenaric->getRandomGenerator() ) * kNoiseMax;
    }
    LastWheelingRandomNoise = SlewLimit(
        WheelingRandomNoise, LastWheelingRandomNoise, deltaTime * kNoiseRate );

    float frontBack = std::min(
        std::max( LastWheelingRandomNoise + frontBackCurve, 0.0f ), 1.0f );
    if ( pBikeFrontLimit != nullptr && pBikeBackLimit != nullptr ) {
        frontBack = ( 1.0f - frontBack ) * *pBikeBackLimit +
                    *pBikeFrontLimit * frontBack;
        frontBack = std::min( std::max( frontBack, -1.0f ), 1.0f );
    }
    const float leanStep = deltaTime * kLeanRate;
    frontBack = SlewLimit( frontBack, LastLeanFrontBack, leanStep );
    LastLeanFrontBack = frontBack;

    const float ratioLean = SlewLimit( LeanResponseCurve.evaluate( speed ),
                                       LastRatioLean, leanStep );
    LastRatioLean = ratioLean;

    const float leanNormalised = ( lean + 1.0f ) * kToNormalised;
    const float notTurn = 1.0f - turn;
    const float notStand = 1.0f - stand;
    const float rideUpright = notStand * ( 1.0f - ratioLean );
    const float rideUprightBack = rideUpright * ( 1.0f - frontBack );

    const auto setBlend = []( AnimStateBlendEntry &entry, float weight,
                              float ratio ) {
        entry.Weight = weight;
        entry.Ratio = ratio;
    };
    setBlend( BikeStandDriveRatio[0], notTurn * stand, kAnimRatioFollowCurve );
    setBlend( BikeStandDriveRatio[1], stand * turn, steerNormalised );
    setBlend( BikeStandDriveRatio[2], rideUprightBack * notTurn,
              kAnimRatioFollowCurve );
    setBlend( BikeStandDriveRatio[3], rideUpright * notTurn * frontBack,
              kAnimRatioFollowCurve );
    setBlend( BikeStandDriveRatio[4], rideUprightBack * turn, steerNormalised );
    setBlend( BikeStandDriveRatio[5], rideUpright * frontBack * turn,
              steerNormalised );
    setBlend( BikeStandDriveRatio[6],
              ratioLean * notStand * ( 1.0f - frontBack ), leanNormalised );
    setBlend( BikeStandDriveRatio[7], ratioLean * notStand * frontBack,
              leanNormalised );
    setBlend( BikeHeadTurnRatio[0], 1.0f - std::fabs( lean ), headSteer );
    setBlend( BikeHeadTurnRatio[1], std::fabs( lean ), headSteer );
    setBlend( BikeHeadHeightRatio, 1.0f,
              kFrontBackCenter - wheeling * kHeadHeightScale );
    setBlend( BikeLeftRightTorso, 1.0f, leanNormalised );
    setBlend( BikeHandSizeRatio, 1.0f, kAlmostOne );

    float handSizeWeight = 0.0f;
    if ( pBikeHandSize != nullptr ) {
        handSizeWeight = std::min( std::max( *pBikeHandSize, 0.0f ), 1.0f );
    }

    LastAccelBrakeRatio = SlewLimit( pBody->Throttle, LastAccelBrakeRatio,
                                     deltaTime * kAccelRate );
    setBlend( BikeAccelerateRatio, 1.0f,
              std::max( LastAccelBrakeRatio, 0.0f ) * kAlmostOne );

    const PhysicsVehicle *pPhysics = pCarState->getPhysicsVehicle();
    const int32_t shiftState =
        ( pPhysics != nullptr ) ? pPhysics->getDrivingGearbox().ShiftState : 0;
    if ( !bIsPlayingBikeGearingAnim && ( shiftState == 3 || shiftState == 5 ) &&
         speed >= standThreshold ) {
        bIsPlayingBikeGearingAnim = true;
        CurrentBikeGearAnimRatio = 0.0f;
    }
    if ( bIsPlayingBikeGearingAnim ) {
        CurrentBikeGearAnimRatio += turnStep;
        if ( CurrentBikeGearAnimRatio >= 1.0f ) {
            bIsPlayingBikeGearingAnim = false;
            CurrentBikeGearAnimRatio = kAlmostOne;
        }
        setBlend( BikeFootGearingRatio, 1.0f, CurrentBikeGearAnimRatio );
    } else {
        setBlend( BikeFootGearingRatio, 0.0f, 0.0f );
    }

    const auto setRideRatios = [this]( int32_t matchID,
                                       const AnimStateBlendEntry *pEntries,
                                       int32_t count ) {
        if ( bRideRatiosLinked[matchID] ) {
            animation.setChannelBlendRatios( matchID, pEntries, count );
        }
    };
    setRideRatios( 0, BikeStandDriveRatio, kNumBikeStandDriveRatios );
    setRideRatios( 1, BikeHeadTurnRatio, 2 );
    animation.setChannelLayer( 1, AnimChannel::ALM_BindDelta,
                               BikeLayersCoeff * rideUpright );
    setRideRatios( 2, &BikeHeadHeightRatio, 1 );
    animation.setChannelLayer( 2, AnimChannel::ALM_BindDelta,
                               BikeLayersCoeff * notStand );
    setRideRatios( 3, &BikeLeftRightTorso, 1 );
    animation.setChannelLayer( 3, AnimChannel::ALM_BindDelta,
                               BikeLayersCoeff * rideUpright );
    setRideRatios( 4, &BikeHandSizeRatio, 1 );
    animation.setChannelLayer( 4, AnimChannel::ALM_BindDelta,
                               handSizeWeight * BikeLayersCoeff );
    setRideRatios( 5, &BikeAccelerateRatio, 1 );
    animation.setChannelLayer( 5, AnimChannel::ALM_BindDelta, BikeLayersCoeff );
    if ( bIsFreeMovement && bIsUsedIK ) {
        animation.setChannelLayer( 1, AnimChannel::ALM_Weighted, 1.0f );
    }
    setRideRatios( 6, &BikeFootGearingRatio, 1 );
    animation.setChannelLayer( 6, AnimChannel::ALM_BindDelta, BikeLayersCoeff );
}

// Rotation slerped from one frame to another; keeps the translation of `to`.
static Matrix4x4f_t
SlerpFrame( const Matrix4x4f_t &from, const Matrix4x4f_t &to, float ratio ) {
    const Eigen::Quaternionf qFrom( from.block<3, 3>( 0, 0 ) );
    const Eigen::Quaternionf qTo( to.block<3, 3>( 0, 0 ) );
    Matrix4x4f_t frame =
        BuildRotationMatrix( QuaternionSlerpShortest( qFrom, qTo, ratio ) );
    frame.row( 3 ) = to.row( 3 );
    return frame;
}

void EntityDriver::playFallState( int32_t slot,
                                  uint32_t clipID,
                                  bool bLooped ) {
    // FUN_00401400
    static constexpr float kFallTransition = 0.4f; // DAT_00f4175c

    RenderAnm &anim = AnimFalls[slot];
    if ( anim.pAnimState == nullptr && anim.pRefBas != nullptr &&
         gpScenaric != nullptr ) {
        anim.pAnimState = gpScenaric->resolveAnimState( nullptr, anim.pRefBas );
    }

    const AnimStateInstance *pState = anim.pAnimState;
    if ( pState == nullptr || !animation.isValid() ) {
        return;
    }

    std::vector<AnimStateBlendEntry> blendSet;
    pState->getBlendSet( blendSet );
    blendSet.erase( std::remove_if( blendSet.begin(), blendSet.end(),
                                    []( const AnimStateBlendEntry &entry ) {
                                        return entry.pClip == nullptr;
                                    } ),
                    blendSet.end() );
    if ( blendSet.empty() ) {
        return;
    }

    const int32_t matchID = static_cast<int32_t>( pState->getMatchID() );
    animation.playChannelFromPosture( matchID, clipID, blendSet.data(),
                                      static_cast<int32_t>( blendSet.size() ),
                                      kFallTransition, bLooped );
    if ( matchID >= 0 && matchID < kNumDriverAnims ) {
        bRideRatiosLinked[matchID] = false;
    }
}

void EntityDriver::updateRiderFall( float deltaTime,
                                    bool bJustThrown,
                                    Matrix4x4f_t &outMatrix ) {
    // FUN_007ec250
    static constexpr float kLayerFadeRate = 2.5f; // DAT_00f416c0
    static constexpr float kRiderBodyDrop = -0.2f;
    static constexpr float kSeatBlendTime = 0.4f;      // DAT_00f4175c
    static constexpr float kSlideRatioRate = 2.0f;     // DAT_00f416b0
    static constexpr float kGetUpUprightStart = 0.4f;  // DAT_00f4175c
    static constexpr float kGetUpUprightLength = 0.3f; // FLOAT_00f416bc

    const CarBodyState *pBody = pCarState->getBody();

    const float layerStep = deltaTime * kLayerFadeRate;
    BikeLayersCoeff = SlewLimit( 0.0f, LastBikeLayersCoeff, layerStep );
    LastBikeLayersCoeff = BikeLayersCoeff;

    if ( bMustCalculateTotalTimeToGetUp ) {
        TotalTimeToGetUp = animation.getChannelDuration( 0 );
        bMustCalculateTotalTimeToGetUp = false;
    }

    if ( !bJustThrown ) {
        outMatrix = pBody->BikeRiderMatrix;
        const Eigen::Vector3f position = TDUMat4x4MulVec(
            outMatrix, Eigen::Vector3f( 0.0f, kRiderBodyDrop, 0.0f ) );
        outMatrix( 3, 0 ) = position.x();
        outMatrix( 3, 1 ) = position.y();
        outMatrix( 3, 2 ) = position.z();
    } else {
        TimeSinceFalled = 0.0f;
        TimeSinceOnGround = 0.0f;
        if ( AnimFalls[0].File != 0 ) {
            playFallState( 0, 1, true );
            RiderFallState = RFS_Thrown;
        }
        bIsUsedIK = false;
        bMustPlayAnims = false;

        const Eigen::Vector3f seatPosition = TDUMat4x4MulVec(
            outMatrix, Eigen::Vector3f( VTranslation.head<3>() ) );
        outMatrix( 3, 0 ) = seatPosition.x();
        outMatrix( 3, 1 ) = seatPosition.y();
        outMatrix( 3, 2 ) = seatPosition.z();
        LastPosOnBike = outMatrix;
    }

    if ( TimeSinceFalled < kSeatBlendTime ) {
        const float seatRatio = TimeSinceFalled * ( 1.0f / kSeatBlendTime );
        outMatrix = SlerpFrame( LastPosOnBike, outMatrix, seatRatio );
        if ( pBikeBikerAngle != nullptr && ( bIsUsedIK || !bIsFreeMovement ) ) {
            outMatrix =
                MakeRotationMatX( *pBikeBikerAngle * seatRatio ) * outMatrix;
        }
    }
    TimeSinceFalled += deltaTime;

    const bool bOnGround = pBody->bBikeRiderOnGround;
    const bool bFaceDown = outMatrix( 2, 1 ) < 0.0f;
    const bool bAtRest = !pBody->bBikeRiderMoving;

    const float slideSpeed = pBody->BikeRiderVelocity.norm();
    LastSlideRatio =
        SlewLimit( BikeFallSlideRatioResponseCurve.evaluate( slideSpeed ),
                   LastSlideRatio, deltaTime * kSlideRatioRate );
    BikeFallSide[0].Weight = 1.0f - LastSlideRatio;
    BikeFallSide[0].Ratio = kAnimRatioFollowCurve;
    BikeFallSide[1].Weight = LastSlideRatio;
    BikeFallSide[1].Ratio = kAnimRatioFollowCurve;

    const eRiderFallState previousState = RiderFallState;
    eRiderFallState nextState = RFS_OnBike;
    switch ( previousState ) {
    case RFS_OnBike:
    case RFS_Thrown:
    case RFS_SlideFaceDown:
    case RFS_SlideFaceUp:
        if ( !bOnGround && !bAtRest ) {
            TotalTimeToGetUp = 0.0f;
            TimeSinceOnGround = 0.0f;
            nextState = previousState;
        } else if ( bOnGround && !bAtRest ) {
            nextState = bFaceDown ? RFS_SlideFaceDown : RFS_SlideFaceUp;
        } else {
            nextState = bFaceDown ? RFS_LieFaceDown : RFS_LieFaceUp;
        }
        break;
    case RFS_LieFaceDown:
    case RFS_LieFaceUp:
        if ( !bAtRest ) {
            nextState = previousState;
        } else if ( TotalTimeToGetUp < TimeSinceOnGround ) {
            nextState = bFaceDown ? RFS_GetUpFaceDown : RFS_GetUpFaceUp;
        } else {
            nextState = bFaceDown ? RFS_LieFaceDown : RFS_LieFaceUp;
        }
        TimeSinceOnGround += deltaTime;
        break;
    case RFS_GetUpFaceDown:
    case RFS_GetUpFaceUp:
        nextState = previousState;
        TimeSinceOnGround += deltaTime;
        break;
    default:
        break;
    }

    if ( nextState != previousState ) {
        switch ( nextState ) {
        case RFS_SlideFaceDown:
            if ( AnimFalls[1].File != 0 ) {
                playFallState( 1, 5, true );
            }
            break;
        case RFS_SlideFaceUp:
            if ( AnimFalls[2].File != 0 ) {
                playFallState( 2, 6, true );
            }
            break;
        case RFS_LieFaceDown:
            if ( AnimFalls[3].File != 0 ) {
                playFallState( 3, 2, false );
                bMustCalculateTotalTimeToGetUp = true;
            }
            break;
        case RFS_LieFaceUp:
            if ( AnimFalls[4].File != 0 ) {
                playFallState( 4, 3, false );
                bMustCalculateTotalTimeToGetUp = true;
            }
            break;
        case RFS_GetUpFaceDown:
            if ( AnimFalls[5].File != 0 ) {
                playFallState( 5, 4, true );
            }
            break;
        case RFS_GetUpFaceUp:
            if ( AnimFalls[6].File != 0 ) {
                playFallState( 6, 7, true );
            }
            break;
        default:
            break;
        }
    }
    RiderFallState = nextState;

    const bool bGrounded = nextState >= RFS_LieFaceDown;
    const float uprightStart = TotalTimeToGetUp * kGetUpUprightStart;
    if ( !bGrounded || TimeSinceOnGround < uprightStart ||
         TotalTimeToGetUp <= 0.0f ) {
        return;
    }

    Matrix4x4f_t upright = outMatrix;
    Eigen::Vector3f axisX = upright.block<1, 3>( 0, 0 ).transpose();
    Eigen::Vector3f axisY = upright.block<1, 3>( 1, 0 ).transpose();
    axisX.y() = 0.0f;
    axisX.normalize();
    axisY.y() = 0.0f;
    axisY.normalize();
    const Eigen::Vector3f axisZ = axisX.cross( axisY );
    axisY = axisZ.cross( axisX );
    upright.block<1, 3>( 0, 0 ) = axisX.transpose();
    upright.block<1, 3>( 1, 0 ) = axisY.transpose();
    upright.block<1, 3>( 2, 0 ) = axisZ.transpose();

    if ( TimeSinceOnGround > TotalTimeToGetUp ) {
        outMatrix = upright;
        return;
    }

    const float uprightRatio =
        std::min( ( TimeSinceOnGround - uprightStart ) /
                      ( TotalTimeToGetUp * kGetUpUprightLength ),
                  1.0f );
    outMatrix = SlerpFrame( outMatrix, upright, uprightRatio );
}

void EntityDriver::updateBikeIKTargets() {
    // FUN_007ed010
    static const Eigen::Vector3f kFootRightBias( 0.06f, 0.06f, -0.02f );
    static const Eigen::Vector3f kFootLeftBias( -0.06f, 0.06f, -0.02f );

    if ( !bIsUsedIK ) {
        IKLeftHand.changeDamping( 0.0f, 0.0f );
        IKLeftHandAlignY.changeDamping( 0.0f, 0.0f );
        IKLeftHand2.changeDamping( 0.0f, 0.0f );
        IKRightHand.changeDamping( 0.0f, 0.0f );
        IKRightHandAlignY.changeDamping( 0.0f, 0.0f );
        IKRightHand2.changeDamping( 0.0f, 0.0f );
        IKRightFoot.changeDamping( 0.0f, 0.0f );
        IKLeftFoot.changeDamping( 0.0f, 0.0f );
        return;
    }

    Matrix4x4f_t driverMatrix = Matrix4x4f_t::Identity();
    const Eigen::Vector4f *pShaking =
        ( pCarState != nullptr ) ? &pCarState->getBody()->Shaking : nullptr;
    driverMatrix( 3, 0 ) = VTranslation.x();
    driverMatrix( 3, 1 ) = VTranslation.y();
    driverMatrix( 3, 2 ) = VTranslation.z();
    if ( pShaking != nullptr ) {
        driverMatrix( 3, 0 ) -= pShaking->x();
        driverMatrix( 3, 1 ) -= pShaking->y();
        driverMatrix( 3, 2 ) -= pShaking->z();
    }
    if ( pBikeBikerAngle != nullptr && ( bIsUsedIK || !bIsFreeMovement ) ) {
        driverMatrix = MakeRotationMatX( *pBikeBikerAngle ) * driverMatrix;
    }
    const Matrix4x4f_t driverInverse = OrthoInvert( driverMatrix );

    const auto resolveTarget =
        [&]( const SCIKPoint &point,
             const Eigen::Vector3f &bias ) -> Eigen::Vector3f {
        Matrix4x4f_t nodeWorld = Matrix4x4f_t::Identity();
        AccumulateNodeWorldMatrix( point.pNode, nodeWorld );

        Eigen::Vector3f position( nodeWorld( 3, 0 ), nodeWorld( 3, 1 ),
                                  nodeWorld( 3, 2 ) );
        if ( point.pOffset != nullptr ) {
            position = TDUMat4x4MulVec(
                nodeWorld,
                Eigen::Vector3f( point.pOffset->Offset.head<3>() + bias ) );
        }
        return TDUMat4x4MulVec( driverInverse, position );
    };

    const auto resolveAlign = [&]( const SCIKPoint &point,
                                   AnimAlign1NParams &align ) {
        if ( point.pOffset == nullptr ) {
            return;
        }

        Matrix4x4f_t nodeWorld = Matrix4x4f_t::Identity();
        AccumulateNodeWorldMatrix( point.pNode, nodeWorld );
        nodeWorld = MakeRotationMatZ( point.pOffset->RotationZ ) * nodeWorld;
        nodeWorld = MakeRotationMatY( point.pOffset->RotationY ) * nodeWorld;

        const Matrix4x4f_t nodeInverse = nodeWorld.inverse();
        align.Target =
            Eigen::Vector4f( -nodeInverse( 0, 0 ), -nodeInverse( 1, 0 ),
                             -nodeInverse( 2, 0 ), 0.0f );
        align.changeDamping( 1.0f, 0.0f );
    };

    const Eigen::Vector3f noBias = Eigen::Vector3f::Zero();

    const Eigen::Vector3f handLeft =
        resolveTarget( BikeIKPoints[kBikeIKPointHandLeft], noBias );
    IKLeftHand.TargetParams.setTarget( handLeft, 0.0f );
    IKLeftHand.changeDamping( 1.0f, 0.0f );
    IKLeftHand2.TargetParams.setTarget( handLeft, 0.0f );
    IKLeftHand2.changeDamping( 1.0f, 0.0f );
    resolveAlign( BikeIKPoints[kBikeIKPointHandLeft], IKLeftHandAlignY );

    const Eigen::Vector3f handRight =
        resolveTarget( BikeIKPoints[kBikeIKPointHandRight], noBias );
    IKRightHand.TargetParams.setTarget( handRight, 0.0f );
    IKRightHand.changeDamping( 1.0f, 0.0f );
    IKRightHand2.TargetParams.setTarget( handRight, 0.0f );
    IKRightHand2.changeDamping( 1.0f, 0.0f );
    resolveAlign( BikeIKPoints[kBikeIKPointHandRight], IKRightHandAlignY );

    IKRightFoot.TargetParams.setTarget(
        resolveTarget( BikeIKPoints[kBikeIKPointFootRight], kFootRightBias ),
        0.0f );
    IKRightFoot.changeDamping( 1.0f, 0.0f );

    IKLeftFoot.TargetParams.setTarget(
        resolveTarget( BikeIKPoints[kBikeIKPointFootLeft], kFootLeftBias ),
        0.0f );
    IKLeftFoot.changeDamping( 1.0f - LastStandWeight, 0.0f );
}

void EntityDriver::updateAnimChannels( float deltaTime ) {
    static constexpr float kSlewUp = 2.5f;    // DAT_00f416c0
    static constexpr float kSlewDown = -2.5f; // DAT_00f41dd8
    static constexpr float kToNormalised =
        0.49999749660491943f;                              // _DAT_00f420c0
    static constexpr float kHandSizeRatio = 0.9999899864f; // DAT_00f41fd0

    if ( pCarState == nullptr || !animation.isValid() ) {
        return;
    }

    const float slewUp = deltaTime * kSlewUp;
    const float slewDown = deltaTime * kSlewDown;
    float accel = pCarState->getBody()->AccelDiff.z();
    if ( accel - KeepAccelRatio > slewUp ) {
        accel = KeepAccelRatio + slewUp;
    }
    if ( slewDown > accel - KeepAccelRatio ) {
        accel = KeepAccelRatio - slewUp;
    }
    accel = std::min( std::max( accel, -1.0f ), 1.0f );
    KeepAccelRatio = accel;

    AnimStateBlendEntry frontBack;
    frontBack.Weight = 1.0f;
    frontBack.Ratio = ( accel + 1.0f ) * kToNormalised;
    animation.setChannelBlendRatios( 1, &frontBack, 1 );
    animation.setChannelLayer( 1, AnimChannel::ALM_Weighted,
                               bIsAccelerationAnimPlayed ? 1.0f : 0.0f );

    AnimStateBlendEntry handSize;
    handSize.Weight = 1.0f;
    handSize.Ratio = kHandSizeRatio;
    animation.setChannelBlendRatios( 2, &handSize, 1 );

    float handSizeWeight = 0.0f;
    if ( pCarHandSize != nullptr ) {
        handSizeWeight = std::min( std::max( *pCarHandSize, 0.0f ), 1.0f );
    }
    animation.setChannelLayer( 2, AnimChannel::ALM_BindDelta, handSizeWeight );

    AnimStateBlendEntry shake;
    shake.Weight = 1.0f;
    shake.Ratio = kAnimRatioFollowCurve;
    animation.setChannelBlendRatios( 3, &shake, 1 );
    animation.setChannelLayer( 3, AnimChannel::ALM_BindDelta, 1.0f );

    animation.setChannelBlendRatios( 4, &HandOpenRatio, 1 );
    animation.setChannelLayer( 4, AnimChannel::ALM_Weighted,
                               HandOpenLayerWeight );

    animation.setChannelBlendRatios( 5, GearLeverRatio, 5 );
    animation.setChannelLayer( 5, AnimChannel::ALM_Weighted,
                               GearLeverLayerWeight );

    animation.setChannelBlendRatios( 6, &RadioTurnRatio, 1 );
    animation.setChannelLayer( 6, AnimChannel::ALM_Weighted,
                               RadioTurnLayerWeight );
}

float EntityDriver::updateSteerValue( float deltaTime ) {
    static constexpr float kSteerStackScale = 0.25f;      // DAT_00f416dc
    static constexpr float kSteerSmoothRate = 0.3f;       // FLOAT_00f416bc
    static constexpr float kSteerSmoothMaxStep = 0.05f;   // DAT_00f41778
    static constexpr float kSteerSmoothGain = 19.999998f; // _DAT_00f41bec

    SteerTimeRatio.CurrentTime += deltaTime;

    if ( pCarState == nullptr ) {
        return 0.0f;
    }

    const float wheelSteer = pCarState->getBody()->Steering;

    SteerSM = ( SteerSM - SteerStack[SteerStackIndex] ) + wheelSteer;
    SteerStack[SteerStackIndex] = wheelSteer;
    SteerStackIndex++;
    if ( SteerStackIndex > 3 ) {
        SteerStackIndex = 0;
    }

    const CarState::eOwner owner = pCarState->getOwner();
    if ( owner == CarState::eOwner::CSO_Bot ||
         owner == CarState::eOwner::CSO_Traffic ) {
        return SteerSM * kSteerStackScale;
    }

    const PhysicsVehicle *pPhysicsVehicle = pCarState->getPhysicsVehicle();
    if ( pPhysicsVehicle != nullptr && pPhysicsVehicle->isSteerControlUsed() ) {
        if ( deltaTime * kSteerSmoothRate < kSteerSmoothMaxStep ) {
            LastWheelRatio += ( wheelSteer - LastWheelRatio ) * deltaTime *
                              kSteerSmoothRate * kSteerSmoothGain;
        }
        return LastWheelRatio;
    }

    return wheelSteer;
}

void EntityDriver::updateSteeringWheel( float steerNormalised ) {
    // FUN_007ee970
    static constexpr float kSteerColumnRake = 0.26f;
    static constexpr float kSteerWheelRange = 2.29999995f;     // _DAT_00f41fb4
    static constexpr float kSteerWheelHalfRange = 1.14999998f; // DAT_00f416a8
    static constexpr float kWheelOffsetScale =
        0.0007874015718698502f;                     // DAT_00f41758
    static constexpr float kWheelOffsetBias = 0.1f; // FLOAT_00f41668

    if ( pSteer == nullptr ) {
        return;
    }

    Matrix4x4f_t steerMatrix = MakeRotationMatX( kSteerColumnRake );
    steerMatrix( 3, 0 ) = SteerInit( 3, 0 );
    steerMatrix( 3, 1 ) = SteerInit( 3, 1 );
    steerMatrix( 3, 2 ) = SteerInit( 3, 2 );

    const float lockAngle =
        -( steerNormalised * kSteerWheelRange - kSteerWheelHalfRange );
    const Matrix4x4f_t lockRotation = MakeRotationMatZ( lockAngle );
    steerMatrix = ( lockRotation * steerMatrix ).eval();

    CameraVehicle *pCamera =
        ( gpCamera != nullptr ) ? gpCamera->getActiveVehicleCamera() : nullptr;
    if ( pCamera != nullptr &&
         ( pCamera->getFlags() & kShowFlagViewCockpit ) != 0 ) {
        steerMatrix( 3, 1 ) += static_cast<float>( pCamera->getWheelOffset() ) *
                                   kWheelOffsetScale -
                               kWheelOffsetBias;
    }

    pSteer->TransformMatrix = steerMatrix;
    if ( pSteer->pParent == nullptr ) {
        pSteer->pInstance->setModelMatrix( pSteer->TransformMatrix );
    } else {
        Matrix4x4f_t worldMatrix = pSteer->TransformMatrix *
                                   pSteer->pParent->pInstance->getModelMatrix();
        pSteer->pInstance->setModelMatrix( worldMatrix );
    }
    pSteer->propagateTransformMatricesNoScale();
}

void EntityDriver::updateBodyPositionFSM( float deltaTime ) {
    // FUN_007f05ce
    static constexpr float kReachForLever = 5.0f; // DAT_00f41750
    static constexpr float kHandBack = 7.0f;      // DAT_00f41844
    static constexpr float kNeutralHoldTime = 1.0f;

    // Reset EVERY frame, before the FSM - nothing latches.
    HandsTarget = HT_Wheel;

    if ( pCarState == nullptr ) {
        return;
    }
    PhysicsVehicle *pPhysics = pCarState->getPhysicsVehicle();
    if ( pPhysics == nullptr || bIsFreeMovement ) {
        return;
    }

    const PhysicsVehicle::DrivingGearbox gearbox =
        pPhysics->getDrivingGearbox();
    const int32_t shiftState = gearbox.ShiftState;
    const int32_t currentGear = gearbox.Gear;
    const float anticipation = pPhysics->getGearLeverAnticipation();
    const bool bMechanical = ( GearboxType == GBT_Mechanical );

    switch ( CurrentBodyPosition ) {
    case DBP_2HandsOnWheel: {
        const bool bStarting =
            ( shiftState == 1 )
                ? ( LastGear != 0 && kReachForLever > anticipation )
                : ( shiftState == 3 || shiftState == 5 );
        if ( bStarting ) {
            CurrentBodyPosition = DBP_LeverNeutral;
            if ( bMechanical ) {
                HandsTarget = HT_GearLever;
            }
            TimeInLeverNeutralPos = 0.0f;
        }

        LastGear = currentGear; // unconditional

        const bool bDown =
            ( ( static_cast<uint8_t>( LastGear - 1 ) & 1 ) == 0 ) ||
            currentGear == 1 || currentGear == 0;
        updateGearLeverNode( bDown ? GearLeverAngleDown : GearLeverAngleUp );
        break;
    }

    case DBP_LeverUp:
    case DBP_LeverDown: {
        if ( bMechanical ) {
            HandsTarget = HT_GearLever;
        }
        if ( shiftState == 1 && anticipation > kHandBack ) {
            CurrentBodyPosition = DBP_2HandsOnWheel;
        }
        if ( bMechanical ) {
            if ( LastGear != currentGear ) {
                CurrentBodyPosition = static_cast<eDriverBodyPosition>(
                    1000 | ( static_cast<uint8_t>(
                                 ~( static_cast<uint8_t>( LastGear ) - 1 ) ) &
                             1 ) );
                LastGear = currentGear;
            }
        } else if ( shiftState == 5 ) {
            CurrentBodyPosition = DBP_LeverDown;
        } else if ( shiftState == 3 ) {
            CurrentBodyPosition = DBP_LeverUp;
        }
        break;
    }

    case DBP_LeverNeutral: {
        TimeInLeverNeutralPos += deltaTime;
        if ( bMechanical ) {
            HandsTarget = HT_GearLever;
        }
        if ( shiftState == 1 && anticipation > kHandBack &&
             HandsTarget == HT_GearLever &&
             TimeInLeverNeutralPos >= kNeutralHoldTime ) {
            CurrentBodyPosition = DBP_2HandsOnWheel;
        }
        if ( bMechanical ) {
            if ( shiftState == 3 || shiftState == 5 ) {
                CurrentBodyPosition = static_cast<eDriverBodyPosition>(
                    1000 | ( static_cast<uint8_t>(
                                 ~( static_cast<uint8_t>( LastGear ) - 1 ) ) &
                             1 ) );
            }
        } else if ( shiftState == 3 ) {
            CurrentBodyPosition = DBP_LeverUp;
            bMustPlayGearUpAnim = true;
        } else if ( shiftState == 5 ) {
            CurrentBodyPosition = DBP_LeverDown;
            bMustPlayGearDownAnim = true;
        }
        break;
    }

    default:
        break;
    }
}

void IKInterpolator::restart( bool bHorizontalFirst,
                              float deltaTime,
                              float duration,
                              TimeRatio &timeRatio ) {
    // FUN_00424ff0
    bHorizontalTransitionFirst = bHorizontalFirst;
    timeRatio.CurrentTime = deltaTime;
    timeRatio.TimeEndInv = 1.0f / duration;
    LastTarget = CurrentTarget;
}

const Eigen::Vector4f &IKInterpolator::evaluate( const Eigen::Vector4f &target,
                                                 const TimeRatio &timeRatio ) {
    // FUN_00425030
    const float t = timeRatio.TimeEndInv * timeRatio.CurrentTime;

    if ( !( t < 1.0f ) ) {
        CurrentTarget = target;
        return CurrentTarget;
    }

    Eigen::Vector4f corner;
    if ( bHorizontalTransitionFirst ) {
        corner =
            Eigen::Vector4f( target.x(), LastTarget.y(),
                             LastTarget.z() + TransitionZOffset, target.w() );
    } else {
        corner =
            Eigen::Vector4f( LastTarget.x(), target.y(),
                             target.z() + TransitionZOffset, LastTarget.w() );
    }

    const Eigen::Vector4f control1 =
        LastTarget + ( corner - LastTarget ) * TangentLength;
    const Eigen::Vector4f control2 =
        target + ( corner - target ) * TangentLength;

    const float u = 1.0f - t;
    const float k = t * 3.0f * u;
    CurrentTarget = LastTarget * ( u * u * u ) + control1 * ( k * u ) +
                    control2 * ( k * t ) + target * ( t * t * t );
    return CurrentTarget;
}

void EntityDriver::updateGearLeverNode( float angle ) {
    // FUN_007ee8c0
    if ( pGearLever == nullptr || GearboxType != GBT_Mechanical ) {
        return;
    }

    const Matrix4x4f_t rotation = MakeRotationMatX( angle );
    pGearLever->TransformMatrix = ( rotation * GearLeverInit ).eval();

    if ( pGearLever->pParent == nullptr ) {
        pGearLever->pInstance->setModelMatrix( pGearLever->TransformMatrix );
    } else {
        Matrix4x4f_t worldMatrix =
            pGearLever->TransformMatrix *
            pGearLever->pParent->pInstance->getModelMatrix();
        pGearLever->pInstance->setModelMatrix( worldMatrix );
    }
    pGearLever->propagateTransformMatricesNoScale();
}

void EntityDriver::updateGearRatios( float deltaTime ) {
    // FUN_007f07f8
    static constexpr float kGearIKEpsilon = 1e-5f;  // DAT_00f41710
    static constexpr float kGearIKHold = 0.98f;     // _DAT_00f420bc
    static constexpr float kGearTransition = 0.3f;  // FLOAT_00f416bc
    static constexpr float kRadioTransition = 0.5f; // FLOAT_00f41674
    static constexpr float kMechanicalGate = 0.99f; // DAT_00f41a44
    static constexpr float kMechanicalRate = 5.0f;  // DAT_00f41750
    static constexpr float kSequentialRate = 2.5f;  // DAT_00f416c0

    if ( HandsTarget == HT_GearLever && PrevHandsTarget != HT_GearLever ) {
        HandsTargetTransitionTime = kGearTransition;
    }

    const float ratio01 = std::min(
        SteerTimeRatio.TimeEndInv * SteerTimeRatio.CurrentTime, 1.0f );

    const float target = ( HandsTarget == HT_GearLever ) ? 1.0f : 0.0f;
    const float step = ( 1.0f / HandsTargetTransitionTime ) * deltaTime;

    IKRatioGear = target;
    if ( target - LastIKRatioGear > step ) {
        IKRatioGear = LastIKRatioGear + step;
    }
    if ( 0.0f - step > IKRatioGear - LastIKRatioGear ) {
        IKRatioGear = LastIKRatioGear - step;
    }
    if ( HandsTarget == HT_GearLever && IKRatioGear > kGearIKHold &&
         ratio01 < 1.0f ) {
        IKRatioGear = kGearIKHold;
    }
    LastIKRatioGear = IKRatioGear;

    const bool bIsGearIKActive = IKRatioGear > kGearIKEpsilon;
    bool bIsRadioAnimActive = false;

    if ( !bMustPlayRadioAnim || bIsGearIKActive ) {
        RatioRadioAnim = 0.0f;
        PrevRatioRadioAnim = 0.0f;
        bMustPlayRadioAnim = false;
    } else {
        const float radioStep = deltaTime * RadioAnimTotalTimeInv;

        RatioRadioAnim = 1.0f;
        if ( 1.0f - PrevRatioRadioAnim > radioStep ) {
            RatioRadioAnim = PrevRatioRadioAnim + radioStep;
        }
        if ( RatioRadioAnim - PrevRatioRadioAnim < 0.0f - radioStep ) {
            RatioRadioAnim = PrevRatioRadioAnim - radioStep;
        }
        PrevRatioRadioAnim = RatioRadioAnim;

        HandsTarget = ( RatioRadioAnim <= RadioAnimTotalTimeInv * 0.2f )
                          ? HT_Wheel
                          : HT_Radio;
        HandsTargetTransitionTime = kRadioTransition;
        bIsRadioAnimActive = true;
    }

    if ( RatioRadioAnim > 1.0f - RadioAnimTotalTimeInv * 0.7f ) {
        HandsTarget = HT_Wheel;
    }
    if ( std::fabs( RatioRadioAnim - 1.0f ) < kGearIKEpsilon ) {
        bMustPlayRadioAnim = false;
        RatioRadioAnim = 0.0f;
    }

    if ( HandsTarget != PrevHandsTarget ) {
        IKSteerInterpolator.restart( PrevHandsTarget == HT_Wheel, deltaTime,
                                     HandsTargetTransitionTime,
                                     SteerTimeRatio );
        PrevHandsTarget = HandsTarget;
    }

    float handOpen = 0.0f;
    HandOpenLayerWeight = 0.0f;
    if ( bIsGearIKActive ) {
        handOpen = GearingHandOpenResponseCurve.evaluate( IKRatioGear );
        HandOpenLayerWeight = handOpen;
    } else if ( bIsRadioAnimActive ) {
        handOpen = RatioHandOpenResponseCurve.evaluate( RatioRadioAnim );
    }
    HandOpenRatio.Ratio = handOpen;
    HandOpenRatio.Weight = 1.0f;

    if ( GearboxType == GBT_Mechanical ) {
        const bool bOddGear =
            ( ( static_cast<uint8_t>( LastGear ) - 1 ) & 1 ) == 0;

        RatioGearUp = 0.0f;
        RatioGearDown = 0.0f;
        if ( IKRatioGear > kMechanicalGate ) {
            if ( CurrentBodyPosition == DBP_LeverUp ) {
                RatioGearUp = 1.0f;
            } else if ( CurrentBodyPosition == DBP_LeverDown ) {
                RatioGearDown = 1.0f;
            }
        }

        const float rate = deltaTime * kMechanicalRate;
        if ( RatioGearUp - PrevRatioGearUp > rate ) {
            RatioGearUp = PrevRatioGearUp + rate;
        }
        if ( -rate > RatioGearUp - PrevRatioGearUp ) {
            RatioGearUp = PrevRatioGearUp - rate;
        }
        PrevRatioGearUp = RatioGearUp;

        if ( RatioGearDown - PrevRatioGearDown > rate ) {
            RatioGearDown = PrevRatioGearDown + rate;
        }
        if ( -rate > RatioGearDown - PrevRatioGearDown ) {
            RatioGearDown = PrevRatioGearDown - rate;
        }
        PrevRatioGearDown = RatioGearDown;

        if ( CurrentBodyPosition == DBP_LeverDown ) {
            updateGearLeverNode( ( 1.0f - RatioGearDown ) * GearLeverAngleDown +
                                 GearLeverAngleUp * RatioGearDown );
        } else if ( CurrentBodyPosition == DBP_LeverUp ) {
            updateGearLeverNode( ( 1.0f - RatioGearUp ) * GearLeverAngleUp +
                                 GearLeverAngleDown * RatioGearUp );
        }
        float leverUpWeight = bOddGear ? 1.0f : 0.0f;
        float leverDownWeight = bOddGear ? 0.0f : 1.0f;
        if ( IKRatioGear > kMechanicalGate ) {
            if ( CurrentBodyPosition == DBP_LeverUp ) {
                leverUpWeight = 1.0f;
                leverDownWeight = 0.0f;
            } else if ( CurrentBodyPosition == DBP_LeverDown ) {
                leverUpWeight = 0.0f;
                leverDownWeight = 1.0f;
            }
        }

        GearLeverRatio[0] = {};
        GearLeverRatio[1] = {};
        GearLeverRatio[2] = {};
        GearLeverRatio[3].Weight = leverUpWeight * IKRatioGear;
        GearLeverRatio[3].Ratio = RatioGearUp;
        GearLeverRatio[4].Weight = leverDownWeight * IKRatioGear;
        GearLeverRatio[4].Ratio = RatioGearDown;
        GearLeverLayerWeight =
            GearingLayerResponseCurve.evaluate( IKRatioGear );
    } else if ( GearboxType == GBT_Sequential ) {
        RatioGearUp = 0.0f;
        RatioGearDown = 0.0f;
        float seqUpWeight = 0.0f;
        float seqDownWeight = 0.0f;
        if ( bMustPlayGearUpAnim ) {
            RatioGearUp = 1.0f;
            seqUpWeight = 1.0f;
        } else if ( bMustPlayGearDownAnim ) {
            RatioGearDown = 1.0f;
            seqDownWeight = 1.0f;
        }

        const float rate = deltaTime * kSequentialRate;
        if ( RatioGearUp - PrevRatioGearUp > rate ) {
            RatioGearUp = PrevRatioGearUp + rate;
        }
        if ( -rate > RatioGearUp - PrevRatioGearUp ) {
            RatioGearUp = PrevRatioGearUp - rate;
        }
        if ( RatioGearDown - PrevRatioGearDown > rate ) {
            RatioGearDown = PrevRatioGearDown + rate;
        }
        if ( -rate > RatioGearDown - PrevRatioGearDown ) {
            RatioGearDown = PrevRatioGearDown - rate;
        }
        PrevRatioGearUp = RatioGearUp;
        PrevRatioGearDown = RatioGearDown;

        if ( RatioGearUp == 1.0f ) {
            bMustPlayGearUpAnim = false;
            PrevRatioGearUp = 0.0f;
        }
        if ( RatioGearDown == 1.0f ) {
            bMustPlayGearDownAnim = false;
            PrevRatioGearDown = 0.0f;
        }

        GearLeverRatio[0] = {};
        GearLeverRatio[1].Weight = seqUpWeight;
        GearLeverRatio[1].Ratio = RatioGearUp;
        GearLeverRatio[2].Weight = seqDownWeight;
        GearLeverRatio[2].Ratio = RatioGearDown;
        GearLeverRatio[3] = {};
        GearLeverRatio[4] = {};
        GearLeverLayerWeight = 1.0f;
    } else if ( GearboxType == GBT_Auto ) {
        GearLeverLayerWeight = 0.0f;
    }

    RadioTurnRatio.Weight = 1.0f;
    RadioTurnRatio.Ratio =
        RadioTurnRatioResponseCurve.evaluate( RatioRadioAnim );
    RadioTurnLayerWeight =
        RadioTurnWeightResponseCurve.evaluate( RatioRadioAnim );
}

void EntityDriver::updateStandDriveRatio( float deltaTime ) {
    // FUN_007f046f
    static constexpr float kStandDriveSpeedThreshold = 4.0f;  // DAT_00f418d4
    static constexpr float kStandDriveSteerDeadZone = 0.001f; // FLOAT_00f4167c
    static constexpr float kStandDriveSlewUp = 2.5f;          // DAT_00f416c0
    static constexpr float kStandDriveSlewDown = -2.5f;       // _DAT_00f41dd8
    static constexpr float kSteerToNormalised =
        0.49999749660491943f; // _DAT_00f420c0

    if ( pCarState == nullptr ) {
        return;
    }

    const float steer = updateSteerValue( deltaTime );
    const float steerNormalised = ( steer + 1.0f ) * kSteerToNormalised;

    updateSteeringWheel( steerNormalised );

    float weight = 1.0f;
    if ( pCarState->getBody()->SpeedInKmh >= kStandDriveSpeedThreshold ||
         std::fabs( steer ) >= kStandDriveSteerDeadZone ) {
        weight = 0.0f;
    }

    const float slewUp = deltaTime * kStandDriveSlewUp;
    if ( slewUp < weight - LastStandDriveWeight ) {
        weight = LastStandDriveWeight + slewUp;
    }
    const float slewDown = deltaTime * kStandDriveSlewDown;
    if ( weight - LastStandDriveWeight < slewDown ) {
        weight = LastStandDriveWeight - slewUp;
    }

    LastStandDriveWeight = weight;
    StandDriveRatio[0].Weight = weight;
    StandDriveRatio[0].Ratio = kAnimRatioFollowCurve;
    StandDriveRatio[1].Weight = 1.0f - weight;
    StandDriveRatio[1].Ratio = steerNormalised;

    animation.setBlendRatios( StandDriveRatio, 2 );
}

void EntityDriver::buildIKChains( int32_t matchID ) {
    static constexpr uint32_t kLeftArmNodes[3] = { 0x74d3d78d, 0xd94546d3,
                                                   0x0f4112f6 };
    static constexpr uint32_t kRightArmNodes[3] = { 0xd5d9aac6, 0x4748f02b,
                                                    0x3428fa75 };
    static constexpr uint32_t kLeftLegNodes[3] = { 0xd9b2d508, 0x74cdca98,
                                                   0x1f4220f4 };
    static constexpr uint32_t kRightLegNodes[3] = { 0xb4d7252b, 0xcfccb5c6,
                                                    0x3536f885 };

    for ( int32_t i = 0; i < 3; i++ ) {
        IKLeftHand.NodesID[i] = kLeftArmNodes[i];
        IKRightHand.NodesID[i] = kRightArmNodes[i];
        IKLeftFoot.NodesID[i] = kLeftLegNodes[i];
        IKRightFoot.NodesID[i] = kRightLegNodes[i];
    }

    for ( int32_t i = 0; i < 3; i++ ) {
        IKLeftHand2.NodesID[i] = kLeftArmNodes[i];
        IKRightHand2.NodesID[i] = kRightArmNodes[i];
    }

    IKLeftHandAlignY.NodeID = 0xd8d4c694;  // "LeftHand", node 22
    IKRightHandAlignY.NodeID = 0xd6c8b12a; // "RightHand", node 62
    IKLeftHandAlignY.LocalAxis = Eigen::Vector4f( 0.0f, 0.0f, 1.0f, 0.0f );
    IKRightHandAlignY.LocalAxis = Eigen::Vector4f( 0.0f, 0.0f, 1.0f, 0.0f );
    IKLeftHandAlignY.changeDamping( 1.0f, 0.0f );
    IKRightHandAlignY.changeDamping( 1.0f, 0.0f );

    IKBack.NodesID[0] = 0x6e6970b8; // "Spine", node 14
    IKBack.NodesID[1] = 0x42caaeaa; // Spine2 effector marker, node 17

    IKLeftShoulderBelt1.NodeID = 0xe8d2fbbe;  // node 72
    IKLeftShoulderBelt2.NodeID = 0xe8d2fcbe;  // node 73
    IKRightShoulderBelt1.NodeID = 0xd4fddb3a; // node 74
    IKRightShoulderBelt2.NodeID = 0xd4fedb3a; // node 75
    IKRightHipsBelt.NodeID = 0xd9d0ce95;      // node 76
    IKLeftHipsBelt.NodeID = 0xd93741e5;       // node 77

    animation.setExpressionList( ( DriveType == RT_Bike ) ? &BikeExpressionList
                                                          : &ExpressionList,
                                 matchID );
}

void EntityDriver::updateIKTargets() {
    // FUN_007ee050
    if ( !bIsUsedIK ) {
        IKLeftHand.changeDamping( 0.0f, 0.0f );
        IKRightHand.changeDamping( 0.0f, 0.0f );
        IKLeftFoot.changeDamping( 0.0f, 0.0f );
        IKRightFoot.changeDamping( 0.0f, 0.0f );
        IKBack.changeDamping( 0.0f, 0.0f );
        return;
    }

    const Eigen::Vector3f driverOrigin = VTranslation.head<3>();

    const auto resolvePoint = [&]( const SCIKPoint &point ) -> Eigen::Vector3f {
        Matrix4x4f_t nodeWorld = Matrix4x4f_t::Identity();
        AccumulateNodeWorldMatrix( point.pNode, nodeWorld );

        Eigen::Vector3f position( nodeWorld( 3, 0 ), nodeWorld( 3, 1 ),
                                  nodeWorld( 3, 2 ) );
        if ( point.pOffset != nullptr ) {
            position = TDUMat4x4MulVec(
                nodeWorld, Eigen::Vector3f( point.pOffset->Offset.x(),
                                            point.pOffset->Offset.y(),
                                            point.pOffset->Offset.z() ) );
        }
        return position - driverOrigin;
    };

    const auto pushTarget = [&]( const SCIKPoint &point,
                                 AnimExpression &expression,
                                 AnimTargetParams &target ) {
        if ( point.pNode == nullptr ) {
            expression.changeDamping( 0.0f, 0.0f );
            return;
        }
        target.setTarget( resolvePoint( point ), 0.0f );
        expression.changeDamping( 1.0f, 0.0f );
    };

    const auto pushRightHand = [&]( const SCIKPoint &point ) {
        if ( point.pNode == nullptr ) {
            IKRightHand.changeDamping( 0.0f, 0.0f );
            return;
        }
        const Eigen::Vector3f raw = resolvePoint( point );
        const Eigen::Vector4f smoothed = IKSteerInterpolator.evaluate(
            Eigen::Vector4f( raw.x(), raw.y(), raw.z(), 0.0f ),
            SteerTimeRatio );
        IKRightHand.TargetParams.setTarget( smoothed.head<3>(), 0.0f );
        IKRightHand.changeDamping( 1.0f, 0.0f );
    };

    pushTarget( CarIKPoints[kCarIKPointHandLeft], IKLeftHand,
                IKLeftHand.TargetParams );

    switch ( HandsTarget ) {
    case HT_Wheel:
        pushRightHand( CarIKPoints[kCarIKPointHandRight] );
        break;
    case HT_GearLever:
        pushRightHand( CarIKPoints[kCarIKPointGearLever] );
        break;
    case HT_Radio:
        pushRightHand( CarIKPoints[kCarIKPointRadio] );
        break;
    default:
        IKRightHand.changeDamping( 0.0f, 0.0f );
        break;
    }
    pushTarget( CarIKPoints[kCarIKPointFootRight], IKRightFoot,
                IKRightFoot.TargetParams );
    pushTarget( CarIKPoints[kCarIKPointFootLeft], IKLeftFoot,
                IKLeftFoot.TargetParams );
    pushTarget( CarIKPoints[kCarIKPointBackLeft], IKBack, IKBack.TargetParams );

    updateBeltTargets();
}

void EntityDriver::updateBeltTargets() {
    // FUN_007ee050 (inlined)
    AnimLinkParams *pBelts[6] = { &IKRightShoulderBelt1, &IKRightShoulderBelt2,
                                  &IKLeftShoulderBelt1,  &IKLeftShoulderBelt2,
                                  &IKRightHipsBelt,      &IKLeftHipsBelt };
    static constexpr int32_t kBeltPoints[6] = {
        kCarIKPointRightShoulderBelt1, kCarIKPointRightShoulderBelt2,
        kCarIKPointLeftShoulderBelt1,  kCarIKPointLeftShoulderBelt2,
        kCarIKPointRightHipsBelt,      kCarIKPointLeftHipsBelt };

    if ( !bIsUsedIK && !bIsSeating ) {
        for ( AnimLinkParams *pBelt : pBelts ) {
            pBelt->changeDamping( 0.0f, 0.0f );
        }
        return;
    }

    if ( pSeatModel == nullptr ) {
        return;
    }

    Matrix4x4f_t seatWorld = Matrix4x4f_t::Identity();
    AccumulateNodeWorldMatrix( pSeatModel, seatWorld );

    const Eigen::Vector3f driverOrigin = VTranslation.head<3>();

    for ( int32_t i = 0; i < 6; i++ ) {
        const SCIKOffset *pOffset = CarIKPoints[kBeltPoints[i]].pOffset;
        if ( pOffset == nullptr ) {
            continue;
        }

        const Eigen::Vector3f anchor = TDUMat4x4MulVec(
            seatWorld,
            Eigen::Vector3f( pOffset->Offset.x(), pOffset->Offset.y(),
                             pOffset->Offset.z() ) );

        pBelts[i]->TargetParams.setTarget( anchor - driverOrigin, 0.0f );
        pBelts[i]->changeDamping( 1.0f, 0.0f );
    }
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

int32_t EntityDriver::getRenderSceneID() {
    // FUN_007ec180
    CameraGame *pCamera =
        ( gpCamera != nullptr ) ? gpCamera->getActiveCamera() : nullptr;
    if ( pCamera == nullptr ) {
        return EntityAvatar::getRenderSceneID();
    }

    eRenderPass renderPass = eRenderPass::RP_Invalid;
    const bool bIsPlayerDriver = ( avatarType == kAvatarTypeDriver );

    if ( !bIsPlayerDriver && ( gpActiveGMScene == nullptr ||
                               !gpActiveGMScene->isDrawnInFakeDOF() ) ) {
        renderPass = eRenderPass::RP_NearOpaque;
    }

    const uint32_t showFlags = pCamera->getFlags();
    const bool bInGarage =
        ( gpTestDriveInstance != nullptr &&
          gpTestDriveInstance->getActiveGameMode() == GM_Garage );
    if ( ( showFlags & ( kShowFlagViewCockpit | kShowFlagViewHood ) ) != 0 &&
         bIsPlayerDriver && !bInGarage ) {
        renderPass = eRenderPass::RP_Cockpit;
    }

    OTDU_UNIMPLEMENTED_ONCE; // TODO: GM_INTRO - forced cockpit scene

    if ( renderPass == eRenderPass::RP_Invalid ) {
        return EntityAvatar::getRenderSceneID();
    }

    if ( AlphaSceneSlot != -1 && gpMngCarAlpha != nullptr ) {
        renderPass = gpMngCarAlpha->bindAndGetRenderPassForAlpha(
            renderPass, static_cast<uint32_t>( AlphaSceneSlot ) );
    }

    return renderPass;
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
