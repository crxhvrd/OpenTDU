#pragma once

#include "filesystem/bank.h"
#include "filesystem/streamed_resource.h"
#include "render/2db.h"
#include "render/2dm.h"
#include "render/file_collection_2d.h"

struct Material;

// Generic spot map screenshots
// (FrontEnd/AllRes/MapScreens/<category>/<name>.bnk)
class MapScreenshotBank : public Bank, public StreamedResource {
public:
    enum eCategory : int32_t {
        C_ExtraChallenges = 0,
        C_IGE = 1,
        C_Spots = 2,
    };

    using OnLoaded_t = bool ( * )( MapScreenshotBank *, void * );

    static constexpr uint32_t kBufferSize = 0x10000;

public:
    inline bool isLoading() const { return state == MSS_Loading; }
    inline bool isReady() const { return state == MSS_Ready; }
    inline void setBuffer( void *pNewBuffer ) { pBuffer = pNewBuffer; }
    inline void *getBuffer() const { return pBuffer; }

public:
    MapScreenshotBank();
    ~MapScreenshotBank();

    bool load( const char *pName,
               eCategory category,
               OnLoaded_t pCallback,
               void *pUserData );
    void release();
    Material *getMaterial();

    void loadContent() override;
    void instanciate() override;

private:
    enum eMapScreensState : int32_t {
        MSS_Idle = 0,
        MSS_Loading = 1,
        MSS_Ready = 2,
    };

    static constexpr int32_t kMax2DB = 1;

private:
    char name[0x40];
    int32_t state;
    void *pBuffer;
    bool b2DMInitialized;
    int32_t num2DB;
    Render2DB render2DBs[kMax2DB];
    FileCollection2D collection2D;
    Render2DM render2DM;
    void *pCallbackUserData;
    OnLoaded_t pOnLoaded;
};
