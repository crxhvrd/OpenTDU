#include "shared.h"
#include "first_person_pointer.h"

#include "config/gs_config.h"
#include "core/maths_helper.h"
#include "filesystem/gs_file.h"
#include "render/gs_render.h"
#include "render/material.h"
#include "render/render_scene.h"

static constexpr const char *kPointerBitmapPath =
    "OpenTDU/first_person_pointer.2db"; // TODO: PLACEHODLER

static constexpr uint64_t kPointerMaterialHashcode = 0x230588678a67836dull;
static constexpr uint8_t kFlashOrderTable = 0x2a;
static constexpr uint64_t kBoundTextureHashcode = 0x0000000100000000ull;

static constexpr float kPointerSizePixels = 48.0f; // TODO: PLACEHOLDER
static constexpr float kReferenceHeight = 720.0f;

static constexpr float kHighlightFadeDuration = 0.15f;
static constexpr float kHighlightScale = 1.12f;
static constexpr float kPulseAmplitude = 0.10f;
static constexpr float kPulseFrequency = 1.5f;

FirstPersonPointer::FirstPersonPointer()
    : pBitmapResource( nullptr ),
      bitmap(),
      pTexture( nullptr ),
      pMaterialMemory( nullptr ),
      material2DM(),
      pMaterial( nullptr ),
      drawList(),
      highlightBlend( 0.0f ),
      pulsePhase( 0.0f ),
      bLoaded( false ),
      bHighlighted( false ) {}

FirstPersonPointer::~FirstPersonPointer() { destroy(); }

bool FirstPersonPointer::loadBitmap() {
    std::string resPath = gpConfig->getResRootPath();
    resPath += kPointerBitmapPath;

    if ( !gpFile->loadFile( resPath.c_str(), &pBitmapResource, nullptr ) ) {
        return false;
    }

    if ( !bitmap.initialize( pBitmapResource ) ) {
        return false;
    }

    pTexture = bitmap.getFirstBitmap();
    if ( pTexture == nullptr || !Render2DB::UploadTextureToGPU( pTexture ) ) {
        return false;
    }

    return true;
}

bool FirstPersonPointer::initialize() {
    if ( bLoaded ) {
        return true;
    }

    if ( !loadBitmap() ) {
        destroy();
        return false;
    }

    const uint32_t materialSize = Render2DM::CalcSize( 0, 0 );
    pMaterialMemory = TestDrive::Alloc( materialSize );
    if ( pMaterialMemory == nullptr ) {
        destroy();
        return false;
    }

    pMaterial = Render2DM::Create( pMaterialMemory, kPointerMaterialHashcode, 1,
                                   0, 0, 0 );
    if ( pMaterial == nullptr ) {
        destroy();
        return false;
    }

    pMaterial->DepthTest = '\x01';
    pMaterial->DepthWrite = '\x02';
    pMaterial->AlphaTest = eComparisonFunction::CF_Never;
    pMaterial->SrcBlend = eBlendMode::BM_SrcAlpha;
    pMaterial->DstBlend = eBlendMode::BM_InvSrcAlpha;
    pMaterial->BlendOP = eBlendOP::BO_Add;
    pMaterial->AlphaSource = '\0';
    pMaterial->BackfaceCullingMode = eCullMode::CM_Disabled;

    MaterialParameter *pParameter = pMaterial->getParameterByIndex( 0 );
    pParameter->Flags.NumPrelight = '\x01';
    pParameter->Flags.NumUVMaps = '\x01';
    pParameter->Flags.NumTextures = '\x01';

    MaterialLayer *pLayer = pParameter->getLayer( 0 );
    memset( pLayer->pLayerTextures, 0,
            sizeof( MaterialLayerTexture ) * MaterialLayer::kMaxNumTextures );
    pLayer->pLayerTextures[0].Hashcode = kBoundTextureHashcode;
    pLayer->pLayerTextures[0].pTextureSection = bitmap.getBitmapSection();
    pLayer->pLayerTextures[0].SamplerAddress[0] = MTM_Clamp;
    pLayer->pLayerTextures[0].SamplerAddress[1] = MTM_Clamp;
    pLayer->pLayerTextures[0].SamplerAddress[2] = MTM_Clamp;
    pLayer->pLayerTextures[0].MipFilter = eTextureFiltering::TF_Point;

    if ( !material2DM.initialize( pMaterialMemory ) ) {
        destroy();
        return false;
    }
    pMaterial->OT = kFlashOrderTable;

    DrawStreams streams;
    streams.Normals = '\0';
    streams.Diffuse = '\x01';
    streams.Specular = '\0';
    streams.Tangent = '\0';
    streams.Binormal = '\0';
    streams.UVMap = '\x01';
    streams.bIndex16 = '\x01';

    if ( !drawList.initialize( 1, 4, 0, true, &streams, "FirstPersonPointer",
                               false ) ) {
        destroy();
        return false;
    }

    bLoaded = true;

    OTDU_LOG_DEBUG( "Cursor '%s' loaded\n", kPointerBitmapPath );

    return true;
}

void FirstPersonPointer::destroy() {
    if ( drawList.isInitialized() ) {
        drawList.destroy();
    }

    material2DM.destroy();
    pMaterial = nullptr;

    if ( pMaterialMemory != nullptr ) {
        TestDrive::Free( pMaterialMemory );
        pMaterialMemory = nullptr;
    }

    pTexture = nullptr;
    bitmap.destroy();

    if ( pBitmapResource != nullptr ) {
        TestDrive::Free( pBitmapResource );
        pBitmapResource = nullptr;
    }

    highlightBlend = 0.0f;
    pulsePhase = 0.0f;
    bHighlighted = false;
    bLoaded = false;
}

void FirstPersonPointer::draw( float deltaTime ) {
    if ( !bLoaded || gpRender == nullptr ) {
        return;
    }

    const float renderWidth = (float)gpRender->getWidth();
    const float renderHeight = (float)gpRender->getHeight();
    if ( renderWidth <= 0.0f || renderHeight <= 0.0f ) {
        return;
    }

    const float fadeStep = deltaTime / kHighlightFadeDuration;
    highlightBlend = bHighlighted ? Min( highlightBlend + fadeStep, 1.0f )
                                  : Max( highlightBlend - fadeStep, 0.0f );

    if ( highlightBlend > 0.0f ) {
        pulsePhase =
            std::fmod( pulsePhase + deltaTime * kPulseFrequency, 1.0f );
    } else {
        pulsePhase = 0.0f;
    }

    // Smoothstep so the ring eases in/out instead of popping
    const float blend =
        highlightBlend * highlightBlend * ( 3.0f - 2.0f * highlightBlend );
    const float pulse = 0.5f - 0.5f * std::cos( pulsePhase * kTwoPI );
    const float scale =
        1.0f + blend * ( ( kHighlightScale - 1.0f ) + kPulseAmplitude * pulse );

    const float sizePixels =
        kPointerSizePixels * scale * renderHeight / kReferenceHeight;
    const float halfHeight = ( sizePixels / renderHeight ) * 0.5f;
    const float halfWidth = ( sizePixels / renderWidth ) * 0.5f;

    const float left = 0.5f - halfWidth;
    const float right = 0.5f + halfWidth;
    const float top = 0.5f - halfHeight;
    const float bottom = 0.5f + halfHeight;

    drawList.reset();
    drawList.setActiveMaterial( pMaterial );

    if ( !drawList.beginPrimitive( ePrimitiveType::PT_TriangleStrip, 4 ) ) {
        return;
    }

    drawList.setDiffuse( 0, ColorRGBA( 0xa0ffffffu ) );

    drawList.setTexCoords( 0, { 0.0f, 0.0f } );
    drawList.pushVertex( left, top, 0.0f );

    drawList.setTexCoords( 0, { 0.0f, 1.0f } );
    drawList.pushVertex( left, bottom, 0.0f );

    drawList.setTexCoords( 0, { 1.0f, 0.0f } );
    drawList.pushVertex( right, top, 0.0f );

    drawList.setTexCoords( 0, { 1.0f, 1.0f } );
    drawList.pushVertex( right, bottom, 0.0f );

    drawList.commitPrimitive();

    RenderScene *pScene = gpRender->getRenderPass<eRenderPass::RP_2D>().pScene;
    if ( pScene != nullptr ) {
        pScene->enqueueDynamicDrawList( &drawList );
    }
}
