#pragma once

#include "game/mng_base.h"
#include "flash/entities/flash_entity.h"
#include "core/mutex.h"

static constexpr int32_t kNumChatLines = 4;       // Limited by Flash movie
static constexpr float kChatLineUnused = -1.0f;   // FLOAT_00f41664
static constexpr float kChatLineDuration = 10.0f; // FLOAT_00f4166c
// Database hashcode of the sentence used to format a chat line
// (i.e. "<AUTHOR§> : <CHATTEXT§>")
static constexpr uint64_t kChatLineSentenceHashcode = 0x32fd6baull;

struct ChatLine {
    enum eState : int32_t {
        // The line has been posted and is (or is about to be) on screen
        CLS_Visible = 0,
        // The line lifetime is over; its close animation has been triggered
        CLS_Closing = 1,
        // The line has never been posted
        CLS_Unused = 2
    };

    char pText[0xa0];
    int32_t ColorIndex;
    float DisplayUntil;
    eState State;
};
OTDU_SIZE_MUST_MATCH( ChatLine, 0xac );

class MngChat : public Manager {
public:
    inline bool isOnlineAvailable() const { return bOnlineAvailable; }

public:
    MngChat();
    ~MngChat();

    virtual void tick( float, float ) override;
    virtual void reset() override;
    virtual void pause() override;

    bool onMessage( FlashMessage *pMessage );

    void
    addLine( const char *pText, const char *pAuthor, const int32_t colorIndex );

    static void FlashClose();

private:
    TestDriveMutex mutex;
    float time;
    bool bOnlineAvailable;
    bool field700_0x331;

    std::array<ChatLine, kNumChatLines> lines;

    char pTextInput[256];

private:
    void resetLines();
    void closeInputPopup();
    void tickLines();
    void refreshLineVariables();

    int32_t getLocalPlayerIndex() const;
};

extern MngChat *gpMngChat;
