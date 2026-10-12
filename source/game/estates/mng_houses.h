#pragma once

#include "filesystem/bank.h"
#include "filesystem/streamed_resource.h"
#include "render/2db.h"
#include "render/2dm.h"
#include "render/file_collection_2d.h"

struct Material;

// House map screenshots (FrontEnd/AllRes/RealtorScr/<house spot>.bnk)
class HousePictureBank : public Bank, public StreamedResource {
public:
    using OnLoaded_t = bool ( * )( HousePictureBank *, void * );

    static constexpr uint32_t kBufferSize = 0x20000;

public:
    inline bool isLoading() const { return state == HPS_Loading; }
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
