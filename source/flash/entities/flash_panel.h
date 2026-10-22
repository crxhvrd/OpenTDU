#pragma once

#include "core/notifier.h"

class CarDesc;
class EbayOffer;
class FlashPlayer;

class FlashPanel : public Notifier {
public:
    FlashPanel();
    virtual ~FlashPanel();
    virtual void tick() = 0;

    void onUpdate( uint32_t /*param*/ ) override { tick(); }
    void detach();

    FlashPlayer *pMovie;
    Notifiable *pTarget;
};

// Online / e-trade / family-settings state.
class FlashPanelOnline : public FlashPanel {
public:
    FlashPanelOnline();
    ~FlashPanelOnline();

    void tick() override;
};

// Player money / coupons.
class FlashPanelMoney : public FlashPanel {
public:
    FlashPanelMoney();
    ~FlashPanelMoney();

    void tick() override;
};

// Car rental timer.
class FlashPanelRental : public FlashPanel {
public:
    FlashPanelRental();
    ~FlashPanelRental();

    void tick() override;
};

// Number of messages in the player mailbox.
class FlashPanelNews : public FlashPanel {
public:
    FlashPanelNews();
    ~FlashPanelNews();

    void tick() override;
};

// Current car name / group / upgrade level.
class FlashPanelInfoCurCar : public FlashPanel {
public:
    FlashPanelInfoCurCar();
    ~FlashPanelInfoCurCar();

    void tick() override;

private:
    char pCarName[64];
};

// Car specs sheet (odometer, upgrade pack, brand logo, ...) for garage and
// dealership
class FlashPanelCarSpecs : public FlashPanel {
public:
    inline CarDesc *getCar() const { return pCar; }

public:
    FlashPanelCarSpecs();
    ~FlashPanelCarSpecs();

    void tick() override;
    void watch( CarDesc *pNewCar );
    void refresh( CarDesc *pDisplayedCar, const bool bUpgrade );

private:
    CarDesc *pCar;
    void *pUpgradeEntry;
    bool bPictureInstalled;
};

// e-trade offer details (car name, price, seller)
class FlashPanelEbayOffer : public FlashPanel {
public:
    inline void setOffer( EbayOffer *pNewOffer ) { pOffer = pNewOffer; }
    inline EbayOffer *getOffer() const { return pOffer; }

public:
    FlashPanelEbayOffer();
    ~FlashPanelEbayOffer();

    void tick() override;

private:
    EbayOffer *pOffer;
};

struct DBEntryCarPhysicsData;

void ShowCarDataGauges( FlashPlayer *pPlayer,
                        const char *pPanelPath,
                        DBEntryCarPhysicsData *pEntry );
