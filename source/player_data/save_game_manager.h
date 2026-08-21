#pragma once

#include "core/event.h"
#include "core/mutex.h"

struct LocalPlayerData;

class SavegameManager {
public:
    SavegameManager();
    ~SavegameManager();

    void create();

    bool doSaveBlocking( LocalPlayerData *pPlayerData = nullptr );
    void requestSave();

    bool isProfileChangeInProgress() const;
    void toggleProfileChange();

    void setUnknownBool2( bool param_1 );
    bool getUnknownBool2() const;

    bool isDword2Zero() const;
    bool isDwordDiffFromPendingSaveCount() const;
    void increaseNumSaveCount();

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
extern bool BOOL_010e7364; // DAT_010e7364