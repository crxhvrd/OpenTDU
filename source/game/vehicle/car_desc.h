#pragma once

#include "core/color.h"
#include "core/notifier.h"
#include "paint.h"

class edStore;

struct CarConfig {
    int32_t Hash;
    int32_t PowerCC;
    int32_t Weight;
    std::string FullName;
    std::string Name;
    std::string Model;
    std::string SubModel;
    std::string ConfigName;
    std::string BankGeometryName;
    std::string BankPhysicsName;
    float MaxSpeed;
    float TimeTo100Kmh;
    float TimeTo60Mph;

    CarConfig()
        : Hash( 0 ),
          PowerCC( 0 ),
          Weight( 0 ),
          FullName( "" ),
          Name( "" ),
          Model( "" ),
          SubModel( "" ),
          ConfigName( "" ),
          BankGeometryName( "" ),
          BankPhysicsName( "" ),
          MaxSpeed( 0.0f ),
          TimeTo100Kmh( 0.0f ),
          TimeTo60Mph( 0.0f ) {}
};

class CarDesc : public Notifiable {
public:
    enum class eState {
        CDS_None = 0,
        CDS_PutOnEbay = 1,
        CDS_Invalid = 2,
        CDS_Corrupted = 4,
        CDS_Rental = 5,
        CDS_TestDrive = 6,
        CDS_SoldOnEbay = 7
    };

    static constexpr int32_t kDefaultCarConfigID = 0x3e7;
    static constexpr int32_t kDefaultBikeConfigID = 0x29a;
    static constexpr int32_t kFirstBikeConfigID = 0x5dc;
    static constexpr int32_t kFirstAfterBikeConfigID = 0x7cf;

    static constexpr uint32_t kUnsetByteID = 0xffu;

public:
    inline uint32_t getCarID() const { return carID; }
    inline const CarConfig *getConfig() const { return pConfig; }
    bool isValid() override { /* FUN_00415fe0 */
        return state != eState::CDS_Invalid;
    }

public:
    CarDesc();
    ~CarDesc();

    void clear();
    void deserialize( edStore &stream );
    void serialize( edStore &stream ) const;
    void setArchetype( const uint32_t configID );

    uint32_t getCarConfigID() const;
    void setCarConfigID( const uint32_t configID );

    float getUpgradeRate() const;
    void setUpgradeRate( const float upgradeRate );

    int32_t getPackLevel( int32_t *pOutMaxLevel = nullptr ) const;

public:
    uint32_t pCarConfigID[2] = {}; // TEA Encrypted
    uint32_t carID = 0xffffffffu;
    eState state = eState::CDS_Invalid;
    const CarConfig *pConfig = nullptr;
    uint32_t pUpgradeRate[2] = {}; // TEA Encrypted
    ePaintType paintType = ePaintType::PT_Default;
    int32_t paintIndex = -1;
    ColorRGBA deluxeMainColor = 0u;
    ColorRGBA deluxeSecondColor = 0u;
    ColorRGBA deluxeLogoColor = 0u;
    ColorRGBA deluxeSecondColor2 = 0u;
    int32_t rimID = -1;
    int32_t interiorID = -1;
    int32_t price = 0;
    float odometerKms = 0.0f;
    double playTime = 0.0;
    uint64_t packHashcode = 0xffffffffffffffffull;
};
