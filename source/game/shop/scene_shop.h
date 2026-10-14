#pragma once

#include "filesystem/bank.h"
#include "game/scenaric/scenaric_bank.h"
#include "game/scene_car_park.h"
#include "game/scene_interior.h"

class DrawList;
class EntityCinematic;
class FlashPlayer;
class FlashResource;
class GMShop;
struct FlashRECT;

// Shop interior
class SceneShop {
public:
    virtual SceneInterior *getContent() {
        // FUN_0042ce10
        return &content;
    }

    inline SceneCarPark &getCarPark() { return carPark; }
    inline const SceneCarPark &getCarPark() const { return carPark; }
    inline void setOwner( GMShop *pShop ) { pOwner = pShop; }
    inline int32_t getSelectedCar() const { return selectedCar; }
    inline void setSelectedCar( const int32_t index ) { selectedCar = index; }
    inline bool isCameraMoving() const {
        return lureTimer >= 0.0f || skipCineTimer >= 0.0f;
    }
    inline void setLureTimer( const float value ) { lureTimer = value; }
    inline void setSkipCineTimer( const float value, FlashPlayer *pPlayer ) {
        skipCineTimer = value;
        pSkipCinePlayer = pPlayer;
    }

public:
    SceneShop();
    virtual ~SceneShop();

    virtual void terminate();
    virtual void tick( float deltaTime );
    virtual void setupCamera();
    virtual void suspend();

    void loadResources( const bool bRental );
    void draw();

    void focusCar( const int32_t carIndex );
    bool playCarCinematic( const char *pName,
                           const int32_t carIndex,
                           const bool bDetails );
    void playOptionCinematic( const int32_t option, const int32_t carIndex );
    void playOptionDetailsCinematic( const int32_t option,
                                     const int32_t carIndex );
    void moveToCar( const int32_t fromCar, int32_t toCar );

    void enterShowcase();
    void leaveShowcase();
    void enterCar();
    void leaveCar();

private:
    static void LureCallback( FlashRECT *pRect,
                              float deltaTime,
                              DrawList *pDrawList,
                              void *pUserData,
                              FlashResource *pResource,
                              const char *pClipName );

    void onInteriorReady();

private:
    GMShop *pOwner;

    EntityCinematic *pDetailsCinematic;
    EntityCinematic *pCinematic;
    EntityCinematic *pPendingDetailsCinematic;
    EntityCinematic *pPendingCinematic;
    ScenaricBank bank;

    SceneInterior content;
    SceneCarPark carPark;
    Bank interiorBank;

    int32_t selectedCar;
    float lureTimer;
    float skipCineTimer;
    FlashPlayer *pSkipCinePlayer;
};
