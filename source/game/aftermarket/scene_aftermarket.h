#pragma once

#include "filesystem/bank.h"
#include "game/scene_car_park.h"
#include "game/scene_interior.h"

class SceneAfterMarket {
public:
    inline SceneInterior &getContent() { return content; }
    inline SceneCarPark &getCarPark() { return carPark; }

public:
    SceneAfterMarket();
    ~SceneAfterMarket();

    void loadResources();
    void placeCars();
    void terminate();

private:
    SceneInterior content;
    SceneCarPark carPark;
    Bank interiorBank;
};
