#pragma once

#include "game/mng_base.h"

#include "game/freeride/mng_screenshots.h"

struct FlashMessage;
struct FlashRECT;
class DrawList;
class FlashPlayer;
class FlashResource;
class GMLiving;
class MngCollection;
struct Material;
class TestDriveGameInstance;
class SavedExtraChallenges;

extern MngCollection *gpMngCollection; // DAT_010e7d10

// GMLiving progress (achievements) + photo album + rankings manager
class MngCollection : public Manager {
public:
    static constexpr int32_t kNumPhotosPerPage = 4; // DAT_00f418d4
    static constexpr const char *kPhotoContext = "Photo";
    static constexpr const wchar_t *kPhotoContextDisplay = L"Photos";
    static constexpr const char *kPhotoPattern = "*.jpg";
    static constexpr uint32_t kThumbnailWidth = 0x100;
    static constexpr uint32_t kThumbnailHeight = 0x80;
    static constexpr size_t kMaxTitleLength = 0x80;

public:
    const char *getName() const override {
        // FUN_0042d6a0
        return "MngCollection";
    }

    inline void setGameMode( GMLiving *pGameMode ) { pOwner = pGameMode; }

public:
    MngCollection();
    ~MngCollection();

    bool initialize( TestDriveGameInstance * ) override;
    void tick( float deltaTime, float totalTime ) override;
    void terminate() override;
    bool onMessage( FlashMessage *pMessage );

    inline void resetPhotoSelector() { selectorIndex = -1; }
    int32_t countPhotos() const;
    void scrollPhotoSelector( const int32_t index );
    Material *grabSelectorPhoto();

    static void SelectorDrawCallback( FlashRECT *pViewport,
                                      float depth,
                                      DrawList *pDrawList,
                                      void *pUser,
                                      FlashResource *pResource,
                                      const char *pClipName );

private:
    struct AlbumSlot {
        MngScreenshots Shot;
        Material *pMaterial = nullptr;
        char Title[kMaxTitleLength] = {};
    };

private:
    FlashPlayer *getFlashPlayer() const;
    SavedExtraChallenges *getStatistics() const;

    void openAlbum();
    void closeAlbum();
    void loadAlbumPage();
    void loadAlbumSlot( int32_t slot, int32_t page );
    void showBigPhoto();
    void hideBigPhoto();
    void deleteSelectedPhoto();
    void publishAchievementHeader();
    void publishAchievementDetails();

    int32_t getSelectedSlot() const;
    void clearAlbumMaterials();

    static void ThumbnailDrawCallback( FlashRECT *pViewport,
                                       float depth,
                                       DrawList *pDrawList,
                                       void *pUser,
                                       FlashResource *pResource,
                                       const char *pClipName );
    static void BigPhotoDrawCallback( FlashRECT *pViewport,
                                      float depth,
                                      DrawList *pDrawList,
                                      void *pUser,
                                      FlashResource *pResource,
                                      const char *pClipName );

private:
    GMLiving *pOwner = nullptr;
    AlbumSlot albumSlots[kNumPhotosPerPage];
    MngScreenshots bigPhoto;
    Material *pBigPhotoMaterial = nullptr;
    int32_t drawSlotCounter = 1;
    int32_t selectorIndex = -1;
    int32_t selectorPhotoIndices[64] = {};
    bool bUploadPending = false;
};
