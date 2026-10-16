#include "shared.h"
#include "scene_aftermarket.h"

#include "config/gs_config.h"
#include "filesystem/gs_file.h"
#include "game/scenaric/entity/entity_shop_data.h"
#include "render/camera/gs_camera.h"

static constexpr const char *kDefaultAfterMarketBank = "Interior/IAM_E.bnk";
static constexpr int32_t kMaxNumParkedCars = 2;
static constexpr int32_t kParkedCarConfigIDs[kMaxNumParkedCars] = {
    0x76, 0x72 }; // DAT_00f91c70

SceneAfterMarket::SceneAfterMarket() : content(), carPark(), interiorBank() {
    // FUN_007bb930
}

SceneAfterMarket::~SceneAfterMarket() {
    // FUN_007bb8b0
}

void SceneAfterMarket::loadResources() {
    // FUN_007bb960
    char bankPath[512] = {};
    if ( !BuildInteriorBankPath( GM_Invalid, bankPath ) ) {
        snprintf( bankPath, sizeof( bankPath ), "%s%s",
                  gpConfig->getResRootPath(), kDefaultAfterMarketBank );
    }

    if ( !interiorBank.isLoaded() ) {
        if ( interiorBank.loadBank( gpFile, bankPath ) ) {
            interiorBank.loadContentAsync();
        }
    }

    if ( !interiorBank.isLoaded() ) {
        return;
    }

    content.enter( &interiorBank, false );
    carPark.setEnvMapTargets(
        content.getBankBitmap( GetIdentifier64bit( "CUBECAR" ) ),
        content.getBankBitmap( GetIdentifier64bit( "CUBECARB" ) ) );

    EntityShopData *pShop = content.getShopData();
    int32_t numCars = 0;
    if ( pShop != nullptr ) {
        numCars = std::min( pShop->getNumPositions(), kMaxNumParkedCars );
    }

    for ( int32_t i = 0; i < numCars; i++ ) {
        carPark.pushConfig( kParkedCarConfigIDs[i] );
    }

    if ( !carPark.isLoaded() ) {
        if ( carPark.getNumEntries() != 0 ) {
            carPark.streamAround( 0 );
            carPark.loadBank();
        }
        carPark.setLoaded( true );
    }

    gpCamera->setMouseCam( false );
}

void SceneAfterMarket::placeCars() {
    // FUN_007bb760
    EntityShopData *pShop = content.getShopData();

    for ( int32_t i = 0; i < carPark.getNumUsedEntries(); i++ ) {
        const int32_t numPositions =
            ( pShop != nullptr ) ? pShop->getNumPositions() : 0;
        if ( i >= numPositions ) {
            continue;
        }

        Matrix4x4f_t transform = Matrix4x4f_t::Identity();
        content.getPositionMatrix( i, transform );
        transform( 3, 3 ) = 1.0f;

        carPark.setEntryTransform( i, transform, 0.0f );
    }

    content.startCameraPath(
        ( pShop != nullptr ) ? pShop->getCameraPath() : nullptr, false, false );
}

void SceneAfterMarket::terminate() {
    // FUN_007bb850
    carPark.releaseAll();
    content.leave();
    interiorBank.unload();

    gpCamera->setMouseCam( true );
}
