#pragma once

#include "core/notifier.h"

class edStore;

class Driver : public Notifiable {
public:
    static constexpr size_t kNicknameLength = 32;
    static constexpr size_t kClubNameLength = 16;

public:
    inline bool isValid() override { return ID != 0xffffffffu; }

public:
    Driver();
    ~Driver();

    void clear();
    void deserialize( edStore &stream );
    void serialize( edStore &stream, const uint16_t garageCurrentCar );
    void initialize( const char *pNickname,
                     const uint32_t id,
                     const uint32_t money,
                     const uint32_t numCoupons,
                     const uint16_t currentCar,
                     const uint32_t teamID,
                     const uint32_t teamSpotID,
                     const uint32_t param_9 );

    uint32_t getMoney() const;
    void setMoney( const uint32_t value );
    void setMoneyAndNotify( const int32_t value );

    uint32_t getNumCoupons() const;
    void setNumCoupons( const uint32_t value );
    void spendCoupons( const uint32_t numCoupons );
    void earnCoupons( const uint32_t numCoupons );

    int32_t getPendingMoneyOperation() const;
    void setPendingMoneyOperation( const int32_t value );
    void tickMoneyOperation( const float deltaTime );
    void addExpense( const int32_t price,
                     const int32_t statCounter,
                     const bool bCharge );
    void startMoneyAnimation();

    uint64_t getCurrentHouse() const;
    void setCurrentHouse( const uint64_t hashcode );
    void selectCurrentHouse( const uint64_t hashcode );

    uint32_t getLocale() const;
    void setLocale( const uint32_t value );

    float getReputation() const;
    void setReputation( const float value );

    float getOdometerKms() const;
    void setOdometerKms( const float value );
    void addOdometerKms( const float kms );

public:
    uint32_t ID = 0xffffffffu;
    uint32_t TeamID = 0u;
    uint32_t TeamSpotID = 0u;
    uint32_t field4_0x24 = 0u;
    uint32_t pMoney[2] = {};      // TEA encrypted
    uint32_t pNumCoupons[2] = {}; // TEA encrypted
    uint32_t pMoneyOP[2] = {};    // TEA encrypted
    float ApplyMoneyOPPendingTimeLeft = -1.0f;
    uint16_t CurrentCar = 0u;
    int32_t TutorialState = 0;
    int32_t TutorialUserParam = 0;
    uint32_t pCurrentHouse[2] = {}; // TEA encrypted
    char Nickname[kNicknameLength] = {};
    uint32_t field28_0x78 = 0u;
    uint32_t pLocale[2] = {};     // TEA encrypted
    uint32_t pReputation[2] = {}; // TEA encrypted
    char ClubName[kClubNameLength + 1] = {};
    uint32_t pOdometerKms[2] = {}; // TEA encrypted
    bool bNewGamePending = false;
};
