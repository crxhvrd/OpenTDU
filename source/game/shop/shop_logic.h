#pragma once

#include "core/notifier.h"
#include "core/random/bcpl.h"
#include "game/scenaric/gs_scenaric.h"
#include "game/vehicle/car_desc.h"
#include "flash_list_trade_in.h"

class DrawList;
class FlashPlayer;
class FlashResource;
class GMShop;
struct FlashRECT;
class EntityCinematic;
struct FlashMessage;
struct DBEntryCarPack;

// Per-shop behaviour of GMShop. The instance is picked from the current spot
// type when the game mode initializes (see GMShop::createShopLogic).
class ShopLogic {
public:
    static constexpr int32_t kNumCarSlots = 0x80;

    enum eType : int32_t {
        SLT_Shop = 0,   // Car/bike dealers and tuning shops
        SLT_Paint = 1,  // ST_Unknown8 subtypes 1 and 9
        SLT_Rental = 2, // ST_Unknown8 subtypes 2 and 3
    };

public:
    inline void setOwner( GMShop *pShop ) { pOwner = pShop; }
    inline void setFlashPlayer( FlashPlayer *pPlayer ) {
        pFlashPlayer = pPlayer;
    }

public:
    ShopLogic();
    virtual ~ShopLogic() {}

    virtual eType getType() const = 0;
    virtual void onFlashReady() = 0;
    virtual void pause() {
        // FUN_0063f2b0
        // NOP
    }
    virtual void tick( float deltaTime, float totalTime ) = 0;
    virtual bool fillCarList( int32_t *pOutCarIDs ) = 0;
    virtual bool isReady( int32_t *pCarIDs ) {
        // FUN_0071ee10
        return true;
    }
    virtual bool usesClassicCarStock() const {
        // FUN_00630320
        return false;
    }
    virtual void onCarParkPopulated() {
        // FUN_0063f2b0
        // NOP
    }
    virtual int32_t getBuyOption() const {
        // FUN_0047a8a0
        return 0;
    }
    virtual bool onCinematicEvent( int32_t event,
                                   EntityCinematic *pCinematic,
                                   void *pData ) {
        // FUN_00609bc0
        return false;
    }
    virtual bool onMessage( FlashMessage *pMessage, FlashPlayer *pPlayer ) = 0;
    virtual void onCarSelected( int32_t carIndex ) {
        // FUN_00614df0
        // NOP
    }

protected:
    static bool FillCarListFromShop( int32_t *pOutCarIDs );

protected:
    FlashPlayer *pFlashPlayer;
    GMShop *pOwner;
};

// Car and bike dealers (ST_CarDealer, ST_BikeDealer). Also used when the
// game mode is entered without a valid spot.
class ShopLogicDealer : public ShopLogic {
public:
    static constexpr int32_t kNumGarageSlots = 0x280;
    static constexpr int32_t kNumColorSwatches = 20;

    enum eBuyOption : int32_t {
        BO_None = 0,
        BO_Summary = 1,
        BO_Rims = 2,
        BO_Paint = 3,
        BO_Interior = 4,
        BO_Confirm = 5,
    };

public:
    ShopLogicDealer();

    eType getType() const override {
        // FUN_0047a8a0
        return SLT_Shop;
    }
    void onFlashReady() override;
    void pause() override;
    void tick( float deltaTime, float totalTime ) override {
        // FUN_00721340
        // NOP
    }
    bool fillCarList( int32_t *pOutCarIDs ) override;
    bool isReady( int32_t *pCarIDs ) override;
    bool usesClassicCarStock() const override {
        // FUN_0042e7a0
        return bClassicCarStock;
    }
    int32_t getBuyOption() const override {
        // FUN_0042e7b0
        return buyOption;
    }
    bool onMessage( FlashMessage *pMessage, FlashPlayer *pPlayer ) override;
    void onCarSelected( int32_t carIndex ) override;

    void resetTradeIn();
    bool isTradedIn( const int32_t slot ) const;

private:
    inline bool isAnyCarTradedIn() const {
        return std::any_of( std::begin( tradeInSlots ),
                            std::end( tradeInSlots ),
                            []( const int32_t slot ) { return slot != -1; } );
    }

private:
    class GarageWatcher : public Notifier {
    public:
        GarageWatcher( ShopLogicDealer *pOwner );

        void onUpdate( uint32_t param ) override;

    private:
        ShopLogicDealer *pDealer;
    };

private:
    static void ColorSwatchCallback( FlashRECT *pRect,
                                     float deltaTime,
                                     DrawList *pDrawList,
                                     void *pUserData,
                                     FlashResource *pResource,
                                     const char *pClipName );
    void drawColorSwatch( DrawList *pDrawList,
                          FlashResource *pResource,
                          const char *pClipName );

    bool hasInteriors() const;
    void enterBuyScreen();
    void stepBuyOption( const bool bNext );
    void changeBuyOption();
    void setupColorSwatches( const int32_t option );
    void updateOptionPrices( const bool bBike );
    void evaluatePurchase();
    float getTradeInValue() const;
    uint32_t publishTradeInCredit();
    void addTradeIn();
    void removeTradeIn();
    void updateNoMoreDVDVehicle();

private:
    uint64_t colorSwatchHashes[kNumColorSwatches];
    int32_t newCarSlot;
    int32_t tradeInSlots[kNumGarageSlots];
    FlashListTradeIn tradeInList;
    int32_t buyOption;
    GarageWatcher garageWatcher;
    bool bClassicCarStock;
};

// Tuning shops (ST_AfterMarket)
class ShopLogicTuning : public ShopLogic {
public:
    ShopLogicTuning();

    eType getType() const override {
        // FUN_0047a8a0
        return SLT_Shop;
    }
    void onFlashReady() override;
    void pause() override;
    void tick( float deltaTime, float totalTime ) override {
        // FUN_009bb500
        // NOP
    }
    bool fillCarList( int32_t *pOutCarIDs ) override;
    void onCarParkPopulated() override;
    bool onMessage( FlashMessage *pMessage, FlashPlayer *pPlayer ) override;

private:
    // Chains the car loop cinematic once the car intro cinematic ends.
    class CinematicInterface : public CinematicPlayer {
    public:
        CinematicInterface( ShopLogicTuning *pOwner );

        bool executeAction( int32_t param_2,
                            void *param_3,
                            void *pData = nullptr ) override;

    private:
        ShopLogicTuning *pTuning;
    };

private:
    DBEntryCarPack *getManufacturerPack( int32_t index ) const;
    void refreshUpgradeScreen();
    void refreshMaxUpgrade();
    void refreshEnoughMoney();
    void buyUpgrade();
    void previewUpgrade();
    void leaveShop();

private:
    CinematicInterface cinematicInterface;
    CarDesc carDesc;
    SpotMap *pSpot;
    EntityCinematic *pCarIntroCinematic;
    EntityCinematic *pCarLoopCinematic;
};

// Paint shops (ST_Unknown8 subtypes 1 and 9)
class ShopLogicPaint : public ShopLogic {
public:
    static constexpr int32_t kNumColorSlots = 4;

public:
    ShopLogicPaint();

    eType getType() const override {
        // FUN_00423f20
        return SLT_Paint;
    }
    void onFlashReady() override;
    void pause() override;
    void tick( float deltaTime, float totalTime ) override;
    bool fillCarList( int32_t *pOutCarIDs ) override;
    bool onCinematicEvent( int32_t event,
                           EntityCinematic *pCinematic,
                           void *pData ) override;
    bool onMessage( FlashMessage *pMessage, FlashPlayer *pPlayer ) override;

private:
    // Drives the C_PAINT_CAR cinematic (HUD pause, interior preview, paint
    // commit and exit to free ride).
    class CinematicInterface : public CinematicPlayer {
    public:
        CinematicInterface( ShopLogicPaint *pOwner );

        bool executeAction( int32_t param_2,
                            void *param_3,
                            void *pData = nullptr ) override;

    private:
        ShopLogicPaint *pPaint;
    };

private:
    static void ColorSwatchCallback( FlashRECT *pRect,
                                     float deltaTime,
                                     DrawList *pDrawList,
                                     void *pUserData,
                                     FlashResource *pResource,
                                     const char *pClipName );
    static void SelectedColorCallback( FlashRECT *pRect,
                                       float deltaTime,
                                       DrawList *pDrawList,
                                       void *pUserData,
                                       FlashResource *pResource,
                                       const char *pClipName );

    void setDoorsOpen( const bool bOpen );
    int32_t refreshPaintName();
    void setupOrbitCamera();
    void refreshColors( const int32_t paintIndex, const bool bRandomInterior );
    void refreshPrice();
    void refreshEnoughMoney();
    void applyDeluxePaintPreview();

private:
    CinematicInterface cinematicInterface;
    EntityCinematic *pPaintCinematic;
    Matrix4x4f_t orbitCarTransform;
    SpotMap *pSpot;
    bool bHasColorStrips;
    bool bHasTwoTones;
    int32_t carConfigID;
    int32_t paintIndex;
    int32_t interiorID;
    bool bPaintStarted;
    RandomGen random;
    int32_t currentColorSlot;
    ColorRGBA colors[kNumColorSlots];
    ePaintType paintType;
    bool bPaletteCursorPending;
    bool bDeluxe;
    int32_t numColorSlots;
};

// Car rental (ST_Unknown8 subtypes 2 and 3)
class ShopLogicRental : public ShopLogic {
public:
    ShopLogicRental();

    eType getType() const override {
        // FUN_00441620
        return SLT_Rental;
    }
    void onFlashReady() override;
    void tick( float deltaTime, float totalTime ) override {
        // FUN_00407f80
        // NOP
    }
    bool fillCarList( int32_t *pOutCarIDs ) override;
    bool onMessage( FlashMessage *pMessage, FlashPlayer *pPlayer ) override;
    void onCarSelected( int32_t carIndex ) override;
};
