#pragma once

#include "game/gm_world.h"

#include "flash/entities/flash_panel.h"
#include "game/first_person_mode.h"
#include "game/mng_logo_display.h"
#include "game/vehicle/car_desc.h"
#include "render/mng_reflection.h"

#include "mng_color_palette.h"
#include "scene_shop.h"
#include "shop_logic.h"

class GMShop : public GameModeWorldBase {
public:
    inline ShopLogic *getShopLogic() const { return pShopLogic; }
    inline SceneShop &getScene() { return sceneShop; }
    inline int32_t getSelectedCarID() const { return selectedCarID; }
    inline int32_t getCarID( const int32_t slot ) const {
        return carIDs[ClampSlot( slot )];
    }
    inline int32_t getSelectedSlot() const { return previousCarIndex; }
    inline CarDesc &getSelectedCarDesc() { return selectedCarDesc; }
    inline EntityCinematic *getBuyCinematic() const { return pBuyCinematic; }
    inline FlashPanelCarSpecs &getCarSpecs() { return carSpecs; }

    static void OnCarFadeOut( void *pData, void *pSource );

public:
    GMShop();
    ~GMShop();

    void initialize() override;
    void reset() override;
    void tick( float deltaTime, float totalTime ) override;
    void draw( float deltaTime, float totalTime ) override;
    void pause() override;
    void destroy() override;

    bool onMessage( FlashMessage *pMessage ) override;
    bool executeAction( int32_t param_2,
                        void *param_3,
                        void *pData = nullptr ) override;
    bool isDrawnInFakeDOF() override {
        // FUN_0042e5c0
        return sceneShop.getContent()->isDrawnInDOFPass();
    }

    int32_t getNumShopCars() const;
    void selectCar( const int32_t carIndex );
    void updatePrices();
    void commitRims();
    void commitPaint();
    void commitInterior();

private:
    static inline int32_t ClampSlot( const int32_t slot ) {
        return ( slot < 0 || slot >= ShopLogic::kNumCarSlots ) ? 0 : slot;
    }
    static void OnCarFadeIn( void *pData, void *pSource );

    void createShopLogic();
    int32_t getShopCarSlot( int32_t carIndex ) const;
    bool isShopSlotAvailable( const int32_t slot ) const;
    void startCarCameraPath( const int32_t slot );
    void onSceneReady();
    void populateCarPark();
    void startTestDrive();
    void updateFirstPersonSpecs();
    void selectFirstPersonCar( const int32_t slot );
    void validateFirstPersonCar();

private:
    int32_t carIDs[ShopLogic::kNumCarSlots];
    int32_t carPositionGroups[ShopLogic::kNumCarSlots];

    MngReflection mngReflection;
    MngLogoDisplay mngLogoDisplay;
    MngColorPalette mngColorPalette;

    SceneShop sceneShop;

    int32_t previousCarIndex;
    int32_t selectedCarID;
    bool bShopFlag1; // DBEntryCarShop::Flags bit 1

    CarDesc selectedCarDesc;
    CarDesc hoveredCarDesc;
    FlashPanelCarSpecs carSpecs;

    FirstPersonMode firstPerson;
    std::vector<const JPH::Body *> parkedCarBodies;
    int32_t specsHoveredCar;
    CarDesc *pSpecsRestoreCar;
    bool bSpecsShowHoveredCar;
    float pendingValidateTime;

    ShopLogic *pShopLogic;
    EntityCinematic *pBuyCinematic;
    ScenaricBank bank;
    uint32_t randomSeed;
    bool bCarShop;
};
