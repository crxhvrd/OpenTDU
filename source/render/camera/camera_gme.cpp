#include "shared.h"
#include "camera_gme.h"

#include "config/gs_config.h"
#include "core/maths_helper.h"
#include "render/camera/gs_camera.h"
#include "render/mng_shadow.h"
#include "world/gs_world.h"
#include "world/gs_world_streaming.h"
#include "world/mng_world.h"
#include "world/streaming_manager.h"

static float gGMEDefaultDistance = 50.0f; // DAT_00f7c7d8
static float gGMENearPlane = 100.0f;      // DAT_00f7c7e0
static float gGMEFarPlane = 10000.0f;     // DAT_00f7c7e4

static constexpr float kGMEDefaultPitch = 0.09817477f;     // DAT_00f41c88
static constexpr float kGMENearPlaneDistanceRatio = 0.07f; // DAT_00fab338

CameraGME::CameraGME()
    : CameraGame(),
      direction( Eigen::Vector4f::Zero() ),
      zoomSpeed( 0.0f ),
      distance( gGMEDefaultDistance ),
      pitch( kGMEDefaultPitch ),
      yaw( 0.0f ),
      panSpeedX( 0.0f ),
      panSpeedZ( 0.0f ),
      bAutoZoom( false ),
      bField0x1a1( false ),
      autoPitchTimer( 0.0f ),
      gmeMode( 0 ),
      pGameModeEditor( nullptr ) {
    // FUN_008dae50
}

CameraGME::~CameraGME() {}

void CameraGME::initialize() {
    // FUN_008dad40
    CameraGame::initialize();

    bField0x1a1 = false;
    if ( gpMngWorld != nullptr ) {
        gpMngWorld->setDisableStreamingOnCar( true );
    }
    if ( gpMngShadow != nullptr ) {
        gpMngShadow->setCastersEnabled( false );
    }
    distance = gGMEDefaultDistance;
    bAutoZoom = false;
}

bool CameraGME::tick( float deltaTime, bool param_1, bool param_2 ) {
    // FUN_008db4f0
    CameraGame::tick( deltaTime, param_1, param_2 );

    direction = Eigen::Vector4f( 0.0f, -1.0f, 0.0f, 0.0f );
    Matrix4x4f_t rotationX = MakeRotationMatX( pitch );
    Matrix4x4f_t rotationY = MakeRotationMatY( yaw );
    direction = TDUMat4x4MulVec( rotationX, direction );
    direction = TDUMat4x4MulVec( rotationY, direction );

    if ( gmeMode == 1 ) {
        updatePan( deltaTime );
    } else if ( gmeMode == 2 ) {
        gGMENearPlane = distance * kGMENearPlaneDistanceRatio;
        updatePan( deltaTime );
        updateZoom( deltaTime );

        float groundHeight = SampleHeightmapSurface( worldTarget, nullptr );
        worldTarget.y() = std::max( groundHeight, 0.0f );
        setTarget( worldTarget );

        if ( pGameModeEditor != nullptr ) {
            OTDU_UNIMPLEMENTED; // TODO: GME; writes lerp(2.5, 9.5)
        }
    }

    worldPosition = worldTarget - direction * distance;
    setPosition( worldPosition );
    setTarget( worldTarget );
    updateMatrix( worldPosition.head<3>(), upVector.head<3>(),
                  worldTarget.head<3>() );
    setPosition( worldPosition );
    setTarget( worldTarget );

    nearPlane = gGMENearPlane;
    farPlane = gGMEFarPlane;
    gpCamera->setNearPlane( nearPlane );
    gpCamera->setFarPlane( farPlane );
    gpCamera->setEnableDepthAware( true );
    gpCamera->setInteriorNearPlane( interiorNearPlane );
    gpCamera->setInteriorFarPlane( interiorFarPlane );
    gpCamera->setEnableDepthAwareInterior( true );

    OTDU_UNIMPLEMENTED_ONCE; // TODO: MngCursor (DAT_010e7cf4)

    return true;
}

void CameraGME::destroy() {
    // FUN_008db830
    StreamingManager *pStreaming =
        ( gpWorld != nullptr ) ? gpWorld->getStreamingManager() : nullptr;
    if ( pStreaming != nullptr ) {
        SectorManager &sectors = pStreaming->sectorManager;
        AreaGrid &areas = pStreaming->areaGrid;

        sectors.bDrawSector = false;
        sectors.bNeedStreamUpdate = true;
        areas.bNeedStreamUpdate = true;
        areas.bDrawArea = false;
        pStreaming->setActiveRenderLayers( 1 );
        sectors.bStreamSector = true;
        gpWorldStreaming->updateStreaming();
        areas.bStreamArea = true;
    }
    if ( gpMngWorld != nullptr ) {
        gpMngWorld->setDisableStreamingOnCar( false );
    }
    if ( gpMngShadow != nullptr ) {
        gpMngShadow->setCastersEnabled( false );
    }
}

void CameraGME::updatePan( float deltaTime ) {
    // FUN_008daed0
    if ( pGameModeEditor == nullptr ) {
        return;
    }

    OTDU_UNIMPLEMENTED; // TODO: GME + MngCursor (DAT_010e7cf4)
}

void CameraGME::updateZoom( float deltaTime ) {
    // FUN_008dab10
    if ( gpConfig->NbMaxPlayers < 1 || pGameModeEditor == nullptr ) {
        return;
    }

    OTDU_UNIMPLEMENTED; // TODO: GME; integrates zoomSpeed/distance from the GME
                        // zoom inputs.
}
