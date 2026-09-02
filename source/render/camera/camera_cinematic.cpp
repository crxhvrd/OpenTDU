#include "shared.h"
#include "camera_cinematic.h"

#include "config/gs_config.h"
#include "render/gs_render.h"

#include "gs_camera.h"

CameraCinematic::CameraCinematic()
    : CameraGame(), pCinematicInterface( nullptr ), bKeepGlobalPlanes( false ) {
    // FUN_008ce2c0
    nearPlane = gpRender->getDefaultNearPlane();
    farPlane = gpRender->getDefaultFarPlane();

    setTargetOffseted( Eigen::Vector4f( 0.0f, 0.0f, 1.0f, 1.0f ) );
    setPositionOffseted( Eigen::Vector4f( 0.0f, 0.0f, 0.0f, 1.0f ) );

    upVector = Eigen::Vector4f( 0.0f, 1.0f, 0.0f, 0.0f );
}

CameraCinematic::~CameraCinematic() {}

void CameraCinematic::initialize() {
    // FUN_008ce220
    bInitialized = true;

    nearPlane = gpRender->getDefaultNearPlane();
    farPlane = gpRender->getDefaultFarPlane();

    interiorNearPlane = gpRender->getDefaultInteriorNearPlane();
    interiorFarPlane = gpRender->getDefaultInteriorFarPlane();

    if ( bKeepGlobalPlanes ) {
        return;
    }

    gpCamera->setNearPlane( nearPlane );
    gpCamera->setFarPlane( farPlane );
    gpCamera->setEnableDepthAware( true );

    gpCamera->setInteriorNearPlane( interiorNearPlane );
    gpCamera->setInteriorFarPlane( interiorFarPlane );
    gpCamera->setEnableDepthAwareInterior( true );
}

bool CameraCinematic::tick( float deltaTime, bool param_1, bool param_2 ) {
    // FUN_008ce200
    // NOP
    return true;
}

void CameraCinematic::destroy() {
    // FUN_008ce1f0
    pCinematicInterface = nullptr;
}
