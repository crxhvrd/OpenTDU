#include "shared.h"
#include "lobby_list_vehicles.h"

#include "gm_lobby.h"
#include "mng_lobby.h"

#include "database/gs_database.h"
#include "flash/gs_flash.h"
#include "flash/implementation/flash_player.h"
#include "game/scenaric/gs_scenaric.h"
#include "game/scenaric/spots.h"
#include "player_data/driver.h"
#include "player_data/gs_playerdata.h"
#include "player_data/player_data_interface.h"
#include "player_data/saved_garage.h"

// Column header labels (database 0).
static constexpr uint64_t kColumnLabelHashcodes[4] = { 0x36d02e2, 0x3c31222,
                                                       0x362c9b2, 0x3649e72 };
static constexpr uint64_t kPowerUnitHashcode = 0x58f3d3;
static constexpr float kRatioBase = 100.0f;     // DAT_00f41708
static constexpr float kRatioToPercent = 0.01f; // DAT_00f416d4

static inline Garage *GetGarage() {
    return gpPlayerData->getLoadedSave()->getGarage();
}

static int32_t ComputePower( const CarDesc &car,
                             const DBEntryCarPhysicsData *pEntry ) {
    const DBEntryAftermarketPack *pPack =
        gpDatabase->getAftermarketPack( car.packHashcode );
    if ( pPack == nullptr ) {
        return pEntry->BHP;
    }
    return int32_t( ( pPack->BHPRatio + kRatioBase ) * float( pEntry->BHP ) *
                    kRatioToPercent );
}

static bool IsSpotRestrictedToSolo( const SpotMap *pSpot ) {
    // FUN_0042d990
    if ( pSpot == nullptr ) {
        return true;
    }
    if ( pSpot->Type == ST_DriveIn ) {
        return true;
    }
    if ( pSpot->Type == ST_Challenge ) {
        return ( pSpot->SubType & 0xf ) == 0;
    }
    return false;
}

LobbyListVehiclesNotifier::LobbyListVehiclesNotifier(
    LobbyListVehicles *pOwner )
    : FlashPanel(), pList( pOwner ) {}

void LobbyListVehiclesNotifier::tick() {
    // FUN_00759080
    pList->onGarageUpdated();
}

LobbyListVehicles::LobbyListVehicles()
    : FlashList(),
      notifier( this ),
      pOwner( nullptr ),
      bEmpty( false ),
      slots{},
      carSpecs(),
      numCars( 0 ) {
    // FUN_00759170
    levelIndex = 1;
    field16_0x67 = true;

    populate();
}

void LobbyListVehicles::populate() {
    // FUN_00758b20
    slots[0] = 0;
    numCars = 0;

    Garage *pGarage = GetGarage();
    for ( int32_t carIndex = 0; carIndex < pGarage->getNumCars(); carIndex++ ) {
        int32_t validIndex =
            ( carIndex < 0 || carIndex > Garage::kNumSavedCarSlots ) ? 0
                                                                     : carIndex;

        CarDesc *pCar = &pGarage->pCars[0];
        for ( int32_t slot = 0; slot < Garage::kNumCarSlots; slot++ ) {
            if ( !pGarage->pCars[slot].isValid() ) {
                continue;
            }
            if ( validIndex == 0 ) {
                pCar = &pGarage->pCars[slot];
                break;
            }
            validIndex--;
        }

        if ( pOwner == nullptr || pOwner->getOwner() == nullptr ) {
            continue;
        }

        bool bAllowed = false;
        if ( gpScenaric->isFromDriveIn() ) {
            OTDU_UNIMPLEMENTED_ONCE; // TODO: EventParameters (FUN_0075d4e0,
                                     // drive-in vehicle restriction)
            bAllowed = true;
        } else if ( IsSpotRestrictedToSolo( pOwner->getOwner()->getSpot() ) ) {
            bAllowed =
                IsCarAllowedOnSpot( pCar, pOwner->getOwner()->getSpot() );
        } else {
            OTDU_UNIMPLEMENTED_ONCE; // TODO: ONLINE (FUN_0075cce0, GameSpy
                                     // session vehicle filter)
        }

        if ( bAllowed ) {
            slots[numCars++] = pGarage->getSlotIndex( pCar );
        }
    }

    if ( pGarage->pCars[kRentalSlot].state == CarDesc::eState::CDS_Rental ) {
        slots[numCars++] = kRentalSlot;
    }
}

bool LobbyListVehicles::compareSlots( const int32_t slotA,
                                      const int32_t slotB ) const {
    // FUN_007586e0
    Garage *pGarage = GetGarage();
    const CarDesc &carA = pGarage->pCars[slotA];
    const CarDesc &carB = pGarage->pCars[slotB];

    switch ( selectedColumnIndex ) {
    case 0:
        return strcmp( carA.getConfig()->FullName.c_str(),
                       carB.getConfig()->FullName.c_str() ) >= 0;
    case 1: {
        const DBEntryAftermarketPack *pPackA =
            gpDatabase->getAftermarketPack( carA.packHashcode );
        const DBEntryAftermarketPack *pPackB =
            gpDatabase->getAftermarketPack( carB.packHashcode );
        const int32_t powerA =
            ( pPackA != nullptr )
                ? int32_t( ( pPackA->BHPRatio + kRatioBase ) *
                           float( carA.getConfig()->PowerCC ) *
                           kRatioToPercent )
                : carA.getConfig()->PowerCC;
        const int32_t powerB =
            ( pPackB != nullptr )
                ? int32_t( ( pPackB->BHPRatio + kRatioBase ) *
                           float( carB.getConfig()->PowerCC ) *
                           kRatioToPercent )
                : carB.getConfig()->PowerCC;
        return powerA >= powerB;
    }
    case 2: {
        const DBEntryCarPhysicsData *pEntryB =
            gpDatabase->getCarDBEntry( int32_t( carB.getCarConfigID() ) );
        const DBEntryCarPhysicsData *pEntryA =
            gpDatabase->getCarDBEntry( int32_t( carA.getCarConfigID() ) );
        return strcmp( pEntryA->CarGroup.pAsString,
                       pEntryB->CarGroup.pAsString ) >= 0;
    }
    default:
        return slotA > slotB;
    }
}

void LobbyListVehicles::rebuild() {
    // FUN_00758c80
    populate();

    for ( int32_t i = 0; i < numCars - 1; i++ ) {
        for ( int32_t j = i + 1; j < numCars; j++ ) {
            const bool bOrdered = compareSlots( slots[i], slots[j] );
            const bool bSwap = bInverted ? !bOrdered : bOrdered;
            if ( bSwap ) {
                std::swap( slots[i], slots[j] );
            }
        }
    }
}

int32_t LobbyListVehicles::getListLength() const {
    // FUN_00758660
    return bEmpty ? 0 : numCars;
}

void LobbyListVehicles::open() {
    // FUN_00758df0
    FlashList::open();

    if ( pOwner != nullptr ) {
        pOwner->onVehicleListOpened();
    }

    for ( int32_t i = 0; i < 4; i++ ) {
        const char *pLabel =
            gpDatabase->getStringByHashcode( 0, kColumnLabelHashcodes[i] );

        char columnVariable[128];
        snprintf( columnVariable, sizeof( columnVariable ), "%s:column%d",
                  movieName.c_str(), i + 1 );
        pMovie->setVariableValue( columnVariable, pLabel );
    }

    carSpecs.pMovie = gpFlash->getFlashPlayer( "CHALLENG" );
    sort( 0 );
    carSpecs.watch( &GetGarage()->pCars[getSelectedSlot()] );

    OTDU_UNIMPLEMENTED; // TODO: ONLINE (declare the local car to the session)

    GetGarage()->subscribe( &notifier );
}

void LobbyListVehicles::close() {
    // FUN_00758680
    GetGarage()->unsubscribe( &notifier );
    carSpecs.detach();
    bOpened = false;
    if ( pOwner != nullptr ) {
        pOwner->onVehicleListClosed();
    }
}

void LobbyListVehicles::sort( int32_t /*param_1*/ ) {
    // FUN_00758dd0
    rebuild();
    reset();
    selectedLineChanged();
}

void LobbyListVehicles::update( const int32_t param_2, const int32_t param_3 ) {
    // FUN_007588b0
    if ( getListLength() <= param_2 ) {
        setLineState( eLineState::FLLS_Empty, param_3 );
        return;
    }

    Garage *pGarage = GetGarage();
    const CarDesc &car = pGarage->pCars[slots[param_2]];
    if ( car.state == CarDesc::eState::CDS_PutOnEbay ) {
        setLineState( eLineState::FLLS_Red, param_3 );
    } else {
        const Driver *pDriver = gpPlayerData->getLoadedSave()->getDriver();
        const bool bIsCurrentCar =
            pGarage->pCars[pDriver->CurrentCar].getCarID() == car.getCarID();

        char linePath[128];
        snprintf( linePath, sizeof( linePath ), "%s/line%d", movieName.c_str(),
                  param_3 + 1 );

        char validVariable[128];
        snprintf( validVariable, sizeof( validVariable ), "%s/line%d/:IsValid",
                  movieName.c_str(), param_3 + 1 );

        pMovie->setVariableValue( validVariable, 1 );
        pMovie->showFrame( bIsCurrentCar ? "gold" : "normal", linePath, false );
    }

    if ( car.getConfig() != nullptr ) {
        setLineField( param_3, "name", car.getConfig()->FullName.c_str() );
    } else {
        setLineFieldNonLinked( param_3, "name", "??" );
    }

    const DBEntryCarPhysicsData *pEntry =
        gpDatabase->getCarDBEntry( int32_t( car.getCarConfigID() ) );
    if ( pEntry == nullptr ) {
        setLineFieldNonLinked( param_3, "power", "" );
        setLineFieldNonLinked( param_3, "type", "" );
        return;
    }

    char power[32];
    snprintf( power, sizeof( power ), "%d %s", ComputePower( car, pEntry ),
              gpDatabase->getStringByHashcode( 0, kPowerUnitHashcode ) );
    setLineFieldNonLinked( param_3, "power", power );
    setLineFieldNonLinked( param_3, "type", pEntry->CarGroup.pAsString );
}

void LobbyListVehicles::selectedLineChanged() {
    // FUN_00758d20
    CarDesc *pCar = &GetGarage()->pCars[getSelectedSlot()];
    carSpecs.watch( pCar );

    DBEntryCarPhysicsData *pEntry =
        gpDatabase->getCarDBEntry( int32_t( pCar->getCarConfigID() ) );
    if ( pOwner != nullptr && pEntry != nullptr ) {
        ShowCarDataGauges( pOwner->getFlashPlayer(), "/data/datas_base",
                           pEntry );
    }
}

void LobbyListVehicles::onGarageUpdated() {
    // FUN_00759080
    bEmpty = false;
    rebuild();

    if ( notifier.pMovie != nullptr ) {
        char numCarsText[32];
        snprintf( numCarsText, sizeof( numCarsText ), "%d", numCars );
        notifier.pMovie->setVariableValueNonLinked( "/:nb_cars", numCarsText );
    }

    reset();
    selectedLineChanged();
}
