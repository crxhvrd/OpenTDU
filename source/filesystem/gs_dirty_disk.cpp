#include "shared.h"
#include "gs_dirty_disk.h"

#include "core/thread.h"
#include "flash/modal_message_box.h"

GSDirtyDisk *gpDirtyDisk = nullptr;

// Hashcode of '_Connection lost'
static constexpr uint64_t kConnectionLostHashcode = 0x355fc5a;

GSDirtyDisk::GSDirtyDisk()
    : GameSystem(),
      PopupCallback(),
      pThread( new TestDriveThread() ),
      bFailureReported( false ) {
    gpDirtyDisk = this;
}

GSDirtyDisk::~GSDirtyDisk() { delete pThread; }

void GSDirtyDisk::onDataLoadFailure() {
    // FUN_007c6c20
    if ( !bFailureReported ) {
        TestDrive::DisplayModalPrompt(
            false, kConnectionLostHashcode, 0, nullptr, this, nullptr, 0,
            "_Connection lost", true, false, MPT_Warning, gpFlashMessageBox );
    }

    bFailureReported = true;
}

void GSDirtyDisk::onClose( ePopupAnswer, void *, int32_t ) {
    OTDU_UNIMPLEMENTED_ONCE;
}
