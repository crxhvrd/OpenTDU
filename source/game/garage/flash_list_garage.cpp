#include "shared.h"
#include "flash_list_garage.h"

#include "gm_garage.h"

#include "database/gs_database.h"
#include "flash/implementation/flash_player.h"
#include "player_data/driver.h"
#include "player_data/gs_playerdata.h"
#include "player_data/player_data_interface.h"
#include "player_data/saved_garage.h"

// Column header labels (database 0).
static constexpr uint64_t kColumnLabelHashcodes[4] = {
    0x36d02e2, 0x3c31222, 0x362c9b2, 0x3649e72 };

static inline Garage *GetGarage() {
    return gpPlayerData->getLoadedSave()->getGarage();
}

FlashListGarageNotifier::FlashListGarageNotifier( FlashListGarage *pOwner )
    : FlashPanel(), pList( pOwner ) {}

void FlashListGarageNotifier::tick() {
    // FUN_009c4100
    pList->onGarageUpdated();
}

FlashListGarage::FlashListGarage( GMGarage *pOwner )
    : FlashList(),
      notifier( this ),
      pGMGarage( pOwner ),
      bSuspended( true ),
      slots{},
      pHouseNames{} {
    // FUN_009c3bc0
    levelIndex = 1;
    field16_0x67 = true;

    refreshSlots();
}

void FlashListGarage::refreshSlots() {
    // FUN_009c3a80
    for ( int32_t &slot : slots ) {
        slot = -1;
    }

    Garage *pGarage = GetGarage();

    int32_t numSlots = 0;
    for ( int32_t i = 0; i < kNumSlots; i++ ) {
        if ( pGarage->pCars[i].isValid() ) {
            slots[numSlots++] = i;
        }
    }
}

void FlashListGarage::refreshHouseNames() {
    // FUN_009c3c90
    Garage *pGarage = GetGarage();
    Driver *pDriver = gpPlayerData->getLoadedSave()->getDriver();
    const GameDatabase &gameDB = gpDatabase->getGameDB();
    const uint32_t numHouses = gameDB.pNumItems[Garage::kHouseDatabaseIndex];

    for ( int32_t i = 0; i < kNumSlots; i++ ) {
        pHouseNames[i] = nullptr;

        GarageSlot &car = pGarage->pCars[i];
        if ( !car.isValid() ) {
            continue;
        }

        const bool bIsCurrentCar =
            pGarage->pCars[pDriver->CurrentCar].getCarID() == car.getCarID();
        const uint64_t houseHashcode = bIsCurrentCar
                                           ? pDriver->getCurrentHouse()
                                           : car.getHouseHashcode();

        for ( uint32_t j = 0; j < numHouses; j++ ) {
            const DBEntryHouse &house = gameDB.DBHouse[j];
            if ( GetIdentifier64bit( house.SpotName.pAsString ) ==
                 houseHashcode ) {
                pHouseNames[i] = house.HouseName.pAsString;
                break;
            }
        }
    }
}

bool FlashListGarage::compareSlots( const int32_t slotA,
                                    const int32_t slotB ) const {
    // FUN_009c3ae0
    Garage *pGarage = GetGarage();

    switch ( selectedColumnIndex ) {
    case 0:
        return strcmp( pGarage->pCars[slotA].getConfig()->FullName.c_str(),
                       pGarage->pCars[slotB].getConfig()->FullName.c_str() ) >=
               0;
    case 1:
        return strcmp( pHouseNames[slotA], pHouseNames[slotB] ) >= 0;
    default:
        return slotA > slotB;
    }
}

void FlashListGarage::rebuild() {
    // FUN_009c3e70
    refreshSlots();
    refreshHouseNames();

    const int32_t numCars = GetGarage()->getNumCars();
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

int32_t FlashListGarage::getListLength() const {
    // FUN_009c3c70
    if ( bSuspended ) {
        return 0;
    }

    return GetGarage()->getNumCars();
}

void FlashListGarage::open() {
    // FUN_009c41f0
    FlashList::open();

    for ( int32_t i = 0; i < 4; i++ ) {
        const char *pLabel =
            gpDatabase->getStringByHashcode( 0, kColumnLabelHashcodes[i] );

        char columnVariable[128];
        snprintf( columnVariable, sizeof( columnVariable ), "%s:column%d",
                  movieName.c_str(), i + 1 );
        pMovie->setVariableValue( columnVariable, pLabel );
    }

    sort( 1 );
}

void FlashListGarage::sort( int32_t /*param_1*/ ) {
    // FUN_009c3f20
    rebuild();
    reset();
    selectedLineChanged();
}

void FlashListGarage::update( const int32_t param_2, const int32_t param_3 ) {
    // FUN_009c3f40
    if ( getListLength() <= param_2 ) {
        setLineState( eLineState::FLLS_Empty, param_3 );
        return;
    }

    const int32_t slot = slots[param_2];
    if ( slot == -1 ) {
        return;
    }

    Garage *pGarage = GetGarage();
    GarageSlot &car = pGarage->pCars[slot];

    if ( car.state == CarDesc::eState::CDS_PutOnEbay ) {
        setLineState( eLineState::FLLS_Red, param_3 );
    } else {
        const bool bIsSelectedCar =
            pGMGarage != nullptr &&
            pGarage->pCars[pGMGarage->getSelectedSlot()].getCarID() ==
                car.getCarID();

        char linePath[128];
        snprintf( linePath, sizeof( linePath ), "%s/line%d", movieName.c_str(),
                  param_3 + 1 );

        char validVariable[128];
        snprintf( validVariable, sizeof( validVariable ), "%s/line%d/:IsValid",
                  movieName.c_str(), param_3 + 1 );

        pMovie->setVariableValue( validVariable, bIsSelectedCar ? 0 : 1 );
        pMovie->showFrame( bIsSelectedCar ? "gold" : "normal", linePath,
                           false );
    }

    if ( car.getConfig() != nullptr ) {
        setLineField( param_3, "name", car.getConfig()->FullName.c_str() );
    } else {
        setLineFieldNonLinked( param_3, "name", "??" );
    }

    setLineField( param_3, "house", pHouseNames[slot] );
}

void FlashListGarage::selectedLineChanged() {
    // FUN_009c41a0
    Garage *pGarage = GetGarage();
    const GarageSlot &car = pGarage->pCars[getSelectedSlot()];
    const bool bIsCarHere = pGarage->getHouseCarIndex( &car ) != -1;

    notifier.pMovie->setVariableValue( "/:IsCarHere", bIsCarHere ? 1 : 0 );
}

void FlashListGarage::onGarageUpdated() {
    // FUN_009c4100
    bSuspended = false;

    Driver *pDriver = gpPlayerData->getLoadedSave()->getDriver();
    if ( pDriver->isValid() && pGMGarage != nullptr ) {
        pGMGarage->watchCar( pDriver->CurrentCar );
    }

    const int32_t numHouseCars = GetGarage()->getNumHouseCars();
    if ( notifier.pMovie != nullptr ) {
        char numCars[32];
        snprintf( numCars, sizeof( numCars ), "%d", numHouseCars );
        notifier.pMovie->setVariableValueNonLinked( "/:nb_cars", numCars );
    }

    rebuild();
    reset();
}
