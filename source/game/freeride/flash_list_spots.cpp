#include "shared.h"
#include "flash_list_spots.h"

FlashListSpots::FlashListSpots()
    : FlashList(), pPlayer( nullptr ), pSelectedSpot( nullptr ) {
    // FUN_0042c500
    levelIndex = 1;
    bIsEmpty = true;
    field16_0x67 = true;
}

FlashListSpots::~FlashListSpots() {}

int32_t FlashListSpots::getListLength() const {
    // FUN_006e6760
    OTDU_UNIMPLEMENTED; // TODO: SCENARIC
    return 0;
}

void FlashListSpots::open() {
    // FUN_006ec700
    OTDU_UNIMPLEMENTED; // TODO: SCENARIC (spot types) / DATABASE
}

void FlashListSpots::close() {
    // FUN_006e80b0
    OTDU_UNIMPLEMENTED; // TODO: SCENARIC (spot activity list)
}

void FlashListSpots::update( const int32_t /*param_2*/,
                             const int32_t /*param_3*/ ) {
    // FUN_006e7f80
    OTDU_UNIMPLEMENTED; // TODO: SCENARIC (spot activity list)
}

void FlashListSpots::selectedLineChanged() {
    // FUN_006e6600
    OTDU_UNIMPLEMENTED; // TODO: SCENARIC (spot activity list)
}
