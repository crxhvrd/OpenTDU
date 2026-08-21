#pragma once

#include "core/popup_callback.h"
#include "game/gs_base.h"

class TestDriveThread;

class GSDirtyDisk : public GameSystem, public PopupCallback {
public:
    const char *getName() const override { return "Service : Dirty Disk"; }

public:
    GSDirtyDisk();
    ~GSDirtyDisk();

    bool initialize( TestDriveGameInstance * ) override { return true; }
    void terminate() override {}
    void onDataLoadFailure();

protected:
    void
    onClose( ePopupAnswer answer, void *param_2, int32_t param_3 ) override;

private:
    TestDriveThread *pThread;
    uint8_t bFailureReported : 1;
};

extern GSDirtyDisk *gpDirtyDisk;
