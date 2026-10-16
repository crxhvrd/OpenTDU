#pragma once

#include "core/notifier.h"

class DrawList;
class edStore;
class FlashPlayer;
class FlashResource;
struct FlashRECT;
struct Material;

// One message of the mailbox
struct MailMessage {
    static constexpr uint32_t kTitleLength = 0x90u;
    static constexpr uint32_t kBodyLength = 0x800u;
    static constexpr uint32_t kSenderLength = 0xa0u;

    int32_t Kind = 0;
    int32_t Category = 0;
    uint16_t Date = 0u;
    char Title[kTitleLength] = {};
    char Body[kBodyLength] = {};
    char Sender[kSenderLength] = {};
    uint32_t Language = 0u; // two letters, big endian (e.g. 'fr')
    int32_t Index = -1;
    uint64_t Hashcode = 0ull;
    uint32_t Param0 = 0u;
    uint32_t Param1 = 0u;
    uint32_t Param2 = 0u;
    uint8_t Blob0[0x10] = {};
    uint8_t Blob1[0x20] = {};
    bool bRead = false;
    bool bFlagged = false;
    Material *pPicture = nullptr;
    FlashPlayer *pPictureMovie = nullptr;

    void clear();
    void copyFrom( const MailMessage &other );
    bool isVisible() const;
    void deserialize( edStore &stream );
    void serialize( edStore &stream ) const;
    void fillNewsLayout( FlashPlayer *pMovie ) const;
    void locateOnMap( FlashPlayer *pMovie ) const;

    static void PictureDrawCallback( FlashRECT *pViewport,
                                     float depth,
                                     DrawList *pDrawList,
                                     void *pUserData,
                                     FlashResource *pResource,
                                     const char *pClipName );
};

uint32_t GetMailLanguageCode( const char *pLanguage );

class SavedMailbox : public Notifiable {
public:
    static constexpr int32_t kNumMessages = 0x40;

public:
    SavedMailbox();
    ~SavedMailbox();

    bool isValid() override { /*FUN_0042df00*/ return true; }

    void clear();
    void deserialize( edStore &stream );
    void serialize( edStore &stream ) const;

    void post( const MailMessage &message );
    int32_t getNumVisibleMessages() const;
    MailMessage *getVisibleMessage( int32_t index );
    void removeVisibleMessage( int32_t index );
    void detachPictures();

private:
    int32_t getFreeSlotIndex( int32_t numToSkip ) const;

public:
    MailMessage Messages[kNumMessages];
};
