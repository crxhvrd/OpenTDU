#pragma once

#include <array>

#include "core/notifier.h"
#include "flash/entities/flash_panel.h"
#include "game/scenaric/gs_scenaric.h"
#include "game/vehicle/car_desc.h"

#include "flash_list_ebay.h"
#include "scene_trading.h"

struct FlashMessage;
struct FlashRECT;
class DrawList;
class EntityCinematic;
class FlashPlayer;
class FlashResource;
class GMLiving;
class MngFlash;
struct Material;

// House e-trade (eBay) menu
class TradingMenu : public Notifier,
                    public CinematicPlayer,
                    public IEbayListOwner {
public:
    enum eCinematic : int32_t {
        TMC_Intro = 0,     // C_INTRO
        TMC_IntroLoop = 1, // C_INT_L
        TMC_Car = 2,       // C_CAR
        TMC_CarBack = 3,   // C_CAR_BACK
        TMC_Count
    };

public:
    inline GMLiving *getLiving() const { return pLiving; }
    inline void setLiving( GMLiving *pGameMode ) { pLiving = pGameMode; }
    inline void setFlashPlayer( FlashPlayer *pPlayer ) {
        pFlashPlayer = pPlayer;
    }
    inline void setFlashManager( MngFlash *pManager ) { pMngFlash = pManager; }
    inline EntityCinematic *getCinematic( const eCinematic cinematic ) const {
        return cinematics[cinematic];
    }
    inline bool isOpen() const { return bOpen; }
    inline SceneTrading &getScene() { return scene; }

public:
    TradingMenu();
    ~TradingMenu();

    void setupTables();
    void tick( const float deltaTime );
    void detachObservers();
    void terminate();
    bool onMessage( FlashMessage *pMessage );

    void onUpdate( uint32_t param ) override;
    bool executeAction( int32_t param_2,
                        void *param_3,
                        void *pData = nullptr ) override;

    FlashPlayer *getFlashPlayer() const override { return pFlashPlayer; }
    eEbayTradeMode getTradeMode() const override { return tradeMode; }
    void setTradeMode( const eEbayTradeMode mode ) override {
        tradeMode = mode;
    }
    void setActiveList( FlashListEBay *pList ) override { pActiveList = pList; }
    CarDesc &getPreviewCar() override { return previewCar; }
    FlashPanelCarSpecs &getPreviewCarPanel() override {
        return previewCarPanel;
    }
    void watchOffer( EbayOffer *pOffer ) override;
    void detachOfferPanel() override;

    bool playCinematic( const eCinematic cinematic );
    void onRequestFailed( const int32_t requestID );

private:
    struct DownloadPager {
        char Descriptions[3][0x180];
        int32_t Page;
        int32_t NumPages;
        int32_t NumDownloads;

        void reset();
        void open( FlashPlayer *pPlayer );
        void nextPage( FlashPlayer *pPlayer );
        void previousPage( FlashPlayer *pPlayer );
        void refresh( FlashPlayer *pPlayer ) const;
    };

private:
    void open();
    void close();
    void readFilterInputs();
    void refreshFilterLabels();
    void cycleModel( const bool bNext );
    void cycleBrand( const bool bNext );
    void refreshHelp();
    void testBuy();
    void buyOffer();
    void sellCar();
    void removeOffer();
    void showOfferCar();
    void showTradingError( const uint64_t hashcode );
    void unlockSellCar();
    void lockSellCar();
    int32_t getSelectedOfferIndex() const;
    bool isModelSelectable( const DBEntryCarPhysicsData &car ) const;
    bool isBrandSelectable( const DBEntryBrand *pBrand ) const;

    static void PhotoClipCallback( FlashRECT *pViewport,
                                   float depth,
                                   DrawList *pDrawList,
                                   void *pUser,
                                   FlashResource *pResource,
                                   const char *pClipName );

private:
    int32_t buyRequestID;
    int32_t sellRequestID;
    CarDesc previewCar;
    FlashPanelCarSpecs previewCarPanel;
    FlashPanelEbayOffer offerPanel;
    Material *pPhotoMaterial;
    std::array<EbayQuery, 4> queries;
    EbayQuery savedQuery;
    FlashPlayer *pFlashPlayer;
    FlashListEBayOffers carOffersList;
    FlashListEBayOffers bikeOffersList;
    FlashListEBayGarage garageCarsList;
    FlashListEBayGarage garageBikesList;
    FlashListEBayHistory purchasesList;
    FlashListEBayHistory salesList;
    MngFlash *pMngFlash;
    std::array<EntityCinematic *, TMC_Count> cinematics;
    EntityCinematic *pActiveCinematic;
    eEbayTradeMode tradeMode;
    FlashListEBay *pActiveList;
    SceneTrading scene;
    GMLiving *pLiving;
    bool bOpen;
    int32_t carCountRequestID;
    int32_t bikeCountRequestID;
    int32_t numCarOffers;
    int32_t numBikeOffers;
    char helpText[0x40];
    bool bSavingBeforeSell;
    int32_t sellSlotIndex;
    int32_t sellPrice;
    bool bSavingBeforeBuy;
    uint32_t buyOfferID;
    int16_t buyGarageSlot;
    int32_t buyPrice;
    DownloadPager downloads;
    Matrix4x4f_t photoCarTransform;
};
