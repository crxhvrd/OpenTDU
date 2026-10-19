#include "shared.h"
#include "flash_panel.h"

#include "config/gs_config.h"
#include "core/format.h"
#include "database/gs_database.h"
#include "filesystem/gs_file.h"
#include "flash/mng_flash_localize.h"
#include "flash/gs_flash.h"
#include "flash/implementation/flash_player.h"
#include "game/mng_logo_display.h"
#include "game/vehicle/car_desc.h"
#include "player_data/driver.h"
#include "player_data/gs_playerdata.h"
#include "player_data/player_data_interface.h"
#include "player_data/saved_ebay_state.h"
#include "player_data/saved_garage.h"
#include "player_data/saved_mailbox.h"

static bool gbEtradeAvailable =
    false; // DAT_014d5af2 // TODO: ONLINE: MOVE ME TO ONLINE
FlashPanelOnline *gpFlashPanelOnline = nullptr; // DAT_010e7ee8

static char gFreeMessage[0x80] = "ERROR"; // s_ERROR_00fab990

void FUN_00719350( FlashPlayer *param_1, bool param_2 ) {
    // FUN_00719350
    if ( gpMngFlashLocalize == nullptr ) {
        return;
    }

    const uint32_t messageHashcode =
        gpPlayerData->isOnline() ? 0x575da4u : 0x58e444u;
    const char *pMessage =
        gpDatabase->getStringByHashcode( 11, messageHashcode );

    int32_t numPlayers = 0;
    if ( param_2 ) {
        OTDU_UNIMPLEMENTED; // TODO: ONLINE (free ride player count)
    }

    FlashPlayer *pConfigPlayer = gpFlash->getFlashPlayer( "CONFIGPC" );
    // TODO: ONLINE: also requires a valid GPS tracker IP address
    if ( numPlayers < 1 ) {
        if ( pMessage == nullptr ) {
            param_1->setVariableValueNonLinked( "/:FreeMessage", " " );
            if ( pConfigPlayer != nullptr ) {
                pConfigPlayer->setVariableValueNonLinked( "/:FreeMessage",
                                                          " " );
            }
        } else {
            param_1->setVariableValue( "/:FreeMessage", pMessage );
            if ( pConfigPlayer != nullptr ) {
                pConfigPlayer->setVariableValue( "/:FreeMessage", pMessage );
            }
        }
        return;
    }

    const char *pPlayersLabel = gpDatabase->getStringByHashcode( 0, 0x3c5d142 );
    if ( pMessage != nullptr && pPlayersLabel != nullptr ) {
        snprintf( gFreeMessage, sizeof( gFreeMessage ), "%s - %u %s", pMessage,
                  numPlayers, pPlayersLabel );
        gFreeMessage[sizeof( gFreeMessage ) - 1] = '\0';
    }
    param_1->setVariableValue( "/:FreeMessage", gFreeMessage );
    if ( pConfigPlayer != nullptr ) {
        pConfigPlayer->setVariableValue( "/:FreeMessage", gFreeMessage );
    }
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

FlashPanelNews::FlashPanelNews() : FlashPanel() {}

FlashPanelNews::~FlashPanelNews() {}

void FlashPanelNews::tick() {
    // FUN_00719cd0
    if ( gpMngFlashLocalize == nullptr ) {
        return;
    }

    SavedMailbox *pMailbox = gpPlayerData->getLoadedSave()->getMailbox();
    const uint64_t helpHashcode =
        ( pMailbox->getNumVisibleMessages() < 1 ) ? 0x3723302ull : 0x362f0c2ull;
    gpMngFlashLocalize->setVarLocalized( pMovie, "v_NewsHelp", helpHashcode );

    pMovie->setVariableValue( "/:nb_news", pMailbox->getNumVisibleMessages() );
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

FlashPanelCarSpecs::FlashPanelCarSpecs()
    : FlashPanel(),
      pCar( nullptr ),
      pUpgradeEntry( nullptr ),
      bPictureInstalled( false ) {}

FlashPanelCarSpecs::~FlashPanelCarSpecs() {
    // FUN_007195a0
    if ( bPictureInstalled ) {
        OTDU_UNIMPLEMENTED; // TODO: FLASH (car picture render target release)
    }
}

void FlashPanelCarSpecs::tick() {
    // FUN_0071b120
    refresh( pCar, false );
}

static constexpr float kKmToMiles = 0.621504f;         // DAT_00f418c0
static constexpr float kNmToLbFt = 0.737463f;          // DAT_00f42190
static constexpr double kKgToLbs = 2.2046226218487757; // DAT_00f42188
static constexpr float kRatioToPercent = 0.01f;        // DAT_00f416d4
static constexpr float kRatioBase = 100.0f;            // DAT_00f41708
static constexpr float kAccelBarOrigin = 9.0f;         // DAT_00f4192c
static constexpr float kAccelBarScale = -14.814815f;   // DAT_00f42184
static constexpr float kSpeedBarOrigin = 190.0f;       // DAT_00f41f8c
static constexpr float kSpeedBarScale = 0.4f;          // DAT_00f4175c
static constexpr float kBrakingBarOrigin = 170.0f;     // DAT_00f42180
static constexpr float kBrakingBarScale = -1.3513514f; // DAT_00f4217c
static constexpr uint64_t kNoWindowsTypeHashcode = 0x4a1475b;

static constexpr bool kIntegrityCheckPassed =
    true; // Toggled (in the original pcode) if Securom tampering has been
          // detected

static inline const char *GetUnit( const uint64_t hashcode ) {
    // FUN_009af890
    return gpDatabase->getStringByHashcode( 0, hashcode );
}

static inline bool HasWindows( const DBEntryCarPhysicsData *pEntry ) {
    // FUN_0042ce80
    return gpDatabase->getGameDB().Databases[1].getHashcodeForPointer(
               pEntry->WindowsType.pAsRawPointer ) != kNoWindowsTypeHashcode;
}

static inline int32_t ClampBar( const int32_t value ) {
    return Clamp( value, 0, 100 );
}

void FlashPanelCarSpecs::refresh( CarDesc *pDisplayedCar,
                                  const bool bUpgrade ) {
    // FUN_00719d50
    if ( gpMngFlashLocalize == nullptr || pMovie == nullptr ||
         pDisplayedCar == nullptr ) {
        return;
    }

    const int32_t configID = int32_t( pDisplayedCar->getCarConfigID() );
    const int32_t rimID = pDisplayedCar->rimID;
    const bool bDefaultCar = configID == CarDesc::kDefaultCarConfigID ||
                             configID == CarDesc::kDefaultBikeConfigID;

    DBEntryCarPhysicsData *pCarEntry = gpDatabase->getCarDBEntry( configID );
    const bool bValid = pCarEntry != nullptr && IsCarEntryValid( pCarEntry ) &&
                        gpDatabase->carBankExists( pCarEntry );
    if ( !bValid ) {
        if ( pUpgradeEntry != nullptr ) {
            OTDU_UNIMPLEMENTED; // TODO: ONLINE (marketplace entry fallback)
        }
        return;
    }

    auto SelectVar = [bUpgrade]( const char *pUpgradeVar, const char *pVar ) {
        return bUpgrade ? pUpgradeVar : pVar;
    };

    pMovie->setVariableValue(
        SelectVar( "/:newCarName", "/:Current_car" ),
        gpDatabase->getCarConfig( configID )->FullName.c_str() );
    pMovie->setVariableValueNonLinked(
        SelectVar( "/:cur_group_upg", "/:cur_group" ),
        pCarEntry->CarGroup.pAsString );

    char value[32];
    const float odometerKms = pDisplayedCar->odometerKms;
    const float odometerMiles = odometerKms * kKmToMiles;
    snprintf( value, sizeof( value ), "%08d", int32_t( odometerMiles ) );
    pMovie->setVariableValueNonLinked( "/:Km_i", value );
    snprintf( value, sizeof( value ), "%01d",
              int32_t( ( odometerMiles - float( int32_t( odometerMiles ) ) ) *
                       10.0f ) );
    pMovie->setVariableValueNonLinked( "/:Centaine_i", value );
    snprintf( value, sizeof( value ), "%08d", int32_t( odometerKms ) );
    pMovie->setVariableValueNonLinked( "/:Km_m", value );
    snprintf(
        value, sizeof( value ), "%01d",
        int32_t( ( odometerKms - float( int32_t( odometerKms ) ) ) * 10.0f ) );
    pMovie->setVariableValueNonLinked( "/:Centaine_m", value );

    const DBEntryAftermarketPack *pPack =
        gpDatabase->getAftermarketPack( pDisplayedCar->packHashcode );

    const DBEntryCarPack *pCarPack = nullptr;
    int32_t upgradeLevel = -1;
    if ( pPack != nullptr ) {
        const GameDatabase &gameDB = gpDatabase->getGameDB();
        for ( uint32_t i = 0; i < gameDB.pNumItems[0xd]; i++ ) {
            const DBEntryCarPack &carPack = gameDB.DBCarPack[i];
            if ( reinterpret_cast<DBEntryCarPhysicsData *>(
                     carPack.CarHashcode ) != pCarEntry ) {
                continue;
            }

            pCarPack = &carPack;
            for ( int32_t level = 0; level < 3; level++ ) {
                if ( reinterpret_cast<const DBEntryAftermarketPack *>(
                         carPack.Upgrades[level].PackHashcode ) == pPack ) {
                    upgradeLevel = level + 1;
                    break;
                }
            }
            if ( upgradeLevel != -1 ) {
                break;
            }
        }
    }
    const CarUpgrade *pUpgrade = ( pCarPack != nullptr && upgradeLevel != -1 )
                                     ? &pCarPack->Upgrades[upgradeLevel - 1]
                                     : nullptr;

    if ( !bUpgrade ) {
        pMovie->setVariableValue( "/:carBrand",
                                  pCarEntry->Brand->BrandNameID.pAsString );
        pMovie->setVariableValue( "/:cur_upgrade",
                                  pDisplayedCar->getPackLevel() );
        pMovie->setVariableValueNonLinked(
            "/:state_Vehicle",
            gpDatabase->isBike( pCarEntry ) ? "bike" : "car" );
        pMovie->setVariableValue( "/:IsWindows",
                                  HasWindows( pCarEntry ) ? 1 : 0 );
        pMovie->setVariableValue( "/:IsDoors",
                                  int32_t( ( pCarEntry->BitField >> 3 ) & 1 ) );

        if ( gpMngLogoDisplay != nullptr ) {
            gpMngLogoDisplay->setLogo(
                pMovie, pCarEntry->Brand->BrandID.pAsString, nullptr );
        }

        uint32_t price = uint32_t( pCarEntry->Price );
        if ( !kIntegrityCheckPassed && price > 99999 ) {
            price *= 10;
        }

        char priceString[32];
        if ( float( price ) <= 0.0f ) {
            strcpy( priceString, "--" );
        } else {
            FormatMoneyString( priceString, int32_t( price ) );
        }
        pMovie->setVariableValueNonLinked( "/:CurrentPrice", priceString );

        pMovie->setVariableValue( "/:cur_category",
                                  pCarEntry->Category.pAsString );
        pMovie->setVariableValue( "/:cur_engine",
                                  pCarEntry->EngineType.pAsString );
        pMovie->setVariableValueVariadic( "/:cur_torque_m", "%d %s",
                                          pCarEntry->TorqueNm,
                                          GetUnit( 0x6046d3 ) );
        pMovie->setVariableValueVariadic(
            "/:cur_torque_i", "%d %s",
            int32_t( float( pCarEntry->TorqueNm ) * kNmToLbFt ),
            GetUnit( 0x576d33 ) );
        pMovie->setVariableValueVariadic(
            "/:cur_bhp_t_m", "%.3f %s/%s",
            double( pCarEntry->Weight ) / double( pCarEntry->BHP ),
            GetUnit( 0x574623 ), GetUnit( 0x52b243 ) );
        pMovie->setVariableValueVariadic(
            "/:cur_bhp_t_i", "%.3f %s/%s",
            ( double( pCarEntry->Weight ) * kKgToLbs ) /
                double( pCarEntry->BHP ),
            GetUnit( 0x5d87b3 ), GetUnit( 0x58f3d3 ) );
    }

    if ( pPack == nullptr ) {
        pMovie->setVariableValueVariadic(
            SelectVar( "/:cur_weight_m_upg", "/:cur_weight_m" ), "%d %s",
            pCarEntry->Weight, GetUnit( 0x574623 ) );
        pMovie->setVariableValueVariadic(
            SelectVar( "/:cur_weight_i_upg", "/:cur_weight_i" ), "%d %s",
            uint32_t( double( pCarEntry->Weight ) * kKgToLbs ),
            GetUnit( 0x5d87b3 ) );
        pMovie->setVariableValueVariadic(
            SelectVar( "/:cur_power_m_upg", "/:cur_power_m" ), "%d %s",
            pCarEntry->BHP, GetUnit( 0x58f3d3 ) );
        pMovie->setVariableValueVariadic(
            SelectVar( "/:cur_power_i_upg", "/:cur_power_i" ), "%d %s",
            pCarEntry->BHP, GetUnit( 0x58f3d3 ) );
    } else {
        const float weight = ( pPack->WeightRatio + kRatioBase ) *
                             float( pCarEntry->Weight ) * kRatioToPercent;
        const int32_t power =
            int32_t( ( pPack->BHPRatio + kRatioBase ) *
                     float( pCarEntry->BHP ) * kRatioToPercent );

        pMovie->setVariableValueVariadic(
            SelectVar( "/:cur_weight_m_upg", "/:cur_weight_m" ), "%d %s",
            int32_t( weight ), GetUnit( 0x574623 ) );
        pMovie->setVariableValueVariadic(
            SelectVar( "/:cur_weight_i_upg", "/:cur_weight_i" ), "%d %s",
            uint32_t( double( weight ) * kKgToLbs ), GetUnit( 0x5d87b3 ) );
        pMovie->setVariableValueVariadic(
            SelectVar( "/:cur_power_m_upg", "/:cur_power_m" ), "%d %s", power,
            GetUnit( 0x58f3d3 ) );
        pMovie->setVariableValueVariadic(
            SelectVar( "/:cur_power_i_upg", "/:cur_power_i" ), "%d %s", power,
            GetUnit( 0x58f3d3 ) );
    }

    const float *pAccel0100 = nullptr;
    if ( pCarPack == nullptr ) {
        pAccel0100 = &pCarEntry->AccelerationKMH;
    } else if ( pUpgrade != nullptr ) {
        pAccel0100 = &pUpgrade->Acceleration;
    }
    if ( pAccel0100 != nullptr ) {
        pMovie->setVariableValueVariadic(
            SelectVar( "/:cur_0_100_m_upg", "/:cur_0_100_m" ), "%.1f %s",
            double( *pAccel0100 ), GetUnit( 0x5aa183 ) );
        pMovie->setVariableValueVariadic(
            SelectVar( "/:cur_0_100_i_upg", "/:cur_0_100_i" ), "%.1f %s",
            double( *pAccel0100 ), GetUnit( 0x5aa183 ) );
    }

    const float topSpeed = ( pPack != nullptr )
                               ? ( pPack->SpeedRatio + kRatioBase ) *
                                     pCarEntry->TopSpeedKMH * kRatioToPercent
                               : pCarEntry->TopSpeedKMH;

    gpMngFlashLocalize->setSysVar( "SPEED", int32_t( topSpeed ) );
    gpMngFlashLocalize->setVarLocalized(
        pMovie, SelectVar( "/:cur_max_speed_m_upg", "/:cur_max_speed_m" ),
        0x3b5cbb2ull );
    gpMngFlashLocalize->clearAllSysVar();
    gpMngFlashLocalize->setSysVar( "SPEED", int32_t( topSpeed * kKmToMiles ) );
    gpMngFlashLocalize->setVarLocalized(
        pMovie, SelectVar( "/:cur_max_speed_i_upg", "/:cur_max_speed_i" ),
        0x3c50df2ull );
    gpMngFlashLocalize->clearAllSysVar();

    if ( !bUpgrade ) {
        pMovie->setVariableValue( "/:cur_localisation",
                                  pCarEntry->EngineLocalisation.pAsString );
        pMovie->setVariableValue( "/:cur_type",
                                  pCarEntry->EngineType.pAsString );
        pMovie->setVariableValueVariadic( "/:cur_displacement_m", "%d %s",
                                          pCarEntry->Displacement,
                                          GetUnit( 0x532773 ) );
        pMovie->setVariableValueVariadic( "/:cur_displacement_i", "%d %s",
                                          pCarEntry->Displacement,
                                          GetUnit( 0x532773 ) );
        pMovie->setVariableValueNonLinked( "/:cur_turbo", "n/a" );
        pMovie->setVariableValue( "/:cur_balance",
                                  pCarEntry->DriveType.pAsString );
        pMovie->setVariableValue( "/:cur_layout",
                                  pCarEntry->EngineLocalisation.pAsString );
        pMovie->setVariableValueNonLinked( "/:cur_interior", "n/a" );
        pMovie->setVariableValue( "/:cur_drive",
                                  pCarEntry->DriveType.pAsString );
        pMovie->setVariableValue( "/:cur_gearbox",
                                  pCarEntry->GearboxType.pAsString );
        pMovie->setVariableValue( "/:cur_brakes_front_m",
                                  pCarEntry->BrakesFront.pAsString );
        pMovie->setVariableValue( "/:cur_brakes_front_i",
                                  pCarEntry->BrakesFront.pAsString );
        pMovie->setVariableValue( "/:cur_brakes_rear_m",
                                  pCarEntry->BrakesRear.pAsString );
        pMovie->setVariableValue( "/:cur_brakes_rear_i",
                                  pCarEntry->BrakesRear.pAsString );
        pMovie->setVariableValueNonLinked( "/:cur_abs", "n/a" );
        pMovie->setVariableValueNonLinked( "/:cur_susp_f", "n/a" );
        pMovie->setVariableValueNonLinked( "/:cur_susp_r", "n/a" );

        const DBEntryRim *pRim = gpDatabase->getRim( configID, rimID );
        if ( pRim == nullptr ) {
            pMovie->setVariableValueNonLinked( "/:cur_tires_dim_f", "n/a" );
            pMovie->setVariableValueNonLinked( "/:cur_tires_dim_r", "n/a" );
        } else {
            pMovie->setVariableValueVariadic(
                "/:cur_tires_dim_f", "%d/%d/%d", pRim->FrontWidth,
                pRim->FrontHeight, pRim->FrontDiameter );
            pMovie->setVariableValueVariadic( "/:cur_tires_dim_r", "%d/%d/%d",
                                              pRim->RearWidth, pRim->RearHeight,
                                              pRim->RearDiameter );
        }

        pMovie->setVariableValueVariadic( "/:cur_red_line_m", "%d %s",
                                          pCarEntry->Redline,
                                          GetUnit( 0x54ae13 ) );
        pMovie->setVariableValueVariadic( "/:cur_red_line_i", "%d %s",
                                          pCarEntry->Redline,
                                          GetUnit( 0x54ae13 ) );

        if ( pCarEntry->QuarterMileSec == 0.0f ) {
            pMovie->setVariableValueVariadic( "/:cur_q_mile_i", "n/a" );
            pMovie->setVariableValueVariadic( "/:cur_q_mile_m", "n/a" );
        } else {
            pMovie->setVariableValueVariadic(
                "/:cur_q_mile_i", "%.1f %s",
                double( pCarEntry->QuarterMileSec ), GetUnit( 0x5aa183 ) );
            pMovie->setVariableValueVariadic(
                "/:cur_q_mile_m", "%.1f %s",
                double( pCarEntry->QuarterMileSec ), GetUnit( 0x5aa183 ) );
        }

        pMovie->setVariableValueVariadic( "/:l_dim_01", "%d mm\n%d mm\n%dmn",
                                          pCarEntry->Width, pCarEntry->Length,
                                          pCarEntry->Height );
        pMovie->setVariableValueVariadic(
            "/:l_dim_02", "%d mm\n%d mm\n%dmn", pCarEntry->TrackFront,
            pCarEntry->TrackBack, pCarEntry->WheelBase );
    }

    float accel = 0.0f;
    float speed = 0.0f;
    float handling = 0.0f;
    float braking = 0.0f;
    if ( pPack == nullptr ) {
        accel = pCarEntry->AccelerationKMH;
        speed = pCarEntry->TopSpeedKMH;
        handling = float( pCarEntry->Handling );
        braking = pCarEntry->BrakingKMH;
    } else if ( pUpgrade != nullptr ) {
        accel = pUpgrade->Acceleration;
        speed = topSpeed;
        handling = pUpgrade->Handling;
        braking = pUpgrade->Braking;
    } else {
        return;
    }

    pMovie->setVariableValue(
        SelectVar( "/:cur_accel_upg", "/:cur_accel" ),
        bDefaultCar ? 0
                    : ClampBar( int32_t( ( accel - kAccelBarOrigin ) *
                                         kAccelBarScale ) ) );
    pMovie->setVariableValue(
        SelectVar( "/:cur_speed_upg", "/:cur_speed" ),
        bDefaultCar ? 0
                    : ClampBar( int32_t( ( speed - kSpeedBarOrigin ) *
                                         kSpeedBarScale ) ) );
    pMovie->setVariableValue(
        SelectVar( "/:cur_handling_upg", "/:cur_handling" ),
        bDefaultCar ? 0 : ClampBar( int32_t( handling ) ) );
    pMovie->setVariableValue(
        SelectVar( "/:cur_braking_upg", "/:cur_braking" ),
        bDefaultCar ? 0
                    : ClampBar( int32_t( ( braking - kBrakingBarOrigin ) *
                                         kBrakingBarScale ) ) );
}

void FlashPanelCarSpecs::watch( CarDesc *pNewCar ) {
    // FUN_007902f0 (inlined)
    pCar = pNewCar;
    pUpgradeEntry = nullptr;

    detach();
    pTarget = pNewCar;
    if ( pNewCar != nullptr ) {
        pNewCar->subscribe( this );
    }
}

FlashPanelEbayOffer::FlashPanelEbayOffer() : FlashPanel(), pOffer( nullptr ) {}

FlashPanelEbayOffer::~FlashPanelEbayOffer() {}

void FlashPanelEbayOffer::tick() {
    // FUN_00767370
    if ( pMovie == nullptr || pOffer == nullptr ) {
        return;
    }

    if ( pOffer->CarID == 0xffffu ) {
        pMovie->setVariableValueNonLinked( "/:CarName", "" );
        pMovie->setVariableValueNonLinked( "/:CurrentPrice", "" );
        pMovie->setVariableValueNonLinked( "/:CurrentSeller", "" );
        return;
    }

    for ( const CarConfig &car : gpDatabase->getCarList() ) {
        if ( uint32_t( car.Hash ) == pOffer->CarID ) {
            pMovie->setVariableValue( "/:CarName", car.FullName.c_str() );
            break;
        }
    }

    char priceString[32];
    FormatMoneyString( priceString, int32_t( pOffer->Price ) );
    pMovie->setVariableValueNonLinked( "/:BuyPrice", priceString );
    pMovie->setVariableValueNonLinked( "/:CurrentSeller", pOffer->pOwnerName );
    pMovie->setVariableValueNonLinked( "/:SellType", "???" );
}
