#include "shared.h"
#include "player_save.h"

#include "core/ed_store.h"
#include "core/hash/crc32.h"
#include "core/mutex.h"
#include "core/time.h"
#include "database/gs_database.h"
#include "filesystem/gs_file.h"
#include "flash/gs_flash.h"
#include "flash/audio/flash_sounds.h"
#include "game/game_cheats.h"
#include "game/scenaric/gs_scenaric.h"
#include "game/scenaric/spots.h"
#include "gs_playerdata.h"
#include "user_profile.h"
#include "save_game_manager.h"
#include "flash/modal_message_box.h"
#include "online/gs_online.h"
#include "game/gs_tutorial.h"
#include "game/scenaric/game_intro_tutorial.h"

#include "tdu_instance.h"

static constexpr uint32_t kNewGameMoney = 200000u;
static constexpr uint32_t kStarterNumCoupons = 200u; // DAT_00f7f578
static constexpr uint64_t kProfileWarningHashcode = 0x3a1f97aull;

int32_t gSaveStreamRevision = 0; // DAT_010e7530
int32_t gSaveStreamTrailer = 0;  // DAT_010e7534

PlayerSave::PlayerSave()
    : IPlayerData(),
      driver(),
      carReserve(),
      garage(),
      ranking(),
      mailbox(),
      newsList(),
      nextCarUniqueID( 0u ),
      itemTable(),
      avatar(),
      roadDiscovery(),
      stringTable(),
      achievements(),
      extraChallenges(),
      extraChallengesNotifier( &extraChallenges ),
      numPendingOperations( 0 ),
      bInitialized( false ),
      bProfilePromptPending( false ),
      saveIndex( -1 ) {
    // FUN_0089ac90
    OTDU_UNIMPLEMENTED_ONCE; // TODO: EBAY (eBay list and car) / ONLINE (radio
                             // and advertising resource tables)
}

PlayerSave::~PlayerSave() {}

void PlayerSave::initialize() {
    // FUN_0089ae80
    achievements.build();
    driver.subscribe( &extraChallengesNotifier );

    clear();

    bInitialized = true;
}

void PlayerSave::terminate() {
    // FUN_00899a30
    if ( !gpTestDriveInstance->hasRequestedExit() && gpPlayerData != nullptr &&
         gpPlayerData->isSaveEnabled() && gbSaveAllowed ) {
        gpPlayerData->flushSaveRequest();
    }

    achievements.releaseChecks();
    driver.unsubscribe( &extraChallengesNotifier );

    bInitialized = false;
}

void PlayerSave::tick( const float deltaTime ) {
    // FUN_0089a5b0
    driver.tickMoneyOperation( deltaTime );
    carReserve.tick();
    achievements.tick();
    extraChallenges.tickStatisticsScreen();

    if ( gUserProfileRegister.FUN_00630320() ) {
        return;
    }

    if ( bProfilePromptPending &&
         gUserProfileRegister.getUnknownDword() == 0 ) {
        TestDrive::DisplayModalPrompt(
            false, kProfileWarningHashcode, 0, nullptr, nullptr, nullptr, 0,
            nullptr, true, false, MPT_Warning, gpFlashMessageBox );
    }
    bProfilePromptPending = false;

    if ( numPendingOperations == 0 || gUserProfileRegister.FUN_00630320() ) {
        return;
    }

    const uint32_t profileState = gUserProfileRegister.getUnknownDword();
    if ( profileState == 0 || profileState == 6 ) {
        gpPlayerData->setSaveEnabled( false );
        gUserProfileRegister.setUnknownDword3( 1 );
        gUserProfileRegister.setUnknownDword( 6 );
        gUserProfileRegister.setUnknownDword3( 0 );
        bProfilePromptPending = true;
    } else if ( numPendingOperations == 2 ) {
        gSavegameManager.requestSave();
    }

    numPendingOperations = 0;
}

uint32_t PlayerSave::allocateCarUniqueID() {
    // FUN_00415d20
    return nextCarUniqueID++;
}

void PlayerSave::newGameWithCheats() {
    // FUN_0089a050
    nextCarUniqueID = 1u;

    uint32_t driverID = 0u;
    if ( gpPlayerData != nullptr && gpPlayerData->isOnline() ) {
        OTDU_UNIMPLEMENTED_ONCE; // TODO: ONLINE
    } else {
        const uint32_t timeMs = uint32_t( MonotonicToMs( GetTimeMonotonic() ) );
        driverID = ( timeMs * 0x7ff8a3edu + 0x2aa01d31u ) % 1000000u + 1000000u;
    }

    const uint32_t money =
        gbScenarioTutorialEnabled ? uint32_t( gDefaultMoney ) : kNewGameMoney;
    const uint32_t numCoupons =
        gbScenarioTutorialEnabled ? kStarterNumCoupons : 0u;

    driver.initialize( ( gpOnline != nullptr ) ? gpOnline->getUsername() : "",
                       driverID, money, numCoupons, 0u, 0u, 0u, 0u );

    installCheatCars();

    if ( !gbScenarioTutorialEnabled ) {
        if ( garage.isHouseOwned( 0ull ) ) {
            driver.setCurrentHouse( 0ull );
        }
        driver.notify( 1u );
    }

    garage.refreshNumCars();

    if ( garage.getNumCars() < 1 && gbScenarioTutorialEnabled &&
         !gDefaultGarageConfig.empty() ) {
        garage.pCars[0].setup( uint32_t( gDefaultGarageConfig[0] ), -1, -1, -1,
                               allocateCarUniqueID() );
        garage.refreshNumCars();
    }

    garage.notify( 0u );

    if ( gUnlockAllRoads ) {
        roadDiscovery.unlockAllRoads();
    }
}

bool PlayerSave::installCheatCars() {
    if ( gpDatabase != nullptr && gpScenaric != nullptr ) {
        const GameDatabase &gameDB = gpDatabase->getGameDB();
        const uint32_t numHouses =
            gameDB.pNumItems[Garage::kHouseDatabaseIndex];
        Spots &spots = gpScenaric->getSpotList();
        const int32_t numSpots = int32_t( spots.getNumWorldSpots() );

        for ( uint32_t i = 0; i < numHouses; i++ ) {
            const DBEntryHouse &houseEntry = gameDB.DBHouse[i];
            const char *pSpotName = houseEntry.SpotName.pAsString;
            if ( pSpotName == nullptr ) {
                continue;
            }

            if ( !gbAllHousesWithDefCar &&
                 strcmp( pSpotName, gDefaultHouse.c_str() ) != 0 ) {
                continue;
            }

            const uint64_t houseHashcode = GetIdentifier64bit( pSpotName );
            bool bHasSpot = false;
            for ( int32_t spotIndex = 0; spotIndex < numSpots; spotIndex++ ) {
                if ( spots.getWorldSpot( spotIndex )->Hashcode ==
                     houseHashcode ) {
                    bHasSpot = true;
                    break;
                }
            }

            if ( !bHasSpot ||
                 ( uint32_t( houseHashcode ) &
                   uint32_t( houseHashcode >> 32ull ) ) == 0xffffffffu ) {
                continue;
            }

            grantStarterHouse( houseHashcode );
        }

        if ( numSpots == 0 ) {
            return false;
        }
    }

    garage.refreshNumCars();
    garage.notify( 0u );

    return true;
}

void PlayerSave::grantStarterHouse( const uint64_t houseHashcode ) {
    // FUN_0089a050 (inlined)
    garage.addHouse( houseHashcode );
    achievements.notify( ACF_Challenge | ACF_Road | ACF_Collection, false );

    if ( !gDefaultGarageConfig.empty() && gDefaultGarageConfig[0] != -1 ) {
        const int32_t numSlots = GetHouseNumGarageSlots( houseHashcode );

        for ( int32_t slot = 0; slot < int32_t( gDefaultGarageConfig.size() ) &&
                                gDefaultGarageConfig[slot] != -1;
              slot++ ) {
            if ( slot >= numSlots ) {
                break;
            }

            const int32_t carConfigID = gDefaultGarageConfig[slot];

            const int32_t paintIndex =
                ( gpDatabase->getNumColors( carConfigID ) > 0 ) ? 0 : -1;
            const int32_t rimID =
                ( gpDatabase->getNumRims( carConfigID ) > 0 ) ? 0 : -1;

            int32_t interiorID = -1;
            DBEntryCarColor *pColor =
                gpDatabase->getColorByIndex( carConfigID, paintIndex );
            if ( pColor != nullptr &&
                 gpDatabase->getInteriorByIndex( pColor, 0 ) != nullptr ) {
                interiorID = 0;
            }

            GarageSlot *pSlot =
                garage.getCarAtIndex( slot, houseHashcode, true );
            if ( pSlot != nullptr ) {
                pSlot->setup( uint32_t( carConfigID ), paintIndex, rimID,
                              interiorID, allocateCarUniqueID() );
            }
        }
    }

    if ( garage.isHouseOwned( houseHashcode ) ) {
        driver.setCurrentHouse( houseHashcode );
    }

    driver.notify( 1u );
}

void PlayerSave::newGame( const bool bInstallCheats ) {
    // FUN_0089a500
    clear();

    if ( bInstallCheats ) {
        newGameWithCheats();
        return;
    }

    nextCarUniqueID = 1u;

    uint32_t driverID = 0u;
    if ( gpPlayerData != nullptr && gpPlayerData->isOnline() ) {
        OTDU_UNIMPLEMENTED_ONCE; // TODO: ONLINE (GSOnline's player id)
    } else {
        const uint32_t timeMs = uint32_t( MonotonicToMs( GetTimeMonotonic() ) );
        driverID = ( timeMs * 0x7ff8a3edu + 0x2aa01d31u ) % 1000000u + 1000000u;
    }

    driver.initialize( ( gpOnline != nullptr ) ? gpOnline->getUsername() : "",
                       driverID, kNewGameMoney, 0u, 0u, 0u, 0u, 0u );

    garage.refreshNumCars();
}

void PlayerSave::setCurrentCar( const uint16_t index ) {
    // FUN_00899200
    driver.CurrentCar = index;
    driver.notify( 0u );
}

void PlayerSave::buyCar( const uint32_t carConfigID,
                         const int32_t slot,
                         const int32_t paintIndex,
                         const int32_t rimID,
                         const int32_t interiorID ) {
    // FUN_00899e00
    const int32_t carSlot =
        ( slot < 0 || slot > Garage::kNumSavedCarSlots ) ? 0 : slot;
    GarageSlot &car = garage.pCars[carSlot];
    if ( car.getHouseHashcode() != 0xffffffffull ) {
        ScopedMutexLock lock( gpPlayerData->getMutex() );

        car.setup( carConfigID, paintIndex, rimID, interiorID,
                   allocateCarUniqueID() );

        const bool bBike =
            gpDatabase->isBike( gpDatabase->getCarDBEntry( carConfigID ) );
        const int32_t price = int32_t( gpDatabase->getCarPrice(
            carConfigID, paintIndex, rimID, interiorID ) );
        extraChallenges.addCounter( CounterIndex( 0xf0c0 ) + ( bBike ? 1 : 0 ),
                                    price );
        driver.setPendingMoneyOperation( driver.getPendingMoneyOperation() -
                                         price );

        gpFlash->getFlashSound()->stopGroup( "money", 0.0f );
        gpFlash->getFlashSound()->playFrontEndSound( "money_up:money", 1,
                                                     false );
        driver.ApplyMoneyOPPendingTimeLeft = 1.0f;

        garage.refreshNumCars();
        achievements.addRewardCarBought( carConfigID );
        extraChallenges.countCarTransaction( carConfigID, true, false );

        if ( achievements.getField516_0x204() && achievements.isReady() ) {
            achievements.setCurrentGameMode(
                gpTestDriveInstance->getActiveGameMode() );
        }
        achievements.notify( ACF_Challenge | ACF_Road | ACF_Collection, false );

        // FUN_00898770 (inlined)
        gpPlayerData->requestSave( 2 );
    }

    garage.notify( 1u );
}

void PlayerSave::sellCar( const uint32_t carID ) {
    // FUN_00899660
    int32_t carIndex = 0;
    for ( ; carIndex < Garage::kNumSavedCarSlots; carIndex++ ) {
        if ( garage.getCarAtIndex( carIndex, 0ull, false )->getCarID() ==
             carID ) {
            break;
        }
    }
    if ( carIndex == Garage::kNumSavedCarSlots ) {
        return;
    }

    {
        ScopedMutexLock lock( gpPlayerData->getMutex() );

        GarageSlot *pCar = garage.getCarAtIndex( carIndex, 0ull, false );
        const float resalePrice = gpDatabase->getCarResalePrice(
            int32_t( pCar->getCarConfigID() ), pCar->paintIndex, pCar->rimID,
            pCar->interiorID );

        // NOTE: Retail looks the database entry up with the car unique ID
        // instead of its config ID, so the bike split of the sales counter
        // only works by accident.
        const bool bBike =
            gpDatabase->isBike( gpDatabase->getCarDBEntry( carID ) );
        const int32_t resale = int32_t( resalePrice );
        extraChallenges.addCounter( CounterIndex( 0xf07c ) + ( bBike ? 1 : 0 ),
                                    resale );
        driver.setPendingMoneyOperation( driver.getPendingMoneyOperation() +
                                         resale );

        gpFlash->getFlashSound()->stopGroup( "money", 0.0f );
        gpFlash->getFlashSound()->playFrontEndSound( "money_up:money", 1,
                                                     false );
        driver.ApplyMoneyOPPendingTimeLeft = 1.0f;

        // FUN_00888d70 (inlined)
        DBEntryCarPhysicsData *pCarEntry =
            gpDatabase->getCarDBEntry( pCar->getCarConfigID() );
        if ( pCarEntry != nullptr ) {
            extraChallenges.addCounter(
                CounterIndex( gpDatabase->isBike( pCarEntry ) ? 0xf054
                                                              : 0xf050 ),
                1 );
        }

        garage.getCarAtIndex( carIndex, 0ull, false )->CarDesc::clear();
        garage.refreshNumCars();

        // FUN_00898770 (inlined)
        gpPlayerData->requestSave( 2 );
    }

    garage.notify( 2u );
}

void PlayerSave::upgradeCar( DBEntryCarPack *pCarPack, const int32_t level ) {
    // FUN_00899ad0
    DBEntryCarPhysicsData *pNewCar = nullptr;
    DBEntryAftermarketPack *pPack = nullptr;
    int32_t price = 0;
    if ( level >= 1 && level <= 3 ) {
        const CarUpgrade &upgrade = pCarPack->Upgrades[level - 1];
        pNewCar =
            reinterpret_cast<DBEntryCarPhysicsData *>( upgrade.CarHashcode );
        if ( gpDatabase->isTraffic( pNewCar ) ) {
            pPack = reinterpret_cast<DBEntryAftermarketPack *>(
                upgrade.PackHashcode );
            pNewCar = nullptr;
        }
        price = upgrade.Price;
    }

    GarageSlot &car = garage.pCars[driver.CurrentCar];
    if ( pNewCar == nullptr ) {
        if ( pPack != nullptr &&
             strcmp( reinterpret_cast<const char *>( pPack->PackID ),
                     "None" ) != 0 ) {
            car.setPack( pPack );
        }
    } else {
        car.setConfigFromID( uint32_t( pNewCar->CarID ) );
        car.packHashcode = 0xffffffffffffffffull;
    }

    const bool bBike =
        gpDatabase->isBike( gpDatabase->getCarDBEntry( car.getCarConfigID() ) );
    extraChallenges.addCounter( CounterIndex( 0xf0c0 ) + 8 + ( bBike ? 1 : 0 ),
                                price );
    driver.setPendingMoneyOperation( driver.getPendingMoneyOperation() -
                                     price );
    extraChallenges.countCarTuned( bBike );

    gpFlash->getFlashSound()->stopGroup( "money", 0.0f );
    gpFlash->getFlashSound()->playFrontEndSound( "money_up:money", 1, false );
    driver.ApplyMoneyOPPendingTimeLeft = 1.0f;

    if ( achievements.getField516_0x204() && achievements.isReady() ) {
        achievements.setCurrentGameMode(
            gpTestDriveInstance->getActiveGameMode() );
    }
    achievements.addCarTuned();
    achievements.notify( ACF_Challenge | ACF_Road | ACF_Collection, false );

    // FUN_00898770 (inlined)
    gpPlayerData->requestSave( 2 );

    garage.notify( 6u );
}

void PlayerSave::swapCars( const int32_t indexA,
                           const uint64_t houseHashcodeA,
                           const int32_t indexB,
                           const uint64_t houseHashcodeB ) {
    // FUN_00899330
    const CarDesc carA(
        *garage.getCarAtIndex( indexA, houseHashcodeA, false ) );

    CarDesc *pCarA = garage.getCarAtIndex( indexA, houseHashcodeA, false );
    *pCarA = *garage.getCarAtIndex( indexB, houseHashcodeB, false );

    CarDesc *pCarB = garage.getCarAtIndex( indexB, houseHashcodeB, false );
    *pCarB = carA;

    garage.notify( 4u );
}

void PlayerSave::clear() {
    // FUN_0089aa90
    ScopedMutexLock lock( gpPlayerData->getMutex() );

    gSaveStreamRevision = 0;
    gSaveStreamTrailer = 0;

    driver.clear();
    garage.clear( true );
    OTDU_UNIMPLEMENTED_ONCE; // TODO: EBAY (FUN_008943f0)
    ranking.clear();
    mailbox.clear();
    newsList.clear();
    itemTable.clear();
    avatar.clear();
    carReserve.reset();
    roadDiscovery.clear();
    achievements.reset();
    extraChallenges.clear( true );

    if ( gpTutorial != nullptr ) {
        gpTutorial->resetProgress();
    }
}

void PlayerSave::unStart() {
    // FUN_0089ab80
    driver.clear();
    carReserve.reset();
    garage.clear( true );
    ranking.clear();
    mailbox.clear();
    newsList.clear();
    nextCarUniqueID = 0u;
    itemTable.clear();
    avatar.clear();
    roadDiscovery.clear();
    stringTable.clear();
    achievements.reset();
    extraChallenges.clear( true );

    OTDU_UNIMPLEMENTED_ONCE; // TODO: EBAY (FUN_008943f0 and the item table's
                             // unmapped tail at +0x274..+0x2a0)
}

void PlayerSave::serialize( edStore &stream ) {
    // FUN_00899420 (inlined)
    stream.writeInteger( kPlayerSaveMagic );
    stream.writeInteger( gSaveStreamRevision );

    const LocalPlayerData &localPlayerData = gpPlayerData->getLocalPlayerData();
    stream.writeInteger(
        int32_t( TestDriveCRC32Unfolded( localPlayerData.Nickname ) ) );
    stream.writeInteger(
        int32_t( TestDriveCRC32Unfolded( localPlayerData.Email ) ) );
    stream.writeInteger(
        int32_t( TestDriveCRC32Unfolded( localPlayerData.Password ) ) );

    driver.serialize( stream, uint16_t( garage.getCurrentCar() ) );
    garage.serialize( stream );

    ranking.serialize( stream );
    mailbox.serialize( stream );
    newsList.serialize( stream );
    avatar.serialize( stream );
    itemTable.serialize( stream );
    roadDiscovery.serialize( stream );
    stringTable.serialize( stream );
    carReserve.serialize( stream );
    achievements.serialize( stream );
    extraChallenges.serialize( stream );

    OTDU_ASSERT( gpTutorial != nullptr );
    if ( gpTutorial != nullptr ) {
        gpTutorial->serialize( stream );
    }

    stream.writeInteger( gSaveStreamTrailer );
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

    ranking.deserialize( saveStream );
    mailbox.deserialize( saveStream );
    newsList.deserialize( saveStream );

    const time_t nowSeconds = time( nullptr );
    const struct tm *pNow = localtime( &nowSeconds );
    if ( pNow != nullptr ) {
        newsList.removeExpiredEntries( PackSaveDate(
            uint32_t( pNow->tm_year + 1900 ), uint32_t( pNow->tm_mon + 1 ),
            uint32_t( pNow->tm_mday ) ) );
    }

    avatar.deserialize( saveStream );
    itemTable.deserialize( saveStream );

    roadDiscovery.deserialize( saveStream );
    if ( gUnlockAllRoads ) {
        roadDiscovery.unlockAllRoads();
    }

    stringTable.deserialize( saveStream );
    carReserve.deserialize( saveStream );
    achievements.deserialize( saveStream );
    extraChallenges.deserialize( saveStream );

    OTDU_ASSERT( gpTutorial != nullptr );
    if ( gpTutorial != nullptr ) {
        gpTutorial->deserialize( saveStream );
    }

    gSaveStreamTrailer =
        saveStream.isWithinBounds() ? int32_t( saveStream.readULong() ) : 0;

    const uint32_t bytesLeft = saveDataSize - saveStream.getReadCursor();
    if ( bytesLeft >= 8u ) {
        OTDU_LOG_WARN( "'playersave' was not fully consumed: stopped at %u of "
                       "%u (%u bytes left)\n",
                       saveStream.getReadCursor(), saveDataSize, bytesLeft );
    } else {
        OTDU_LOG_DEBUG( "'playersave' fully consumed: %u of %u bytes (%u byte "
                        "of TEA padding)\n",
                        saveStream.getReadCursor(), saveDataSize, bytesLeft );
    }

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
