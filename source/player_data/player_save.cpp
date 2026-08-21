#include "shared.h"
#include "player_save.h"

#include "core/ed_store.h"
#include "core/hash/crc32.h"
#include "core/time.h"
#include "filesystem/gs_file.h"
#include "gs_playerdata.h"
#include "online/gs_online.h"
#include "game/scenaric/game_intro_tutorial.h"

#include "tdu_instance.h"

// Money handed out to a brand new driver.
static constexpr uint32_t kNewGameMoney = 200000u;

int32_t gSaveStreamRevision = 0; // DAT_010e7530
int32_t gSaveStreamTrailer = 0;  // DAT_010e7534

PlayerSave::PlayerSave()
    : IPlayerData(),
      driver(),
      garage(),
      nextCarUniqueID( 0u ),
      achievements(),
      numPendingOperations( 0 ),
      bInitialized( false ),
      saveIndex( -1 ) {
    // FUN_0089ac90
    OTDU_UNIMPLEMENTED_ONCE; // TODO: PLAYERSAVE (stock, eBay, news, team
                             // invites, extra challenges, avatar save, road
                             // discovery, environment and radio tables)
}

PlayerSave::~PlayerSave() {}

void PlayerSave::initialize() {
    // FUN_0089ae80
    // The original also binds the achievement service to this save and attaches
    // an inline observer to the driver (to keep the money HUD in sync).
    OTDU_UNIMPLEMENTED_ONCE; // TODO: PLAYERSAVE

    bInitialized = true;
}

void PlayerSave::terminate() {
    // FUN_00899a30
    if ( !gpTestDriveInstance->hasRequestedExit() && gpPlayerData != nullptr &&
         gpPlayerData->isSaveEnabled() && gpFile != nullptr &&
         gpFile->hasTaskPending() ) {
        numPendingOperations = 0;
        OTDU_UNIMPLEMENTED_ONCE; // TODO: PLAYERSAVE (flush the pending save)
    }

    bInitialized = false;
}

void PlayerSave::tick( const float deltaTime ) {
    // FUN_0089a5b0
    OTDU_UNIMPLEMENTED_ONCE; // TODO: PLAYERSAVE
}

void PlayerSave::newGame( const bool bResetOnly ) {
    // FUN_0089a500
    clear();

    if ( bResetOnly ) {
        OTDU_UNIMPLEMENTED_ONCE; // TODO: PLAYERSAVE (FUN_0089a050)
        return;
    }

    uint32_t driverID = 0u;
    if ( gpPlayerData != nullptr && gpPlayerData->isOnline() ) {
        OTDU_UNIMPLEMENTED_ONCE; // TODO: ONLINE (GSOnline's player id)
    } else {
        const uint32_t timeMs = uint32_t( MonotonicToMs( GetTimeMonotonic() ) );
        driverID = ( timeMs * 0x7ff8a3edu + 0x2aa01d31u ) % 1000000u + 1000000u;
    }

    driver.initialize( ( gpOnline != nullptr ) ? gpOnline->getUsername() : "",
                       driverID, kNewGameMoney, 0u, 0u, 0u, 0u, 0u );

    OTDU_UNIMPLEMENTED_ONCE; // TODO: PLAYERSAVE (garage reset)
}

void PlayerSave::clear() {
    // FUN_0089aa90
    gSaveStreamRevision = 0;
    gSaveStreamTrailer = 0;

    driver.clear();
    garage.clear( true );
    achievements.reset();

    OTDU_UNIMPLEMENTED_ONCE; // TODO: PLAYERSAVE (stock, eBay, news, team
                             // invites, extra challenges, avatar save and road
                             // discovery)
}

bool PlayerSave::deserialize( const void *pSaveData,
                              const uint32_t saveDataSize ) {
    // FUN_0089a730
    if ( pSaveData == nullptr || saveDataSize == 0u ) {
        return false;
    }

    edStore saveStream;
    saveStream.bindExternalBuffer( const_cast<void *>( pSaveData ),
                                   saveDataSize );
    saveStream.seek( 0u );

    saveIndex = saveStream.readInteger();
    gSaveStreamRevision = saveStream.readInteger();

    if ( !saveStream.isWithinBounds() || saveIndex != kPlayerSaveMagic ) {
        return false;
    }

    const uint32_t nicknameHash = uint32_t( saveStream.readInteger() );
    const uint32_t emailHash = uint32_t( saveStream.readInteger() );
    const uint32_t passwordHash = uint32_t( saveStream.readInteger() );

    const LocalPlayerData &localPlayerData = gpPlayerData->getLocalPlayerData();
    if ( nicknameHash != TestDriveCRC32Unfolded( localPlayerData.Nickname ) ||
         emailHash != TestDriveCRC32Unfolded( localPlayerData.Email ) ||
         passwordHash != TestDriveCRC32Unfolded( localPlayerData.Password ) ) {
        return false;
    }

    driver.deserialize( saveStream );
    if ( !driver.isValid() ) {
        return false;
    }

    garage.deserialize( saveStream );
    if ( !garage.isValid() ) {
        return false;
    }

    nextCarUniqueID = 0u;
    for ( int32_t i = 0; i < garage.getNumCars(); i++ ) {
        const uint32_t carID =
            garage.getCarAtIndex( i, 0ull, false )->getCarID();
        if ( nextCarUniqueID < carID ) {
            nextCarUniqueID = carID;
        }
    }
    nextCarUniqueID++;

    OTDU_LOG_DEBUG( "Save loaded: driver '%s' (id %u, level %.1f), %u credits, "
                    "%.1f km, car %u, %d car(s) in garage, next car id %u\n",
                    driver.Nickname, driver.ID, driver.getLevel(),
                    driver.getMoney(), driver.getOdometerKms(),
                    driver.CurrentCar, garage.getNumCars(), nextCarUniqueID );

    OTDU_UNIMPLEMENTED_ONCE; // TODO: PLAYERSAVE
    gSaveStreamTrailer = 0;

    bool bTutorialAdvanced = false;
    if ( driver.TutorialState == kTutorialStateBuyHouse &&
         garage.getNumOwnedHouses() > 0 ) {
        driver.TutorialState = kTutorialStateHouseBought;
        driver.TutorialUserParam = 0;
        bTutorialAdvanced = true;
    } else if ( driver.TutorialState == kTutorialStateBuyCar &&
                garage.getNumCars() > 0 ) {
        driver.TutorialState = kTutorialStateCarBought;
        bTutorialAdvanced = true;
    }

    if ( bTutorialAdvanced ) {
        driver.notify( 0u );
    }

    return true;
}
