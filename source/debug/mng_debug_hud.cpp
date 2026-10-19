#include "shared.h"
#include "mng_debug_hud.h"

#include "flash/gs_flash.h"
#include "core/arg_parser.h"
#include "core/color.h"
#include "debug/ed_debug_text.h"

#include "render/text_renderer.h"
#include "render/camera/gs_camera.h"
#include "render/camera/camera_game.h"

#include "game/gm_scene.h"
#include "game/vehicle/car_state.h"
#include "world/gs_world_streaming.h"

#include "tdu_instance.h"

bool gFPSCounterEnabled = false; // DAT_0143f5b4
static CmdLineArg CmdLineArgFPS( "fps", []( const char *pArg ) {
    gFPSCounterEnabled = true;
} );

bool gPOSEnabled = false; // DAT_0143f5b5
static CmdLineArg CmdLineArgPOS( "pos", []( const char *pArg ) {
    gPOSEnabled = true;
} );

MngDebugHUD *gpMngDebugHUD = nullptr; // DAT_010e8040

static constexpr uint32_t kLineHeight = 12;

MngDebugHUD::MngDebugHUD()
    : Manager(), pGeneralPlayer( nullptr ), bVisible( false ) {
    gpMngDebugHUD = this;
}

MngDebugHUD::~MngDebugHUD() { gpMngDebugHUD = nullptr; }

void MngDebugHUD::reset() {
    // FUN_006b58f0
    pGeneralPlayer = gpFlash->getFlashPlayer( "GENERAL" );
}

void MngDebugHUD::draw() {
    // FUN_006b5590
    char buffer[268];

    if ( gFPSCounterEnabled ) {
        ColorRGBA fpsColor = RetrieveDebugTextColor( DDC_Red );
        gTextRenderer.setColor( fpsColor );

        DrawDebugText( 50, 20, TestDrive::GetVersion() );

        snprintf( buffer, sizeof( buffer ), "%d", gCurrentFPS );
        DrawDebugText( 360, 20, buffer );
    }

    if ( gPOSEnabled ) {
        ColorRGBA posColor = RetrieveDebugTextColor( DDC_White_NoAlpha );
        gTextRenderer.setColor( posColor );

        const Eigen::Vector4f shift =
            ( gpWorldStreaming != nullptr && gpWorldStreaming->isEnabled() )
                ? gpWorldStreaming->getPosition()
                : Eigen::Vector4f::Zero();

        uint32_t posY = 50;

        CarState *pCarState = ( gpScenaric != nullptr )
                                  ? gpScenaric->getLocalPlayer().getCarState()
                                  : nullptr;
        if ( pCarState != nullptr ) {
            const Eigen::Vector4f carPos = pCarState->getWorldPosition();
            snprintf( buffer, sizeof( buffer ),
                      "CAR.............%2.2f %2.2f %2.2f",
                      carPos.x() + shift.x(), carPos.y() + shift.y(),
                      carPos.z() + shift.z() );
            DrawDebugText( 100, posY, buffer );
            posY += kLineHeight;
        }

        CameraGame *pCamera =
            ( gpCamera != nullptr ) ? gpCamera->getActiveCamera() : nullptr;
        if ( pCamera != nullptr ) {
            const Eigen::Vector4f camPos =
                pCamera->getWorldPositionFromViewMatrix();
            snprintf( buffer, sizeof( buffer ),
                      "CAMERA..........%2.2f %2.2f %2.2f  (%s)", camPos.x(),
                      camPos.y(), camPos.z(), pCamera->getName() );
            DrawDebugText( 100, posY, buffer );
            posY += kLineHeight;
        }

        OTDU_UNIMPLEMENTED; // TODO: PHYSICS
    }
}
