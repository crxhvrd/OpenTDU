#include "shared.h"
#include "gm_boat_tour.h"

#include "config/gs_config.h"
#include "core/arg_parser.h"
#include "core/maths_helper.h"
#include "core/tcb_spline.h"
#include "game/scenaric/entity/entity_boat.h"
#include "game/scenaric/entity/entity_boat_path.h"
#include "game/scenaric/entity/entity_boat_tour.h"
#include "game/scenaric/entity/entity_factory.h"
#include "game/scenaric/gs_scenaric.h"
#include "render/camera/camera_steady_cam.h"
#include "render/camera/gs_camera.h"
#include "render/gs_render.h"
#include "world/gs_boat.h"
#include "world/mng_ocean.h"
#include "world/gs_world.h"

static constexpr int32_t kTourCameraMode = 0xc;
static constexpr float kTourSpeed = 50.0f; // DAT_00f41848
static constexpr float kCameraTransitionTime = 1.0f;
static constexpr const char *kBoatTourBankName =
    "OpenTDU/boattour";            //"scenarics/boattour"
static int32_t gBoatTourIndex = 3; // BOPA path index (3 = catamaran; 0 = ferry)

static float ComputeCameraYaw( const Matrix4x4f_t &transform ) {
    const Eigen::Vector4f axis =
        Eigen::Vector4f( transform( 0, 2 ), transform( 1, 2 ),
                         transform( 2, 2 ), transform( 3, 2 ) ) *
        kMinusOne;

    float angle = acosf( axis.x() );
    if ( axis.z() < 0.0f ) {
        angle = kTwoPI - angle;
    }
    return angle;
}

static void ComputeTourFraming( const int32_t boatGroup,
                                const float waypointHeight,
                                Matrix4x4f_t &outOffset,
                                float &outSpeed ) {
    (void)waypointHeight;

    outOffset = MakeRotationMatY( -kHalfPI );
    if ( boatGroup == 1 ) {
        outOffset.row( 3 ) =
            Eigen::Vector4f( 0.0f, 30.0f, -85.0f, kOne ).transpose();
        outSpeed = kTourSpeed;
    } else {
        outOffset.row( 3 ) =
            Eigen::Vector4f( -2.50f, 2.50f, 5.0f, kOne ).transpose();
        outSpeed = kTourSpeed / 10.0f;
    }
}

static Matrix4x4f_t MakeBoatFacingTransform( const Matrix4x4f_t &tourFrame ) {
    Matrix4x4f_t boatFrame = tourFrame;
    MatrixPreRotateY( boatFrame, kPI );
    return boatFrame;
}

GMBoatTour::GMBoatTour()
    : GameModeWorldBase(),
      boatTourBank(),
      pTour( nullptr ),
      currentDistanceOnPath( 0.0f ),
      tourSpeed( kTourSpeed ),
      pBoatInstance( nullptr ) {
    // FUN_009d8270
    setTransitionFlags( 0x27 );
}

GMBoatTour::~GMBoatTour() {
    // FUN_0042bba0
}

void GMBoatTour::initialize() {
    // FUN_009d8770
    GameModeWorldBase::initialize();

    std::string bankPath = gpConfig->getResRootPath();
    bankPath += kBoatTourBankName;
    bankPath += ".bnk";

    boatTourBank.loadFromFile( bankPath.c_str(), nullptr, false );
    boatTourBank.forceLoad();
    boatTourBank.activate( nullptr );
}

Matrix4x4f_t ComputeTransform( const TCBSplinePosition &spline,
                               const float distance ) {
    // FUN_009d8140
    int32_t polygonIndex = 0;
    float polygonTime = 0.0f;

    spline.positionAL( distance, &polygonIndex, &polygonTime );
    const Eigen::Vector4f position =
        spline.evaluatePolygon( polygonIndex, polygonTime );

    spline.positionAL( distance, &polygonIndex, &polygonTime );
    const Eigen::Vector4f forward = Normalize( spline.evaluatePolygonDerivative(
                                        polygonIndex, polygonTime ) ) *
                                    kMinusOne;

    const Eigen::Vector4f right = Vector4DCross( kWorldUpVector, forward );
    const Eigen::Vector4f up = Vector4DCross( forward, right );

    Matrix4x4f_t transform;
    transform.row( 0 ) = right.transpose();
    transform.row( 1 ) = up.transpose();
    transform.row( 2 ) = forward.transpose();
    transform.row( 3 ) = position.transpose();
    return transform;
}

const TCBSplinePosition *GMBoatTour::getActiveSpline() const {
    if ( pTour != nullptr ) {
        return &pTour->getPathSpline();
    }
    return nullptr;
}

Entity *GMBoatTour::getActiveBoat() const { return pBoatInstance; }

Entity *GMBoatTour::resolveBoatInstance( Entity *pTemplate ) {
    if ( pTemplate == nullptr ) {
        return nullptr;
    }

    if ( pTemplate->getType() == eEntityType::ET_BOAT &&
         static_cast<EntityBoat *>( pTemplate )->hasRenderInstance() ) {
        return pTemplate;
    }

    Entity *pInstance = getActorManager().instanciateActor( pTemplate );
    if ( pInstance == nullptr ) {
        return nullptr;
    }

    return pInstance;
}

bool GMBoatTour::getCameraOffset( Matrix4x4f_t &outOffset ) const {
    if ( pTour != nullptr ) {
        Entity *pCameraEntity = pTour->getCamera();
        if ( pCameraEntity == nullptr ) {
            return false;
        }
        outOffset = pCameraEntity->getModelMatrix();
        return true;
    }

    return false;
}

CameraSteadyCam *GMBoatTour::getTourCamera() const {
    CameraGame *pCamera = gpCamera->getActiveCamera();
    if ( pCamera == nullptr || pCamera->getType() != kTourCameraMode ) {
        return nullptr;
    }
    return static_cast<CameraSteadyCam *>( pCamera );
}

void GMBoatTour::applyCameraTransform( CameraSteadyCam *pCamera,
                                       const Matrix4x4f_t &boatTransform,
                                       const bool bResetPitch ) {
    Matrix4x4f_t cameraOffset;
    if ( pCamera == nullptr || !getCameraOffset( cameraOffset ) ) {
        return;
    }

    const Matrix4x4f_t cameraTransform = cameraOffset * boatTransform;

    pCamera->setRootPosition( cameraTransform.row( 3 ).transpose() );
    if ( bResetPitch ) {
        pCamera->setRootAngleX( 0.0f );
    }
    pCamera->setRootAngleY( ComputeCameraYaw( cameraTransform ) );
}

void GMBoatTour::reset() {
    // FUN_009d84a0
    GameModeScene::reset();

    CameraGame *pCameraGame = gpCamera->findCameraWithMode(
        kTourCameraMode, gpCamera->getBoundCarState() );
    CameraSteadyCam *pCamera = static_cast<CameraSteadyCam *>( pCameraGame );
    if ( pCamera == nullptr ) {
        return;
    }

    pCamera->setFPSMode( 0 );
    pCamera->setPadMode( 1 );
    pCamera->setInvertMouse( true );
    pCamera->setBlockPosition( true );

    gpCamera->setCameraMode( kTourCameraMode, 0, kCameraTransitionTime );
    gpWorld->setStreamingPositionPtr( pCamera->getRootPositionRW() );

    pTour = nullptr;
    int32_t tourCount = 0;
    for ( int32_t i = 0; i < boatTourBank.getEntityCount(); i++ ) {
        Entity *pEntity = boatTourBank.getEntity( i );
        if ( pEntity == nullptr ||
             pEntity->getType() != eEntityType::ET_BOTO ) {
            continue;
        }

        EntityBoatTour *pCandidate = static_cast<EntityBoatTour *>( pEntity );
        if ( pCandidate->getTourType() != 0 ) {
            continue;
        }

        if ( tourCount == gBoatTourIndex ) {
            pTour = pCandidate;
            break;
        }
        tourCount++;
    }

    currentDistanceOnPath = 0.0f;

    const TCBSplinePosition *pSpline = getActiveSpline();
    if ( pSpline == nullptr ) {
        return;
    }

    Matrix4x4f_t boatTransform = ComputeTransform( *pSpline, 0.0f );

    Entity *pBoat = getActiveBoat();
    if ( pBoat != nullptr ) {
        Matrix4x4f_t boatFrame = MakeBoatFacingTransform( boatTransform );
        pBoat->setTransform( boatFrame, true );
    }

    applyCameraTransform( pCamera, boatTransform, true );
}

void GMBoatTour::tick( float deltaTime, float totalTime ) {
    // FUN_009d82f0
    GameModeScene::tick( deltaTime, totalTime );

    if ( pTour != nullptr && pBoatInstance == nullptr ) {
        Entity *pTemplate = pTour->getBoat();
        if ( pTemplate != nullptr &&
             pTemplate->getType() == eEntityType::ET_BOAT &&
             static_cast<EntityBoat *>( pTemplate )->hasInstanceDefChunk() ) {
            pBoatInstance = resolveBoatInstance( pTemplate );

            if ( pBoatInstance != nullptr ) {
                Matrix4x4f_t unusedOffset;
                ComputeTourFraming(
                    static_cast<EntityBoat *>( pBoatInstance )->getBoatType(),
                    0.0f, unusedOffset, tourSpeed );
            }
        }
    }

    const TCBSplinePosition *pSpline = getActiveSpline();
    if ( pSpline == nullptr ) {
        return;
    }

    const TCBSplinePosition &spline = *pSpline;
    const float totalLength = spline.TotalLength;
    Matrix4x4f_t boatTransform =
        ComputeTransform( spline, currentDistanceOnPath );

    Entity *pBoat = getActiveBoat();
    if ( pBoat != nullptr ) {
        Matrix4x4f_t boatFrame = MakeBoatFacingTransform( boatTransform );
        pBoat->setTransform( boatFrame, true );
    }

    currentDistanceOnPath += deltaTime * tourSpeed;
    if ( totalLength < currentDistanceOnPath ) {
        currentDistanceOnPath = totalLength;
    }

    boatTransform = ComputeTransform( spline, currentDistanceOnPath );

    CameraSteadyCam *pCamera = getTourCamera();
    if ( pCamera == nullptr ) {
        return;
    }

    applyCameraTransform( pCamera, boatTransform, false );

    pCamera->tick( deltaTime, false, true );
}

void GMBoatTour::draw( float /*deltaTime*/, float /*totalTime*/ ) {
    // FUN_009d80c0
    // NOP
}

void GMBoatTour::pause() {
    // FUN_009d82b0
    GameModeScene::pause();

    CameraSteadyCam *pCamera = getTourCamera();
    if ( pCamera == nullptr ) {
        return;
    }

    pCamera->setBlockPosition( false );
    pCamera->setRootAngleX( 0.0f );
    pCamera->setRootAngleY( 0.0f );

    gpCamera->restoreCamera();
}

void GMBoatTour::destroy() {
    // FUN_009d8710
    GameModeWorldBase::destroy();

    boatTourBank.destroy();
    boatTourBank.terminate();
    boatTourBank.unload();
}
