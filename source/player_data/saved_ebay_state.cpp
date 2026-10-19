#include "shared.h"
#include "saved_ebay_state.h"

EbayOffer::EbayOffer()
    : Notifiable(),
      ID( 0xffffffffu ),
      ExtraID( 0xffffffffu ),
      OwnerID( 0xffffffffu ),
      pOwnerName{},
      CarID( 0xffffffffu ),
      Price( 0xffffffffu ),
      PaintID( -1 ),
      RimID( -1 ),
      InteriorID( -1 ),
      PackHashcode( 0ull ),
      DeluxePaintType( 0 ),
      PaintDeluxeMain( 0u ),
      PaintDeluxeSec( 0u ),
      PaintDeluxeLogo( 0u ),
      PaintDeluxeSec2( 0u ),
      Odometer( 0.0f ),
      LastBid( 0xffffffffu ),
      NumBids( 0xffffffffu ),
      LastBidderID( 0xffffffffu ),
      pLastBidderName{},
      Date( 0xffffu ),
      TimeLimit( -1 ),
      YourLastBid( -1 ),
      Car(),
      pTimeLeft{},
      pStrTimeLeft{},
      pStrPrice{} {
    // FUN_008946f0
    Car.clear();
}

EbayOffer::~EbayOffer() {}

void EbayOffer::clear() {
    // FUN_008946f0 (inlined in FUN_008943f0)
    YourLastBid = -1;
    InteriorID = -1;
    RimID = -1;
    PaintID = -1;
    TimeLimit = -1;
    LastBidderID = 0xffffffffu;
    NumBids = 0xffffffffu;
    LastBid = 0xffffffffu;
    Price = 0xffffffffu;
    CarID = 0xffffffffu;
    OwnerID = 0xffffffffu;
    ExtraID = 0xffffffffu;
    ID = 0xffffffffu;
    Date = 0xffffu;
    pOwnerName[0] = '\0';
    pLastBidderName[0] = '\0';
    Car.clear();
}

EbayList::EbayList()
    : Notifiable(),
      Query(),
      NumOffers( -1 ),
      FirstOfferIndex( 0 ),
      LastOfferIndex( 0 ) {
    // FUN_00895340
}

EbayList::~EbayList() {}

void EbayList::clear( const bool bClearOffers ) {
    // FUN_008943f0
    FirstOfferIndex = 0;
    LastOfferIndex = 0;
    NumOffers = -1;

    if ( bClearOffers ) {
        for ( EbayOffer &offer : Offers ) {
            offer.clear();
        }

        Query.Make = -1;
        Query.Archetype = -1;
    }
}
