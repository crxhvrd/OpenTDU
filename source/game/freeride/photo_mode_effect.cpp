#include "shared.h"
#include "photo_mode_effect.h"

static constexpr const char *kSliderVarNames[9] = { // DAT_00f9c150
    "/:PM_FOV",      "/:PM_RadialBlur", "/:PM_Saturation", "/:null", "/:null",
    "/:PM_Contrast", "/:null",          "/:null",          "/:null" };

PhotoModeEffect::PhotoModeEffect()
    : activeSliderIndex( -1 ),
      field_0x418( 0 ),
      field_0x421( false ),
      pFlashPlayer( nullptr ),
      field_0x428( 0xffffffff ),
      field_0x42c( 0xffffffff ) {
    // FUN_009b5340
    setSlidersRange();

    for ( int32_t i = 0; i < 9; i++ ) {
        pParametersBeforeOptions[i] = 0.0f;
        pParametersBeforePhotoMode[i] = 0.0f;
    }
}

PhotoModeEffect::~PhotoModeEffect() {
    // FUN_00431e60
}

void PhotoModeEffect::setSlidersRange() {
    // FUN_009b50b0
    for ( int32_t i = 0; i < 9; i++ ) {
        pSliders[i].setAutoVelocity( true );
    }

    pSliders[0].setRange( 0.3f, 1.7f );
    pSliders[1].setRange( 0.0f, 16.0f );
    pSliders[2].setRange( 0.0f, 1.0f );
    pSliders[3].setRange( 0.0f, 4.0f );
    pSliders[4].setRange( 0.0f, 86400.0f );
    pSliders[5].setRange( 0.75f, 2.0f );
    pSliders[6].setRange( -1.0f, 1.0f );
    pSliders[7].setRange( -1.0f, 1.0f );
    pSliders[8].setRange( -1.0f, 1.0f );
}

void PhotoModeEffect::setFlashPlayer( FlashPlayer *pPlayer ) {
    // FUN_009b51a0
    pFlashPlayer = pPlayer;

    for ( int32_t i = 0; i < 9; i++ ) {
        pSliders[i].rebind( pFlashPlayer, kSliderVarNames[i] );
    }
}

void PhotoModeEffect::tick( float /*deltaTime*/ ) {
    // FUN_009b5d70
    OTDU_UNIMPLEMENTED; // TODO: CAMERA/POSTFX
}
