#pragma once

#include "game/gm_scene.h"

#include "flash/entities/flash_panel.h"
#include "game/mng_logo_display.h"
#include "game/vehicle/mng_car_alpha.h"
#include "render/mng_reflection.h"
#include "game/first_person_mode.h"

#include "flash_list_garage.h"
#include "scene_garage.h"

class FlashPlayer;
class GMGarage;

// Leaves the garage once a pending garage operation completes.
class GMGarageNotifier : public Notifier {
public:
    GMGarageNotifier( GMGarage *pOwner );

    void onUpdate( uint32_t param ) override;

private:
    GMGarage *pGMGarage;
};

class GMGarage : public GameModeScene {
public:
    enum eExitAction : int32_t {
        EA_None = 0,
        EA_Home = 1,
        EA_Living = 2,
        EA_FreeRide = 3,
        EA_Cancel = 4,
    };

public:
    inline int32_t getSelectedSlot() const { return selectedSlot; }

public:
    GMGarage();
    ~GMGarage();

    void initialize() override;
    void reset() override;
    void tick( float deltaTime, float totalTime ) override;
    void draw( float deltaTime, float totalTime ) override {
        // FUN_00790190
        // NOP
    }
    void pause() override {
        // FUN_00790660
        firstPerson.leave();

        GameModeScene::pause();
    }
    void destroy() override;

    bool onMessage( FlashMessage *pMessage ) override;

    void watchCar( const int32_t slot );
    void exit();

private:
    void selectCar( const int32_t carIndex );
    bool leave( const eExitAction action );
    void refreshCarStatus( const bool bSelectedCar );
    void swapCars();

private:
    MngReflection mngReflection;
    MngLogoDisplay mngLogoDisplay;
    MngCarAlpha mngCarAlpha;
    GMGarageNotifier garageNotifier;
    FlashPlayer *pFlashPlayer;
    SceneGarage sceneGarage;
    FirstPersonMode firstPerson;
    std::vector<const JPH::Body *> parkedCarBodies;
    FlashListGarage carList;
    int32_t sortSourceSlot;
    FlashPanelCarSpecs carSpecs;
    int32_t selectedCar;
    int32_t selectedSlot;
    eExitAction exitAction;
    int32_t innerViewDelay;
};
