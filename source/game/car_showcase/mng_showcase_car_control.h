#pragma once

#include "game/mng_base.h"
#include "physics/vehicle/physics_vehicle.h"

class VehicleBase;
class VehicleCar;

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

// Driving input while seated in a showcase car
class ShowCasePhysControl : public PhysicsDrivingControl {
public:
    void forwardInput( PhysicsVehicle &vehicle,
                       float deltaTime,
                       int32_t argument ) override;

public:
    bool bRev = false;      // 'star' / 'stao'
    bool bHonk = false;     // 'honk' / 'hoof'
    bool bBackView = false; // 'bcvi' / 'ftvi'

    bool bHonking = false;
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
    void closeLeftDoor();
    void openLeftDoor( VehicleBase *pVehicleBase );
    void setVehicle( VehicleBase *pVehicleBase );
    bool isBikeCategory() const;
    void closeShowcase();
    void setPlayerVehicleForceInterior( const bool bForce );
    void enterCar();
    void leaveCar();

    inline bool hasWindows() const { return bHasWindows; }
    inline void setEnabled( const bool bValue ) { bEnabled = bValue; }
    inline ShowCasePhysControl &getPhysicsControl() { return physicsControl; }
    inline DoorState &getRightDoor() { return RightDoor; }
    inline WindowState &getLeftWindow() { return LeftWindow; }
    inline WindowState &getRightWindow() { return RightWindow; }

private:
    void updateVehicleSpecs();
    void restartWindows();

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

    ShowCasePhysControl physicsControl;
    PhysicsDrivingControl *pSavedControl;
    void *pBankBuffer;
};
