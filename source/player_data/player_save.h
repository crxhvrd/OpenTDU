#pragma once

#include "achievements_data.h"
#include "driver.h"
#include "player_data_interface.h"
#include "saved_garage.h"

// Magic the 'playersave' stream starts with.
static constexpr int32_t kPlayerSaveMagic = 0x5d;

extern int32_t gSaveStreamRevision;

class PlayerSave : public IPlayerData {
public:
    Driver *getDriver() override { return &driver; }
    AchievementsData *getAchievements() override { return &achievements; }

    inline Garage *getGarage() { return &garage; }
    bool isReady() const override {
        return numPendingOperations == 0 && bInitialized;
    }
    inline void resetPendingOperations() { numPendingOperations = 0; }

public:
    PlayerSave();
    ~PlayerSave() override;

    void initialize() override;
    void terminate() override;
    void tick( const float deltaTime ) override;
    void newGame( const bool bResetOnly ) override;
    void clear() override;

    bool deserialize( const void *pSaveData, const uint32_t saveDataSize );

private:
    Driver driver;
    Garage garage;
    uint32_t nextCarUniqueID;
    AchievementsData achievements;
    int32_t numPendingOperations;
    bool bInitialized;
    int32_t saveIndex;
};
