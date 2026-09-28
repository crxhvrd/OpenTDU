#pragma once

#include "game/mng_base.h"

class VehicleBase;
class VehicleCar;
class PhysicsVehicleSnapshot;

struct DoorState {
    float ElapsedTime = 1.0f;
    float MoveDuration = 1.0f;
    bool bIsOpening = false;
};

struct WindowState {
    float ElapsedTime = 1.0f;
    float MoveDuration = 1.0f;
    bool bIsOpening = false;
    bool bPaused = false;

    inline float getRoll() const {
        const float progress = ElapsedTime / MoveDuration;
        return bIsOpening ? progress : ( 1.0f - progress );
    }
};

// Photo mode camera logic (has nothing to do with GMCarShowcase somehow)
class MngShowCaseCarControl : public Manager {
public:
    const char *getName() const override { return "ShowCaseCarControl"; }
    inline VehicleBase *getFocusedVehicle() const { return pVehicle; }

public:
    MngShowCaseCarControl();
    ~MngShowCaseCarControl();

    void tick( float, float ) override;

    void focusOn( VehicleBase *pVehicleBase );
    void moveDoor( bool bLeft, bool bOpening, bool bApply, float duration );
    void resetShowcase();
    void closeDoorsAndWindows();
    void setVehicle( VehicleBase *pVehicleBase );
    bool isBikeCategory() const;
    void closeShowcase();
    void enterCar();
    void leaveCar();

    inline bool hasWindows() const { return bHasWindows; }
    inline void setEnabled( const bool bValue ) { bEnabled = bValue; }
    inline DoorState &getRightDoor() { return RightDoor; }
    inline WindowState &getLeftWindow() { return LeftWindow; }
    inline WindowState &getRightWindow() { return RightWindow; }

private:
    void updateVehicleSpecs();

private:
    VehicleBase *pVehicle;
    VehicleCar *pCar;
    VehicleBase *pPlayerVehicle;

    bool bEnabled;
    bool bInVehicle;
    bool bHasWindows;
    bool bIsConvertible;

    float TimePressUp;
    float TimePressDown;

    DoorState LeftDoor;
    DoorState RightDoor;
    WindowState LeftWindow;
    WindowState RightWindow;

    bool bMainControl;
    PhysicsVehicleSnapshot *pSavedPhysics;
    void *pBankBuffer;
};
