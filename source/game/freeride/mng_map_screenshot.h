#pragma once

#include "game/mng_base.h"
#include "mng_screenshots.h"

class CameraGame;

class MngMapScreenshot : public Manager {
public:
    enum eState : int32_t {
        MSS_Idle = 0,
        MSS_TakeScreenshots = 1,
        MSS_WaitStreaming = 2,
        MSS_Screenshot = 3,
        MSS_WaitSaveEnd = 4,
    };

    enum eCategory : int32_t {
        MSC_Spots = 0,
        MSC_Challenges = 1,
        MSC_ExtraChallenges = 2,

        kNumCategories
    };

public:
    inline bool isCapturing() const { return State != MSS_Idle; }
    inline int32_t getNumScreenshots() const { return NumScreenshots; }

    static bool SearchCurrentSpot( const int32_t category, char *pOutName );
    static void ShowPlayerCar( const bool bShow );

public:
    MngMapScreenshot();
    ~MngMapScreenshot();

    bool initialize( TestDriveGameInstance * ) override;
    void tick( float, float ) override;
    void reset() override;
    void terminate() override;

    bool buildScreenshots( const int32_t category,
                           const int32_t firstIndex,
                           const int32_t lastIndex );
    void saveSpotParams( const int32_t category,
                         const char *pName,
                         CameraGame *pCamera );
    void cleanDB();
    bool saveConfigFile( bool bMapScreenshotsEnabled );

#ifdef OTDU_USE_IMGUI
    void drawDebug();
#endif

private:
    struct Entry {
        uint32_t Type;
        uint8_t __PADDING__[12];
        char pName[64];
        float ViewMatrix[4][4];
        float FOV;
        float NearPlane;
        float FarPlane;
        uint32_t Unknown;
    };
    OTDU_SIZE_MUST_MATCH( Entry, 0xA0 );

private:
    Entry *pScreenshotsData;
    int32_t MaxNumScreenshots;
    int32_t NumScreenshots;
    int32_t CurrentScreenshotIndex;
    int32_t MaxScreenshotIndex;
    int32_t CurrentCategory;
    bool bIsDirty;
    eState State;
    CameraGame *pOldCamera;
    bool bSavedFreeRideFlag;
    MngScreenshots mngScreenshots;
    float CurrentWaitTime;

#ifdef OTDU_USE_IMGUI
    struct DebugRequest {
        bool bBuild = false;
        int32_t Category = -1;
        int32_t FirstIndex = 0;
        int32_t LastIndex = -1;
        bool bSaveSpotParams = false;
    };

    DebugRequest debugRequest;
#endif

private:
    void allocScreenshotsData();
    bool loadFromConfigFileBuffer( void *pBuffer );
    void saveConfigToMemory( uint8_t *pOut );
    void applyEntryCamera( const Entry &entry );
    bool isReadyForScreenshot() const;
    void finishScreenshots();
#ifdef OTDU_USE_IMGUI
    void processDebugRequest();
#endif
    static bool IsEntryValid( const uint32_t category, const char *pName );
};

extern bool gbMapScreenshotsMngEnabled;
extern MngMapScreenshot *gpMngMapScreenshot;
