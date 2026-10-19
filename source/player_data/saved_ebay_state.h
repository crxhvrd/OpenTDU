#pragma once

#include "core/color.h"
#include "core/notifier.h"
#include "game/vehicle/car_desc.h"

// One car offer of the e-trade (eBay) marketplace, as downloaded from the
// server.
class EbayOffer : public Notifiable {
public:
    bool isValid() override { /* FUN_00416340 */ return ID != 0xffffffffu; }

public:
    EbayOffer();
    ~EbayOffer();

    void clear();

public:
    uint32_t ID;
    uint32_t ExtraID;
    uint32_t OwnerID;
    char pOwnerName[32];
    uint32_t CarID;
    uint32_t Price;
    int32_t PaintID;
    int32_t RimID;
    int32_t InteriorID;
    uint64_t PackHashcode;
    int32_t DeluxePaintType;
    ColorRGBA PaintDeluxeMain;
    ColorRGBA PaintDeluxeSec;
    ColorRGBA PaintDeluxeLogo;
    ColorRGBA PaintDeluxeSec2;
    float Odometer;
    uint32_t LastBid;
    uint32_t NumBids;
    uint32_t LastBidderID;
    char pLastBidderName[32];
    uint16_t Date;
    int32_t TimeLimit;
    int32_t YourLastBid;
    CarDesc Car;
    uint8_t pTimeLeft[6];
    char pStrTimeLeft[32];
    char pStrPrice[32];
};

// Window of offers matching the last e-trade query. NumOffers is -1 until the
// server answered a count query (-2 on error).
class EbayList : public Notifiable {
public:
    static constexpr int32_t kNumOffers = 128;

    struct QueryParameters {
        int32_t Make = -1;
        int32_t Archetype = -1;
    };

public:
    bool isValid() override { /* FUN_00416330 */ return NumOffers != -1; }

    inline EbayOffer &getOffer( int32_t index ) {
        if ( index < 0 || index >= kNumOffers ) {
            index = 0;
        }
        return Offers[index];
    }

public:
    EbayList();
    ~EbayList();

    void clear( const bool bClearOffers );

public:
    EbayOffer Offers[kNumOffers];
    QueryParameters Query;
    int32_t NumOffers;
    int32_t FirstOfferIndex;
    int32_t LastOfferIndex;
};
