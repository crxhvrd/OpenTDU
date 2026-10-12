#include "shared.h"
#include "map_screenshot_bank.h"

#include "config/gs_config.h"
#include "filesystem/gs_file.h"
#include "render/material.h"
#include "tdu_instance.h"

static constexpr const char *kCategoryFolders[] = {
    "ExtraChallenges", // PTR_s_ExtraChallenges_00f96b90
    "IGE",
    "Spots",
};

static void SetupPictureMaterial( Material *pMaterial ) {
    pMaterial->OT = 0x2a;
    pMaterial->DepthTest = 1;
    pMaterial->DepthWrite = 2;
    pMaterial->AlphaTest = CF_Never;
}

MapScreenshotBank::MapScreenshotBank()
    : Bank(),
      StreamedResource(),
      name(),
      state( MSS_Idle ),
      pBuffer( nullptr ),
      b2DMInitialized( false ),
      num2DB( 0 ),
      collection2D(),
      render2DM(),
      pCallbackUserData( nullptr ),
      pOnLoaded( nullptr ) {
    // FUN_0074cf80
}

MapScreenshotBank::~MapScreenshotBank() {}

bool MapScreenshotBank::load( const char *pName,
                              const eCategory category,
                              OnLoaded_t pCallback,
                              void *pUserData ) {
    // FUN_0074d220
    if ( state == MSS_Loading ) {
        gpFile->flushPendingAsync();
    }
    if ( state == MSS_Ready ) {
        release();
    }

    char bankPath[256];
    snprintf( bankPath, sizeof( bankPath ),
              "%sFrontEnd/AllRes/MapScreens/%s/%s.bnk",
              gpConfig->getResRootPath(), kCategoryFolders[category], pName );
    snprintf( name, sizeof( name ), "%s/%s", kCategoryFolders[category],
              pName );
    pOnLoaded = pCallback;
    pCallbackUserData = pUserData;
    state = MSS_Loading;

    if ( !isLoaded() &&
         !loadBankWithMemory( gpFile, bankPath, pBuffer, kBufferSize, true ) ) {
        state = MSS_Idle;
        return false;
    }

    state = MSS_Loading;
    return true;
}

void MapScreenshotBank::release() {
    // FUN_0074d150
    if ( state == MSS_Loading ) {
        gpFile->flushPendingAsync();
    }
    if ( state != MSS_Ready ) {
        state = MSS_Idle;
        return;
    }

    for ( int32_t i = 0; i < num2DB; i++ ) {
        collection2D.unregister2DB( &render2DBs[i] );
        render2DBs[i].destroy();
    }
    if ( b2DMInitialized ) {
        collection2D.unregister2DM( &render2DM );
        render2DM.destroy();
        b2DMInitialized = false;
    }

    unload();
    name[0] = '\0';
    state = MSS_Idle;
}

Material *MapScreenshotBank::getMaterial() {
    // FUN_00745a50 (inlined)
    if ( state != MSS_Ready || !b2DMInitialized ) {
        return nullptr;
    }
    return render2DM.getMaterialAtIndex( 0 );
}

void MapScreenshotBank::loadContent() {
    // FUN_0074cec0
    gpTestDriveInstance->pushResourceToStream( this );
}

void MapScreenshotBank::instanciate() {
    // FUN_0074d020
    if ( !isLoaded() ) {
        return;
    }

    void *p2DM = getFirstEntry( AG_WaterFX, AT_2DM, nullptr );
    if ( p2DM == nullptr ) {
        return;
    }
    b2DMInitialized = render2DM.initialize( p2DM );

    num2DB = 0;
    void *p2DB = getFirstEntry( AG_WaterFX, AT_2DB, nullptr );
    while ( p2DB != nullptr && num2DB < kMax2DB ) {
        render2DBs[num2DB].initialize( p2DB );
        collection2D.register2DB( &render2DBs[num2DB] );
        Material *pMaterial = render2DM.getMaterialAtIndex( num2DB );
        if ( pMaterial != nullptr ) {
            SetupPictureMaterial( pMaterial );
        }
        num2DB++;
        p2DB = getNextEntry( AG_WaterFX, AT_2DB, nullptr );
    }
    collection2D.register2DM( &render2DM );

    state = MSS_Ready;
    if ( pOnLoaded != nullptr ) {
        pOnLoaded( this, pCallbackUserData );
        pOnLoaded = nullptr;
    }
}
