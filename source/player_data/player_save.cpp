#include "shared.h"
#include "player_save.h"

#include "core/ed_store.h"
#include "core/hash/crc32.h"
#include "core/time.h"
#include "database/gs_database.h"
#include "filesystem/gs_file.h"
#include "game/game_cheats.h"
#include "game/scenaric/gs_scenaric.h"
#include "game/scenaric/spots.h"
#include "gs_playerdata.h"
#include "online/gs_online.h"
#include "game/gs_tutorial.h"
#include "game/scenaric/game_intro_tutorial.h"

#include "tdu_instance.h"

static constexpr uint32_t kNewGameMoney = 200000u;
static constexpr uint32_t kStarterNumCoupons = 200u; // DAT_00f7f578

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
      saveIndex( -1 ) {
    // FUN_0089ac90
    OTDU_UNIMPLEMENTED_ONCE; // TODO: PLAYERSAVE (eBay state, the classic car
                             // stock at +0x145b8, and the environment and
                             // radio tables)
}

PlayerSave::~PlayerSave() {}

void PlayerSave::initialize() {
    // FUN_0089ae80
    achievements.build();
    driver.subscribe( &extraChallengesNotifier );

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

    driver.unsubscribe( &extraChallengesNotifier );

    bInitialized = false;
}

void PlayerSave::tick( const float deltaTime ) {
    // FUN_0089a5b0
    achievements.tick();
    extraChallenges.tickStatisticsScreen();

    OTDU_UNIMPLEMENTED_ONCE; // TODO: PLAYERSAVE (the original also ticks the
                             // driver, restocks the classic car stock on a
                             // timer and drives the profile prompts)
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
    gSaveStreamRevision = 0;
    gSaveStreamTrailer = 0;

    unStart();
}

void PlayerSave::unStart() {
    // FUN_0089ab80
    driver.clear();
    carReserve.clear();
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
    extraChallenges.clear();

    OTDU_UNIMPLEMENTED_ONCE; // TODO: PLAYERSAVE (eBay state and the classic
                             // car stock)
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
