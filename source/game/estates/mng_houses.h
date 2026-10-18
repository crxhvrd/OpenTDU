#pragma once

#include "filesystem/bank.h"
#include "filesystem/streamed_resource.h"
#include "render/2db.h"
#include "render/2dm.h"
#include "render/file_collection_2d.h"

struct Material;
struct SpotMap;
struct FlashRECT;
struct DBEntryHouse;
class FlashPlayer;
class FlashResource;
class DrawList;

// House map screenshots (FrontEnd/AllRes/RealtorScr/<house spot>.bnk)
class HousePictureBank : public Bank, public StreamedResource {
public:
    using OnLoaded_t = bool ( * )( HousePictureBank *, void * );

    static constexpr uint32_t kBufferSize = 0x20000;

public:
    inline bool isLoading() const { return state == HPS_Loading; }
    inline bool isReady() const { return state == HPS_Ready; }
    inline void setBuffer( void *pNewBuffer ) { pBuffer = pNewBuffer; }
    inline void *getBuffer() const { return pBuffer; }

public:
    HousePictureBank();
    ~HousePictureBank();

    bool load( const char *pHouseName, OnLoaded_t pCallback, void *pUserData );
    void release();
    Material *getMaterial( const char *pMaterialName );

    void loadContent() override;
    void instanciate() override;

private:
    enum eHousePictureState : int32_t {
        HPS_Idle = 0,
        HPS_Loading = 1,
        HPS_Ready = 2,
    };

    static constexpr int32_t kMax2DB = 3;

private:
    char name[0x40];
    int32_t state;
    void *pBuffer;
    bool b2DMInitialized;
    int32_t num2DB;
    Render2DB render2DBs[kMax2DB];
    FileCollection2D collection2D;
    Render2DM render2DM;
    bool bDeferInstanciation;
    void *pCallbackUserData;
    OnLoaded_t pOnLoaded;
};

// Houses listed by GMEstates
class EstatesHouses {
public:
    struct Entry {
        const char *pName;
        const char *pSpotName;
        int32_t Level;
        int32_t PriceBuy;
        int32_t PriceSell;
        int32_t NumGarageSlot;
        bool bOwned;
        SpotMap *pSpot;
    };

    static constexpr int32_t kMaxNumEntries = 64;

public:
    inline FlashPlayer *getFlashPlayer() const { return pFlashPlayer; }
    inline void setFlashPlayer( FlashPlayer *pPlayer ) {
        pFlashPlayer = pPlayer;
    }
    inline HousePictureBank &getPictureBank() { return pictureBank; }
    inline bool isRealtor() const { return bRealtor; }
    inline int32_t getNumEntries() const { return numEntries; }
    inline Entry &getSelectedEntry() { return entries[selectedEntry]; }

    static void PictureDrawCallback( FlashRECT *pViewport,
                                     float depth,
                                     DrawList *pDrawList,
                                     void *pUserData,
                                     FlashResource *pResource,
                                     const char *pClipName );

public:
    EstatesHouses();

    void initialize( SpotMap *pSpot );
    bool selectHouse( HousePictureBank::OnLoaded_t pCallback,
                      void *pUserData,
                      const int32_t index );
    bool requestPicture( HousePictureBank::OnLoaded_t pCallback,
                         void *pUserData,
                         const char *pSpotName );
    void tickPendingPicture();
    void setPictureCallbacksEnabled( const bool bEnabled );

private:
    bool collectRealtorHouses( const uint64_t realtorHashcode );
    Material *getPicture( const int32_t pictureIndex );

private:
    FlashPlayer *pFlashPlayer;
    HousePictureBank pictureBank;
    bool bRealtor;
    Entry entries[kMaxNumEntries];
    int32_t numEntries;
    int32_t selectedEntry;

    HousePictureBank::OnLoaded_t pPendingCallback;
    char pendingSpotName[0x40];
    void *pPendingUserData;
};

const DBEntryHouse *FindHouseEntry( const uint64_t spotHashcode );
