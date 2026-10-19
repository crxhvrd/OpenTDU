#include "shared.h"
#include "trading_menu.h"

#include "gm_living.h"
#include "mng_collection.h"

#include "core/format.h"
#include "database/gs_database.h"
#include "flash/flash_callbacks_register.h"
#include "flash/gs_flash.h"
#include "flash/mng_flash.h"
#include "flash/mng_flash_localize.h"
#include "flash/modal_message_box.h"
#include "flash/implementation/flash_player.h"
#include "flash/implementation/flash_sprite.h"
#include "game/cinematic/mng_cinematics.h"
#include "game/freeride/mng_photo_mode.h"
#include "game/mng_hud.h"
#include "game/mng_logo_display.h"
#include "game/scenaric/entity/entity_cinematic.h"
#include "player_data/achievements_data.h"
#include "player_data/driver.h"
#include "player_data/gs_playerdata.h"
#include "player_data/player_data_interface.h"
#include "player_data/save_game_manager.h"
#include "player_data/saved_ebay_state.h"
#include "player_data/saved_garage.h"
#include "render/camera/camera_showcase.h"
#include "render/camera/gs_camera.h"

static constexpr int32_t kShowcaseCameraMode = 6;
static constexpr int32_t kPhotoShootCameraMode = 10;
static constexpr float kCameraTransitionTime = 1.0f; // FLOAT_00f4165c
static constexpr float kCinematicHeightOffset = -50.0f;
static constexpr float kPhotoCameraMaxDistance = 20.0f;
static constexpr int32_t kNumDownloadsPerPage = 3;
static constexpr uint64_t kAllLabelHashcode = 0x37abe82;
static constexpr uint64_t kNoBikeTradeRewardHashcode = 0x5f2d20;
static constexpr uint64_t kSellFailedHashcode = 0x5c438c;
static constexpr uint64_t kBuyFailedHashcode = 0x5abcec;
static constexpr uint64_t kPhotoCardMissingHashcode = 0x55d704;

static constexpr const char *kBuyTablePath =
    "/ss_menu/table/tables/buy_carbike";
static constexpr const char *kSellTablePath =
    "/ss_menu/table/tables/sell_carbike";
static constexpr const char *kPurchasesTablePath =
    "ss_menu/table/tables/my_purch";
static constexpr const char *kSalesTablePath = "ss_menu/table/tables/my_sells";
static constexpr const char *kBrandLogoPath =
    "/ss_menu/table/filtres/gauge_1/pict";

static inline IPlayerData *GetSave() { return gpPlayerData->getLoadedSave(); }

static inline EbayList *GetEbayList() { return GetSave()->getEbayList(); }

static inline int32_t ClampGarageSlot( const int32_t slot ) {
    return ( slot < 0 || slot > Garage::kNumSavedCarSlots ) ? 0 : slot;
}

static inline uint32_t ReadFlashUint( FlashPlayer *pPlayer,
                                      const char *pVarName ) {
    char value[32] = {};
    pPlayer->getVariableAsString( pVarName, value );
    return uint32_t( StringToFloat( value ) );
}

static int32_t SendEbaySell() {
    // FUN_00884ec0
    OTDU_UNIMPLEMENTED_ONCE; // TODO: ONLINE
    return -1;
}

static int32_t SendEbayBuy() {
    // FUN_00883ef0
    OTDU_UNIMPLEMENTED_ONCE; // TODO: ONLINE
    return -1;
}

static int32_t SendEbayQueryCount( EbayQuery * /*pQuery*/ ) {
    // FUN_00884440
    OTDU_UNIMPLEMENTED_ONCE; // TODO: ONLINE
    return -1;
}

static int32_t GetNumNetDownloads() {
    // DAT_014c02fc
    OTDU_UNIMPLEMENTED_ONCE; // TODO: ONLINE (net resource download list)
    return 0;
}

TradingMenu::TradingMenu()
    : Notifier(),
      CinematicPlayer(),
      buyRequestID( 0 ),
      sellRequestID( 0 ),
      previewCar(),
      previewCarPanel(),
      offerPanel(),
      pPhotoMaterial( nullptr ),
      queries(),
      savedQuery(),
      pFlashPlayer( nullptr ),
      pMngFlash( nullptr ),
      cinematics{},
      pActiveCinematic( nullptr ),
      tradeMode( ETM_None ),
      pActiveList( nullptr ),
      scene(),
      pLiving( nullptr ),
      bOpen( false ),
      carCountRequestID( 0 ),
      bikeCountRequestID( 0 ),
      numCarOffers( 0 ),
      numBikeOffers( 0 ),
      helpText{},
      bSavingBeforeSell( false ),
      sellSlotIndex( 0 ),
      sellPrice( 0 ),
      bSavingBeforeBuy( false ),
      buyOfferID( 0u ),
      buyGarageSlot( 0 ),
      buyPrice( 0 ),
      downloads(),
      photoCarTransform( Matrix4x4f_t::Identity() ) {
    // FUN_007684b0
    downloads.reset();
    scene.setMenu( this );
}

TradingMenu::~TradingMenu() {}

void TradingMenu::setupTables() {
    // FUN_00765fd0
    // TODO: ONLINE (registers the menu as gLobbyNetworkService packet
    // listener)

    carOffersList.setQuery( &queries[0] );
    bikeOffersList.setQuery( &queries[0] );
    garageCarsList.setQuery( &queries[1] );
    garageBikesList.setQuery( &queries[1] );
    purchasesList.setQuery( &queries[2] );
    salesList.setQuery( &queries[3] );

    carOffersList.setBikes( false );
    carOffersList.setPlayer( pFlashPlayer, kBuyTablePath );
    bikeOffersList.setBikes( true );
    bikeOffersList.setPlayer( pFlashPlayer, kBuyTablePath );
    garageCarsList.setBikes( false );
    garageCarsList.setPlayer( pFlashPlayer, kSellTablePath );
    garageBikesList.setBikes( true );
    garageBikesList.setPlayer( pFlashPlayer, kSellTablePath );
    purchasesList.setPurchases( true );
    purchasesList.setPlayer( pFlashPlayer, kPurchasesTablePath );
    salesList.setPurchases( false );
    salesList.setPlayer( pFlashPlayer, kSalesTablePath );

    carOffersList.setOwner( this );
    bikeOffersList.setOwner( this );
    garageCarsList.setOwner( this );
    garageBikesList.setOwner( this );
    purchasesList.setOwner( this );
    salesList.setOwner( this );

    offerPanel.pMovie = pFlashPlayer;
    previewCarPanel.pMovie = pFlashPlayer;
}

void TradingMenu::tick( const float deltaTime ) {
    // FUN_00768a90
    scene.tick( deltaTime );

    if ( bSavingBeforeSell &&
         !gSavegameManager.isDwordDiffFromPendingSaveCount() ) {
        bSavingBeforeSell = false;

        sellRequestID = SendEbaySell();
        if ( sellRequestID == -1 ) {
            sellRequestID = 0;
            unlockSellCar();
            showTradingError( kSellFailedHashcode );
            gpPlayerData->requestSave( 1 );
        } else {
            pFlashPlayer->setVariableValue( "/:connect_ok", 0 );
        }
    }

    if ( bSavingBeforeBuy &&
         !gSavegameManager.isDwordDiffFromPendingSaveCount() ) {
        bSavingBeforeBuy = false;

        buyRequestID = SendEbayBuy();
        if ( buyRequestID != -1 ) {
            pFlashPlayer->setVariableValue( "/:connect_ok", 0 );
            return;
        }

        Driver *pDriver = GetSave()->getDriver();
        pDriver->setMoneyAndNotify( int32_t( pDriver->getMoney() ) + buyPrice );

        buyRequestID = 0;
        showTradingError( kBuyFailedHashcode );
        gpPlayerData->requestSave( 1 );
    }
}

void TradingMenu::detachObservers() {
    // FUN_00765f80
    // TODO: ONLINE (unregisters the gLobbyNetworkService packet listener)
    offerPanel.detach();
    previewCarPanel.detach();
}

void TradingMenu::terminate() {
    // FUN_009dc690
    scene.terminate();
}

void TradingMenu::watchOffer( EbayOffer *pOffer ) {
    offerPanel.setOffer( pOffer );
    offerPanel.detach();
    offerPanel.pTarget = pOffer;
    pOffer->subscribe( &offerPanel );
}

void TradingMenu::detachOfferPanel() { offerPanel.detach(); }

void TradingMenu::onUpdate( uint32_t /*param*/ ) {
    // FUN_007666b0
    if ( pActiveList != nullptr ) {
        return;
    }

    GetEbayList()->unsubscribe( this );

    char help[64] = {};
    pFlashPlayer->getVariableAsString( "/:help", help );

    uint64_t sentenceHashcode = 0ull;
    if ( strcmp( help, "Bcars" ) == 0 ) {
        gpMngFlashLocalize->setSysVar( "NB_CARS", numCarOffers );
        sentenceHashcode = 0x389b2a2;
    } else if ( strcmp( help, "Bbikes" ) == 0 ) {
        gpMngFlashLocalize->setSysVar( "NB_BIKES", numBikeOffers );
        sentenceHashcode = 0x37a7062;
    } else if ( strcmp( help, "Bparts" ) == 0 ) {
        snprintf( helpText, sizeof( helpText ), "_There are %d parts for sale",
                  GetEbayList()->NumOffers );
        pFlashPlayer->setVariableValue( "/:Etrade_help", helpText );
        return;
    } else {
        return;
    }

    gpMngFlashLocalize->makeSentence(
        helpText, sizeof( helpText ),
        gpDatabase->getStringByHashcode( 0, sentenceHashcode ) );
    gpMngFlashLocalize->clearAllSysVar();
    pFlashPlayer->setVariableValue( "/:Etrade_help", helpText );
}

bool TradingMenu::executeAction( int32_t param_2,
                                 void *param_3,
                                 void * /*pData*/ ) {
    // FUN_00768360
    if ( param_2 != CE_Ended ) {
        return false;
    }

    if ( param_3 != nullptr && param_3 == pActiveCinematic &&
         ( param_3 == cinematics[TMC_Intro] ||
           param_3 == cinematics[TMC_CarBack] ) ) {
        playCinematic( TMC_IntroLoop );
    }

    return true;
}

bool TradingMenu::playCinematic( const eCinematic cinematic ) {
    // FUN_007676e0
    StopActiveForegroundCinematic();

    EntityCinematic *pCinematic = cinematics[cinematic];
    if ( pCinematic == nullptr ) {
        return false;
    }

    pCinematic->setApplyCineMix( false );
    pCinematic->setRestoreCineMix( false );
    if ( cinematic == TMC_Car && cinematics[TMC_CarBack] != nullptr ) {
        cinematics[TMC_CarBack]->stop( true );
    }

    pCinematic->setGameModeInterface( this );
    pCinematic->launchSpotCine( scene.getContent().getSourceSpot(),
                                kCinematicHeightOffset );
    pActiveCinematic = pCinematic;
    return true;
}

void TradingMenu::onRequestFailed( const int32_t requestID ) {
    // FUN_00766ee0
    if ( buyRequestID == requestID ) {
        buyRequestID = 0;
        showTradingError( kBuyFailedHashcode );
        return;
    }

    if ( sellRequestID == requestID ) {
        sellRequestID = 0;
        unlockSellCar();
        showTradingError( kSellFailedHashcode );
        gpPlayerData->requestSave( 1 );
    }
}

void TradingMenu::showTradingError( const uint64_t hashcode ) {
    pFlashPlayer->setVariableValue( "/:connect_ok", 1 );
    pFlashPlayer->setVariableValue(
        "/:error_msg", gpDatabase->getStringByHashcode( 11, hashcode ) );
    pFlashPlayer->setVariableValue( "/:error_trading", 1 );
}

void TradingMenu::lockSellCar() {
    GarageSlot &slot =
        GetSave()->getGarage()->pCars[ClampGarageSlot( sellSlotIndex )];
    slot.ebayID = 0xffffffffu;
    if ( slot.state != CarDesc::eState::CDS_PutOnEbay ) {
        slot.state = CarDesc::eState::CDS_PutOnEbay;
        slot.notify( 0u );
    }
}

void TradingMenu::unlockSellCar() {
    GarageSlot &slot =
        GetSave()->getGarage()->pCars[ClampGarageSlot( sellSlotIndex )];
    slot.ebayID = 0u;
    if ( slot.state != CarDesc::eState::CDS_None ) {
        slot.state = CarDesc::eState::CDS_None;
        slot.notify( 0u );
    }
}

int32_t TradingMenu::getSelectedOfferIndex() const {
    switch ( tradeMode ) {
    case ETM_BuyCars:
        return carOffersList.getSelectedIndex();
    case ETM_BuyBikes:
        return bikeOffersList.getSelectedIndex();
    default:
        return 0;
    }
}

void TradingMenu::open() {
    // FUN_00768940
    if ( !scene.isLoaded() ) {
        scene.load();

        ScenaricBank &bank = scene.getContent().getBank();
        static constexpr const char *kCinematicNames[TMC_Count] = {
            "C_INTRO", "C_INT_L", "C_CAR", "C_CAR_BACK" };
        for ( int32_t i = 0; i < TMC_Count; i++ ) {
            cinematics[i] = static_cast<EntityCinematic *>(
                bank.findEntity( eEntityType::ET_CINE, kCinematicNames[i] ) );
        }
    }

    bOpen = true;
    gpFlash->getFlashSound()->playFrontEndMusic( true );

    pMngFlash->addWidget( "ebca", &carOffersList );
    pMngFlash->addWidget( "ebbi", &bikeOffersList );
    pMngFlash->addWidget( "seca", &garageCarsList );
    pMngFlash->addWidget( "sebi", &garageBikesList );
    pMngFlash->addWidget( "ebpu", &purchasesList );
    pMngFlash->addWidget( "ebse", &salesList );

    scene.enter();

    const bool bNoBikeTrade = GetSave()->getAchievements()->isRewardLocked(
        kNoBikeTradeRewardHashcode );
    pFlashPlayer->setVariableValue( "/:NoBikeTrade", bNoBikeTrade ? 1u : 0u );
}

void TradingMenu::close() {
    // FUN_007683d0
    bOpen = false;
    gpFlash->getFlashSound()->playFrontEndMusic( true );

    GetEbayList()->unsubscribe( this );

    pMngFlash->removeWidget( &carOffersList );
    pMngFlash->removeWidget( &bikeOffersList );
    pMngFlash->removeWidget( &garageCarsList );
    pMngFlash->removeWidget( &garageBikesList );
    pMngFlash->removeWidget( &purchasesList );
    pMngFlash->removeWidget( &salesList );

    scene.leave();
}

void TradingMenu::readFilterInputs() {
    // FUN_007661a0
    EbayQuery &query = queries[0];

    query.MinLevel = pFlashPlayer->getVariableAsInt( "/:up_min" );

    query.MaxPrice = int32_t( ReadFlashUint(
        pFlashPlayer, "/ss_menu/table/filtres/gauge_4/:param" ) );
    if ( query.MaxPrice == 0 ) {
        query.MaxPrice = -1;
    }

    char group[12] = {};
    pFlashPlayer->getVariableAsString( "/:group_filter", group );
    if ( strcmp( group, "all_cars" ) == 0 ||
         strcmp( group, "all_bikes" ) == 0 ) {
        query.Group[0] = '\0';
        return;
    }

    strncpy( query.Group, group, 3 );
    query.Group[2] = '\0';
}

void TradingMenu::refreshFilterLabels() {
    // FUN_00767920
    const EbayQuery &query = queries[0];

    char logoPath[64];
    snprintf( logoPath, sizeof( logoPath ), "%s", kBrandLogoPath );

    if ( query.isAnyBrand() ) {
        gpMngFlashLocalize->setVarLocalized( pFlashPlayer, "/:v_All",
                                             kAllLabelHashcode );
        if ( gpMngLogoDisplay != nullptr ) {
            gpMngLogoDisplay->clearLogo( pFlashPlayer, logoPath );
        }
    } else if ( gpMngLogoDisplay != nullptr ) {
        gpMngFlashLocalize->setVarLocalized( pFlashPlayer, "", "/:v_All" );
        gpMngLogoDisplay->setLogo( pFlashPlayer, query.Brand.pAsString,
                                   logoPath );
    }

    if ( query.ModelCarID == -1 ) {
        gpMngFlashLocalize->setVarLocalized( pFlashPlayer, "/:modele",
                                             kAllLabelHashcode );
        gpMngFlashLocalize->setVarLocalized( pFlashPlayer, "/:NameTemp",
                                             kAllLabelHashcode );
        gpMngFlashLocalize->setVarLocalized(
            pFlashPlayer, "/ss_menu/table/filtres/gauge_2/:param",
            kAllLabelHashcode );
    } else {
        for ( const CarConfig &car : gpDatabase->getCarList() ) {
            if ( car.Hash == query.ModelCarID ) {
                const char *pName = car.FullName.c_str();
                pFlashPlayer->setVariableValue( "/:modele", pName );
                pFlashPlayer->setVariableValue( "/:NameTemp", pName );
                pFlashPlayer->setVariableValue(
                    "/ss_menu/table/filtres/gauge_2/:param", pName );
                break;
            }
        }
    }

    pFlashPlayer->setVariableValue(
        "/:up_min", ( query.MinLevel == -1 ) ? 1 : query.MinLevel );

    if ( query.MaxPrice == -1 ) {
        pFlashPlayer->setVariableValueNonLinked( "/:NameTemp", "---" );
        pFlashPlayer->setVariableValueNonLinked( "/:pr_max", "---" );
        pFlashPlayer->setVariableValueNonLinked(
            "/ss_menu/table/filtres/gauge_4/:param", "---" );
        return;
    }

    char price[32];
    FormatMoneyString( price, query.MaxPrice );
    pFlashPlayer->setVariableValueNonLinked( "/:NameTemp", price );
    pFlashPlayer->setVariableValueNonLinked( "/:pr_max", price );
    pFlashPlayer->setVariableValueNonLinked(
        "/ss_menu/table/filtres/gauge_4/:param", price );
}

bool TradingMenu::isModelSelectable( const DBEntryCarPhysicsData &car ) const {
    const DBEntryBrand *pBrand = car.Brand;
    if ( pBrand == nullptr ||
         gpDatabase->getStringByHashcode( 6, 0x3540681 ) ==
             pBrand->BrandID.pAsString ||
         ( car.BitField & 1 ) == 0 || ( pBrand->Flags & 1 ) == 0 ||
         car.CarID == CarDesc::kDefaultCarConfigID ||
         car.CarID == CarDesc::kDefaultBikeConfigID ||
         ( car.BitField >> 5 & 1 ) != 0 ) {
        return false;
    }

    const EbayQuery &query = queries[0];
    if ( gpDatabase->isBike( &car ) != query.bWantBikes ) {
        return false;
    }

    return query.isAnyBrand() ||
           query.Brand.Hashcode == pBrand->BrandID.Hashcode;
}

void TradingMenu::cycleModel( const bool bNext ) {
    // FUN_00767b50
    EbayQuery &query = queries[0];
    const GameDatabase &gameDB = gpDatabase->getGameDB();
    const int32_t numCars = int32_t( gameDB.pNumItems[1] );

    if ( query.ModelCarID == -1 ) {
        if ( !bNext ) {
            int32_t lastCarID = -1;
            for ( int32_t i = 0; i < numCars; i++ ) {
                if ( isModelSelectable( gameDB.DBCarData[i] ) ) {
                    lastCarID = gameDB.DBCarData[i].CarID;
                }
            }
            query.ModelCarID = lastCarID;
            refreshFilterLabels();
            return;
        }

        for ( int32_t i = 0; i < numCars; i++ ) {
            if ( isModelSelectable( gameDB.DBCarData[i] ) ) {
                query.ModelCarID = gameDB.DBCarData[i].CarID;
                refreshFilterLabels();
                return;
            }
        }
        return;
    }

    int32_t previousCarID = -1;
    int32_t currentIndex = 0;
    for ( ; currentIndex < numCars; currentIndex++ ) {
        const DBEntryCarPhysicsData &car = gameDB.DBCarData[currentIndex];
        if ( car.CarID == query.ModelCarID ) {
            break;
        }
        if ( isModelSelectable( car ) ) {
            previousCarID = car.CarID;
        }
    }
    if ( currentIndex >= numCars ) {
        return;
    }

    if ( !bNext ) {
        query.ModelCarID = previousCarID;
        refreshFilterLabels();
        return;
    }

    for ( int32_t i = currentIndex + 1; i < numCars; i++ ) {
        if ( isModelSelectable( gameDB.DBCarData[i] ) ) {
            query.ModelCarID = gameDB.DBCarData[i].CarID;
            refreshFilterLabels();
            return;
        }
    }

    query.ModelCarID = -1;
    refreshFilterLabels();
}

bool TradingMenu::isBrandSelectable( const DBEntryBrand *pBrand ) const {
    if ( gpDatabase->isTrafficOnlyBrand( pBrand ) || pBrand == nullptr ||
         ( pBrand->Flags & 1 ) == 0 || ( pBrand->Flags >> 1 & 1 ) != 0 ||
         !gpDatabase->brandHasAvailableCar( pBrand ) ) {
        return false;
    }

    return gpDatabase->brandHasCarOfCategory( pBrand, queries[0].bWantBikes );
}

void TradingMenu::cycleBrand( const bool bNext ) {
    // FUN_00768040
    EbayQuery &query = queries[0];
    const GameDatabase &gameDB = gpDatabase->getGameDB();
    DBEntryBrand *pBrands = gameDB.DBBrand;
    const int32_t numBrands = int32_t( gameDB.pNumItems[6] );

    if ( query.isAnyBrand() ) {
        if ( bNext ) {
            for ( int32_t i = 0; i < numBrands; i++ ) {
                if ( isBrandSelectable( &pBrands[i] ) ) {
                    query.Brand = pBrands[i].BrandID;
                    break;
                }
            }
        } else {
            // NOTE: Retail never picks the first brand when going backward
            for ( int32_t i = numBrands - 1; i > 0; i-- ) {
                if ( isBrandSelectable( &pBrands[i] ) ) {
                    query.Brand = pBrands[i].BrandID;
                    break;
                }
            }
        }
    } else {
        const DBEntryBrand *pPreviousBrand = nullptr;
        for ( int32_t i = 0; i < numBrands; i++ ) {
            if ( pBrands[i].BrandID.Hashcode == query.Brand.Hashcode ) {
                if ( bNext ) {
                    query.Brand.Hashcode = 0xffffffffffffffffull;
                    for ( int32_t j = i + 1; j < numBrands; j++ ) {
                        if ( isBrandSelectable( &pBrands[j] ) ) {
                            query.Brand = pBrands[j].BrandID;
                            break;
                        }
                    }
                } else if ( pPreviousBrand != nullptr ) {
                    query.Brand = pPreviousBrand->BrandID;
                } else {
                    query.Brand.Hashcode = 0xffffffffffffffffull;
                }
                break;
            }

            if ( isBrandSelectable( &pBrands[i] ) ) {
                pPreviousBrand = &pBrands[i];
            }
        }
    }

    query.ModelCarID = -1;
    refreshFilterLabels();
}

void TradingMenu::refreshHelp() {
    // FUN_00766810
    pFlashPlayer->setVariableValueNonLinked( "/:Etrade_help", "" );
    GetEbayList()->unsubscribe( this );

    char help[64] = {};
    pFlashPlayer->getVariableAsString( "/:help", help );

    const bool bBuyCars = strcmp( help, "Bcars" ) == 0;
    if ( bBuyCars || strcmp( help, "Bbikes" ) == 0 ) {
        GetEbayList()->unsubscribe( this );
        GetEbayList()->subscribe( this );

        EbayQuery &query = queries[0];
        query.Sort = 0;
        query.Brand.Hashcode = 0xffffffffffffffffull;
        query.field_0x08 = 0xffffffffffffffffull;
        query.ModelCarID = -1;
        query.MaxPrice = -1;
        query.MinLevel = -1;
        query.bReverse = false;
        query.Group[0] = '\0';
        query.bWantBikes = !bBuyCars;

        const int32_t requestID = SendEbayQueryCount( &query );
        if ( bBuyCars ) {
            carCountRequestID = requestID;
        } else {
            bikeCountRequestID = requestID;
        }
        return;
    }

    if ( strcmp( help, "Bparts" ) == 0 ) {
        return;
    }

    const bool bSellCars = strcmp( help, "Scars" ) == 0;
    if ( bSellCars || strcmp( help, "Sbikes" ) == 0 ) {
        Garage *pGarage = GetSave()->getGarage();
        gpMngFlashLocalize->setSysVar(
            bSellCars ? "NB_CARS" : "NB_BIKES",
            pGarage->getNumCarsOfKind( 0ull, !bSellCars ) );
        gpMngFlashLocalize->makeSentence(
            helpText, sizeof( helpText ),
            gpDatabase->getStringByHashcode( 0, bSellCars ? 0x35bebe2
                                                          : 0x34ca9a2 ) );
        pFlashPlayer->setVariableValue( "/:Etrade_help", helpText );
        gpMngFlashLocalize->clearAllSysVar();
        return;
    }

    if ( strcmp( help, "Sparts" ) == 0 ) {
        pFlashPlayer->setVariableValueNonLinked( "/:Etrade_help",
                                                 "parts not done" );
    }
}

void TradingMenu::testBuy() {
    // FUN_00766ac0
    if ( pFlashPlayer == nullptr ) {
        return;
    }

    const EbayOffer &offer = GetEbayList()->getOffer( getSelectedOfferIndex() );
    const int32_t freeSlot = GetSave()->getGarage()->findFreeSlot( 0ull );
    const uint32_t money = GetSave()->getDriver()->getMoney();

    int32_t result = 0;
    if ( money < offer.Price ) {
        result = 2;
    } else {
        result = ( freeSlot != -1 ) ? 1 : 0;
    }

    char value[32];
    snprintf( value, sizeof( value ), "%d", result );
    pFlashPlayer->setVariableValueNonLinked( "/:test_buy", value );
}

void TradingMenu::buyOffer() {
    // FUN_00767480
    if ( pFlashPlayer == nullptr ) {
        return;
    }

    const EbayOffer &offer = GetEbayList()->getOffer( getSelectedOfferIndex() );
    const int32_t freeSlot = GetSave()->getGarage()->findFreeSlot( 0ull );
    const uint32_t price = offer.Price;

    char value[32];
    FormatMoneyString( value, int32_t( price ) );

    Driver *pDriver = GetSave()->getDriver();

    int32_t bidState = 0;
    if ( pDriver->getMoney() < price ) {
        pFlashPlayer->setVariableValueNonLinked( "/:m_Message",
                                                 "NOT ENOUGH MONEY" );
    } else if ( freeSlot == -1 ) {
        pFlashPlayer->setVariableValueNonLinked( "/:m_Message",
                                                 "NO PLACE IN GARAGE" );
    } else {
        pFlashPlayer->setVariableValueNonLinked( "/:BuyPrice", value );
        snprintf( value, sizeof( value ), "%d", offer.NumBids + 1 );
        pFlashPlayer->setVariableValueNonLinked( "/:CurrentNbBid", value );
        bidState = 2;
    }

    snprintf( value, sizeof( value ), "%d", bidState );
    pFlashPlayer->setVariableValueNonLinked( "/:IsBidOk", value );

    if ( bidState != 2 ) {
        return;
    }

    bSavingBeforeBuy = true;
    buyOfferID = offer.ID;
    buyGarageSlot = int16_t( freeSlot );
    buyPrice = int32_t( offer.Price );
    pFlashPlayer->setVariableValue( "/:connect_ok", 0 );

    pDriver->setMoneyAndNotify( int32_t( pDriver->getMoney() - offer.Price ) );
    gpPlayerData->requestSave( 1 );
}

void TradingMenu::sellCar() {
    // FUN_00767030
    const int32_t salePrice =
        int32_t( ReadFlashUint( pFlashPlayer, "/:SalePrice" ) );

    char whereTo[32] = {};
    pFlashPlayer->getVariableAsString( "/:WhereTo3", whereTo );

    Garage *pGarage = GetSave()->getGarage();

    CarDesc car;
    int32_t slotIndex = salePrice;
    if ( tradeMode == ETM_SellBikes ) {
        car = pGarage->pCars[garageBikesList.getSelectedGarageSlot()];

        slotIndex = 0;
        for ( int32_t i = 0; i <= Garage::kNumSavedCarSlots; i++ ) {
            if ( pGarage->pCars[i].getCarID() == car.getCarID() ) {
                slotIndex = i;
                break;
            }
        }
    } else if ( tradeMode == ETM_SellCars ) {
        car = pGarage->pCars[garageCarsList.getSelectedGarageSlot()];
        slotIndex = pGarage->getSlotIndex( &car );
    }

    if ( strcmp( whereTo, "direct" ) == 0 ) {
        GetSave()->sellCar( car.getCarID() );
        pFlashPlayer->setVariableValue( "/:error_trading", 0 );
        pFlashPlayer->setVariableValueNonLinked( "/:error_msg", "" );
        pFlashPlayer->setVariableValue( "/:connect_ok", 1 );
        return;
    }

    bSavingBeforeSell = true;
    sellSlotIndex = slotIndex;
    sellPrice = salePrice;
    pFlashPlayer->setVariableValue( "/:connect_ok", 0 );

    lockSellCar();
    pGarage->pCars[ClampGarageSlot( slotIndex )].price = salePrice;

    gpPlayerData->requestSave( 1 );
}

void TradingMenu::removeOffer() {
    // FUN_00765f10
    if ( tradeMode != ETM_Sales ) {
        return;
    }

    const EbayOffer &offer =
        GetEbayList()->getOffer( salesList.getSelectedIndex() );
    if ( offer.ExtraID != 0xffffffffu ) {
        OTDU_UNIMPLEMENTED_ONCE; // TODO: ONLINE (FUN_00883f80 cancel sale)
    }

    if ( pActiveList != nullptr ) {
        pActiveList->refresh();
    }
}

void TradingMenu::showOfferCar() {
    // FUN_007688a0
    if ( previewCar.getCarConfigID() == 0u ) {
        return;
    }

    scene.showPreviewCar(
        previewCar.getCarConfigID(), previewCar.paintIndex, previewCar.rimID,
        previewCar.interiorID, previewCar.paintType, previewCar.deluxeMainColor,
        previewCar.deluxeSecondColor, &previewCar.deluxeLogoColor,
        &previewCar.deluxeSecondColor2 );

    if ( cinematics[TMC_CarBack] != nullptr ) {
        cinematics[TMC_CarBack]->stop( true );
    }
    if ( cinematics[TMC_IntroLoop] != nullptr ) {
        cinematics[TMC_IntroLoop]->stop( true );
    }

    playCinematic( TMC_Car );
}

void TradingMenu::PhotoClipCallback( FlashRECT * /*pViewport*/,
                                     float /*depth*/,
                                     DrawList *pDrawList,
                                     void *pUser,
                                     FlashResource *pResource,
                                     const char * /*pClipName*/ ) {
    // FUN_00766bc0
    TradingMenu *pMenu = static_cast<TradingMenu *>( pUser );
    if ( pMenu->pFlashPlayer == nullptr ) {
        return;
    }

    FlashClip clip;
    if ( !pMenu->pFlashPlayer->getClipPlayer( "/ss_menu/frame/frame/lurephoto",
                                              &clip ) ) {
        return;
    }

    FlashRECT rect;
    clip.getPropertyViewport( eFlashProperty::FP_Viewport, rect );

    if ( pMenu->pPhotoMaterial == nullptr ) {
        return;
    }

    pDrawList->setActiveMaterial( pMenu->pPhotoMaterial );
    pDrawList->setDiffuse( 0, 0xffffffffu );

    DrawGaugeQuad( 0.0f, 0.0f, 1.0f, 1.0f, rect.X, rect.Y, rect.Width + rect.X,
                   rect.Height + rect.Y, pResource );
}

static inline void SetCameraMode( const int32_t mode ) {
    gpCamera->setCameraMode( mode, 0, kCameraTransitionTime );
}

static inline void SetShowcaseCameraInput( const bool bEnabled ) {
    CameraShowcase *pCamera =
        static_cast<CameraShowcase *>( gpCamera->findCameraWithMode(
            kShowcaseCameraMode, gpCamera->getBoundCarState() ) );
    if ( pCamera != nullptr ) {
        pCamera->setInputEnabled( bEnabled );
    }
}

static inline void SetRadialMenuOpen() {
    if ( gpActiveGMScene != nullptr ) {
        static_cast<GMLiving *>( gpActiveGMScene )->setRadialMenuOpen( true );
    }
}

bool TradingMenu::onMessage( FlashMessage *pMessage ) {
    // FUN_00768da0
    switch ( *pMessage ) {
    case FlashMessage( "ebin" ):
        SetRadialMenuOpen();
        open();
        return true;

    case FlashMessage( "ebou" ):
        close();
        SetRadialMenuOpen();
        return true;

    case FlashMessage( "help" ):
        refreshHelp();
        return true;

    case FlashMessage( "tbuy" ):
        testBuy();
        return true;

    case FlashMessage( "buyc" ):
        buyOffer();
        return true;

    case FlashMessage( "sell" ):
        sellCar();
        return true;

    case FlashMessage( "remo" ):
        removeOffer();
        return true;

    case FlashMessage( "ebda" ):
        showOfferCar();
        return true;

    case FlashMessage( "bsok" ):
    case FlashMessage( "ebdb" ):
        playCinematic( TMC_CarBack );
        scene.clearPreviewCar();
        return true;

    case FlashMessage( "bpon" ):
    case FlashMessage( "dain" ):
        if ( pActiveList != nullptr ) {
            pActiveList->setInputEnabled( false );
        }
        return true;

    case FlashMessage( "bpof" ):
    case FlashMessage( "daou" ):
        if ( pActiveList != nullptr ) {
            pActiveList->setInputEnabled( true );
        }
        return true;

    case FlashMessage( "albr" ):
        queries[0].field_0x08 = 0xffffffffffffffffull;
        queries[0].Brand.Hashcode = 0xffffffffffffffffull;
        refreshFilterLabels();
        return true;

    case FlashMessage( "almo" ):
        queries[0].ModelCarID = -1;
        refreshFilterLabels();
        return true;

    case FlashMessage( "nebr" ):
        cycleBrand( true );
        return true;

    case FlashMessage( "prbr" ):
        cycleBrand( false );
        return true;

    case FlashMessage( "nemo" ):
        cycleModel( true );
        return true;

    case FlashMessage( "prmo" ):
        cycleModel( false );
        return true;

    case FlashMessage( "fiin" ):
        savedQuery = queries[0];
        refreshFilterLabels();
        carOffersList.setNavigationEnabled( false );
        bikeOffersList.setNavigationEnabled( false );
        return true;

    case FlashMessage( "fica" ):
        queries[0] = savedQuery;
        if ( pActiveList != nullptr ) {
            pActiveList->refresh();
        }
        return true;

    case FlashMessage( "fiou" ):
        readFilterInputs();
        if ( pActiveList != nullptr ) {
            pActiveList->refresh();
        }
        carOffersList.setNavigationEnabled( true );
        bikeOffersList.setNavigationEnabled( true );
        return true;

    case FlashMessage( "rsfi" ):
        queries[0].reset();
        return true;

    case FlashMessage( "doin" ):
        downloads.open( pFlashPlayer );
        return true;

    case FlashMessage( "nedo" ):
        downloads.nextPage( pFlashPlayer );
        return true;

    case FlashMessage( "prdo" ):
        downloads.previousPage( pFlashPlayer );
        return true;

    case FlashMessage( "doou" ):
        downloads.reset();
        return true;

    case FlashMessage( "psin" ): {
        const int32_t numPhotos = gpMngCollection->countPhotos();
        if ( pFlashPlayer != nullptr ) {
            pFlashPlayer->setVariableValue( "/:nbphotos", numPhotos );
        }

        static constexpr const char *kSelectorClips[4] = { "ph1", "ph2", "ph3",
                                                           "ph4" };
        for ( const char *pClip : kSelectorClips ) {
            gFlashCallbacksRegister.add(
                pClip, &MngCollection::SelectorDrawCallback, gpMngCollection );
        }

        gpMngCollection->resetPhotoSelector();
        gpMngCollection->scrollPhotoSelector( 0 );
        return true;
    }

    case FlashMessage( "psch" ):
        if ( gpMngCollection != nullptr && pFlashPlayer != nullptr ) {
            gpMngCollection->scrollPhotoSelector(
                pFlashPlayer->getVariableAsInt( "/:index_photo" ) - 1 );
        }
        return true;

    case FlashMessage( "psou" ):
        gpMngCollection->resetPhotoSelector();
        gFlashCallbacksRegister.removeAllSynonyms(
            &MngCollection::SelectorDrawCallback );
        return true;

    case FlashMessage( "phin" ): {
        SceneCarPark &carPark = scene.getCarPark();
        SetupShowcaseCameraOnCar(
            photoCarTransform, true, false, kPhotoCameraMaxDistance, true, 0,
            kCameraTransitionTime, false, carPark.getCarState( 0 ) );
        for ( int32_t i = 1; i < carPark.getNumUsedEntries(); i++ ) {
            carPark.setVehicleVisible( i, false );
        }
        return true;
    }

    case FlashMessage( "phou" ): {
        SetCameraMode( kPhotoShootCameraMode );
        SceneCarPark &carPark = scene.getCarPark();
        for ( int32_t i = 1; i < carPark.getNumUsedEntries(); i++ ) {
            carPark.setVehicleVisible( i, true );
        }
        return true;
    }

    case FlashMessage( "phsh" ):
        SetShowcaseCameraInput( true );
        SetCameraMode( kPhotoShootCameraMode );
        gFlashCallbacksRegister.add( "lurephoto",
                                     &TradingMenu::PhotoClipCallback, this );
        return true;

    case FlashMessage( "phcl" ):
        SetShowcaseCameraInput( false );
        pPhotoMaterial = gpMngCollection->grabSelectorPhoto();
        return true;

    case FlashMessage( "phsa" ):
        pFlashPlayer->setVariableValue( "/ss_menu/frame/:photo_ok", 1 );
        pFlashPlayer->showFrame( "close_photo", "/ss_menu/frame", true );
        [[fallthrough]];
    case FlashMessage( "phca" ):
        SetCameraMode( kShowcaseCameraMode );
        gFlashCallbacksRegister.removeAllSynonyms(
            &TradingMenu::PhotoClipCallback );
        return true;

    case FlashMessage( "pcd0" ):
        TestDrive::DisplayModalPrompt(
            false, kPhotoCardMissingHashcode, 0xb, nullptr, nullptr, nullptr, 0,
            nullptr, true, false, MPT_Warning, gpFlashMessageBox );
        return true;

    case FlashMessage( "ssmn" ):
    case FlashMessage( "bids" ):
    case FlashMessage( "chbr" ):
        return true;

    default:
        return false;
    }
}

void TradingMenu::DownloadPager::reset() {
    // FUN_00765e50
    NumPages = 0;
    Page = 0;
    NumDownloads = 0;
    for ( char ( &description )[0x180] : Descriptions ) {
        description[0x17f] = '\0';
        description[0] = '\0';
    }
}

void TradingMenu::DownloadPager::open( FlashPlayer *pPlayer ) {
    // FUN_00766650
    NumDownloads = GetNumNetDownloads();
    Page = 0;
    NumPages = Max( NumDownloads - 1, 0 ) / kNumDownloadsPerPage + 1;
    refresh( pPlayer );
}

void TradingMenu::DownloadPager::nextPage( FlashPlayer *pPlayer ) {
    // FUN_00766620
    if ( Page < NumPages - 1 ) {
        Page++;
    }
    refresh( pPlayer );
}

void TradingMenu::DownloadPager::previousPage( FlashPlayer *pPlayer ) {
    // FUN_00768da0 (inlined)
    if ( Page > 0 ) {
        Page--;
    }
    refresh( pPlayer );
}

void TradingMenu::DownloadPager::refresh( FlashPlayer *pPlayer ) const {
    // FUN_00766280
    pPlayer->setVariableValue( "/:num_page_download", Page + 1 );
    pPlayer->setVariableValue( "/:nb_pages_download", NumPages );

    for ( int32_t line = 0; line < kNumDownloadsPerPage; line++ ) {
        char descriptionVar[32];
        char typeVar[32];
        snprintf( descriptionVar, sizeof( descriptionVar ),
                  "/:download_des_%02d", line + 1 );
        snprintf( typeVar, sizeof( typeVar ), "/:download_type_%02d",
                  line + 1 );

        pPlayer->setVariableValueNonLinked( descriptionVar, "" );
        pPlayer->setVariableValueNonLinked( typeVar, "" );

        if ( Page * kNumDownloadsPerPage + line < NumDownloads ) {
            OTDU_UNIMPLEMENTED_ONCE; // TODO: ONLINE (download entry name/type)
        }
    }
}
