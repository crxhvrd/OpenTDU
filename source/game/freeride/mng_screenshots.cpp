#include "shared.h"
#include "mng_screenshots.h"

#include "filesystem/gs_file.h"
#include "render/bitmap_helpers.h"
#include "render/camera/camera_showcase.h"
#include "render/camera/gs_camera.h"
#include "render/ed_render.h"
#include "render/material.h"
#include "render/vulkan/render_device.h"
#include "tdu_instance.h"

bool gCubemapShooting = false;        // DAT_010e7254
bool gHighResScreenEnabled = false;   // DAT_010e7255
int32_t gSizeIncrease = 5;            // DAT_00f6b2b4
float gSampleMinSpace = 0.039999999f; // DAT_00f6b2b8
float gPixSamplingWidth = 1.0f;       // DAT_00f6b2c0

static constexpr const char *kCaptureBitmapName = "LOADBUFF";

static constexpr uint32_t kCaptureBitmapFlags = 0x412;
static constexpr uint64_t kCaptureMaterialHashcode = 0x73078865ca37336dull;

static constexpr int32_t kShowcaseCameraMode = 6;

static constexpr uint32_t kThumbnailWidth = 0x100;
static constexpr uint32_t kThumbnailHeight = 0x80;

MngScreenshots::MngScreenshots()
    : StreamedResource(),
      State( SRS_UnInstalled ),
      Format( VF_X8R8G8B8 ),
      render2DMSize( 0 ),
      render2DM(),
      p2DBBuffer( nullptr ),
      render2DB(),
      collection2D(),
      p2DMData( nullptr ),
      SaveState( SWS_Unknown ),
      pContextToSave{},
      pContextDisplayToSave{},
      pFilenameToSave{},
      pMetaInfosToSave{},
      SaveWidth( 0 ),
      SaveHeight( 0 ) {
    // FUN_006cd210
}

MngScreenshots::~MngScreenshots() {
    // FUN_00431eb0
    if ( p2DBBuffer != nullptr ) {
        collection2D.unregister2DM( &render2DM );
        collection2D.unregister2DB( &render2DB );
        render2DB.destroy();
        render2DM.destroy();
        TestDrive::FreeAligned( p2DBBuffer );
        p2DBBuffer = nullptr;
    }

    if ( p2DMData != nullptr ) {
        TestDrive::FreeAligned( p2DMData );
        p2DMData = nullptr;
    }

    State = SRS_UnInstalled;
}

void MngScreenshots::create( uint32_t width,
                             uint32_t height,
                             eViewFormat format ) {
    // FUN_006cd990
    Format = format;

    render2DMSize = Render2DM::CalcSize( 0, 0 );
    p2DMData = (uint8_t *)TestDrive::AllocAligned( render2DMSize );
    if ( p2DMData == nullptr ) {
        OTDU_LOG_ERROR( "Failed to allocate the screenshot .2DM (%u bytes)\n",
                        render2DMSize );
        return;
    }

    Render2DM::Create( p2DMData, kCaptureMaterialHashcode, 1, 0, 0, 0 );

    const uint32_t bitmapSize = (uint32_t)Render2DB::CalcSize(
        width, height, 1, 1, Format, kCaptureBitmapFlags );
    p2DBBuffer = TestDrive::AllocAligned( bitmapSize );
    if ( p2DBBuffer == nullptr ) {
        OTDU_LOG_ERROR( "Failed to allocate the screenshot .2DB (%u bytes)\n",
                        bitmapSize );
        return;
    }

    render2DB.create( p2DBBuffer, width, height, 1, 1, Format,
                      kCaptureBitmapFlags, kCaptureBitmapName );
    render2DB.initialize( p2DBBuffer );
    render2DM.initialize( p2DMData );

    Texture *pTexture = getBitmap();
    if ( pTexture != nullptr && pTexture->pTexture == nullptr ) {
        Render2DB::CreateAndUploadTexture( pTexture );
    }

    Material *pMaterial = render2DM.getMaterialAtIndex( 0 );
    if ( pMaterial != nullptr ) {
        pMaterial->OT = 0x2a;
        pMaterial->DepthTest = 1;
        pMaterial->DepthWrite = 2;
        pMaterial->AlphaTest = CF_Never;
    }

    if ( pMaterial == nullptr ) {
        return;
    }
    pMaterial->AlphaTest = CF_Less;

    MaterialParameter *pParameter = pMaterial->getParameterByIndex( 0 );
    MaterialLayer *pLayer = pParameter->getLayer( 0 );
    pLayer->pLayerTextures[0].Hashcode =
        GetIdentifier64bit( kCaptureBitmapName );

    collection2D.register2DB( &render2DB );
    collection2D.register2DM( &render2DM );

    State = SRS_Ready;
}

void MngScreenshots::destroy() {
    // FUN_006cdb60
    if ( ( State == SRS_Loading || State == SRS_Saving ) &&
         gpFile != nullptr ) {
        gpFile->flushPendingAsync();
    }

    if ( gpCamera != nullptr ) {
        CameraShowcase *pCamera =
            (CameraShowcase *)gpCamera->findCameraWithMode(
                kShowcaseCameraMode, gpCamera->getBoundCarState() );
        if ( pCamera != nullptr ) {
            pCamera->pTargetCarState = nullptr;
            pCamera->lookAt( nullptr );
        }
    }

    if ( p2DBBuffer != nullptr ) {
        collection2D.unregister2DM( &render2DM );
        collection2D.unregister2DB( &render2DB );
        render2DB.destroy();
        render2DM.destroy();
        TestDrive::FreeAligned( p2DBBuffer );
    }
    p2DBBuffer = nullptr;

    if ( p2DMData != nullptr ) {
        TestDrive::FreeAligned( p2DMData );
    }
    p2DMData = nullptr;

    State = SRS_UnInstalled;
}

Material *MngScreenshots::grabAndGetMaterial() {
    // FUN_006cdd40
    if ( Format != VF_X8R8G8B8 ) {
        Texture *pTexture = getBitmap();
        if ( pTexture == nullptr ) {
            return nullptr;
        }

        const uint32_t width = pTexture->Width;
        const uint32_t height = pTexture->Height;

        destroy();
        create( width, height, VF_X8R8G8B8 );
    }

    if ( grabBackbuffer() ) {
        uploadBitmap();
    }

    return getMaterial();
}

void MngScreenshots::save( const char *pFilename,
                           const char *pContext,
                           const wchar_t *pContextDisplay,
                           const char *pMetaInfos,
                           uint32_t width,
                           uint32_t height ) {
    // FUN_006cd030
    Texture *pTexture = getBitmap();
    if ( pTexture == nullptr ) {
        OTDU_LOG_ERROR( "Cannot save '%s': no capture bitmap\n",
                        ( pFilename != nullptr ) ? pFilename : "" );
        SaveState = SWS_Failure;
        return;
    }

    SaveState = SWS_Unknown;
    State = SRS_Saving;

    strncpy( pFilenameToSave, pFilename, kMaxStringLength );
    pFilenameToSave[kMaxStringLength - 1] = '\0';

    strncpy( pContextToSave, pContext, kMaxStringLength );
    pContextToSave[kMaxStringLength - 1] = '\0';

    wcsncpy( pContextDisplayToSave, pContextDisplay, kMaxStringLength );
    pContextDisplayToSave[kMaxStringLength - 1] = L'\0';

    if ( pMetaInfos == nullptr ) {
        pMetaInfosToSave[0] = '\0';
    } else {
        strncpy( pMetaInfosToSave, pMetaInfos, kMaxStringLength );
        pMetaInfosToSave[kMaxStringLength - 1] = '\0';
    }

    SaveWidth = ( width == kUseBitmapSize ) ? pTexture->Width : width;
    SaveHeight = ( height == kUseBitmapSize ) ? pTexture->Height : height;

    gpTestDriveInstance->pushResourceToStream( this );
}

void MngScreenshots::instanciate() {
    // FUN_006ce570
    if ( State != SRS_Saving ) {
        return;
    }

    Texture *pTexture = getBitmap();
    if ( pTexture == nullptr ) {
        SaveState = SWS_Failure;
        State = SRS_Ready;
        return;
    }

    // Produce thumbnail (".small" .jpeg)
    const bool bNeedRescale =
        ( pTexture->Width != SaveWidth || pTexture->Height != SaveHeight );

    void *pContent = nullptr;
    uint32_t contentSize = 0;
    bool bSuccess = encodeShot( SaveWidth, SaveHeight, pMetaInfosToSave,
                                &pContent, &contentSize );

    void *pThumbnailContent = nullptr;
    uint32_t thumbnailSize = 0;
    if ( bSuccess && !bNeedRescale ) {
        bSuccess = encodeShot( kThumbnailWidth, kThumbnailHeight, nullptr,
                               &pThumbnailContent, &thumbnailSize );
    }

    if ( bSuccess ) {
        if ( pContextToSave[0] == '\0' || gpFile == nullptr ) {
            bSuccess =
                GSFile::SaveFile( pFilenameToSave, pContent, contentSize );
        } else {
            bSuccess = gpFile->saveFileInContext(
                pContextToSave, pFilenameToSave, pContextDisplayToSave,
                pContent, contentSize );

            if ( bSuccess && pThumbnailContent != nullptr ) {
                char pThumbnailName[kMaxStringLength + 8];
                snprintf( pThumbnailName, sizeof( pThumbnailName ), "%s.small",
                          pFilenameToSave );

                bSuccess = gpFile->saveFileInContext(
                    pContextToSave, pThumbnailName, pContextDisplayToSave,
                    pThumbnailContent, thumbnailSize );
            }
        }
    }

    if ( pContent != nullptr ) {
        TestDrive::FreeAligned( pContent );
    }
    if ( pThumbnailContent != nullptr ) {
        TestDrive::FreeAligned( pThumbnailContent );
    }

    SaveState = bSuccess ? SWS_Success : SWS_Failure;
    State = SRS_Ready;
}

Material *MngScreenshots::loadFromMemory( const void *pContent,
                                          uint32_t contentSize ) {
    // FUN_006ce2b0
    if ( Format != VF_SRGB_R8G8B8A8 ) {
        Texture *pTexture = getBitmap();
        if ( pTexture == nullptr ) {
            return nullptr;
        }

        const uint32_t width = pTexture->Width;
        const uint32_t height = pTexture->Height;

        destroy();
        create( width, height, VF_SRGB_R8G8B8A8 );
    }

    Texture *pTexture = getBitmap();
    if ( pTexture == nullptr ) {
        return nullptr;
    }

    uint8_t *pTexels = (uint8_t *)Render2DB::GetTexelsPointer( pTexture, 0 );
    if ( pTexels == nullptr ) {
        return nullptr;
    }

    if ( !DecompressJPEG( pContent, contentSize, pTexels, pTexture->Width,
                          pTexture->Height ) ) {
        return nullptr;
    }

    pTexture->bUploaded = false;
    uploadBitmap();

    return getMaterial();
}

Material *MngScreenshots::loadFromFile( const char *pFilepath ) {
    // FUN_006cedb0
    void *pContent = nullptr;
    uint32_t contentSize = 0;
    if ( !gpFile->loadFile( pFilepath, &pContent, &contentSize ) ) {
        if ( pContent != nullptr ) {
            TestDrive::Free( pContent );
        }
        return nullptr;
    }

    Material *pMaterial = loadFromMemory( pContent, contentSize );

    if ( pContent != nullptr ) {
        TestDrive::Free( pContent );
    }

    return pMaterial;
}

Texture *MngScreenshots::getBitmap() const {
    RenderFile::Section *pSection = render2DB.getBitmapSection();
    return ( pSection != nullptr ) ? (Texture *)( pSection + 1 ) : nullptr;
}

Material *MngScreenshots::getMaterial() {
    if ( p2DMData == nullptr || State == SRS_UnInstalled ) {
        return nullptr;
    }
    return render2DM.getMaterialAtIndex( 0 );
}

void MngScreenshots::uploadBitmap() {
    Texture *pTexture = getBitmap();
    if ( pTexture == nullptr ) {
        return;
    }

    if ( pTexture->pTexture == nullptr ) {
        Render2DB::CreateAndUploadTexture( pTexture );
        return;
    }

    Render2DB::PrepareForUpload( pTexture, true );
}

bool MngScreenshots::grabBackbuffer() {
    // FUN_005136b0
    Texture *pTexture = getBitmap();
    if ( pTexture == nullptr || gEdRender.pRenderDevice == nullptr ) {
        return false;
    }

    uint8_t *pTexels = (uint8_t *)Render2DB::GetTexelsPointer( pTexture, 0 );
    if ( pTexels == nullptr ) {
        return false;
    }

    std::vector<uint8_t> backbuffer;
    uint32_t width = 0;
    uint32_t height = 0;
    eViewFormat format = VF_Invalid;
    if ( !gEdRender.pRenderDevice->copyBackbufferToHost( backbuffer, width,
                                                         height, format ) ) {
        return false;
    }

    if ( format != VF_X8R8G8B8 && format != VF_A8R8G8B8 ) {
        OTDU_UNIMPLEMENTED_ONCE; // TODO: RENDER
        OTDU_LOG_WARN( "Cannot capture a screenshot from a %u backbuffer\n",
                       (uint32_t)format );
        return false;
    }

    const size_t expectedSize = (size_t)width * height * 4;
    if ( backbuffer.size() < expectedSize ) {
        OTDU_LOG_ERROR( "Backbuffer readback is too small (%zu < %zu)\n",
                        backbuffer.size(), expectedSize );
        return false;
    }

    ResampleBGRA( backbuffer.data(), width, height, pTexels, pTexture->Width,
                  pTexture->Height );

    const size_t texelCount = (size_t)pTexture->Width * pTexture->Height;
    for ( size_t i = 0; i < texelCount; i++ ) {
        pTexels[i * 4 + 3] = 0xff;
    }

    return true;
}

bool MngScreenshots::encodeShot( uint32_t width,
                                 uint32_t height,
                                 const char *pMetaInfos,
                                 void **ppOutContent,
                                 uint32_t *pOutContentSize ) {
    // FUN_0045f660 + FUN_0045f6c0
    Texture *pTexture = getBitmap();
    if ( pTexture == nullptr ) {
        return false;
    }

    const uint8_t *pTexels =
        (const uint8_t *)Render2DB::GetTexelsPointer( pTexture, 0 );
    if ( pTexels == nullptr ) {
        return false;
    }

    return CompressToJPEG( pTexels, pTexture->Width, pTexture->Height, width,
                           height, kDefaultQuality, pMetaInfos, ppOutContent,
                           pOutContentSize );
}
