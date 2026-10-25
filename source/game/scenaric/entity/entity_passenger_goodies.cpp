#include "shared.h"
#include "entity_passenger_goodies.h"

#include "database/gs_database.h"
#include "game/scenaric/gs_scenaric.h"
#include "game/vehicle/car_state.h"
#include "game/vehicle/vehicle_controller.h"
#include "render/gs_render.h"
#include "render/camera/gs_camera.h"
#include "render/camera/camera_game.h"
#include "render/mng_shadow.h"
#include "render/hiearchy_node.h"

EntityPassengerGoodies::EntityPassengerGoodies()
    : EntityActor(),
      CurrentBehavior( PG_MovementFree ),
      pCarState( nullptr ),
      Translation( Eigen::Vector4f::Zero() ),
      pCarPsgSeatHeight( nullptr ),
      pCarPsgSeatFrontBack( nullptr ),
      PsgSeatModelInitMatrix( Matrix4x4f_t::Identity() ),
      pPsgSeatModel( nullptr ),
      pPsgSeatModelRoot( nullptr ) {
    // FUN_007ebaa0
    for ( SCIKPoint &point : CarIKPoints ) {
        point.pNode = nullptr;
        point.pOffset = nullptr;
    }
}

EntityPassengerGoodies::~EntityPassengerGoodies() {
    // FUN_00424120
}

void EntityPassengerGoodies::tick( GameMechanism *pGameMech,
                                   float deltaTime,
                                   float totalTime ) {
    // FUN_007ebba0
    EntityActor::tick( pGameMech, deltaTime, totalTime );

    if ( pCarState != nullptr && CurrentBehavior == PG_MovementInCar ) {
        Matrix4x4f_t seatMatrix = pCarState->getWorldMatrix();

        const float tx = Translation.x();
        const float ty = Translation.y();
        const float tz = Translation.z();
        const float worldX = tx * seatMatrix( 0, 0 ) + ty * seatMatrix( 1, 0 ) +
                             tz * seatMatrix( 2, 0 ) + seatMatrix( 3, 0 );
        const float worldY = tx * seatMatrix( 0, 1 ) + ty * seatMatrix( 1, 1 ) +
                             tz * seatMatrix( 2, 1 ) + seatMatrix( 3, 1 );
        const float worldZ = tx * seatMatrix( 0, 2 ) + ty * seatMatrix( 1, 2 ) +
                             tz * seatMatrix( 2, 2 ) + seatMatrix( 3, 2 );
        seatMatrix( 3, 0 ) = worldX;
        seatMatrix( 3, 1 ) = worldY;
        seatMatrix( 3, 2 ) = worldZ;

        setTransform( seatMatrix, false );
    }
}

static void RegisterModelAsShadowCaster( SerializedSkinnedModel &model ) {
    // FUN_00808f00
    if ( gpMngShadow == nullptr ) {
        return;
    }

    if ( model.bDrawSimplifiedMesh ) {
        gpMngShadow->addShadowCaster( model.MIInpostor.pInstance, 1.0f );
        return;
    }

    if ( !model.bFullActorMesh ) {
        return;
    }

    if ( model.pInstance != nullptr ) {
        gpMngShadow->addShadowCaster( model.pInstance, 1.0f );
        return;
    }

    for ( HiearchyNode *pNode = model.pHiearchNode; pNode != nullptr;
          pNode = pNode->pNext ) {
        if ( pNode->pInstance != nullptr ) {
            gpMngShadow->addShadowCaster( pNode->pInstance, 1.0f );
        }
    }
}

bool EntityPassengerGoodies::draw( RenderScene *pScene ) {
    // FUN_007ebb10
    if ( CurrentBehavior == PG_MovementInCar ) {
        CameraGame *pActiveCamera = gpCamera->getActiveCamera();
        if ( pActiveCamera != nullptr &&
             ( ( pActiveCamera->getFlags() >> 0x12 ) & 1 ) ) {
            return EntityActor::draw(
                gpRender->getRenderPass<eRenderPass::RP_Cockpit>().pScene );
        }
    }

    if ( flags & 8 ) {
        RegisterModelAsShadowCaster( Model );
    }

    if ( ( flags & 1 ) && ( flags & 4 ) ) {
        if ( !bLightingValid ) {
            updateSkinningMatrices();
        }
        Model.draw( pScene );
        return true;
    }
    return false;
}

void EntityPassengerGoodies::deserialize( BinaryReader *param_2 ) {
    // FUN_007ebaf0
    EntityActor::deserialize( param_2 );
}

void EntityPassengerGoodies::setCarState( CarState *pNewCarState ) {
    // FUN_007eb950
    if ( pCarState == pNewCarState ) {
        return;
    }
    if ( pNewCarState != nullptr ) {
        DBEntryCarPhysicsData *pCarData =
            gpDatabase->getCarDBEntry( pNewCarState->getCarID() );
        if ( pCarData != nullptr && gpDatabase->isBike( pCarData ) ) {
            return;
        }
    }

    BuildCarIKPoints( CarIKPoints, pNewCarState );
    pCarPsgSeatHeight = nullptr;
    pCarPsgSeatFrontBack = nullptr;
    pCarState = pNewCarState;
    if ( pCarState == nullptr ) {
        return;
    }

    Translation = Eigen::Vector4f( 0.0f, 0.0f, 0.0f, 1.0f );
    SCIKCarOffsets *pOffsets = gpScenaric->getIKOffsetDB().findCarOffsets(
        pCarState->getCarID(), true );
    if ( pOffsets != nullptr ) {
        pCarPsgSeatHeight = &pOffsets->PassengerSeatHeight;
        pCarPsgSeatFrontBack = &pOffsets->PassengerSeatFrontBack;
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

    HiearchyNode *pSeat = nullptr;
    if ( pHighResRoot != nullptr ) {
        pSeat = pHighResRoot->findChild( GetIdentifier64bit( "Seat_HR_R" ) );
    }
    if ( pSeat == nullptr && pLowResRoot != nullptr ) {
        pSeat = pLowResRoot->findChild( GetIdentifier64bit( "Seat_LR_R" ) );
    }
    setSeatModel( pSeat );
}

void EntityPassengerGoodies::setSeatModel( HiearchyNode *pNode ) {
    // FUN_007eb8a0
    pPsgSeatModel = pNode;
    if ( pNode == nullptr ) {
        PsgSeatModelInitMatrix = Matrix4x4f_t::Identity();
        return;
    }

    PsgSeatModelInitMatrix = pNode->TransformMatrix;
    pPsgSeatModelRoot = pNode;
    while ( pPsgSeatModelRoot->pParent != nullptr ) {
        pPsgSeatModelRoot = pPsgSeatModelRoot->pParent;
    }
    updateTranslation();
}

void EntityPassengerGoodies::updateTranslation() {
    // FUN_007eb810
    const SCIKPoint &cargoPoint = CarIKPoints[kCarIKPointCargo];
    Matrix4x4f_t pointWorld = Matrix4x4f_t::Identity();
    AccumulateNodeWorldMatrix( cargoPoint.pNode, pointWorld );
    if ( cargoPoint.pOffset != nullptr ) {
        const Eigen::Vector4f offset( cargoPoint.pOffset->Offset.x(),
                                      cargoPoint.pOffset->Offset.y(),
                                      cargoPoint.pOffset->Offset.z(), 0.0f );
        pointWorld.row( 3 ) +=
            TDUMat4x4MulVec( pointWorld, offset ).transpose();
    }
    Translation = pointWorld.row( 3 ).transpose();
}
