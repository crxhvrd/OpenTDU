#include "shared.h"
#include "flash_list_houses.h"

#include "gm_estates.h"

#include "core/format.h"
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
#include "world/mng_sector_name.h"

static constexpr const char *kDefaultDistrictName = "Ohau";
static constexpr const char *kBonusRealtorName = "ERE_BONUS";
static constexpr float kAlphaToByte = 2.55f; // DAT_00f41ecc

static bool IsBonusHouse( const DBEntryHouse *pHouse ) {
    return pHouse != nullptr &&
           strcmp( pHouse->RealtorHashcode.pAsString, kBonusRealtorName ) == 0;
}

FlashListHouses::FlashListHouses()
    : FlashList(),
      pOwner( nullptr ),
      numHouses( 0 ),
      sortedHouses{},
      houseHashcodes{},
      displayedHouseHashcode( 0ull ) {
    // FUN_0079ac60 (inlined)
    levelIndex = 1;
    field16_0x67 = true;
}

void FlashListHouses::open() {
    // FUN_00799170
    EstatesHouses &houses = pOwner->getHouses();
    gFlashCallbacksRegister.removeAllSynonyms(
        &EstatesHouses::PictureDrawCallback );
    houses.getPictureBank().release();
    houses.setFlashPlayer( nullptr );

    displayedHouseHashcode = ~0ull;
    selectedLineChanged();
    FlashList::open();
}

void FlashListHouses::close() {
    // FUN_0079aba0
    EstatesHouses &houses = pOwner->getHouses();
    houses.initialize( pOwner->getHouseSpot() );
    // NOTE: Retail hands the list itself as the callback user data (the
    // callback then writes past the list); the owner is what it expects.
    houses.selectHouse( &GMEstates::OnHousePictureLoaded, pOwner, 0 );
    pOwner->changeHouse();
    bOpened = false;
}

void FlashListHouses::sort( int32_t /*param_1*/ ) {
    // FUN_00798da0
    sortHouses();
    reset();
    selectedLineChanged();
}

void FlashListHouses::update( const int32_t param_2, const int32_t param_3 ) {
    // FUN_00799a20
    const int32_t houseIndex = sortedHouses[param_2];
    const DBEntryHouse *pHouse = FindHouseEntry( houseHashcodes[houseIndex] );
    const int32_t lineNumber = param_3 + 1;

    char variable[128];
    snprintf( variable, sizeof( variable ), "%s/line%d:%s", movieName.c_str(),
              lineNumber, "name" );
    pMovie->setVariableValueNonLinked( variable, pHouse->HouseName.pAsString );

    char value[32];
    FormatMoneyString( value, pHouse->PriceSell );
    snprintf( variable, sizeof( variable ), "%s/line%d:%s", movieName.c_str(),
              lineNumber, "price" );
    pMovie->setVariableValueNonLinked( variable, value );

    snprintf( value, sizeof( value ), "%d", pHouse->NumGarageSlot );
    snprintf( variable, sizeof( variable ), "%s/line%d:%s", movieName.c_str(),
              lineNumber, "places" );
    pMovie->setVariableValueNonLinked( variable, value );
}

void FlashListHouses::selectedLineChanged() {
    // FUN_0079a3c0
    const int32_t houseIndex = sortedHouses[firstLine + selectedLineIndex];
    const uint64_t hashcode = houseHashcodes[houseIndex];
    if ( hashcode == displayedHouseHashcode ) {
        return;
    }

    EstatesHouses &houses = pOwner->getHouses();
    houses.getPictureBank().release();
    displayedHouseHashcode = hashcode;
    pOwner->setRetakeHouse( hashcode );

    FlashPlayer *pPlayer = pOwner->getFlashPlayer();
    pPlayer->setVariableValue( "/:InfosLoaded", 0 );

    Spots &spots = gpScenaric->getSpotList();
    const int32_t spotIndex = spots.getSpotMapIndex( hashcode );
    if ( spotIndex == -1 ) {
        return;
    }
    SpotMap *pSpot = spots.getWorldSpotChecked( spotIndex );
    if ( pSpot == nullptr ) {
        return;
    }

    const Eigen::Vector4f spotPosition( pSpot->X, pSpot->Y, pSpot->Z, 1.0f );
    const char *pDistrict = ( gpMngSectorName != nullptr )
                                ? gpMngSectorName->getSectorName( spotPosition )
                                : kDefaultDistrictName;
    char address[32];
    strncpy( address, pDistrict, sizeof( address ) - 1 );
    address[sizeof( address ) - 1] = '\0';
    pPlayer->setVariableValueNonLinked( "/:Adress", address );

    const DBEntryHouse *pHouse = FindHouseEntry( hashcode );
    if ( pHouse != nullptr && pHouse->SpotName.pAsString != nullptr ) {
        houses.requestPicture( &GMEstates::OnHousePictureLoaded, pOwner,
                               pHouse->SpotName.pAsString );
    }
}

void FlashListHouses::refresh() {
    // FUN_00798dd0
    gFlashCallbacksRegister.add( "homelure",
                                 &FlashListHouses::HomeLureDrawCallback, this );

    Spots &spots = gpScenaric->getSpotList();
    Garage *pGarage = gpPlayerData->getLoadedSave()->getGarage();
    const int32_t numSpots = int32_t( spots.getNumWorldSpots() );

    numHouses = 0;
    for ( int32_t i = 0; i < numSpots; i++ ) {
        SpotMap *pSpot = spots.getWorldSpotChecked( i );
        if ( pSpot != nullptr && pSpot->Type == 0 &&
             pGarage->isHouseOwned( pSpot->Hashcode ) ) {
            numHouses++;
        }
    }

    int32_t numListed = 0;
    for ( int32_t i = 0; i < numSpots; i++ ) {
        SpotMap *pSpot = spots.getWorldSpotChecked( i );
        if ( pSpot == nullptr || pSpot->Type != 0 ||
             !pGarage->isHouseOwned( pSpot->Hashcode ) ) {
            continue;
        }

        if ( IsBonusHouse( FindHouseEntry( pSpot->Hashcode ) ) ) {
            numHouses--;
            continue;
        }

        houseHashcodes[numListed++] = pSpot->Hashcode;
    }

    sortHouses();
    if ( bOpened ) {
        selectedLineChanged();
    }
}

void FlashListHouses::sortHouses() {
    // FUN_00798630
    for ( int32_t i = 0; i < numHouses; i++ ) {
        sortedHouses[i] = i;
    }

    for ( int32_t i = 0; i < numHouses - 1; i++ ) {
        for ( int32_t j = i + 1; j < numHouses; j++ ) {
            const bool bOrdered =
                compareHouses( sortedHouses[i], sortedHouses[j] );
            if ( bOrdered != bInverted ) {
                std::swap( sortedHouses[i], sortedHouses[j] );
            }
        }
    }
}

bool FlashListHouses::compareHouses( const int32_t houseA,
                                     const int32_t houseB ) const {
    // FUN_00798550
    const DBEntryHouse *pHouseA = FindHouseEntry( houseHashcodes[houseA] );
    const DBEntryHouse *pHouseB = FindHouseEntry( houseHashcodes[houseB] );

    switch ( selectedColumnIndex ) {
    case 0:
        return strcmp( pHouseA->HouseName.pAsString,
                       pHouseB->HouseName.pAsString ) >= 0;
    case 2:
        if ( pHouseA->NumGarageSlot != pHouseB->NumGarageSlot ) {
            return pHouseB->NumGarageSlot < pHouseA->NumGarageSlot;
        }
        [[fallthrough]];
    case 1:
        return pHouseB->PriceSell < pHouseA->PriceSell;
    default:
        return houseA > houseB;
    }
}

void FlashListHouses::HomeLureDrawCallback( FlashRECT * /*pViewport*/,
                                            float /*depth*/,
                                            DrawList *pDrawList,
                                            void *pUserData,
                                            FlashResource *pResource,
                                            const char * /*pClipName*/ ) {
    // LAB_00798c50
    FlashListHouses *pList = static_cast<FlashListHouses *>( pUserData );
    GMEstates *pOwner = pList->pOwner;
    HousePictureBank &pictureBank = pOwner->getHouses().getPictureBank();

    FlashPlayer *pPlayer = pOwner->getFlashPlayer();
    if ( pPlayer == nullptr || !pictureBank.isReady() ) {
        return;
    }

    FlashClip lureClip;
    pPlayer->getClipPlayer( "/ss_menu/array_houses/homelure", &lureClip );

    FlashRECT viewport;
    lureClip.getPropertyViewport( eFlashProperty::FP_Viewport, viewport );

    Material *pPicture = pictureBank.getMaterial( "IMG_EXT" );
    if ( pPicture == nullptr ) {
        return;
    }

    const int32_t alpha =
        Clamp( int32_t( lureClip.getPropertyFloat( eFlashProperty::FP_Alpha ) *
                        kAlphaToByte ),
               0, 0xff );
    pDrawList->setActiveMaterial( pPicture );
    pDrawList->setDiffuse( 0, ( uint32_t( alpha ) << 24 ) | 0xffffffu );
    DrawGaugeQuad( 0.0f, 0.0f, 1.0f, 1.0f, viewport.X, viewport.Y,
                   viewport.X + viewport.Width, viewport.Y + viewport.Height,
                   pResource );
}
