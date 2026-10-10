#pragma once

#include "game/mng_base.h"
#include "ai/ai_road_query.h"
#include "game/bookmark/mng_bookmark.h"
#include "flash/implementation/flash_resource.h"
#include "render/2dm.h"
#include "render/draw_list.h"
#include "spots.h"

class GMMap;
class FlashPlayer;
class EntityExtraChallenge;
struct DBEntryBrand;
struct CarConfig;

// Player house garage panel
struct HouseGaragePanel {
    static constexpr int32_t kMaxNumCars = 10;
    static constexpr int32_t kNumCarsPerPage = 3;
    static constexpr int32_t kLineLength = 256;

    FlashPlayer *pPlayer = nullptr;
    uint64_t HouseHashcode = ~0ull;
    float Clock = 0.0f; // Page scroll timer
    const CarConfig *pCars[kMaxNumCars] = {};
    char Lines[kNumCarsPerPage][kLineLength] = {};
};

// Map spot icon
struct MapSpotRecord {
    static constexpr uint8_t kKindBookmark = 8;
    static constexpr int32_t kMaxBrandLogos = 16;

    Eigen::Vector4f Position = Eigen::Vector4f::Zero();
    int32_t CellX = 0;
    int32_t CellZ = 0;
    SpotIconInfo Info;
    int32_t ID = 0;
    int32_t BookmarkIndex = -1;
    const SpotMap *pSpot = nullptr;
    EntityExtraChallenge *pExtraChallenge = nullptr;
    DBEntryBrand *pBrandLogos[kMaxBrandLogos] = {};
    int32_t NumBrandLogos = 0;
    DBEntryBrand *pCurrentBrandLogo = nullptr;
};

// Map spots manager
class MngSpots : public Manager {
public:
    enum ePadDirection : int32_t {
        PD_Up = 0,
        PD_Down = 1,
        PD_Right = 2,
        PD_Left = 3,

        kNumPadDirections
    };

    static constexpr int32_t kNumDensityColumns = 41;
    static constexpr int32_t kNumDensityRows = 33;

public:
    const char *getName() const override { return "Spots"; }

    inline void setOwner( GMMap *pMap ) { pOwner = pMap; }
    inline void setIconsEnabled( const bool bEnabled ) {
        bIconsEnabled = bEnabled;
    }
    inline bool areIconsEnabled() const { return bIconsEnabled; }
    inline void setPadNavigationEnabled( const bool bEnabled ) {
        bPadNavigationEnabled = bEnabled;
    }
    inline void setShowCursor( const bool bShow ) { bShowCursor = bShow; }
    inline void setDrawDensityMap( const bool bDraw ) {
        bDrawDensityMap = bDraw;
    }
    inline void setDrawChallengeCones( const bool bDraw ) {
        bDrawChallengeCones = bDraw;
    }
    inline void requestResort() { bResortRecords = true; }
    inline void setWasFreeMode( const bool bFreeMode ) {
        bWasFreeMode = bFreeMode;
    }
    inline void setFocus( const Eigen::Vector4f &position ) {
        // FUN_00742690
        focusPosition = position;
    }
    inline bool areMapRectsLatched() const { return bMapRectsLatched; }
    inline const FlashRECT &getFreeModeRect() const { return freeModeRect; }
    inline MapSpotRecord *getSelectedRecord() const { return pSelectedRecord; }
    inline AiRoadLocation *getRouteWaypoint( const int32_t index ) const {
        return &pRouteWaypoints[index];
    }
    inline bool isChallengeInfoRequested() const {
        return bChallengeInfoRequested;
    }
    inline bool isChallengeInfoFromCache() const {
        return bChallengeInfoFromCache;
    }
    inline const SpotMap *getSelectedSpot() const {
        // FUN_00742600
        return ( pSelectedRecord != nullptr ) ? pSelectedRecord->pSpot
                                              : nullptr;
    }
    inline const Bookmark *getSelectedBookmark() const {
        // FUN_00742410
        if ( pSelectedRecord == nullptr ||
             pSelectedRecord->Info.Kind != MapSpotRecord::kKindBookmark ) {
            return nullptr;
        }
        return &bookmarks[pSelectedRecord->BookmarkIndex];
    }
    inline void setSelectedRecord( MapSpotRecord *pRecord ) {
        pSelectedRecord = pRecord;
    }
    inline void setInfoCartShown( const bool bShown ) {
        bInfoCartShown = bShown;
    }
    inline void setInfoCartRequested( const bool bRequested ) {
        bInfoCartRequested = bRequested;
    }

public:
    MngSpots();
    ~MngSpots();

    void tick( float deltaTime, float totalTime ) override;
    bool initialize( TestDriveGameInstance * ) override;
    void reset() override;
    void draw() override;
    void pause() override;
    void terminate() override;

    void setFlashPlayer();
    void refreshDensityMap();
    void refreshFilterCheckboxes();
    void applyFilterCheckboxes();
    void cycleFilterRow( bool bNext );
    void refreshSelectionCart();
    void openInfoCart();
    void closeInfoCart();

    bool isInsideMapRect( const Eigen::Vector4f &position ) const;
    bool isCursorOverRecord( const MapSpotRecord *pRecord,
                             const Eigen::Vector2f &cursor ) const;
    bool findRecordUnderCursor( const Eigen::Vector2f *pCursor,
                                MapSpotRecord **ppOutRecord ) const;
    bool isCursorOverSelection( const Eigen::Vector2f &cursor ) const;

    void selectSpotRecord( const SpotMap *pSpot );
    void deleteSelectedBookmark();

private:
    void buildRecords();
    void fillRecords();
    void latchMapRects();
    void updateSelection();
    bool isOnMapPage( const MapSpotRecord *pRecord ) const;
    float computeSpotHeight( float x, float z ) const;
    uint32_t computeIconColor( const MapSpotRecord *pRecord ) const;
    void pushIconQuads( const MapSpotRecord *pRecord );
    void pushIconQuad( const Eigen::Vector4f screenCorners[4],
                       uint32_t color,
                       float u0,
                       float u1,
                       float v0,
                       float v1,
                       float offsetY );
    Eigen::Vector4f computeSelectionAnchor() const;
    bool computeSelectionMoviePosition( float &outX, float &outY ) const;
    const char *getSpotKindLabel( const SpotIconInfo &info );
    void getSpotDisplayName( const MapSpotRecord *pRecord, char *pOutName );
    void refreshSpotInfoPanel();
    void setDistrictAddress();
    void updatePadNavigation( float deltaTime );
    void computeNeighbours( bool bIgnoreMapRect );
    void resetGaragePanel( const MapSpotRecord *pRecord );
    void fillGaragePanelCars();
    void refreshGaragePanelText();
    void refreshBrandLogos( bool bForce );
    void buildBrandLogoLists();
    void refreshChallengeInfoPanel( bool bReset );
    void updateChallengeInfoRequest( float deltaTime );
    void refreshExtraChallengePanel( EntityExtraChallenge *pChallenge );
    void refreshTuningShopPanel( const SpotMap *pSpot );
    void setSpotLocked( uint64_t lockedTextHashcode );
    void createSpotMaterial();
    void releaseSpotMaterial();
    void pushDensityMap();
    void resetChallengeConeList();
    void pushChallengePopularity( const MapSpotRecord *pRecord );

private:
    GMMap *pOwner;
    bool bIconsEnabled;
    bool bPadNavigationEnabled;
    uint8_t densityMap[kNumDensityColumns * kNumDensityRows];
    bool bDrawDensityMap;
    bool bDrawChallengeCones;
    bool bShowCursor;
    bool bResortRecords;
    bool bWasFreeMode;

    char spotKindLabel[0x20];
    float padRepeatElapsed;
    float padRepeatNext;
    float padRepeatPrevious;
    MapSpotRecord *pNeighbours[kNumPadDirections];
    char vehicleRestriction[0x400];
    char brandsList[0x400];
    float challengeInfoTimer;
    bool bChallengeInfoRequested;
    bool bChallengeInfoFromCache;
    float infoCartTimer;
    bool bInfoCartFlag0;
    bool bInfoCartFlag1;

    const SpotMap *pChallengeInfoSpot;
    AiRoadLocation *pRouteWaypoints;
    int32_t numBookmarks;
    Bookmark bookmarks[MngBookmark::kMaxNumBookmarks];

    std::vector<MapSpotRecord> records;
    int32_t numRecords;
    int32_t numWorldSpotRecords;
    float brandLogoClock; // dealership logo scroll timer
    bool bBrandLogoSecondTicked;
    HouseGaragePanel garagePanel;
    std::vector<MapSpotRecord> onlineRecords;
    int32_t numOnlineRecords;
    std::vector<MapSpotRecord *> visibleRecords;
    int32_t numVisibleRecords;
    MapSpotRecord *pSelectedRecord;
    bool bInfoCartShown;
    bool bInfoCartRequested;
    Eigen::Vector4f focusPosition;
    DrawList iconDrawList;
    DrawList densityDrawList;
    DrawList challengeConeDrawList;
    Render2DM spot2DM;
    void *pSpotMaterialData;
    Material *pSpotMaterial;
    int32_t currentCarNumColors;
    bool bCurrentCarIsBike;
    bool bCurrentCarDeluxePaint;

    FlashPlayer *pMapPlayer;
    FlashRECT mapRect;
    FlashRECT freeModeRect;
    bool bMapRectsLatched;
    char whatSpot[0x20];
    int32_t iconBankState;
};

extern MngSpots *gpMngSpots;

bool IsMapRecordVisible( const MapSpotRecord *pRecord, bool bMinimap );
