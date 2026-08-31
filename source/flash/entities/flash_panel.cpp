#include "shared.h"
#include "flash_panel.h"

#include "core/format.h"
#include "database/gs_database.h"
#include "flash/gs_flash.h"
#include "flash/implementation/flash_player.h"
#include "game/vehicle/car_desc.h"
#include "player_data/driver.h"
#include "player_data/gs_playerdata.h"
#include "player_data/player_data_interface.h"
#include "player_data/saved_garage.h"

static bool gbEtradeAvailable =
    false; // DAT_014d5af2 // TODO: ONLINE: MOVE ME TO ONLINE
FlashPanelOnline *gpFlashPanelOnline = nullptr; // DAT_010e7ee8

void FUN_00719350( FlashPlayer *param_1, bool param_2 ) {
    // FUN_00719350
    OTDU_UNIMPLEMENTED;
}

FlashPanel::FlashPanel() : Notifier(), pMovie( nullptr ), pTarget( nullptr ) {}

FlashPanel::~FlashPanel() {
    // FUN_0042e760
    detach();
}

void FlashPanel::detach() {
    if ( pTarget != nullptr ) {
        pTarget->unsubscribe( this );
        pTarget = nullptr;
    }
}

FlashPanelOnline::FlashPanelOnline() : FlashPanel() {
    gpFlashPanelOnline = this;
}

FlashPanelOnline::~FlashPanelOnline() {
    // FUN_0042e800
    gpFlashPanelOnline = nullptr;
}

void FlashPanelOnline::tick() {
    // FUN_00719660
    FUN_00719350( pMovie, false );

    uint32_t onlineState = ( gpPlayerData->isOnline() ? 1u : 0u );
    pMovie->setVariableValue( "/:state_Online", onlineState );

    uint32_t local_4 = 0;
    uint32_t local_8 = 0;
    bool bVar1 = gpFlash->getFlashMovieIndex( &local_8, &local_4, "CONFIGPC" );
    if ( bVar1 ) {
        MoviePlayer *pMVar2 = gpFlash->getMoviePlayer( local_8, local_4 );
        if ( pMVar2 != nullptr && pMVar2->MovieBank.isLoaded() &&
             pMVar2->MovieBank.getFlashPlayer() != nullptr ) {
            FlashPlayer *peVar3 = gpFlash->getFlashPlayer( "CONFIGPC" );
            peVar3->setVariableValue( "/:state_Online", onlineState );
        }
    }

    pMovie->setVariableValue(
        "/:state_Etrade",
        ( gpPlayerData->isOnline() && gbEtradeAvailable ) ? "1" : "0" );
    pMovie->setVariableValue( "/:state_FamilySettings", 1 );
}

FlashPanelMoney::FlashPanelMoney() : FlashPanel() {}

FlashPanelMoney::~FlashPanelMoney() {}

void FlashPanelMoney::tick() {
    // FUN_00719c50
    IPlayerData *pSave = gpPlayerData->getLoadedSave();

    char pMoneyString[32];
    FormatMoneyString( pMoneyString,
                       int32_t( pSave->getDriver()->getMoney() ) );
    pMovie->setVariableValueNonLinked( "/:money", pMoneyString );

    pMovie->setVariableValue( "/:coupon",
                              int32_t( pSave->getDriver()->getNumCoupons() ) );
}

FlashPanelRental::FlashPanelRental() : FlashPanel() {}

FlashPanelRental::~FlashPanelRental() {}

void FlashPanelRental::tick() {
    // FUN_00719320
    pMovie->setVariableValue( "/:state_Renting",
                              gpPlayerData->isRentingCar() ? 1u : 0u );
}

FlashPanelInfoCurCar::FlashPanelInfoCurCar() : FlashPanel(), pCarName{} {}
FlashPanelInfoCurCar::~FlashPanelInfoCurCar() {}

void FlashPanelInfoCurCar::tick() {
    // FUN_007199e0
    IPlayerData *pSave = gpPlayerData->getLoadedSave();

    Garage *pGarage = pSave->getGarage();
    if ( pGarage->getNumCars() < 1 ) {
        GarageSlot &rentalSlot = pGarage->pCars[Garage::kNumSavedCarSlots];
        if ( rentalSlot.state != CarDesc::eState::CDS_Rental ) {
            pMovie->setVariableValueNonLinked( "/:player_car", "" );
            return;
        }
    }

    const uint16_t currentCar = pSave->getDriver()->CurrentCar;
    GarageSlot &car = pSave->getGarage()->pCars[currentCar];
    if ( !car.isValid() ) {
        return;
    }

    DBEntryCarPhysicsData *pCarEntry =
        gpDatabase->getCarDBEntry( int32_t( car.getCarConfigID() ) );
    if ( pCarEntry == nullptr ) {
        return;
    }

    pMovie->setVariableValue( "/:player_car",
                              car.getConfig()->FullName.c_str() );
    pMovie->setVariableValueNonLinked( "/:player_group",
                                       pCarEntry->CarGroup.pAsString );
    pMovie->setVariableValue( "/:player_cur_upgrade", car.getPackLevel() );
}
