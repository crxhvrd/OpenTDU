#pragma once

#include "game/mng_base.h"
#include "filesystem/streamed_resource.h"
#include "world/gs_world_streaming.h"
#include "core/mutex.h"
#include "flash_list_bookmark.h"

class FlashPlayer;
class MngFlash;
struct Material;
struct FlashMessage;
class MngBookmark;

// Single bookmark entry
struct Bookmark {
    Eigen::Vector3f Position;
    Eigen::Vector3f Orientation;
    int32_t PictureID;
    int32_t DynaMapID;
    char pName[64];
};
OTDU_SIZE_MUST_MATCH( Bookmark, 96 );

enum eBookmarkPicState : int32_t {
    BPS_None = 0,
    BPS_LoadRequested = 1,
    BPS_Loading = 2,
    BPS_Loaded = 3,
};

class MngBookmark : public Manager,
                    public StreamedResource,
                    public StreamingCallback {
public:
    static constexpr FlashMessage kCloseScreen = 0x756f6f62u; // boou
    static constexpr FlashMessage kSaveBookmark = 0x61736f62u; // bosa
    static constexpr FlashMessage kHidePicture = 0x6f626968u; // hibo
    static constexpr FlashMessage kNewBookmark = 0x6277656eu; // newb
    static constexpr FlashMessage kGrabScreenshot = 0x6f626661u; // afbo
    static constexpr FlashMessage kOpenScreen = 0x6e696f62u; // boin
    static constexpr FlashMessage kCreateBookmark = 0x616e6f62u; // bona

public:
    const char *getName() const override {
        // FUN_0042f0a0
        return "Bookmark";
    }
    inline bool isGME() const { return bGME; }
    inline int32_t getNumBookmarks() const { return numBookmarks; }
    inline const Bookmark &getBookmark( const int32_t index ) const {
        return arrayBookmarks[index];
    }

public:
    MngBookmark();
    ~MngBookmark();

    bool initialize( TestDriveGameInstance * ) override;
    void tick( float, float ) override;
    void terminate() override;

    void instanciate() override;
    bool onWorldStreamed( Eigen::Vector4f *param_1,
                          Eigen::Vector4f *param_2,
                          Eigen::Vector4f *param_3 ) override;

    void setMngFlash( MngFlash *pFlash ) { pMngFlash = pFlash; }
    void setFlashPlayer( FlashPlayer *pFlashPlayer );

    bool onMessage( FlashMessage *pMessage );

    static void PictureDrawCallback( struct FlashRECT *pViewport,
                                     float depth,
                                     class DrawList *pDrawList,
                                     void *pUser,
                                     class FlashResource *pResource,
                                     const char *pClipName );

    void onDelete( const int32_t bookmarkIndex );
    void goTo( const int32_t bookmarkIndex );
    void onSelectionChanged( const int32_t bookmarkIndex );

private:
    static constexpr int32_t kMaxNumBookmarks = 64;

private:
    eBookmarkPicState bookmarkPicState;
    int32_t picIndexToLoad;
    int32_t picIndexToLoadStacked;
    TestDriveMutex mutexLoad;

    int32_t tgtBookmarkIndex;
    int32_t state;
    int32_t saveState;
    int32_t saveBookmarkIndex;

    FlashPlayer *pPlayer;
    MngFlash *pMngFlash;
    FlashListBookmark list;

    float pictureX0;
    float pictureY0;
    float pictureX1;
    float pictureY1;
    float pictureU0;
    float pictureV0;
    float pictureU1;
    float pictureV1;
    Material *pBookmarkScreen;
    int32_t numBookmarks;
    Bookmark arrayBookmarks[kMaxNumBookmarks];
    char currentName[64];
    Bookmark *pNewBookmark;
    Eigen::Vector2f lastPlayerPos;
    bool bLastPlayerPosIsSet;
    bool bGME;
    int32_t messageBoxState;

private:
    bool requestPictureLoad( int32_t bookmarkIndex );
    void recordAutosaveBookmark();
    void applyBookmarkTeleport();
    void updateSaveQuery();
    void updateMessageBoxState();
    Material *loadBookmarkPicture( const char *pFileName );

    void openBookmarkScreen();
    void closeBookmarkScreen();
    void grabBookmarkScreenshot();
    void saveNewBookmark();
    void createBookmarkAtPlayer();
    bool loadBookmarkPositions();
    void computePictureLayout();

    static void OnGotoFadeCompleted( void *pData, void *pSource );
};

extern MngBookmark *gpMngBookmark;
extern int32_t gAutosaveBookmarkLength;
