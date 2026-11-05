#pragma once

#include "game/vehicle/gs_brand_car.h"

class CarState;
class VehicleBase;

// Cutscene editor key
struct CinCameraKey {
    float Time = 0.0f;
    Eigen::Vector3f Position = Eigen::Vector3f::Zero(); // world space
    Eigen::Vector3f Target = Eigen::Vector3f::Zero();   // world space
    float FovDegrees = 55.0f;
    bool bCut = false;
};

// Cutscene editor car entity
struct CinShowcaseCar {
    int32_t CarID = -1;                                 // none
    Eigen::Vector3f Position = Eigen::Vector3f::Zero(); // world space, ground
    float YawDegrees = 0.0f;

    // Spawn matrix (rows: right, up, forward, position)
    inline Matrix4x4f_t getSpawnMatrix() const {
        const float yaw = YawDegrees * 0.01745329252f;
        const float s = sinf( yaw );
        const float c = cosf( yaw );
        Matrix4x4f_t matrix = Matrix4x4f_t::Identity();
        matrix.row( 0 ) << c, 0.0f, -s, 0.0f;
        matrix.row( 2 ) << s, 0.0f, c, 0.0f;
        matrix.row( 3 ) << Position.x(), Position.y(), Position.z(), 1.0f;
        return matrix;
    }
};

// Instance of
class ShowcaseCarInstance {
public:
    ShowcaseCarInstance();
    ~ShowcaseCarInstance();

    // Replaces the car (CarID -1 or not a showcase car: none)
    void spawn( const CinShowcaseCar &car );
    void destroy();
    void tick();

    inline bool isPending() const { return bPending; }
    CarState *getCarState() const;
    VehicleBase *getVehicle() const;

private:
    BrandCar brandCar;
    CinShowcaseCar showcaseCar;
    bool bPending;
};

namespace CinBuilder {
    // Default output of the editor, played behind the login menu
    static constexpr const char *kLoginAttractKeysPath =
        "opentdu_data/cinematics/login_attract.txt";
    static constexpr const char *kLoginAttractBankPath =
        "opentdu_data/cinematics/login_attract.bnk";
    // Suffix of the generated cinematic's file name in the bank
    static constexpr const char *kCinematicSuffix = "_M_VTOUR_E.CIN";

    void SamplePath( const std::vector<CinCameraKey> &keys,
                     const float time,
                     Eigen::Vector3f &outPosition,
                     Eigen::Vector3f &outTarget,
                     float &outFovDegrees );

    bool SaveKeys( const char *pPath,
                   const std::vector<CinCameraKey> &keys,
                   const CinShowcaseCar &car );
    bool LoadKeys( const char *pPath,
                   std::vector<CinCameraKey> &outKeys,
                   CinShowcaseCar &outCar );

    std::string GetAnchorPath( const char *pBankPath );
    bool LoadAnchor( const char *pBankPath, Eigen::Vector3f &outAnchor );

    bool BuildCameraBank( const std::vector<CinCameraKey> &keys,
                          const char *pOutputPath,
                          std::string &outError );
} // namespace CinBuilder
