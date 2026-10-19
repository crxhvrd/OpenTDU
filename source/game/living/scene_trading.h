#pragma once

#include "filesystem/bank.h"
#include "game/car_showcase/mng_showcase_car_control.h"
#include "game/scene_car_park.h"
#include "game/scene_interior.h"
#include "game/vehicle/car_desc.h"
#include "render/camera/camera_game.h"
#include "render/gs_render_helper.h"
#include "render/render_scene.h"

class TradingMenu;

// eBay showroom (Interior/IEtrade.bnk)
class SceneTrading {
public:
    inline SceneInterior &getContent() { return content; }
    inline SceneCarPark &getCarPark() { return carPark; }
    inline Bank &getInteriorBank() { return interiorBank; }
    inline MngShowCaseCarControl &getShowcaseControl() {
        return showcaseControl;
    }
    inline CarDesc &getPreviewCar() { return previewCar; }
    inline CameraGame &getCamera() { return camera; }
    inline bool isLoaded() const { return bLoaded; }
    inline void setMenu( TradingMenu *pTradingMenu ) { pMenu = pTradingMenu; }

public:
    SceneTrading();

    void load();
    void terminate();
    void tick( const float deltaTime );
    void enter();
    void leave();

    void clearPreviewCar();
    void showPreviewCar( const uint32_t configID,
                         const int32_t paintIndex,
                         const int32_t rimID,
                         const int32_t interiorID,
                         const ePaintType paintType,
                         const ColorRGBA &mainColor,
                         const ColorRGBA &secondColor,
                         const ColorRGBA *pLogoColor,
                         const ColorRGBA *pSecondColor2 );

private:
    static void OnEnterFade( void *pData, void *pSource );
    static void OnLeaveFade( void *pData, void *pSource );

    void populateShowroom();

private:
    char savedWeatherDesc[0x20];
    SceneInterior content;
    SceneCarPark carPark;
    Bank interiorBank;
    MngShowCaseCarControl showcaseControl;
    CarDesc previewCar;
    RenderScene renderScene;
    Viewport viewport;
    CameraGame camera;
    TradingMenu *pMenu;
    bool bLoaded;
};
