#include "shared.h"
#include "entity_avatar_passenger.h"

#include "entity_bytecode.h"
#include "core/maths_helper.h"
#include "database/gs_database.h"
#include "filesystem/binary.h"
#include "game/avatar/gs_avatar.h"
#include "game/scenaric/gs_scenaric.h"
#include "game/vehicle/car_state.h"
#include "game/vehicle/vehicle_base.h"
#include "game/vehicle/vehicle_car.h"
#include "game/vehicle/vehicle_controller.h"
#include "game/vehicle/vehicle_resource.h"
#include "render/animation/anim_state.h"
#include "render/camera/camera_game.h"
#include "render/camera/gs_camera.h"
#include "render/gs_render.h"
#include "render/hiearchy_node.h"
#include "render/light.h"

static constexpr float kSeatedPassengerLODDistances[3] = {
    20.0f, 10.0f, 0.0f }; // DAT_00f8b270

static constexpr uint32_t kShowFlagInterior = 1u << 16;
static constexpr uint32_t kShowFlagViewCockpit = 1u << 18;

static constexpr float kAccelSlewUp = 2.5f;    // DAT_00f416c0
static constexpr float kAccelSlewDown = -2.5f; // _DAT_00f41dd8
static constexpr float kAccelToNormalised =
    0.49999749660491943f;                             // _DAT_00f420c0
static constexpr float kStandAnimEndEpsilon = 1e-05f; // DAT_00f41710

static constexpr uint64_t kConvertibleBodyHashcode0 = 0x56760d;
static constexpr uint64_t kConvertibleBodyHashcode1 = 0x57fcad;

static constexpr int32_t kCarIKPointPassengerBack = 11;      // "Back_HR_R"
static constexpr int32_t kCarIKPointPassengerFootRight = 12; // "PsgFoot_HR_R"
static constexpr int32_t kCarIKPointPassengerFootLeft = 13;  // "PsgFoot_HR_L"
static constexpr int32_t kCarIKPointPassengerSeat = 10;      // "PsgSeat_HR"
static constexpr int32_t kCarIKPointPassengerRightShoulderBelt1 = 21;
static constexpr int32_t kCarIKPointPassengerRightShoulderBelt2 = 22;
static constexpr int32_t kCarIKPointPassengerLeftShoulderBelt1 = 23;
static constexpr int32_t kCarIKPointPassengerLeftShoulderBelt2 = 24;
static constexpr int32_t kCarIKPointPassengerRightHipsBelt = 25;
static constexpr int32_t kCarIKPointPassengerLeftHipsBelt = 26;

static Eigen::Vector4f RotateIKOffset( const Matrix4x4f_t &nodeWorld,
                                       const SCIKOffset *pOffset ) {
    const Eigen::Vector4f offset( pOffset->Offset.x(), pOffset->Offset.y(),
                                  pOffset->Offset.z(), 0.0f );
    return TDUMat4x4MulVec( nodeWorld, offset );
}

EntityAvatarPassenger::EntityAvatarPassenger()
    : EntityAvatar(),
      CarIKPoints(),
      pCarPassengerSeatHeight( nullptr ),
      pCarPassengerSeatFrontBack( nullptr ),
      pAnimCar(),
      NumAnimCar( 0 ),
      AnimCarStand(),
      FrontBackRatio(),
      LastAccelerationRatio( 0.5f ),
      PassengerSeatModelInit( Matrix4x4f_t::Identity() ),
      pPassengerSeatModel( nullptr ),
      pPassengerSeatModelRoot( nullptr ),
      pCarState( nullptr ),
      Translation( Eigen::Vector4f::Zero() ),
      CurrentBehavior( PB_MovementFree ),
      bHasInteriorHR( false ),
      bMustPlayCarAnims( true ),
      bSkeletonHasChanged( false ),
      bMustPlayPassengerStandAnim( false ),
      bIsPlayingPassengerStandAnim( false ),
      bForceNotUpdatePos( false ),
      AnimHi(),
      AnimPeepin() {
    // FUN_007f4bd0
    IKLeftShoulderBelt1.NodeID = 0xe8d2fbbe;
    IKLeftShoulderBelt2.NodeID = 0xe8d2fcbe;
    IKRightShoulderBelt1.NodeID = 0xd4fddb3a;
    IKRightShoulderBelt2.NodeID = 0xd4fedb3a;
    IKRightHipsBelt.NodeID = 0xd9d0ce95;
    IKLeftHipsBelt.NodeID = 0xd93741e5;
}

EntityAvatarPassenger::~EntityAvatarPassenger() {
    // FUN_00424900
}

bool EntityAvatarPassenger::onMessage( FlashMessage *pMessage ) {
    // FUN_007f4db0
    if ( EntityAvatar::onMessage( pMessage ) ) {
        return true;
    }

    CarState *pLocalCar = gpScenaric->getLocalPlayer().getCarState();

    switch ( *pMessage ) {
    case FlashMessage( "avm1" ):
        CineAvatarMode = 1;
        setBehavior( PB_MovementFree );
        return true;

    case FlashMessage( "avm2" ):
    case FlashMessage( "avm3" ):
        CineAvatarMode = 3;
        if ( pLocalCar != nullptr ) {
            gpScenaric->placePassenger( pLocalCar, this );
        }
        CurrentBehavior = PB_PassengerNoAnims;
        LODDistances[0] = kSeatedPassengerLODDistances[0];
        LODDistances[1] = kSeatedPassengerLODDistances[1];
        LODDistances[2] = kSeatedPassengerLODDistances[2];
        animation.resetChannel( 0 );
        bMustPlayCarAnims = true;
        setSeatBeltVisible( false );
        return true;

    case FlashMessage( "avm4" ):
        CineAvatarMode = 4;
        if ( pLocalCar != nullptr ) {
            gpScenaric->placePassenger( pLocalCar, this );
        }
        return true;

    default:
        return false;
    }
}

void EntityAvatarPassenger::initializeForCinematic( void *param_2,
                                                    void *param_3 ) {
    // FUN_007f4670
    if ( CineAvatarMode == 0 ) {
        CurrentBehavior = PB_MovementFree;
        LODDistances[0] = gAvatarLODDistances[0];
        LODDistances[1] = gAvatarLODDistances[1];
        LODDistances[2] = gAvatarLODDistances[2];
        animation.resetChannel( 0 );
        updateIKTargets();
        setSeatBeltVisible( true );
        CineAvatarMode = 0;
    }

    EntityAvatar::initializeForCinematic( param_2, param_3 );
}

void EntityAvatarPassenger::initialize() {
    // FUN_007f5580
    EntityAvatar::initialize();

    ExpressionList.clear();
    ExpressionList.append( &IKBack );
    ExpressionList.append( &IKRightFoot );
    ExpressionList.append( &IKLeftFoot );
    ExpressionList.append( &IKRightShoulderBelt1 );
    ExpressionList.append( &IKRightShoulderBelt2 );
    ExpressionList.append( &IKLeftShoulderBelt1 );
    ExpressionList.append( &IKLeftShoulderBelt2 );
    ExpressionList.append( &IKRightHipsBelt );
    ExpressionList.append( &IKLeftHipsBelt );
}

void EntityAvatarPassenger::loadResources( ScenaricBank *param_2 ) {
    // FUN_007f5640
    EntityActor::loadResources( param_2 );

    for ( uint32_t i = 0; i < kNumAnimCar; i++ ) {
        if ( pAnimCar[i].File != 0 ) {
            pAnimCar[i].pClip =
                gpScenaric->resolveClip( param_2, pAnimCar[i].pRefBas );
        }
    }
}

void EntityAvatarPassenger::unloadResources() {
    // FUN_007f4bb0
    if ( pCarState != nullptr ) {
        setCarState( nullptr );
    }

    EntityAvatar::unloadResources();
}

void EntityAvatarPassenger::tick( PlayerGameMech *pGameMech,
                                  float deltaTime,
                                  float totalTime ) {
    // FUN_007f5680
    EntityAvatar::tick( pGameMech, deltaTime, totalTime );

    if ( pCarState != nullptr && isSeated() && !bForceNotUpdatePos ) {
        Matrix4x4f_t passengerMatrix =
            pCarState->getBody()->OffsetedWorldMatrix;

        Eigen::Vector4f seatOffset = Translation;
        seatOffset.w() = 0.0f;
        const Eigen::Vector4f seatPosition =
            Mat4x4fTransform( passengerMatrix, seatOffset );
        passengerMatrix( 3, 0 ) = seatPosition.x();
        passengerMatrix( 3, 1 ) = seatPosition.y();
        passengerMatrix( 3, 2 ) = seatPosition.z();

        setTransform( passengerMatrix, false );
    }

    if ( pPooledAvatarEntry != nullptr ) {
        pPooledAvatarEntry->CarBelt.setLowResVisible( isSeated() );
    }

    updateCarAnims( deltaTime );
    updateLightingAttenuation();
}

bool EntityAvatarPassenger::draw( RenderScene *pScene ) {
    // FUN_007f54a0
    CameraGame *pActiveCamera =
        ( gpCamera != nullptr ) ? gpCamera->getActiveCamera() : nullptr;
    if ( pActiveCamera == nullptr ) {
        return true;
    }

    const uint32_t showFlags = pActiveCamera->getFlags();
    if ( !isSeated() || ( showFlags & kShowFlagInterior ) == 0 ||
         ( showFlags & kShowFlagViewCockpit ) != 0 ) {
        return EntityAvatar::draw( pScene );
    }

    return true;
}

void EntityAvatarPassenger::instanciate( MngActor *pMngActor,
                                         Entity *param_1 ) {
    // FUN_007f54e0
    EntityAvatar::instanciate( pMngActor, param_1 );

    EntityAvatarPassenger *pTarget =
        static_cast<EntityAvatarPassenger *>( param_1 );

    pTarget->NumAnimCar = NumAnimCar;
    for ( uint32_t i = 0; i < NumAnimCar && i < kNumAnimCar; i++ ) {
        pTarget->pAnimCar[i].ID = pAnimCar[i].ID;
        pTarget->pAnimCar[i].File = pAnimCar[i].File;
        pTarget->pAnimCar[i].pRefBas = pAnimCar[i].pRefBas;
    }
}

int32_t EntityAvatarPassenger::getRenderSceneID() {
    // FUN_007f3e70
    CameraGame *pActiveCamera =
        ( gpCamera != nullptr ) ? gpCamera->getActiveCamera() : nullptr;
    if ( pActiveCamera != nullptr &&
         ( pActiveCamera->getFlags() & kShowFlagViewCockpit ) != 0 &&
         isSeated() ) {
        return static_cast<int32_t>( eRenderPass::RP_Cockpit );
    }

    return EntityAvatar::getRenderSceneID();
}

EntityAvatarPassenger *EntityAvatarPassenger::getPassenger() {
    // FUN_007f3cf0
    return this;
}

void EntityAvatarPassenger::getCarAlphaScreenRect( Eigen::Vector4f &outRect ) {
    // FUN_007f4710
    if ( pCarState != nullptr && pCarState->getVehicleInstance() != nullptr ) {
        pCarState->getVehicleInstance()->computeScreenRect( outRect );
        return;
    }

    outRect = Eigen::Vector4f( 0.0f, 0.0f, 1.0f, 1.0f );
}

void EntityAvatarPassenger::onSkeletonChange( void *pSkeleton ) {
    // FUN_007f3eb0
    EntityAvatar::onSkeletonChange( pSkeleton );
    bSkeletonHasChanged = true;
}

void EntityAvatarPassenger::relightFromWeather( DirectionalLight *pLight ) {
    // FUN_007f4ef0
    if ( !isSeated() ) {
        EntityAvatar::relightFromWeather( pLight );
        return;
    }

    if ( pCarState == nullptr ) {
        return;
    }

    const VehicleBase *pDisplay = pCarState->getVehicleInstance();
    if ( pDisplay == nullptr ) {
        return;
    }

    const DirectionalLight &carLight = pDisplay->getMainLight();
    const float lightModulator = pLightingAttenuation[ALF_LightModulator];
    pLight->Ambient = carLight.Ambient * lightModulator;
    pLight->Diffuse = carLight.Diffuse * lightModulator;
    pLight->Specular = carLight.Specular * lightModulator;
    pLight->Direction = carLight.Direction;

    currentEnvFactor = pDisplay->getCurrentEnvFactor();
    currentEnvFactorBackFace = pDisplay->getCurrentEnvFactorBackFace();
}

bool EntityAvatarPassenger::isShadowCaster() {
    // FUN_007f3d00
    if ( pPooledAvatarEntry == nullptr || InstanceRefCount <= 0 ) {
        return false;
    }

    if ( !isSeated() ) {
        return true;
    }

    CameraGame *pActiveCamera =
        ( gpCamera != nullptr ) ? gpCamera->getActiveCamera() : nullptr;
    const uint32_t showFlags =
        ( pActiveCamera != nullptr ) ? pActiveCamera->getFlags() : 0u;
    return ( showFlags & kShowFlagInterior ) == 0 ||
           ( showFlags & kShowFlagViewCockpit ) != 0;
}

void EntityAvatarPassenger::setCarState( CarState *pNewCarState ) {
    // FUN_007f49d0
    if ( pCarState != nullptr && pPassengerSeatModel != nullptr ) {
        pPassengerSeatModel->TransformMatrix = PassengerSeatModelInit;
        setSeatBeltVisible( true );
    }

    pCarState = pNewCarState;

    DBEntryCarPhysicsData *pCarData =
        ( pCarState != nullptr && gpDatabase != nullptr )
            ? gpDatabase->getCarDBEntry( pCarState->getCarID() )
            : nullptr;
    if ( pCarData == nullptr || !gpDatabase->isBike( pCarData ) ) {
        BuildCarIKPoints( CarIKPoints, pCarState );
    }

    pCarPassengerSeatHeight = nullptr;
    pCarPassengerSeatFrontBack = nullptr;
    if ( pCarState == nullptr ) {
        return;
    }

    SCIKCarOffsets *pOffsets = gpScenaric->getIKOffsetDB().findCarOffsets(
        pCarState->getCarID(), true );
    if ( pOffsets != nullptr ) {
        pCarPassengerSeatHeight = &pOffsets->PassengerSeatHeight;
        pCarPassengerSeatFrontBack = &pOffsets->PassengerSeatFrontBack;
    }

    VehicleBase *pDisplay = pCarState->getVehicleInstance();
    if ( pDisplay == nullptr ||
         pDisplay->getType() != VehicleBase::eType::VBT_Car ) {
        return;
    }

    const VehicleResource &interior = pDisplay->getInteriorResource();
    bHasInteriorHR =
        ( interior.pRender3DD != nullptr && interior.pResourceBank != nullptr );

    HiearchyNode *pLowResRoot =
        pDisplay->shouldUnstream() ? nullptr : pDisplay->getNodeByIndex( 0 );
    HiearchyNode *pHighResRoot = static_cast<VehicleController *>( pDisplay )
                                     ->getInstantiatedInteriorNode();

    HiearchyNode *pSeat = nullptr;
    if ( pHighResRoot != nullptr ) {
        pSeat = pHighResRoot->findChild( GetIdentifier64bit( "Seat_HR_R" ) );
    }
    if ( pSeat == nullptr && pLowResRoot != nullptr ) {
        pSeat = pLowResRoot->findChild( GetIdentifier64bit( "Seat_LR_R" ) );
    }

    setSeatBeltVisible( !isSeated() );
    setSeatModel( pSeat );
}

void EntityAvatarPassenger::seatInCar() {
    // FUN_008056d0 (inlined)
    CurrentBehavior = PB_Passenger;
    LODDistances[0] = kSeatedPassengerLODDistances[0];
    LODDistances[1] = kSeatedPassengerLODDistances[1];
    LODDistances[2] = kSeatedPassengerLODDistances[2];
    bMustPlayCarAnims = true;
    setSeatBeltVisible( false );
}

void EntityAvatarPassenger::unseat() {
    // FUN_00805620 (inlined)
    CurrentBehavior = PB_MovementFree;
    LODDistances[0] = gAvatarLODDistances[0];
    LODDistances[1] = gAvatarLODDistances[1];
    LODDistances[2] = gAvatarLODDistances[2];
    animation.resetChannel( 0 );
    updateIKTargets();
}

int32_t EntityAvatarPassenger::setBehavior( const int32_t behavior ) {
    // FUN_007f44a0
    const int32_t previousBehavior = CurrentBehavior;
    CurrentBehavior = behavior;

    bool bSeated = false;
    switch ( behavior ) {
    case PB_MovementFree:
        LODDistances[0] = gAvatarLODDistances[0];
        LODDistances[1] = gAvatarLODDistances[1];
        LODDistances[2] = gAvatarLODDistances[2];
        animation.resetChannel( 0 );
        updateIKTargets();
        break;

    case PB_PassengerNoAnims:
        LODDistances[0] = kSeatedPassengerLODDistances[0];
        LODDistances[1] = kSeatedPassengerLODDistances[1];
        LODDistances[2] = kSeatedPassengerLODDistances[2];
        animation.resetChannel( 0 );
        bSeated = true;
        bMustPlayCarAnims = true;
        break;

    case PB_Passenger:
        LODDistances[0] = kSeatedPassengerLODDistances[0];
        LODDistances[1] = kSeatedPassengerLODDistances[1];
        LODDistances[2] = kSeatedPassengerLODDistances[2];
        bSeated = true;
        bMustPlayCarAnims = true;
        break;

    case PB_Idle:
        LODDistances[0] = gAvatarLODDistances[0];
        LODDistances[1] = gAvatarLODDistances[1];
        LODDistances[2] = gAvatarLODDistances[2];
        updateIKTargets();
        if ( AnimStand.File != 0 ) {
            playCarAnimState( AnimStand, nullptr, 0u );
        }
        break;

    default:
        break;
    }

    setSeatBeltVisible( !bSeated );
    return previousBehavior;
}

void EntityAvatarPassenger::deserializeAnims( BinaryReader *pReader,
                                              uint32_t minVersion ) {
    EntityActor::deserialize( pReader );
    NumAnimCar = DecodeBytecodeAnimCarTable( pReader, pAnimCar, kNumAnimCar );
    if ( gpScenaric->getVersion() > minVersion ) {
        AnimHi.deserialize( pReader );
        AnimPeepin.deserialize( pReader );
    }
}

void EntityAvatarPassenger::setSeatModel( HiearchyNode *pNode ) {
    // FUN_007f4920
    pPassengerSeatModel = pNode;

    if ( pNode == nullptr ) {
        PassengerSeatModelInit = Matrix4x4f_t::Identity();
        return;
    }

    PassengerSeatModelInit = pNode->TransformMatrix;
    pPassengerSeatModelRoot = pNode;
    while ( pPassengerSeatModelRoot->pParent != nullptr ) {
        pPassengerSeatModelRoot = pPassengerSeatModelRoot->pParent;
    }

    updateSeatTransform();
}

void EntityAvatarPassenger::updateSeatTransform() {
    // FUN_007f4760
    if ( pPassengerSeatModel != nullptr ) {
        float seatHeight = 0.0f;
        if ( pCarPassengerSeatHeight != nullptr && !bForceNotUpdatePos ) {
            seatHeight = *pCarPassengerSeatHeight;
        }

        float seatFrontBack = 0.0f;
        if ( pCarPassengerSeatFrontBack != nullptr && !bForceNotUpdatePos ) {
            seatFrontBack = *pCarPassengerSeatFrontBack;
        }

        Matrix4x4f_t seatMatrix = PassengerSeatModelInit;
        seatMatrix.row( 3 ) +=
            Eigen::Vector4f( 0.0f, seatHeight, seatFrontBack, 0.0f );

        pPassengerSeatModel->TransformMatrix = seatMatrix;
        if ( pPassengerSeatModel->pParent == nullptr ) {
            pPassengerSeatModel->pInstance->setModelMatrix(
                pPassengerSeatModel->TransformMatrix );
        } else {
            Matrix4x4f_t worldMatrix =
                pPassengerSeatModel->TransformMatrix *
                pPassengerSeatModel->pParent->pInstance->getModelMatrix();
            pPassengerSeatModel->pInstance->setModelMatrix( worldMatrix );
        }
        pPassengerSeatModel->propagateTransformMatricesNoScale();
    }

    const SCIKPoint &passengerSeat = CarIKPoints[kCarIKPointPassengerSeat];

    Matrix4x4f_t seatWorld = Matrix4x4f_t::Identity();
    AccumulateNodeWorldMatrix( passengerSeat.pNode, seatWorld );
    if ( passengerSeat.pOffset != nullptr && !bForceNotUpdatePos ) {
        seatWorld.row( 3 ) +=
            RotateIKOffset( seatWorld, passengerSeat.pOffset ).transpose();
    }

    Translation = seatWorld.row( 3 ).transpose();
}

void EntityAvatarPassenger::updateCarAnims( float deltaTime ) {
    // FUN_007f5030
    if ( !isSeated() || pCarState == nullptr ) {
        bMustPlayCarAnims = false;
        return;
    }

    VehicleBase *pDisplay = pCarState->getVehicleInstance();
    if ( pDisplay != nullptr ) {
        const VehicleResource &interior = pDisplay->getInteriorResource();
        const bool bInteriorHR = ( interior.pRender3DD != nullptr &&
                                   interior.pResourceBank != nullptr );
        if ( bHasInteriorHR != bInteriorHR ) {
            setCarState( pCarState );
        }
    }

    AnimCarStand.Weight = kAnimWeightFull;
    AnimCarStand.Ratio = kAnimRatioFollowCurve;

    if ( bMustPlayPassengerStandAnim || bIsPlayingPassengerStandAnim ) {
        OTDU_UNIMPLEMENTED_ONCE; // TODO: AVATAR (stand anim, only requested
                                 // by the ShoppingGirl voice FUN_007f3650)
        bMustPlayPassengerStandAnim = false;
        bIsPlayingPassengerStandAnim = false;
    }

    if ( pAnimCar[0].File != 0 && bMustPlayCarAnims &&
         CurrentBehavior == PB_Passenger ) {
        playCarAnimState( pAnimCar[0], &AnimCarStand, false );
    }

    const float slewUp = deltaTime * kAccelSlewUp;
    float accel = pCarState->getBody()->AccelDiff.z();
    if ( accel - LastAccelerationRatio > slewUp ) {
        accel = LastAccelerationRatio + slewUp;
    }
    if ( accel - LastAccelerationRatio < deltaTime * kAccelSlewDown ) {
        accel = LastAccelerationRatio - slewUp;
    }
    accel = std::min( std::max( accel, -1.0f ), 1.0f );
    LastAccelerationRatio = accel;

    FrontBackRatio.Weight = kAnimWeightFull;
    FrontBackRatio.Ratio = ( accel + kAnimWeightFull ) * kAccelToNormalised;
    animation.setChannelBlendRatios( 1, &FrontBackRatio, 1 );
    animation.setChannelLayer( 1, AnimChannel::ALM_BindDelta, kAnimWeightFull );

    if ( pAnimCar[1].File != 0 ) {
        if ( bMustPlayCarAnims && CurrentBehavior == PB_Passenger ) {
            playCarAnimState( pAnimCar[1], &FrontBackRatio, pAnimCar[1].ID );
        }

        updateIKTargets();

        if ( bMustPlayCarAnims && pAnimCar[1].pAnimState != nullptr ) {
            const AnimStateRecord *pExpressions =
                pAnimCar[1].pAnimState->getRecordAtIndex( 1 );
            if ( pExpressions != nullptr ) {
                animation.playChannel( pExpressions->MatchID, 0, nullptr, 0 );
                animation.setExpressionList(
                    &ExpressionList,
                    static_cast<int32_t>( pExpressions->MatchID ) );
            }
            bSkeletonHasChanged = false;
        }
    }

    bMustPlayCarAnims = false;
}

void EntityAvatarPassenger::updateIKTargets() {
    // FUN_007f3ed0
    if ( !isSeated() ) {
        IKLeftFoot.changeDamping( 0.0f, 0.0f );
        IKRightFoot.changeDamping( 0.0f, 0.0f );
        IKBack.changeDamping( 0.0f, 0.0f );
        return;
    }

    const auto resolvePoint = [this]( const SCIKPoint &point ) {
        Matrix4x4f_t nodeWorld = Matrix4x4f_t::Identity();
        AccumulateNodeWorldMatrix( point.pNode, nodeWorld );
        if ( point.pOffset != nullptr ) {
            nodeWorld.row( 3 ) +=
                RotateIKOffset( nodeWorld, point.pOffset ).transpose();
        }
        return Eigen::Vector3f( nodeWorld( 3, 0 ) - Translation.x(),
                                nodeWorld( 3, 1 ) - Translation.y(),
                                nodeWorld( 3, 2 ) - Translation.z() );
    };

    IKRightFoot.TargetParams.setTarget(
        resolvePoint( CarIKPoints[kCarIKPointPassengerFootRight] ), 0.0f );
    IKRightFoot.changeDamping( 0.0f, 0.0f );
    IKLeftFoot.TargetParams.setTarget(
        resolvePoint( CarIKPoints[kCarIKPointPassengerFootLeft] ), 0.0f );
    IKLeftFoot.changeDamping( 0.0f, 0.0f );
    IKBack.TargetParams.setTarget(
        resolvePoint( CarIKPoints[kCarIKPointPassengerBack] ), 0.0f );
    IKBack.changeDamping( 0.0f, 0.0f );

    if ( pPassengerSeatModel == nullptr ) {
        return;
    }

    Matrix4x4f_t seatWorld = Matrix4x4f_t::Identity();
    AccumulateNodeWorldMatrix( pPassengerSeatModel, seatWorld );

    const auto pushBelt = [&]( const int32_t pointIndex,
                               AnimLinkParams &belt ) {
        const SCIKOffset *pOffset = CarIKPoints[pointIndex].pOffset;
        if ( pOffset == nullptr ) {
            return;
        }

        const Eigen::Vector4f anchor = seatWorld.row( 3 ).transpose() +
                                       RotateIKOffset( seatWorld, pOffset );
        belt.TargetParams.setTarget(
            Eigen::Vector3f( anchor.x() - Translation.x(),
                             anchor.y() - Translation.y(),
                             anchor.z() - Translation.z() ),
            0.0f );
        belt.changeDamping( 1.0f, 0.0f );
    };

    pushBelt( kCarIKPointPassengerRightShoulderBelt1, IKRightShoulderBelt1 );
    pushBelt( kCarIKPointPassengerRightShoulderBelt2, IKRightShoulderBelt2 );
    pushBelt( kCarIKPointPassengerLeftShoulderBelt1, IKLeftShoulderBelt1 );
    pushBelt( kCarIKPointPassengerLeftShoulderBelt2, IKLeftShoulderBelt2 );
    pushBelt( kCarIKPointPassengerRightHipsBelt, IKRightHipsBelt );
    pushBelt( kCarIKPointPassengerLeftHipsBelt, IKLeftHipsBelt );
}

void EntityAvatarPassenger::updateLightingAttenuation() {
    // FUN_007f3dd0
    if ( pCarState == nullptr || !isSeated() ) {
        pLightingAttenuation = gAvatarOutCarLightFactor;
        return;
    }

    DBEntryCarPhysicsData *pCarData =
        gpDatabase->getCarDBEntry( pCarState->getCarID() );
    if ( pCarData == nullptr ) {
        return;
    }

    if ( gpDatabase->isBike( pCarData ) ) {
        pLightingAttenuation = gAvatarOutCarLightFactor;
        return;
    }

    const uint64_t bodyHashcode =
        gpDatabase->getGameDB().Databases[1].getHashcodeForPointer(
            pCarData->CarBody.pAsRawPointer );
    pLightingAttenuation = ( bodyHashcode == kConvertibleBodyHashcode0 ||
                             bodyHashcode == kConvertibleBodyHashcode1 )
                               ? gAvatarConvertibleLightFactor
                               : gAvatarInCarLightFactor;
}

void EntityAvatarPassenger::playCarAnimState( RenderAnm &anim,
                                              const AnimStateBlendEntry *pRatio,
                                              const uint32_t clipID ) {
    if ( anim.pAnimState == nullptr && anim.pRefBas != nullptr ) {
        anim.pAnimState = gpScenaric->resolveAnimState( nullptr, anim.pRefBas );
    }

    const AnimStateInstance *pState = anim.pAnimState;
    if ( pState == nullptr ) {
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
    animation.playChannel( matchID, clipID, blendSet.data(),
                           static_cast<int32_t>( blendSet.size() ) );
    if ( pRatio != nullptr ) {
        animation.setChannelBlendRatios( matchID, pRatio, 1 );
    }
}

void EntityAvatarPassenger::setSeatBeltVisible( const bool bVisible ) {
    if ( pCarState == nullptr ) {
        return;
    }

    VehicleBase *pDisplay = pCarState->getVehicleInstance();
    if ( pDisplay != nullptr &&
         pDisplay->getType() == VehicleBase::eType::VBT_Car ) {
        static_cast<VehicleCar *>( pDisplay )
            ->setSeatBeltVisible( 1, bVisible );
    }
}
