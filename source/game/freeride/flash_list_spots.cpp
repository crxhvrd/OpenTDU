#include "shared.h"
#include "flash_list_spots.h"

#include "database/gs_database.h"
#include "flash/flash_callbacks_register.h"
#include "flash/implementation/flash_player.h"
#include "flash/implementation/flash_sprite.h"
#include "game/mng_hud.h"
#include "game/scenaric/gs_scenaric.h"
#include "game/scenaric/spots.h"
#include "player_data/gs_playerdata.h"
#include "player_data/player_data_interface.h"
#include "player_data/saved_garage.h"
#include "render/draw_list.h"

static constexpr uint32_t kCarShopDatabaseIndex = 8u;

struct SpotListCategory {
    const char *pName;
    int32_t Type;
    int32_t SubType;
};

static constexpr SpotListCategory kSpotListCategories[] = {
    {"bikeDealer", ST_BikeDealer,  -1},
    {"carDealer",  ST_CarDealer,   -1},
    {"carRental",  ST_Unknown8,    2 },
    {"carWash",    ST_Unknown8,    0 },
    {"estates",    ST_Unknown8,    4 },
    {"goodies",    ST_Goodies,     -1},
    {"tuning",     ST_AfterMarket, -1},
    {"carPaint",   ST_Unknown8,    1 },
};

static Garage *GetPlayerGarage() {
    return gpPlayerData->getLoadedSave()->getGarage();
}

static void BuildHouseGarageList( char *pOutList,
                                  const size_t outSize,
                                  const uint64_t houseHashcode ) {
    // FUN_008932f0
    pOutList[0] = '\0';

    Garage *pGarage = GetPlayerGarage();
    const int32_t numCars = pGarage->getNumCarsInHouse( houseHashcode );
    for ( int32_t i = 0; i < numCars; i++ ) {
        const GarageSlot *pCar =
            pGarage->getCarAtIndex( i, houseHashcode, false );
        const DBEntryCarPhysicsData *pCarData =
            gpDatabase->getCarDBEntry( int32_t( pCar->getCarConfigID() ) );
        if ( pCarData == nullptr ) {
            continue;
        }

        const char *pCarName = pCarData->CarFriendlyName.pAsString;
        if ( *pCarName == '?' ) {
            pCarName = pCarData->ModelName.pAsString;
        }

        const size_t length = strlen( pOutList );
        if ( length == 0 ) {
            snprintf( pOutList, outSize, "%s", pCarName );
        } else {
            snprintf( pOutList + length, outSize - length, "\n%s", pCarName );
        }
    }
}

static const DBEntryCarShop *FindCarShopBySpot( const uint64_t spotHashcode ) {
    const GameDatabase &gameDB = gpDatabase->getGameDB();
    for ( uint32_t i = 0; i < gameDB.pNumItems[kCarShopDatabaseIndex]; i++ ) {
        const DBEntryCarShop &carShop = gameDB.DBCarShop[i];
        if ( GetIdentifier64bit( carShop.SpotName.pAsString ) ==
             spotHashcode ) {
            return &carShop;
        }
    }
    return nullptr;
}

FlashListSpots::FlashListSpots()
    : FlashList(),
      pPicture( nullptr ),
      pPlayer( nullptr ),
      garageList{},
      spotScreens(),
      bSpotScreensLoaded( false ),
      spotType( 0 ),
      spotSubType( 0 ),
      pSelectedSpot( nullptr ) {
    // FUN_0042c500
    levelIndex = 1;
    bIsEmpty = true;
    field16_0x67 = true;
}

FlashListSpots::~FlashListSpots() {}

int32_t FlashListSpots::getListLength() const {
    // FUN_006e6760
    if ( spotType == ST_House ) {
        return GetPlayerGarage()->getNumOwnedHouses();
    }
    return gpScenaric->getSpotList().countWorldSpots( spotType, spotSubType );
}

void FlashListSpots::open() {
    // FUN_006ec700
    pSelectedSpot = nullptr;
    pPicture = nullptr;

    char spotName[32] = {};
    if ( pPlayer != nullptr ) {
        pPlayer->getVariableAsString( "/:WhatSpot", spotName );
    }

    spotType = ST_House;
    for ( const SpotListCategory &category : kSpotListCategories ) {
        if ( strcmp( spotName, category.pName ) == 0 ) {
            spotType = category.Type;
            if ( category.SubType != -1 ) {
                spotSubType = category.SubType;
            }
            break;
        }
    }

    loadSpotScreens();
    FlashList::open();
}

void FlashListSpots::close() {
    // FUN_006e80b0
    bOpened = false;
    gFlashCallbacksRegister.removeAllSynonyms(
        &FlashListSpots::PictureDrawCallback );
    releaseSpotScreens();
}

void FlashListSpots::update( const int32_t param_2, const int32_t param_3 ) {
    // FUN_006e7f80
    if ( spotType == ST_House ) {
        const DBEntryHouse *pHouse =
            GetPlayerGarage()->getOwnedHouse( param_2 );
        if ( pHouse != nullptr ) {
            setLineField( param_3, "name", pHouse->HouseName.pAsString );
        }
        return;
    }

    setLineField( param_3, "name", "UNKNOW NAME" );

    const SpotMap *pSpot = gpScenaric->getSpotList().getWorldSpotOfType(
        param_2, spotType, spotSubType );
    const DBEntryCarShop *pCarShop = FindCarShopBySpot( pSpot->Hashcode );
    if ( pCarShop != nullptr ) {
        setLineField( param_3, "name", pCarShop->Label.pAsString );
    }
}

void FlashListSpots::selectedLineChanged() {
    // FUN_006e6600
    const int32_t index = firstLine + selectedLineIndex;

    uint64_t spotHashcode = 0ull;
    if ( spotType == ST_House ) {
        const DBEntryHouse *pHouse = GetPlayerGarage()->getOwnedHouse( index );
        spotHashcode = GetIdentifier64bit( pHouse->SpotName.pAsString );
        BuildHouseGarageList( garageList, sizeof( garageList ), spotHashcode );
        if ( pPlayer != nullptr ) {
            pPlayer->setVariableValue( "/:garage_list", garageList );
        }
    } else {
        spotHashcode = gpScenaric->getSpotList()
                           .getWorldSpotOfType( index, spotType, spotSubType )
                           ->Hashcode;
    }

    Spots &spots = gpScenaric->getSpotList();
    pSelectedSpot =
        spots.getWorldSpotChecked( spots.getSpotMapIndex( spotHashcode ) );

    pPicture = nullptr;
    gFlashCallbacksRegister.removeAllSynonyms(
        &FlashListSpots::PictureDrawCallback );
    if ( bSpotScreensLoaded ) {
        pPicture = spotScreens.getMaterial( spotHashcode );
        if ( pPicture == nullptr ) {
            pPicture = spotScreens.getMaterial( "undf" );
        }
    }
    if ( pPicture != nullptr ) {
        gFlashCallbacksRegister.add(
            "picture", &FlashListSpots::PictureDrawCallback, this );
    }

    if ( pPlayer != nullptr ) {
        pPlayer->showFrame( "change", "/menus/ss_menus/array/spots/pict",
                            true );
    }
}

void FlashListSpots::loadSpotScreens() {
    // FUN_006ec3d0
    if ( !bSpotScreensLoaded ) {
        bSpotScreensLoaded = spotScreens.loadImmediate();
    }
}

void FlashListSpots::releaseSpotScreens() {
    // FUN_006e7ea0
    if ( !bSpotScreensLoaded ) {
        return;
    }

    pPicture = nullptr;
    spotScreens.release();
    spotScreens.unload();
    bSpotScreensLoaded = false;
}

void FlashListSpots::PictureDrawCallback( FlashRECT * /*pViewport*/,
                                          float /*depth*/,
                                          DrawList *pDrawList,
                                          void *pUserData,
                                          FlashResource *pResource,
                                          const char * /*pClipName*/ ) {
    // FUN_006e6540
    FlashListSpots *pList = static_cast<FlashListSpots *>( pUserData );
    if ( pList->pPlayer == nullptr ) {
        return;
    }

    FlashClip pictureClip;
    if ( !pList->pPlayer->getClipPlayer(
             "/menus/ss_menus/array/spots/pict/picture", &pictureClip ) ) {
        return;
    }

    FlashRECT viewport;
    pictureClip.getPropertyViewport( eFlashProperty::FP_Viewport, viewport );
    if ( pList->pPicture == nullptr ) {
        return;
    }

    pDrawList->setActiveMaterial( pList->pPicture );
    pDrawList->setDiffuse( 0, 0xffffffffu );
    DrawGaugeQuad( 0.0f, 0.0f, 1.0f, 1.0f, viewport.X, viewport.Y,
                   viewport.X + viewport.Width, viewport.Y + viewport.Height,
                   pResource );
}
