#include "shared.h"
#include "mng_houses.h"

#include "config/gs_config.h"
#include "core/format.h"
#include "database/gs_database.h"
#include "filesystem/gs_file.h"
#include "flash/flash_callbacks_register.h"
#include "flash/gs_flash.h"
#include "flash/implementation/flash_player.h"
#include "flash/implementation/flash_sprite.h"
#include "game/mng_hud.h"
#include "game/scenaric/gs_scenaric.h"
#include "game/scenaric/spots.h"
#include "player_data/gs_playerdata.h"
#include "player_data/player_data_interface.h"
#include "player_data/saved_garage.h"
#include "render/draw_list.h"
#include "render/material.h"
#include "tdu_instance.h"
#include "world/mng_sector_name.h"

static constexpr const char *kDefaultDistrictName = "Ohau";
static constexpr float kAlphaToByte = 2.55f; // DAT_00f41ecc

static int CompareHouseEntries( const void *pA, const void *pB ) {
    // LAB_00797540
    const EstatesHouses::Entry *pEntryA =
        static_cast<const EstatesHouses::Entry *>( pA );
    const EstatesHouses::Entry *pEntryB =
        static_cast<const EstatesHouses::Entry *>( pB );
    if ( pEntryA->Level == pEntryB->Level ) {
        return pEntryA->PriceBuy - pEntryB->PriceBuy;
    }
    return pEntryA->Level - pEntryB->Level;
}

static void FillHouseEntry( EstatesHouses::Entry &entry,
                            const DBEntryHouse *pHouse,
                            SpotMap *pSpot,
                            const bool bOwned ) {
    entry.pName = pHouse->HouseName.pAsString;
    entry.pSpotName = pHouse->SpotName.pAsString;
    entry.Level = pHouse->Level;
    entry.PriceBuy = pHouse->PriceBuy;
    entry.PriceSell = pHouse->PriceSell;
    entry.NumGarageSlot = pHouse->NumGarageSlot;
    entry.bOwned = bOwned;
    entry.pSpot = pSpot;
}

const DBEntryHouse *FindHouseEntry( const uint64_t spotHashcode ) {
    // FUN_007975e0
    const GameDatabase &gameDB = gpDatabase->getGameDB();
    const uint32_t numHouses = gameDB.pNumItems[Garage::kHouseDatabaseIndex];
    for ( uint32_t i = 0; i < numHouses; i++ ) {
        const DBEntryHouse &house = gameDB.DBHouse[i];
        if ( GetIdentifier64bit( house.SpotName.pAsString ) == spotHashcode ) {
            return &house;
        }
    }
    return nullptr;
}

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

EstatesHouses::EstatesHouses()
    : pFlashPlayer( nullptr ),
      pictureBank(),
      bRealtor( true ),
      entries{},
      numEntries( 0 ),
      selectedEntry( 0 ),
      pPendingCallback( nullptr ),
      pendingSpotName{},
      pPendingUserData( nullptr ) {
    // FUN_0079ac60 (inlined)
}

void EstatesHouses::initialize( SpotMap *pSpot ) {
    // FUN_00797de0
    pFlashPlayer = gpFlash->getFlashPlayer( "SHOPS" );
    setPictureCallbacksEnabled( false );

    if ( pSpot->Type == 0 ) {
        bRealtor = false;
        numEntries = 0;
        selectedEntry = 0;

        const DBEntryHouse *pHouse = FindHouseEntry( pSpot->Hashcode );
        if ( pHouse != nullptr ) {
            FillHouseEntry( entries[numEntries], pHouse, pSpot, false );
            numEntries++;
        }
    } else {
        bRealtor = true;
        collectRealtorHouses( pSpot->Hashcode );
    }

    pFlashPlayer->setVariableValue( "/:nb_houses", numEntries );
    pFlashPlayer->setVariableValue( "/:num_house", 1 );
}

bool EstatesHouses::collectRealtorHouses( const uint64_t realtorHashcode ) {
    // FUN_007978d0
    numEntries = 0;
    selectedEntry = 0;

    Spots &spots = gpScenaric->getSpotList();
    Garage *pGarage = gpPlayerData->getLoadedSave()->getGarage();
    const int32_t numSpots = int32_t( spots.getNumWorldSpots() );
    for ( int32_t i = 0; i < numSpots; i++ ) {
        SpotMap *pSpot = spots.getWorldSpotChecked( i );
        if ( pSpot == nullptr || pSpot->Type != 0 ) {
            continue;
        }

        const DBEntryHouse *pHouse = FindHouseEntry( pSpot->Hashcode );
        if ( pHouse == nullptr ||
             GetIdentifier64bit( pHouse->RealtorHashcode.pAsString ) !=
                 realtorHashcode ) {
            continue;
        }

        FillHouseEntry( entries[numEntries], pHouse, pSpot,
                        pGarage->isHouseOwned( pSpot->Hashcode ) );
        numEntries++;
    }

    if ( numEntries > 1 ) {
        qsort( entries, size_t( numEntries ), sizeof( Entry ),
               &CompareHouseEntries );
    }
    return numEntries != 0;
}

bool EstatesHouses::selectHouse( HousePictureBank::OnLoaded_t pCallback,
                                 void *pUserData,
                                 const int32_t index ) {
    // FUN_00798250
    selectedEntry = index;

    Spots &spots = gpScenaric->getSpotList();
    const int32_t spotIndex =
        spots.getSpotMapIndex( entries[index].pSpot->Hashcode );
    if ( spotIndex == -1 ) {
        return false;
    }
    SpotMap *pSpot = spots.getWorldSpotChecked( spotIndex );

    Entry &entry = entries[selectedEntry];
    pFlashPlayer->setVariableValueNonLinked( "/:CurHouse", entry.pName );

    char priceText[32];
    FormatMoneyString( priceText, entry.PriceBuy );
    pFlashPlayer->setVariableValueNonLinked( "/:PriceHouse", priceText );
    pFlashPlayer->setVariableValue( "/:GarPlaces", entry.NumGarageSlot );
    pFlashPlayer->setVariableValue( "/:state_sold", entry.bOwned ? 1 : 0 );

    const Eigen::Vector4f spotPosition( pSpot->X, pSpot->Y, pSpot->Z, 1.0f );
    const char *pDistrict = ( gpMngSectorName != nullptr )
                                ? gpMngSectorName->getSectorName( spotPosition )
                                : kDefaultDistrictName;
    char address[32];
    strncpy( address, pDistrict, sizeof( address ) - 1 );
    address[sizeof( address ) - 1] = '\0';
    pFlashPlayer->setVariableValueNonLinked( "/:Adress", address );

    return requestPicture( pCallback, pUserData, entry.pSpotName );
}

bool EstatesHouses::requestPicture( HousePictureBank::OnLoaded_t pCallback,
                                    void *pUserData,
                                    const char *pSpotName ) {
    // FUN_007981f0
    if ( pCallback != nullptr ) {
        gFlashCallbacksRegister.removeAllSynonyms(
            &EstatesHouses::PictureDrawCallback );
        strncpy( pendingSpotName, pSpotName, sizeof( pendingSpotName ) - 1 );
        pendingSpotName[sizeof( pendingSpotName ) - 1] = '\0';
        pPendingUserData = pUserData;
        pPendingCallback = pCallback;
        return true;
    }

    // FUN_007980d0
    pictureBank.release();
    return pictureBank.load( pSpotName, nullptr, nullptr );
}

void EstatesHouses::tickPendingPicture() {
    // FUN_00798410
    if ( pPendingCallback == nullptr ) {
        return;
    }

    if ( pictureBank.isReady() ) {
        pictureBank.release();
    }

    if ( !pictureBank.isLoading() && !pictureBank.isReady() ) {
        pictureBank.load( pendingSpotName, pPendingCallback, pPendingUserData );
        pPendingCallback = nullptr;
    }
}

void EstatesHouses::setPictureCallbacksEnabled( const bool bEnabled ) {
    // FUN_00797d30
    if ( !bEnabled ) {
        gFlashCallbacksRegister.removeAllSynonyms(
            &EstatesHouses::PictureDrawCallback );
        return;
    }

    for ( const char *pClipName : { "outward1", "outward2", "garage1",
                                    "garage2", "living1", "living2" } ) {
        gFlashCallbacksRegister.add(
            pClipName, &EstatesHouses::PictureDrawCallback, this );
    }
}

Material *EstatesHouses::getPicture( const int32_t pictureIndex ) {
    // FUN_00797a70
    if ( !pictureBank.isReady() ) {
        return nullptr;
    }

    switch ( pictureIndex ) {
    case 0:
        return pictureBank.getMaterial( "IMG_EXT" );
    case 1:
        return pictureBank.getMaterial( "IMG_GAR" );
    case 2:
        return pictureBank.getMaterial( "IMG_LVG" );
    default:
        return nullptr;
    }
}

void EstatesHouses::PictureDrawCallback( FlashRECT * /*pViewport*/,
                                         float /*depth*/,
                                         DrawList *pDrawList,
                                         void *pUserData,
                                         FlashResource *pResource,
                                         const char *pClipName ) {
    // LAB_00797ba0
    EstatesHouses *pHouses = static_cast<EstatesHouses *>( pUserData );
    if ( pHouses->pFlashPlayer == nullptr ) {
        return;
    }

    // Only the second clip of each pair hosts the picture
    const char *pClipPath = nullptr;
    int32_t pictureIndex = -1;
    if ( strcmp( pClipName, "outward2" ) == 0 ) {
        pClipPath = "/ss_menu/picts/outward/outward2";
        pictureIndex = 0;
    } else if ( strcmp( pClipName, "garage2" ) == 0 ) {
        pClipPath = "/ss_menu/picts/garage/garage2";
        pictureIndex = 1;
    } else if ( strcmp( pClipName, "living2" ) == 0 ) {
        pClipPath = "/ss_menu/picts/living/living2";
        pictureIndex = 2;
    }

    FlashClip pictureClip;
    if ( pClipPath == nullptr ||
         !pHouses->pFlashPlayer->getClipPlayer( pClipPath, &pictureClip ) ) {
        return;
    }

    FlashRECT viewport;
    pictureClip.getPropertyViewport( eFlashProperty::FP_Viewport, viewport );

    Material *pPicture = pHouses->getPicture( pictureIndex );
    if ( pPicture == nullptr ) {
        return;
    }

    const int32_t alpha = Clamp(
        int32_t( pictureClip.getPropertyFloat( eFlashProperty::FP_Alpha ) *
                 kAlphaToByte ),
        0, 0xff );
    pDrawList->setActiveMaterial( pPicture );
    pDrawList->setDiffuse( 0, ( uint32_t( alpha ) << 24 ) | 0xffffffu );
    DrawGaugeQuad( 0.0f, 0.0f, 1.0f, 1.0f, viewport.X, viewport.Y,
                   viewport.X + viewport.Width, viewport.Y + viewport.Height,
                   pResource );
}
