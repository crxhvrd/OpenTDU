#pragma once

#ifdef OTDU_USE_IMGUI
class CarDesc;
class VehicleBase;

namespace TestDrive {
    enum eCarPickerResult : uint32_t {
        CPR_None = 0,
        CPR_CarChanged = 1 << 0,
        CPR_PaintChanged = 1 << 1,
    };

    // Draws a ImGui list of the vehicle db that can be used to set carID.
    // Returns true when carID changed.
    bool DrawCarList( int32_t &carID );

    // DrawCarList() + paint/interior/rim picker (based on desc db entries).
    // pVehicle is either null or the vehicle instance in the world.
    // Returns eCarPickerResult flags.
    uint32_t DrawCarPicker( CarDesc &desc, VehicleBase *pVehicle );
} // namespace TestDrive
#endif
