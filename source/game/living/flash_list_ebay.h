#pragma once

#include "core/notifier.h"
#include "database/gs_database.h"
#include "flash/entities/flash_list.h"

class CarDesc;
class EbayOffer;
class FlashListEBay;
class FlashPanelCarSpecs;
class FlashPlayer;

// Search filter shared between the trading menu and its lists
struct EbayQuery {
    DBReference Brand;   // +0x00 (DBEntryBrand::BrandID; ~0 = any brand)
    uint64_t field_0x08; // +0x08
    int32_t ModelCarID;  // +0x10 (-1 = any model)
    int32_t MaxPrice;    // +0x14 (-1 = no limit)
    int32_t MinLevel;    // +0x18 (-1 = no limit)
    bool bWantBikes;     // +0x1c
    int32_t Sort;        // +0x20
    bool bReverse;       // +0x24
    char Group[3];       // +0x25

    inline bool isAnyBrand() const {
        return Brand.Hashcode == 0xffffffffffffffffull;
    }

    EbayQuery() { reset(); }

    void reset() {
        Brand.Hashcode = 0xffffffffffffffffull;
        field_0x08 = 0xffffffffffffffffull;
        ModelCarID = -1;
        MaxPrice = -1;
        MinLevel = -1;
        bWantBikes = false;
        Sort = 0;
        bReverse = false;
        Group[0] = '\0';
        Group[1] = '\0';
        Group[2] = '\0';
    }
};

// Trading menu mode set by the list being opened.
enum eEbayTradeMode : int32_t {
    ETM_None = 0,
    ETM_BuyCars = 1,
    ETM_BuyBikes = 2,
    ETM_SellCars = 3,
    ETM_SellBikes = 4,
    ETM_Purchases = 5,
    ETM_Sales = 6
};

// What the eBay lists need from the trading menu that owns them.
class IEbayListOwner {
public:
    virtual ~IEbayListOwner() {}

    virtual FlashPlayer *getFlashPlayer() const = 0;
    virtual eEbayTradeMode getTradeMode() const = 0;
    virtual void setTradeMode( const eEbayTradeMode mode ) = 0;
    virtual void setActiveList( FlashListEBay *pList ) = 0;
    virtual CarDesc &getPreviewCar() = 0;
    virtual FlashPanelCarSpecs &getPreviewCarPanel() = 0;
    virtual void watchOffer( EbayOffer *pOffer ) = 0;
    virtual void detachOfferPanel() = 0;
};

class FlashListEBayObserver : public Notifier {
public:
    FlashListEBayObserver( FlashListEBay *pOwner );

    void onUpdate( uint32_t param ) override;

private:
    FlashListEBay *pList;
};

class FlashListEBay : public FlashList {
public:
    inline void setQuery( EbayQuery *pNewQuery ) { pQuery = pNewQuery; }
    inline void setBikes( const bool bWantBikes ) { bBikes = bWantBikes; }
    inline void setOwner( IEbayListOwner *pNewOwner ) { pOwner = pNewOwner; }
    inline int32_t getPreviewCarID() const { return previewCarID; }
    inline int32_t getSelectedIndex() const {
        return selectedLineIndex + firstLine;
    }

public:
    FlashListEBay();

    void tick( float deltaTime, float totalTime ) override;
    void selectedLineChanged() override {
        // FUN_0077eb40
        // NOP
    }

    virtual void refresh() {
        // FUN_0063f2b0
        // NOP
    }
    virtual void onDataUpdate( const uint32_t param ) = 0;

protected:
    void playLineSubClip( const int32_t lineIndex,
                          const char *pSubClip,
                          const int32_t frame );
    void showLineValidity( const int32_t lineIndex,
                           const char *pFrame,
                           const bool bValid );
    void showSelectedOffer( const bool bShowSoldState );

protected:
    FlashListEBayObserver observer;
    EbayQuery *pQuery;
    bool bBikes;
    IEbayListOwner *pOwner;
    int32_t previewCarID;
};

// Marketplace offers ('ebca' cars / 'ebbi' bikes).
class FlashListEBayOffers : public FlashListEBay {
public:
    FlashListEBayOffers();
    ~FlashListEBayOffers();

    void tick( float deltaTime, float totalTime ) override;
    int32_t getListLength() const override;
    void open() override;
    void close() override;
    void sort( int32_t column ) override;
    void update( const int32_t listIndex, const int32_t lineIndex ) override;
    void selectedLineChanged() override;
    void refresh() override;
    void onDataUpdate( const uint32_t param ) override;

    void requestData( const bool bForce, const bool bClear );

private:
    void onOfferSelected();
    void onCountReceived();

private:
    bool bAwaitingCount;
    bool bAwaitingOffers;
    int32_t firstOfferIndex;
    int32_t lastOfferIndex;
    int32_t numOffers;
    bool bReselectPending;
    float reselectTimer;
    int32_t lastRequestedLine;
    float refreshTimer;
};

// The player's own trades ('ebpu' purchases / 'ebse' sales).
class FlashListEBayHistory : public FlashListEBay {
public:
    inline void setPurchases( const bool bShowPurchases ) {
        bPurchases = bShowPurchases;
    }

public:
    FlashListEBayHistory();
    ~FlashListEBayHistory();

    void tick( float deltaTime, float totalTime ) override;
    int32_t getListLength() const override;
    void open() override;
    void close() override;
    void sort( int32_t column ) override;
    void update( const int32_t listIndex, const int32_t lineIndex ) override;
    void selectedLineChanged() override;
    void refresh() override;
    void onDataUpdate( const uint32_t param ) override;

    void requestData( const bool bForce, const bool bClear );

private:
    void onOfferSelected();
    void onCountReceived();

private:
    int32_t firstOfferIndex;
    int32_t lastOfferIndex;
    int32_t numOffers;
    bool bAwaitingCount;
    bool bReselectPending;
    float reselectTimer;
    int32_t lastRequestedLine;
    float refreshTimer;
    bool bPurchases;
};

// Garage cars the player can put on sale ('seca' cars / 'sebi' bikes).
class FlashListEBayGarage : public FlashListEBay {
public:
    static constexpr int32_t kNumSlots = 0x280;

public:
    inline int32_t getSelectedGarageSlot() const {
        return garageSlots[order[getSelectedIndex()]];
    }

    FlashListEBayGarage();
    ~FlashListEBayGarage();

    void tick( float deltaTime, float totalTime ) override;
    int32_t getListLength() const override;
    void open() override;
    void close() override;
    void sort( int32_t column ) override;
    void update( const int32_t listIndex, const int32_t lineIndex ) override;
    void selectedLineChanged() override;
    void refresh() override;
    void onDataUpdate( const uint32_t param ) override;

private:
    int32_t collectCars();
    bool compareCars( const int32_t carA, const int32_t carB ) const;
    void rebuild();
    void showSelectedCar();

private:
    int32_t order[kNumSlots];
    int32_t garageSlots[kNumSlots];
    DBEntryCarPhysicsData *pCarEntries[kNumSlots];
    uint32_t prices[kNumSlots];
    int32_t numCars;
    float refreshTimer;
};
