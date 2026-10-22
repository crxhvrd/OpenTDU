#include "shared.h"
#include "flash_list_ebay.h"

#include "core/format.h"
#include "database/gs_database.h"
#include "flash/entities/flash_panel.h"
#include "flash/implementation/flash_player.h"
#include "flash/implementation/flash_sprite.h"
#include "game/vehicle/car_desc.h"
#include "player_data/driver.h"
#include "player_data/gs_playerdata.h"
#include "player_data/player_data_interface.h"
#include "player_data/saved_ebay_state.h"
#include "player_data/saved_garage.h"
#include "player_data/saved_news.h"

static constexpr int32_t kOfferPageSize = 0x20;
static constexpr int32_t kOfferWindowHalfSize = 11;
static constexpr float kMillisecondsToSeconds = 0.001f; // FLOAT_00f4167c
static constexpr float kUpgradeLevelToPercent = 5.0f;   // DAT_00f41750

// Line state strings (database 0).
static constexpr uint64_t kSoldOutLineHashcode = 0x38b1232;
static constexpr uint64_t kOfferOpenHashcode = 0x36dc632;
static constexpr uint64_t kOfferClosedHashcode = 0x381ea72;

static inline EbayList *GetEbayList() {
    return gpPlayerData->getLoadedSave()->getEbayList();
}

static inline Garage *GetGarage() {
    return gpPlayerData->getLoadedSave()->getGarage();
}

static inline Driver *GetDriver() {
    return gpPlayerData->getLoadedSave()->getDriver();
}

static inline const char *GetCarGroupName( DBEntryCarPhysicsData *pEntry ) {
    return ( pEntry != nullptr ) ? pEntry->CarGroup.pAsString : "";
}

static float GetQueryRetryDelay() {
    // gLobbyNetworkService + 0xd980
    OTDU_UNIMPLEMENTED_ONCE; // TODO: ONLINE
    return 0.0f;
}

static float GetQueryRefreshDelay() {
    OTDU_UNIMPLEMENTED_ONCE;           // TODO: ONLINE
    const float refreshDelayMs = 0.0f; // gLobbyNetworkService + 0xd94c
    return refreshDelayMs * kMillisecondsToSeconds;
}

void ShowCarDataGauges( FlashPlayer *pPlayer,
                        const char *pPanelPath,
                        DBEntryCarPhysicsData *pEntry ) {
    // FUN_007197d0
    static constexpr const char *kGauges[4] = { "speed", "accel", "handling",
                                                "braking" };

    if ( pPlayer == nullptr || pPanelPath == nullptr || pEntry == nullptr ) {
        return;
    }

    char path[512];
    snprintf( path, sizeof( path ), "%s/group", pPanelPath );
    pPlayer->showFrame( pEntry->CarGroup.pAsString, path, false );

    for ( const char *pGauge : kGauges ) {
        char variable[64];
        snprintf( variable, sizeof( variable ), "/:cur_%s", pGauge );
        const int32_t value = pPlayer->getVariableAsInt( variable );

        snprintf( path, sizeof( path ), "%s/g_%s/gauge", pPanelPath, pGauge );
        FlashClip gauge;
        if ( !pPlayer->getClipPlayer( path, &gauge ) ) {
            continue;
        }
        gauge.setProperty( eFlashProperty::FP_ScaleX, float( value ) );

        snprintf( path, sizeof( path ), "%s/g_%s/:value", pPanelPath, pGauge );
        pPlayer->setVariableValue( path, value );
    }
}

static void ShowPreviewCarData( IEbayListOwner *pOwner ) {
    // FUN_0077fe70 (inlined)
    CarDesc &previewCar = pOwner->getPreviewCar();
    DBEntryCarPhysicsData *pEntry =
        gpDatabase->getCarDBEntry( int32_t( previewCar.getCarConfigID() ) );
    if ( pEntry != nullptr && pOwner->getFlashPlayer() != nullptr ) {
        ShowCarDataGauges( pOwner->getFlashPlayer(), "/data/datas_base",
                           pEntry );
    }
}

static void SetTradeEmptyArray( IEbayListOwner *pOwner, const bool bEmpty ) {
    pOwner->getFlashPlayer()->setVariableValue( "/:TradeEmptyArray",
                                                bEmpty ? 1 : 0 );
}

FlashListEBayObserver::FlashListEBayObserver( FlashListEBay *pOwner )
    : Notifier(), pList( pOwner ) {}

void FlashListEBayObserver::onUpdate( uint32_t param ) {
    pList->onDataUpdate( param );
}

FlashListEBay::FlashListEBay()
    : FlashList(),
      observer( this ),
      pQuery( nullptr ),
      bBikes( false ),
      pOwner( nullptr ),
      previewCarID( -1 ) {
    // FUN_0077ec60
    levelIndex = 1;
    field16_0x67 = true;
    field_0x66 = true;
}

void FlashListEBay::tick( float deltaTime, float totalTime ) {
    // FUN_007800f0
    FlashList::tick( deltaTime, totalTime );

    if ( 0 < previewCarID ) {
        previewCarID = -1;
    }
}

void FlashListEBay::playLineSubClip( const int32_t lineIndex,
                                     const char *pSubClip,
                                     const int32_t frame ) {
    // FUN_0071cb80
    char path[128];
    snprintf( path, sizeof( path ), "%s/line%d/%s", movieName.c_str(),
              lineIndex + 1, pSubClip );
    pMovie->goToFrameAndPlay( frame, path, false );
}

void FlashListEBay::showLineValidity( const int32_t lineIndex,
                                      const char *pFrame,
                                      const bool bValid ) {
    char linePath[128];
    char validityVariable[128];
    snprintf( linePath, sizeof( linePath ), "%s/line%d", movieName.c_str(),
              lineIndex + 1 );
    snprintf( validityVariable, sizeof( validityVariable ),
              "%s/line%d/:IsValid", movieName.c_str(), lineIndex + 1 );

    pMovie->setVariableValue( validityVariable, bValid ? 1 : 0 );
    pMovie->showFrame( pFrame, linePath, false );
}

void FlashListEBay::showSelectedOffer( const bool bShowSoldState ) {
    // FUN_0077fea0 (bShowSoldState) / FUN_0077ffe0
    EbayOffer &offer = GetEbayList()->getOffer( selectedLineIndex + firstLine );

    if ( pOwner != nullptr ) {
        pOwner->watchOffer( &offer );

        CarDesc &previewCar = pOwner->getPreviewCar();
        previewCar = offer.Car;
        pOwner->getPreviewCarPanel().watch( &previewCar );

        if ( bShowSoldState ) {
            pOwner->getFlashPlayer()->setVariableValue(
                "/:IsSold", offer.ExtraID == 0xffffffffu ? 1 : 0 );
        }

        ShowPreviewCarData( pOwner );
    }

    if ( offer.CarID != 0u ) {
        previewCarID = int32_t( offer.CarID );
    }
}

FlashListEBayOffers::FlashListEBayOffers()
    : FlashListEBay(),
      bAwaitingCount( false ),
      bAwaitingOffers( false ),
      firstOfferIndex( -1 ),
      lastOfferIndex( -1 ),
      numOffers( 0 ),
      bReselectPending( false ),
      reselectTimer( 0.0f ),
      lastRequestedLine( -1 ),
      refreshTimer( 0.0f ) {
    // FUN_0077ee10
}

FlashListEBayOffers::~FlashListEBayOffers() {
    // FUN_0077ede0
}

void FlashListEBayOffers::requestData( const bool bForce, const bool bClear ) {
    // FUN_0077f9d0
    const int32_t selectedLine = selectedLineIndex + firstLine;
    if ( lastRequestedLine == selectedLine && !bForce ) {
        return;
    }

    bAwaitingOffers = true;
    bAwaitingCount = true;
    firstOfferIndex = 0;
    lastOfferIndex = kOfferPageSize;

    if ( bClear ) {
        numOffers = 0;
        SetTradeEmptyArray( pOwner, true );
    }

    lastRequestedLine = selectedLine;
    if ( pQuery != nullptr ) {
        pQuery->bWantBikes = bBikes;
        OTDU_UNIMPLEMENTED_ONCE; // TODO: ONLINE (SendEbayQueryCount)
        refreshTimer = GetQueryRefreshDelay();
    }
}

void FlashListEBayOffers::tick( float deltaTime, float totalTime ) {
    // FUN_00780d70
    FlashListEBay::tick( deltaTime, totalTime );

    if ( pOwner == nullptr ) {
        return;
    }

    const eEbayTradeMode mode = pOwner->getTradeMode();
    const bool bActive = ( mode == ETM_BuyCars && !bBikes ) ||
                         ( mode == ETM_BuyBikes && bBikes );
    if ( !bActive ) {
        return;
    }

    if ( bReselectPending ) {
        reselectTimer -= deltaTime;
        if ( reselectTimer < 0.0f ) {
            bReselectPending = false;

            const int32_t selectedLine = selectedLineIndex + firstLine;
            if ( lastRequestedLine != selectedLine ) {
                lastRequestedLine = selectedLine;
                bAwaitingOffers = true;
                bAwaitingCount = true;
                firstOfferIndex = 0;
                lastOfferIndex = kOfferPageSize;

                if ( pQuery != nullptr ) {
                    pQuery->bWantBikes = bBikes;
                    OTDU_UNIMPLEMENTED_ONCE; // TODO: ONLINE
                                             // (SendEbayQueryCount)
                    refreshTimer = GetQueryRefreshDelay();
                }
            }
        }
    }

    refreshTimer -= deltaTime;
    if ( refreshTimer < 0.0f ) {
        requestData( true, false );
    }
}

int32_t FlashListEBayOffers::getListLength() const {
    // FUN_0077eb30
    return numOffers;
}

void FlashListEBayOffers::open() {
    // FUN_0077fde0
    numOffers = 1;
    FlashList::open();

    if ( pOwner != nullptr ) {
        pOwner->setTradeMode( bBikes ? ETM_BuyBikes : ETM_BuyCars );
        pOwner->setActiveList( this );
    }

    EbayList *pEbayList = GetEbayList();
    pEbayList->subscribe( &observer );
    pEbayList->clear( true );

    firstOfferIndex = -1;
    lastOfferIndex = -1;
    reset();

    bReselectPending = false;
    requestData( true, true );
}

void FlashListEBayOffers::close() {
    // FUN_0077ec20
    if ( pOwner != nullptr ) {
        pOwner->setActiveList( nullptr );
    }

    GetEbayList()->unsubscribe( &observer );
    bOpened = false;
}

void FlashListEBayOffers::sort( int32_t column ) {
    // FUN_0077fd10
    GetEbayList()->clear( true );

    firstOfferIndex = -1;
    lastOfferIndex = -1;
    reset();

    lastRequestedLine = -1;
    bReselectPending = true;
    reselectTimer = GetQueryRetryDelay();

    if ( pQuery == nullptr ) {
        return;
    }

    switch ( column ) {
    case 0:
        pQuery->Sort = 0;
        break;
    case 1:
        pQuery->Sort = 3;
        break;
    case 2:
        pQuery->Sort = 1;
        break;
    default:
        break;
    }

    pQuery->bReverse = bInverted;
    selectedLineChanged();
}

void FlashListEBayOffers::update( const int32_t listIndex,
                                  const int32_t lineIndex ) {
    // FUN_00780ec0
    if ( numOffers <= listIndex ) {
        setLineState( eLineState::FLLS_Empty, lineIndex );
        return;
    }

    EbayOffer &offer = GetEbayList()->getOffer( listIndex );
    if ( offer.ID == 0xffffffffu ) {
        showLineValidity( lineIndex, "disabled", false );
        setLineField(
            lineIndex, "name",
            gpDatabase->getStringByHashcode( 0, kSoldOutLineHashcode ) );
        setLineField( lineIndex, "type", "" );
        setLineFieldNonLinked( lineIndex, "price", "" );
        return;
    }

    if ( offer.OwnerID == GetDriver()->ID ) {
        setLineState( eLineState::FLLS_Red, lineIndex );
    } else {
        showLineValidity( lineIndex, "normal", true );
    }

    setLineField( lineIndex, "name", offer.Car.getConfig()->FullName.c_str() );

    DBEntryCarPhysicsData *pEntry =
        gpDatabase->getCarDBEntry( int32_t( offer.Car.getCarConfigID() ) );
    setLineFieldNonLinked( lineIndex, "type", GetCarGroupName( pEntry ) );

    char price[32];
    FormatMoneyString( price, int32_t( offer.Price ) );
    setLineFieldNonLinked( lineIndex, "price", price );
}

void FlashListEBayOffers::selectedLineChanged() {
    // FUN_0077eb70
    bReselectPending = true;
    reselectTimer = GetQueryRetryDelay();
    onOfferSelected();
}

void FlashListEBayOffers::refresh() {
    // FUN_0077fdb0
    GetEbayList()->clear( true );
    reset();

    bReselectPending = false;
    requestData( true, true );
}

void FlashListEBayOffers::onOfferSelected() {
    // FUN_0077ffe0
    showSelectedOffer( false );
}

void FlashListEBayOffers::onCountReceived() {
    // FUN_0077f8d0
    if ( !bAwaitingCount ) {
        return;
    }

    const int32_t numServerOffers = GetEbayList()->NumOffers;
    if ( numServerOffers == -1 || numServerOffers == -2 ) {
        return;
    }

    bAwaitingCount = false;
    bAwaitingOffers = true;
    numOffers = numServerOffers;

    if ( numServerOffers == 0 ) {
        SetTradeEmptyArray( pOwner, true );
        lastOfferIndex = 0;
        bAwaitingOffers = false;
        reset();
        return;
    }

    SetTradeEmptyArray( pOwner, false );
    if ( numOffers <= lastOfferIndex ) {
        lastOfferIndex = numOffers - 1;
    }

    const int32_t selectedLine = selectedLineIndex + firstLine;
    const int32_t windowStart =
        std::max( selectedLine - kOfferWindowHalfSize, 0 );
    const int32_t windowEnd =
        std::min( numOffers - 1, selectedLine + kOfferWindowHalfSize );
    if ( pQuery != nullptr ) {
        pQuery->bWantBikes = bBikes;
        OTDU_UNIMPLEMENTED_ONCE; // TODO: ONLINE (FUN_00884350 offers query)
        (void)windowStart;
        (void)windowEnd;
    }
}

void FlashListEBayOffers::onDataUpdate( const uint32_t param ) {
    // FUN_0077faa0
    if ( param == 1u ) {
        onCountReceived();
        return;
    }

    if ( param != 2u ) {
        return;
    }

    bAwaitingOffers = false;

    EbayList *pEbayList = GetEbayList();
    firstOfferIndex = pEbayList->FirstOfferIndex;
    lastOfferIndex = pEbayList->LastOfferIndex;
    numOffers = pEbayList->NumOffers;
    SetTradeEmptyArray( pOwner, numOffers == 0 );

    onOfferSelected();
    reset();
}

FlashListEBayHistory::FlashListEBayHistory()
    : FlashListEBay(),
      firstOfferIndex( -1 ),
      lastOfferIndex( -1 ),
      numOffers( 0 ),
      bAwaitingCount( false ),
      bReselectPending( false ),
      reselectTimer( 0.0f ),
      lastRequestedLine( -1 ),
      refreshTimer( 0.0f ),
      bPurchases( true ) {
    // FUN_0077ed30
}

FlashListEBayHistory::~FlashListEBayHistory() {
    // FUN_0077ed00
}

void FlashListEBayHistory::requestData( const bool bForce, const bool bClear ) {
    // FUN_0077f050
    const int32_t selectedLine = selectedLineIndex + firstLine;
    if ( lastRequestedLine == selectedLine && !bForce ) {
        return;
    }

    firstOfferIndex = 0;
    lastOfferIndex = kOfferPageSize;
    bAwaitingCount = true;

    if ( bClear ) {
        numOffers = 0;
        SetTradeEmptyArray( pOwner, true );
    }

    lastRequestedLine = selectedLine;
    if ( pQuery != nullptr ) {
        pQuery->bWantBikes = bBikes;
        if ( !bPurchases ) {
            OTDU_UNIMPLEMENTED_ONCE; // TODO: ONLINE (FUN_008840f0 sales count)
        } else {
            OTDU_UNIMPLEMENTED_ONCE; // TODO: ONLINE (FUN_00884290 purchases
                                     // count)
        }
        refreshTimer = GetQueryRefreshDelay();
    }
}

void FlashListEBayHistory::tick( float deltaTime, float totalTime ) {
    // FUN_00780120
    FlashListEBay::tick( deltaTime, totalTime );

    if ( pOwner == nullptr ) {
        return;
    }

    const eEbayTradeMode mode = pOwner->getTradeMode();
    const bool bActive = ( mode == ETM_Purchases && bPurchases ) ||
                         ( mode == ETM_Sales && !bPurchases );
    if ( !bActive ) {
        return;
    }

    if ( bReselectPending ) {
        reselectTimer -= deltaTime;
        if ( reselectTimer < 0.0f ) {
            const int32_t selectedLine = selectedLineIndex + firstLine;
            bReselectPending = false;
            if ( lastRequestedLine != selectedLine ) {
                lastRequestedLine = selectedLine;
                firstOfferIndex = 0;
                lastOfferIndex = kOfferPageSize;
                bAwaitingCount = true;

                if ( pQuery != nullptr ) {
                    pQuery->bWantBikes = bBikes;
                    if ( !bPurchases ) {
                        OTDU_UNIMPLEMENTED_ONCE; // TODO: ONLINE (FUN_008840f0
                                                 // sales count)
                    } else {
                        OTDU_UNIMPLEMENTED_ONCE; // TODO: ONLINE (FUN_00884290
                                                 // purchases count)
                    }
                    refreshTimer = GetQueryRefreshDelay();
                }
            }
        }
    }

    refreshTimer -= deltaTime;
    if ( refreshTimer < 0.0f ) {
        requestData( true, false );
    }
}

int32_t FlashListEBayHistory::getListLength() const {
    // FUN_0077ead0
    return numOffers;
}

void FlashListEBayHistory::open() {
    // FUN_0077fc60
    numOffers = 1;
    FlashList::open();

    if ( pOwner != nullptr ) {
        pOwner->setTradeMode( bPurchases ? ETM_Purchases : ETM_Sales );
        pOwner->setActiveList( this );
    }

    EbayList *pEbayList = GetEbayList();
    pEbayList->subscribe( &observer );
    pEbayList->clear( true );
    reset();

    bReselectPending = false;
    requestData( true, true );
}

void FlashListEBayHistory::close() {
    // FUN_0077eb90
    if ( pOwner != nullptr ) {
        pOwner->setActiveList( nullptr );
    }

    GetEbayList()->unsubscribe( &observer );
    bOpened = false;
}

void FlashListEBayHistory::sort( int32_t column ) {
    // FUN_0077fb40
    GetEbayList()->clear( true );

    firstOfferIndex = -1;
    lastOfferIndex = -1;
    reset();

    lastRequestedLine = -1;
    bReselectPending = true;
    reselectTimer = GetQueryRetryDelay();

    if ( pQuery == nullptr ) {
        return;
    }

    static constexpr int32_t kPurchasesSorts[4] = { 0, 3, 1, 2 };
    static constexpr int32_t kSalesSorts[4] = { 4, 0, 1, 2 };
    if ( 0 <= column && column < 4 ) {
        pQuery->Sort =
            bPurchases ? kPurchasesSorts[column] : kSalesSorts[column];
    }

    pQuery->bReverse = bInverted;
    selectedLineChanged();
}

void FlashListEBayHistory::update( const int32_t listIndex,
                                   const int32_t lineIndex ) {
    // FUN_007802a0
    if ( numOffers <= listIndex ) {
        setLineState( eLineState::FLLS_Empty, lineIndex );
        return;
    }

    EbayOffer &offer = GetEbayList()->getOffer( listIndex );
    if ( offer.ID == 0xffffffffu ) {
        showLineValidity( lineIndex, "disabled", false );
        setLineField(
            lineIndex, "name",
            gpDatabase->getStringByHashcode( 0, kSoldOutLineHashcode ) );
        setLineField( lineIndex, "price", "" );
        setLineFieldNonLinked( lineIndex, "date", "" );
        setLineFieldNonLinked( lineIndex, "type", "" );
        setLineFieldNonLinked( lineIndex, "status", "" );
        return;
    }

    showLineValidity( lineIndex, "normal", true );
    setLineField( lineIndex, "name", offer.Car.getConfig()->FullName.c_str() );

    char text[32];
    FormatMoneyString( text, int32_t( offer.Price ) );
    setLineFieldNonLinked( lineIndex, "price", text );

    DBEntryCarPhysicsData *pEntry =
        gpDatabase->getCarDBEntry( int32_t( offer.Car.getCarConfigID() ) );
    setLineFieldNonLinked( lineIndex, "type", GetCarGroupName( pEntry ) );

    int32_t year = 0;
    int32_t month = 0;
    int32_t day = 0;
    UnpackSaveDate( offer.Date, year, month, day );
    FormatSaveDate( text, sizeof( text ), year, month, day );
    setLineFieldNonLinked( lineIndex, "date", text );

    if ( bPurchases ) {
        return;
    }

    const uint64_t statusHashcode = ( offer.ExtraID == 0xffffffffu )
                                        ? kOfferOpenHashcode
                                        : kOfferClosedHashcode;
    setLineFieldNonLinked(
        lineIndex, "status",
        gpDatabase->getStringByHashcode( 0, statusHashcode ) );
}

void FlashListEBayHistory::selectedLineChanged() {
    // FUN_0077eb50
    bReselectPending = true;
    reselectTimer = GetQueryRetryDelay();
    onOfferSelected();
}

void FlashListEBayHistory::refresh() {
    // FUN_0077fc30
    GetEbayList()->clear( true );
    reset();

    bReselectPending = false;
    requestData( true, true );
}

void FlashListEBayHistory::onOfferSelected() {
    // FUN_0077fea0
    showSelectedOffer( true );
}

void FlashListEBayHistory::onCountReceived() {
    // FUN_0077ee50
    if ( !bAwaitingCount ) {
        return;
    }

    const int32_t numServerOffers = GetEbayList()->NumOffers;
    if ( numServerOffers == -1 || numServerOffers == -2 ) {
        numOffers = numServerOffers;
        SetTradeEmptyArray( pOwner, false );
        reset();
        return;
    }

    numOffers = numServerOffers;
    bAwaitingCount = false;

    if ( numServerOffers == 0 ) {
        lastOfferIndex = 0;
        SetTradeEmptyArray( pOwner, true );
        reset();
        return;
    }

    SetTradeEmptyArray( pOwner, false );
    if ( numOffers <= lastOfferIndex ) {
        lastOfferIndex = numOffers - 1;
    }

    const int32_t selectedLine = selectedLineIndex + firstLine;
    const int32_t windowStart =
        std::max( selectedLine - kOfferWindowHalfSize, 0 );
    const int32_t windowEnd =
        std::min( numOffers, selectedLine + kOfferWindowHalfSize );
    pQuery->bWantBikes = bBikes;
    if ( bPurchases ) {
        OTDU_UNIMPLEMENTED_ONCE; // TODO: ONLINE (FUN_008841b0 purchases query)
    } else {
        OTDU_UNIMPLEMENTED_ONCE; // TODO: ONLINE (FUN_00884020 sales query)
    }
    (void)windowStart;
    (void)windowEnd;
}

void FlashListEBayHistory::onDataUpdate( const uint32_t param ) {
    // FUN_0077efb0
    if ( param == 1u ) {
        onCountReceived();
        return;
    }

    if ( param != 2u ) {
        return;
    }

    EbayList *pEbayList = GetEbayList();
    firstOfferIndex = pEbayList->FirstOfferIndex;
    lastOfferIndex = pEbayList->LastOfferIndex;
    numOffers = pEbayList->NumOffers;
    SetTradeEmptyArray( pOwner, numOffers == 0 );

    onOfferSelected();
    reset();
}

FlashListEBayGarage::FlashListEBayGarage()
    : FlashListEBay(),
      order{},
      garageSlots{},
      pCarEntries{},
      prices{},
      numCars( -1 ),
      refreshTimer( 0.0f ) {
    // FUN_0077edc0
}

FlashListEBayGarage::~FlashListEBayGarage() {
    // FUN_0077ed70
}

int32_t FlashListEBayGarage::collectCars() {
    // FUN_0077f170
    if ( pQuery == nullptr ) {
        return 0;
    }

    Garage *pGarage = GetGarage();
    const int32_t numGarageCars = pGarage->getNumCars();

    int32_t numCollected = 0;
    for ( int32_t i = 0; i < numGarageCars; i++ ) {
        int32_t remaining = ( i < 0 || i > Garage::kNumSavedCarSlots ) ? 0 : i;
        GarageSlot *pSlot = &pGarage->pCars[0];
        for ( int32_t slotIndex = 0; slotIndex <= Garage::kNumSavedCarSlots;
              slotIndex++ ) {
            if ( !pGarage->pCars[slotIndex].isValid() ) {
                continue;
            }
            if ( remaining == 0 ) {
                pSlot = &pGarage->pCars[slotIndex];
                break;
            }
            remaining--;
        }

        CarDesc car;
        car = *pSlot;

        const int32_t configID = int32_t( car.getCarConfigID() );
        DBEntryCarPhysicsData *pEntry = gpDatabase->getCarDBEntry( configID );
        if ( bBikes != gpDatabase->isBike( pEntry ) ) {
            continue;
        }

        if ( !pQuery->isAnyBrand() && strcmp( car.getConfig()->Name.c_str(),
                                              pQuery->Brand.pAsString ) != 0 ) {
            continue;
        }

        if ( pQuery->ModelCarID != -1 && pQuery->ModelCarID != configID ) {
            continue;
        }

        if ( pQuery->MaxPrice != -1 &&
             float( pQuery->MaxPrice ) <
                 gpDatabase->getCarResalePrice( configID, car.paintIndex,
                                                car.rimID, car.interiorID ) ) {
            continue;
        }

        int32_t garageSlot = 0;
        for ( int32_t slotIndex = 0; slotIndex <= Garage::kNumSavedCarSlots;
              slotIndex++ ) {
            if ( pGarage->pCars[slotIndex].getCarID() == car.getCarID() ) {
                garageSlot = slotIndex;
                break;
            }
        }

        garageSlots[numCollected] = garageSlot;
        pCarEntries[numCollected] = pEntry;
        prices[numCollected] = uint32_t( gpDatabase->getCarPrice(
            pEntry, car.paintIndex, car.rimID, car.interiorID ) );
        numCollected++;
    }

    return numCollected;
}

bool FlashListEBayGarage::compareCars( const int32_t carA,
                                       const int32_t carB ) const {
    // FUN_0077f490
    Garage *pGarage = GetGarage();

    CarDesc garageCarA;
    garageCarA = pGarage->pCars[garageSlots[carA]];
    DBEntryCarPhysicsData *pEntryA = pCarEntries[carA];

    CarDesc garageCarB;
    garageCarB = pGarage->pCars[garageSlots[carB]];
    DBEntryCarPhysicsData *pEntryB = pCarEntries[carB];

    switch ( selectedColumnIndex ) {
    case 0:
        return strcmp( garageCarA.getConfig()->FullName.c_str(),
                       garageCarB.getConfig()->FullName.c_str() ) >= 0;
    case 1:
        return prices[carA] >= prices[carB];
    case 2:
        if ( pEntryA == nullptr || pEntryB == nullptr ) {
            return true;
        }
        return strcmp( pEntryA->CarGroup.pAsString,
                       pEntryB->CarGroup.pAsString ) >= 0;
    default:
        return false;
    }
}

void FlashListEBayGarage::rebuild() {
    // FUN_0077f700
    for ( int32_t i = 0; i < kNumSlots; i++ ) {
        order[i] = i;
    }

    numCars = collectCars();
    if ( numCars >= kNumSlots ) {
        numCars = kNumSlots;
    }

    SetTradeEmptyArray( pOwner, numCars == 0 );

    for ( int32_t i = 1; i - 1 < numCars - 1; i++ ) {
        int32_t &entryA = order[i - 1];
        for ( int32_t j = i; j < numCars; j++ ) {
            int32_t &entryB = order[j];
            const bool bAGreaterOrEqual = compareCars( entryA, entryB );
            if ( bAGreaterOrEqual != bInverted ) {
                std::swap( entryA, entryB );
            }
        }
    }
}

void FlashListEBayGarage::showSelectedCar() {
    // FUN_00780b70
    const int32_t entry = order[selectedLineIndex + firstLine];

    Garage *pGarage = GetGarage();
    if ( pGarage->getNumCarsOfKind( 0ull, bBikes ) <= entry ) {
        return;
    }

    GarageSlot *pCar = pGarage->getCarOfKindAtIndex( entry, 0ull, bBikes );

    CarDesc &previewCar = pOwner->getPreviewCar();
    previewCar = *pCar;
    if ( pOwner != nullptr ) {
        pOwner->getPreviewCarPanel().watch( &previewCar );
    }

    ShowPreviewCarData( pOwner );

    const int32_t configID = int32_t( previewCar.getCarConfigID() );
    char price[32];
    FormatMoneyString( price, int32_t( gpDatabase->getCarPrice(
                                  configID, previewCar.paintIndex,
                                  previewCar.rimID, previewCar.interiorID ) ) );
    pOwner->getFlashPlayer()->setVariableValueNonLinked( "/:BuyPrice", price );

    FormatMoneyString( price, int32_t( gpDatabase->getCarResalePrice(
                                  configID, previewCar.paintIndex,
                                  previewCar.rimID, previewCar.interiorID ) ) );
    pOwner->getFlashPlayer()->setVariableValueNonLinked( "/:SalePrice", price );

    previewCarID = configID;
}

void FlashListEBayGarage::tick( float deltaTime, float totalTime ) {
    // FUN_00780ac0
    FlashListEBay::tick( deltaTime, totalTime );

    if ( pOwner == nullptr ) {
        return;
    }

    const eEbayTradeMode mode = pOwner->getTradeMode();
    const bool bActive = ( mode == ETM_SellCars && !bBikes ) ||
                         ( mode == ETM_SellBikes && bBikes );
    if ( !bActive ) {
        return;
    }

    refreshTimer -= deltaTime;
    if ( refreshTimer < 0.0f ) {
        OTDU_UNIMPLEMENTED_ONCE; // TODO: ONLINE (FUN_00884b90 with the
                                 // driver ID)
        refreshTimer = GetQueryRefreshDelay();
    }
}

int32_t FlashListEBayGarage::getListLength() const {
    // FUN_0077eb10
    if ( pQuery == nullptr ) {
        return 0;
    }
    return numCars;
}

void FlashListEBayGarage::open() {
    // FUN_00781120
    if ( pOwner != nullptr ) {
        pOwner->setTradeMode( bBikes ? ETM_SellBikes : ETM_SellCars );
        pOwner->setActiveList( this );
    }

    refreshTimer = GetQueryRefreshDelay();
    rebuild();
    FlashList::open();

    GetDriver()->subscribe( &observer );
    GetGarage()->subscribe( &observer );

    if ( pOwner != nullptr ) {
        showSelectedCar();
    }
}

void FlashListEBayGarage::close() {
    // FUN_0077ebd0
    if ( pOwner != nullptr ) {
        pOwner->setActiveList( nullptr );
    }

    GetDriver()->unsubscribe( &observer );
    GetGarage()->unsubscribe( &observer );
    bOpened = false;
}

void FlashListEBayGarage::sort( int32_t column ) {
    // FUN_0077f880
    rebuild();
    reset();
    selectedLineChanged();
}

void FlashListEBayGarage::update( const int32_t listIndex,
                                  const int32_t lineIndex ) {
    // FUN_007805a0
    if ( getListLength() <= listIndex ) {
        setLineState( eLineState::FLLS_Empty, lineIndex );
        return;
    }

    if ( pQuery == nullptr ) {
        return;
    }

    Garage *pGarage = GetGarage();
    Driver *pDriver = GetDriver();

    int32_t numSellableCars = 0;
    if ( pGarage->pCars[Garage::kNumSavedCarSlots].state ==
         CarDesc::eState::CDS_Rental ) {
        for ( int32_t i = 0; i < Garage::kNumSavedCarSlots; i++ ) {
            GarageSlot &slot = pGarage->pCars[i];
            if ( slot.isValid() &&
                 slot.state != CarDesc::eState::CDS_PutOnEbay ) {
                numSellableCars++;
            }
        }
    }

    const int32_t entry = order[listIndex];

    CarDesc car;
    car = pGarage->pCars[garageSlots[entry]];
    DBEntryCarPhysicsData *pCarEntry = pCarEntries[entry];

    const bool bIsCurrentCar =
        car.getCarID() == pGarage->pCars[pDriver->CurrentCar].getCarID();
    if ( bIsCurrentCar ) {
        showLineValidity( lineIndex, "gold", false );
    } else if ( ( ( pCarEntry->BitField >> 5 ) & 1 ) != 0 ) {
        showLineValidity( lineIndex, "blue", true );
    } else if ( car.state == CarDesc::eState::CDS_Invalid ||
                car.state == CarDesc::eState::CDS_PutOnEbay ||
                numSellableCars == 1 ) {
        setLineState( eLineState::FLLS_Red, lineIndex );
    } else {
        showLineValidity( lineIndex, "normal", true );
    }

    const bool bShowCurrentCar =
        pDriver->isValid() &&
        car.getCarID() == pGarage->pCars[pDriver->CurrentCar].getCarID();
    playLineSubClip( lineIndex, "current_car", bShowCurrentCar ? 1 : 0 );

    setLineField( lineIndex, "name", car.getConfig()->FullName.c_str() );

    DBEntryCarPhysicsData *pEntry =
        gpDatabase->getCarDBEntry( int32_t( car.getCarConfigID() ) );
    setLineFieldNonLinked( lineIndex, "type", GetCarGroupName( pEntry ) );

    char text[32];
    snprintf( text, sizeof( text ), "%d",
              int32_t( float( car.getPackLevel() ) * kUpgradeLevelToPercent ) );
    setLineFieldNonLinked( lineIndex, "upgrade", text );

    FormatMoneyString( text, int32_t( prices[entry] ) );
    setLineFieldNonLinked( lineIndex, "price", text );

    playLineSubClip( lineIndex, "status",
                     car.state == CarDesc::eState::CDS_Corrupted ? 1 : 0 );
}

void FlashListEBayGarage::selectedLineChanged() {
    // FUN_007810e0
    if ( pOwner == nullptr ) {
        return;
    }

    pOwner->detachOfferPanel();
    showSelectedCar();
}

void FlashListEBayGarage::refresh() {
    // FUN_0077f8c0
    rebuild();
    reset();
}

void FlashListEBayGarage::onDataUpdate( const uint32_t param ) {
    // FUN_0077f8a0
    rebuild();
    reset();
}
