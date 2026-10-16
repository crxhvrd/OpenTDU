#pragma once

#include "core/event.h"
#include "core/mutex.h"

struct LocalPlayerData;

struct PlayerSaveDescriptor {
    static constexpr int32_t kMaxCars = 500;

    int32_t Trailer = 0;
    int32_t Revision = 0;
    uint32_t Money = 0u;
    int32_t NumHouses = 0;
    int32_t NumCars = 0;
    uint32_t CurrentCarConfigID = 0u;
    uint64_t CurrentCarPackHashcode = 0ull;
    uint16_t CarConfigIDs[kMaxCars] = {};

    bool isSameAs( const PlayerSaveDescriptor &other ) const;
};

class SavegameManager {
public:
    SavegameManager();
    ~SavegameManager();

    void create();
    void refreshService();

    bool doSaveBlocking( LocalPlayerData *pPlayerData = nullptr );
    bool writeCommonDTBlocking( LocalPlayerData *pPlayerData,
                                const uint64_t profileHashcode );
    void requestSave();

    bool isProfileChangeInProgress() const;
    void toggleProfileChange();

    void setUnknownBool2( bool param_1 );
    bool getUnknownBool2() const;

    bool isDword2Zero() const;
    bool isDwordDiffFromPendingSaveCount() const;
    void increaseNumSaveCount();

private:
    bool buildPlayerSaveBuffer();

private:
    TestDriveEvent event;
    TestDriveMutex mutex;

    uint32_t pendingSaveCount;
    uint32_t unknownDword;
    uint32_t unknownDword2;

    uint8_t bProfileChange : 1;
    uint8_t bUnknownBool1 : 1;
    uint8_t bUnknownBool2 : 1;
    uint8_t bUnknownBool3 : 1;
    uint8_t bUnknownBool4 : 1;
    uint8_t bSaveRequeued : 1;
};

extern SavegameManager gSavegameManager;
extern PlayerSaveDescriptor gPlayerSaveDescriptor;
extern bool BOOL_010e7364; // DAT_010e7364
extern int32_t gPreviousSaveStreamRevision;