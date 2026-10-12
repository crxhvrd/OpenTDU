#include "shared.h"
#include "mng_houses.h"

#include "config/gs_config.h"
#include "filesystem/gs_file.h"
#include "render/material.h"
#include "tdu_instance.h"

HousePictureBank::HousePictureBank()
    : Bank(),
      StreamedResource(),
      name(),
      state( HPS_Idle ),
      pBuffer( nullptr ),
      b2DMInitialized( false ),
      num2DB( 0 ),
      collection2D(),
      render2DM(),
      bDeferInstanciation( false ),
      pCallbackUserData( nullptr ),
      pOnLoaded( nullptr ) {
    // FUN_007976d0
}

HousePictureBank::~HousePictureBank() {}

bool HousePictureBank::load( const char *pHouseName,
                             OnLoaded_t pCallback,
                             void *pUserData ) {
    // FUN_00797f60
    if ( strcmp( pHouseName, name ) == 0 ) {
        return true;
    }

    if ( state == HPS_Loading ) {
        gpFile->flushPendingAsync();
    }
    if ( state == HPS_Ready ) {
        release();
    }

    char bankPath[256];
    snprintf( bankPath, sizeof( bankPath ),
              "%sFrontEnd/AllRes/RealtorScr/%s.bnk", gpConfig->getResRootPath(),
              pHouseName );
    strncpy( name, pHouseName, sizeof( name ) - 1 );
    pOnLoaded = pCallback;
    bDeferInstanciation = true;
    pCallbackUserData = pUserData;
    state = HPS_Loading;

    if ( !isLoaded() &&
         !loadBankWithMemory( gpFile, bankPath, pBuffer, kBufferSize, true ) ) {
        state = HPS_Idle;
        return false;
    }

    state = HPS_Loading;
    return true;
}

void HousePictureBank::release() {
    // FUN_00797ad0
    if ( state == HPS_Loading ) {
        gpFile->flushPendingAsync();
    }
    if ( state != HPS_Ready ) {
        state = HPS_Idle;
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
    state = HPS_Idle;
}

Material *HousePictureBank::getMaterial( const char *pMaterialName ) {
    // FUN_00745b10 (inlined)
    if ( state != HPS_Ready ) {
        return nullptr;
    }
    return render2DM.getMaterial( pMaterialName );
}

void HousePictureBank::loadContent() {
    // FUN_007975b0
    if ( bDeferInstanciation ) {
        gpTestDriveInstance->pushResourceToStream( this );
        return;
    }
    instanciate();
}

void HousePictureBank::instanciate() {
    // FUN_007977d0
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
            pMaterial->OT = 0x2a;
            pMaterial->DepthTest = 1;
            pMaterial->DepthWrite = 2;
            pMaterial->AlphaTest = CF_Never;
        }
        num2DB++;
        p2DB = getNextEntry( AG_WaterFX, AT_2DB, nullptr );
    }
    collection2D.register2DM( &render2DM );

    state = HPS_Ready;
    if ( pOnLoaded != nullptr ) {
        pOnLoaded( this, pCallbackUserData );
        pOnLoaded = nullptr;
    }
}
