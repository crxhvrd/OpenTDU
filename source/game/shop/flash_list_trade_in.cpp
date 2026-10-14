#include "shared.h"
#include "flash_list_trade_in.h"

#include "shop_logic.h"

#include "core/format.h"
#include "database/gs_database.h"
#include "flash/implementation/flash_player.h"
#include "flash/mng_flash_localize.h"
#include "game/vehicle/car_desc.h"
#include "player_data/gs_playerdata.h"
#include "player_data/player_data_interface.h"
#include "player_data/saved_garage.h"

// Column header labels (database 0).
static constexpr uint64_t kColumnLabel1Hashcode = 0x36d02e2;
static constexpr uint64_t kColumnLabel2Hashcode = 0x3611c02;

static inline Garage *GetGarage() {
    return gpPlayerData->getLoadedSave()->getGarage();
}

FlashListTradeIn::FlashListTradeIn()
    : FlashList(),
      slots{},
      pOwner( nullptr ),
      columnLabel1{},
      columnLabel2{},
      prices{},
      pNames{},
      bMissingConfig{} {
    // FUN_00721820
    levelIndex = 1;
    field16_0x67 = true;

    cacheGarageCars();
    refreshSlots();
}

void FlashListTradeIn::cacheGarageCars() {
    // FUN_00721700
    Garage *pGarage = GetGarage();

    for ( int32_t i = 0; i < kNumSlots; i++ ) {
        prices[i] = 0.0f;
        pNames[i] = nullptr;
        bMissingConfig[i] = true;

        GarageSlot &car = pGarage->pCars[i];
        if ( !car.isValid() ) {
            continue;
        }

        const uint32_t configID = car.getCarConfigID();
        prices[i] = gpDatabase->getCarPrice(
            int32_t( configID ), car.paintIndex, car.rimID, car.interiorID );
        pNames[i] = ( car.getConfig() != nullptr )
                        ? car.getConfig()->FullName.c_str()
                        : "";
        bMissingConfig[i] = FindCarConfig( int32_t( configID ) ) == nullptr;
    }
}

void FlashListTradeIn::refreshSlots() {
    // FUN_007214c0
    std::fill( std::begin( slots ), std::end( slots ), -1 );

    Garage *pGarage = GetGarage();
    int32_t numSlots = 0;
    for ( int32_t i = 0; i < kNumSlots; i++ ) {
        GarageSlot &car = pGarage->pCars[i];
        if ( car.isValid() && car.state != CarDesc::eState::CDS_PutOnEbay &&
             0.0f < prices[i] ) {
            slots[numSlots++] = i;
        }
    }
}

bool FlashListTradeIn::compareSlots( const int32_t slotA,
                                     const int32_t slotB ) const {
    // FUN_00721430
    switch ( selectedColumnIndex ) {
    case 0:
        return strcmp( pNames[slotA], pNames[slotB] ) >= 0;
    case 1:
        return prices[slotA] >= prices[slotB];
    default:
        return slotA > slotB;
    }
}

void FlashListTradeIn::rebuild() {
    // FUN_007215c0
    refreshSlots();

    const int32_t numCars = getListLength();
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

int32_t FlashListTradeIn::getListLength() const {
    // FUN_00721f70
    return GetGarage()->getNumSellableCars( 0ull );
}

void FlashListTradeIn::open() {
    // FUN_00722c30
    if ( pOwner != nullptr ) {
        pOwner->resetTradeIn();
    }

    char columnVariable[128];
    if ( gpMngFlashLocalize != nullptr ) {
        gpMngFlashLocalize->makeSentenceFromHashcode(
            columnLabel1, sizeof( columnLabel1 ), kColumnLabel1Hashcode );
        gpMngFlashLocalize->makeSentenceFromHashcode(
            columnLabel2, sizeof( columnLabel2 ), kColumnLabel2Hashcode );

        snprintf( columnVariable, sizeof( columnVariable ), "%s:column%d",
                  movieName.c_str(), 1 );
        pMovie->setVariableValue( columnVariable, columnLabel1 );
        snprintf( columnVariable, sizeof( columnVariable ), "%s:column%d",
                  movieName.c_str(), 2 );
        pMovie->setVariableValue( columnVariable, columnLabel2 );
    } else {
        snprintf( columnVariable, sizeof( columnVariable ), "%s:column%d",
                  movieName.c_str(), 1 );
        pMovie->setVariableValue( columnVariable, "ERR" );
        snprintf( columnVariable, sizeof( columnVariable ), "%s:column%d",
                  movieName.c_str(), 2 );
        pMovie->setVariableValue( columnVariable, "ERR" );
    }

    FlashList::open();
    rebuild();
    reset();
}

void FlashListTradeIn::sort( int32_t /*param_1*/ ) {
    // FUN_00721ef0
    rebuild();
    reset();

    char linePath[128];
    snprintf( linePath, sizeof( linePath ), "%s/line%d", movieName.c_str(),
              selectedLineIndex + 1 );

    char validVariable[128];
    snprintf( validVariable, sizeof( validVariable ), "%s/line%d/:IsValid",
              movieName.c_str(), selectedLineIndex + 1 );

    pMovie->setVariableValue( validVariable, 1 );
    pMovie->showFrame( "selected", linePath, false );
}

void FlashListTradeIn::update( const int32_t param_2, const int32_t param_3 ) {
    // FUN_00722a70
    if ( getListLength() <= param_2 ) {
        setLineState( eLineState::FLLS_Empty, param_3 );
        return;
    }

    const int32_t lineNumber = param_3 + 1;

    char variable[128];
    snprintf( variable, sizeof( variable ), "%s/line%d/:IsValid",
              movieName.c_str(), lineNumber );
    pMovie->setVariableValue( variable, 1 );

    snprintf( variable, sizeof( variable ), "%s/line%d", movieName.c_str(),
              lineNumber );
    pMovie->showFrame( "normal", variable, false );

    const int32_t slot = slots[param_2];
    GarageSlot &car = GetGarage()->pCars[slot];
    if ( car.getConfig() != nullptr ) {
        snprintf( variable, sizeof( variable ), "%s/line%d:%s",
                  movieName.c_str(), lineNumber, "name" );
        pMovie->setVariableValue( variable, car.getConfig()->FullName.c_str() );
    }

    char priceText[32];
    FormatMoneyString( priceText,
                       int32_t( gpDatabase->getCarResalePrice(
                           int32_t( car.getCarConfigID() ), car.paintIndex,
                           car.rimID, car.interiorID ) ) );
    snprintf( variable, sizeof( variable ), "%s/line%d:%s", movieName.c_str(),
              lineNumber, "price" );
    pMovie->setVariableValueNonLinked( variable, priceText );

    showLineSubClipFrame(
        param_3, "checkbox",
        ( pOwner != nullptr && pOwner->isTradedIn( slot ) ) ? "on" : "off" );
}
