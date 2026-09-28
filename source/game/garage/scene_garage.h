#pragma once

#include "filesystem/bank.h"
#include "game/scene_car_park.h"
#include "game/scene_interior.h"

// House garage interior (the GPH_* banks)
class SceneGarage : public SceneInterior {
public:
    inline SceneCarPark &getCarPark() { return carPark; }
    inline int32_t getSelectedCar() const { return selectedCar; }
    inline int32_t getCurrentPage() const { return currentPage; }

public:
    SceneGarage();
    ~SceneGarage() override;

    void loadBank( const bool bReentry );
    void terminate();
    void tick();
    void setupCamera();
    void onBankReady();
    void loadPage( const int32_t page );
    void selectCar( const int32_t carIndex );
    void selectCarImmediate( const int32_t carIndex );

    int32_t getCarPage( const int32_t carIndex ) const;
    int32_t getCarPositionInPage( const int32_t carIndex ) const;
    bool isCarOnCurrentPage( const int32_t carIndex ) const;

    void enterShowcase();
    void leaveShowcase();
    void enterCar();
    void leaveCar();

private:
    static void OnPageFadeOut( void *pData, void *pSource );

private:
    SceneCarPark carPark;
    Bank interiorBank;
    int32_t selectedCar;
    int32_t currentPage;
    bool bBankReentry;
};
