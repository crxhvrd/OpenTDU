#pragma once

#include "filesystem/streamed_resource.h"
#include "render/2db.h"
#include "render/2dm.h"
#include "render/file_collection_2d.h"
#include "render/render_api.h"

struct Material;

enum eScreenshotReadStatus : int32_t {
    SRS_UnInstalled = 0,
    SRS_Ready = 1,
    SRS_Loading = 2,
    SRS_Saving = 3,
};

enum eScreenshotWriteStatus : int32_t {
    SWS_Unknown = 0,
    SWS_Success = 1,
    SWS_Failure = 2,
};

static constexpr uint32_t kUseBitmapSize = ~0u;
static constexpr uint32_t kMaxStringLength = 0x80;

class MngScreenshots : public StreamedResource {
public:
    inline eScreenshotWriteStatus getSaveState() const { return SaveState; }
    inline eScreenshotReadStatus getState() const { return State; }

public:
    MngScreenshots();
    ~MngScreenshots();

    void create( uint32_t width, uint32_t height, eViewFormat format );
    void destroy();
    Material *grabAndGetMaterial();
    void save( const char *pFilename,
               const char *pContext,
               const wchar_t *pContextDisplay,
               const char *pMetaInfos,
               uint32_t width = kUseBitmapSize,
               uint32_t height = kUseBitmapSize );
    Material *loadFromMemory( const void *pContent,
                              uint32_t contentSize,
                              char *pOutMetaInfos = nullptr,
                              size_t metaInfosSize = 0 );
    Material *loadFromFile( const char *pFilepath,
                            const char *pContextName = nullptr,
                            char *pOutMetaInfos = nullptr,
                            size_t metaInfosSize = 0 );
    Material *loadFromContext( const char *pContextName,
                               const wchar_t *pContextDisplay,
                               const int32_t fileIndex );
    Material *copyBitmap( const MngScreenshots &source );
    void instanciate() override;

private:
    eScreenshotReadStatus State;
    eViewFormat Format;
    uint32_t render2DMSize;
    Render2DM render2DM;
    void *p2DBBuffer;
    Render2DB render2DB;
    FileCollection2D collection2D;
    uint8_t *p2DMData;
    eScreenshotWriteStatus SaveState;
    char pContextToSave[kMaxStringLength];
    wchar_t pContextDisplayToSave[kMaxStringLength];
    char pFilenameToSave[kMaxStringLength];
    char pMetaInfosToSave[kMaxStringLength];
    uint32_t SaveWidth;
    uint32_t SaveHeight;

private:
    Texture *getBitmap() const;
    Material *getMaterial();
    void uploadBitmap();
    bool grabBackbuffer();
    bool encodeShot( uint32_t width,
                     uint32_t height,
                     const char *pMetaInfos,
                     void **ppOutContent,
                     uint32_t *pOutContentSize );
};

extern bool gCubemapShooting;
extern bool gHighResScreenEnabled;
extern int32_t gSizeIncrease;
extern float gSampleMinSpace;
extern float gPixSamplingWidth;
