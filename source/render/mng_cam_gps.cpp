#include "shared.h"
#include "mng_cam_gps.h"

MngCamGPS *gpMngCamGPS = nullptr; // DAT_010e804c

inline bool isMapCaptureCameraActive() { return false; }

MngCamGPS::MngCamGPS() : Manager() {
    // FUN_006b3d30
    clipPlaneSetup.setFlags( 0x101 );

    gpMngCamGPS = this;
}

MngCamGPS::~MngCamGPS() {
    if ( gpMngCamGPS == this ) {
        gpMngCamGPS = nullptr;
    }
}

bool MngCamGPS::initialize( TestDriveGameInstance * ) {
    // FUN_006b3c40
    allocateResources();

    OTDU_UNIMPLEMENTED; // TODO: Earth + Ocean resources snapshot
    return true;
}

void MngCamGPS::tick( float deltaTime, float totalTime ) {
    // FUN_006b53f0
    if ( isMapCaptureCameraActive() ) {
        updateCameraForCapture( totalTime );
    }

    noiseWindStart += noiseWindSpeed * deltaTime;
    if ( noiseWindStart + noiseWindSize <= 1.0f ) {
        if ( noiseWindStart < 0.0f ) {
            noiseWindStart = 0.0f;
            noiseWindSpeed = -noiseWindSpeed;
        }
    } else {
        noiseWindStart = 1.0f - noiseWindSize;
        noiseWindSpeed = -noiseWindSpeed;
    }

    if ( isMapCaptureCameraActive() ) {
        OTDU_UNIMPLEMENTED;
    }

    (void)totalTime;
}

void MngCamGPS::reset() {
    // FUN_006b5510
    if ( isMapCaptureCameraActive() ) {
        updateCameraForCapture( 0.0f );
    }

    OTDU_UNIMPLEMENTED; // TODO: WEATHER
}

void MngCamGPS::draw() {
    // FUN_006b5170
    if ( !bDrawEarth || !isMapCaptureCameraActive() ) {
        return;
    }

    OTDU_UNIMPLEMENTED; // TODO: EARTH
}

void MngCamGPS::pause() {
    // FUN_006b3fa0
    toggleGPSClipPlanes( false, false );
}

void MngCamGPS::terminate() {
    // FUN_006b3bc0
    releaseResources();

    OTDU_UNIMPLEMENTED; // streaming restore + earth/ocean setup restore
}

bool MngCamGPS::allocateResources() {
    // FUN_006b3750
    OTDU_UNIMPLEMENTED;
    return false;
}

void MngCamGPS::releaseResources() {
    // FUN_006b3630
    OTDU_UNIMPLEMENTED;
}

void MngCamGPS::toggleGPSClipPlanes( bool bEnable, bool bResetOTMask ) {
    // FUN_006b3530
    (void)bEnable;
    (void)bResetOTMask;
    OTDU_UNIMPLEMENTED;
}

void MngCamGPS::updateCameraForCapture( float progress ) {
    // FUN_006b4fa0
    (void)progress;
    OTDU_UNIMPLEMENTED;
}
