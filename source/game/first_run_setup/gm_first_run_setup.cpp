#include "shared.h"
#include "gm_first_run_setup.h"

#include "asset_extractor/asset_extractor.h"
#include "filesystem/gs_file.h"
#include "render/ed_render.h"
#include "render/gs_render.h"
#include "render/text_renderer.h"

#include <chrono>
static bool AreGeneratedMoviesBuilt() {
    char pWorkingDir[OTDU_MAX_PATH];
    TestDrive::GetWorkingDir( pWorkingDir, OTDU_MAX_PATH );
    return AssetExtractor::AreGeneratedMoviesBuilt( pWorkingDir );
}

// Frame time spent building PSOs (keeps the window responsive)
static constexpr double kPSOBuildBudgetMs = 33.0;
static constexpr float kDisplayDelay = 0.5f;

static constexpr ColorRGBA kProgressBarBackground( 0x30, 0x30, 0x30, 0xff );
static constexpr ColorRGBA kProgressBarFill( 0xd0, 0xd0, 0xd0, 0xff );

static bool AreFarImpostorsBuilt() {
    char pWorkingDir[OTDU_MAX_PATH];
    TestDrive::GetWorkingDir( pWorkingDir, OTDU_MAX_PATH );
    return AssetExtractor::AreFarImpostorsBuilt( pWorkingDir );
}

bool GMFirstRunSetup::IsRequired() {
    return !AreGeneratedMoviesBuilt() || !AreFarImpostorsBuilt() ||
           gEdRender.pRenderDevice->getNumRecordedPSOs() != 0;
}

GMFirstRunSetup::GMFirstRunSetup()
    : GameMode(),
      drawList(),
      bGeneratedMoviesBuilt( false ),
      numPSOsToBuild( gEdRender.pRenderDevice->getNumRecordedPSOs() ),
      elapsedTime( 0.0f ) {
    DrawStreams streams;
    streams.Tangent = '\0';
    streams.Binormal = '\0';
    streams.bIndex16 = '\1';
    streams.Custom = '\0';
    streams.Normals = '\0';
    streams.Diffuse = '\1';
    streams.Specular = '\0';
    streams.UVMap = '\0';

    drawList.initialize( 4, 16, 0, true, &streams, "FirstRunSetup", false );
}

GMFirstRunSetup::~GMFirstRunSetup() { drawList.destroy(); }

void GMFirstRunSetup::tick( float deltaTime, float totalTime ) {
    elapsedTime += deltaTime;

    if ( !bGeneratedMoviesBuilt ) {
        if ( !AreGeneratedMoviesBuilt() ) {
            char pWorkingDir[OTDU_MAX_PATH];
            TestDrive::GetWorkingDir( pWorkingDir, OTDU_MAX_PATH );
            if ( !AssetExtractor::BuildGeneratedMovies( pWorkingDir ) ) {
                OTDU_LOG_WARN(
                    "Failed to generate the OpenTDU front-end movies\n" );
            }
        }
        if ( !AreFarImpostorsBuilt() ) {
            char pWorkingDir[OTDU_MAX_PATH];
            TestDrive::GetWorkingDir( pWorkingDir, OTDU_MAX_PATH );
            if ( !AssetExtractor::BuildFarImpostors( pWorkingDir ) ) {
                OTDU_LOG_WARN( "Failed to bake the far impostor pack\n" );
            }
        }
        bGeneratedMoviesBuilt = true;
    }

    RenderDevice *pRenderDevice = gEdRender.pRenderDevice;
    size_t numRemaining = 0;
    const auto startTime = std::chrono::steady_clock::now();
    do {
        numRemaining = pRenderDevice->prebuildRecordedPSOs( 1 );
    } while ( numRemaining != 0 &&
              std::chrono::duration<double, std::milli>(
                  std::chrono::steady_clock::now() - startTime )
                      .count() < kPSOBuildBudgetMs );

    if ( numRemaining == 0 ) {
        OTDU_LOG_INFO( "First run setup done (%.2f s)\n", elapsedTime );
        bExitRequested = true;
        return;
    }

    if ( elapsedTime >= kDisplayDelay && numPSOsToBuild != 0 ) {
        drawProgress( 1.0f - static_cast<float>( numRemaining ) /
                                 static_cast<float>( numPSOsToBuild ) );
    }
}

void GMFirstRunSetup::drawProgress( const float progress ) {
    const Viewport &viewport2D = gpRender->getViewport2D();
    const float barWidth = viewport2D.Width * 0.5f;
    const float barHeight = viewport2D.Height * 0.01f;
    const float barX = ( viewport2D.Width - barWidth ) * 0.5f;
    const float barY = viewport2D.Height * 0.75f;

    drawList.reset();
    auto pushQuad = [&]( const float x, const float y, const float width,
                         const float height, const ColorRGBA &color ) {
        if ( !drawList.beginPrimitive( ePrimitiveType::PT_TriangleStrip, 4 ) ) {
            return;
        }
        drawList.setDiffuse( 0, color );
        drawList.pushVertex( x, y, -1.0f );
        drawList.pushVertex( x + width, y, -1.0f );
        drawList.pushVertex( x, y + height, -1.0f );
        drawList.pushVertex( x + width, y + height, -1.0f );
        drawList.commitPrimitive();
    };

    pushQuad( 0.0f, 0.0f, viewport2D.Width, viewport2D.Height, gColorBlack );
    pushQuad( barX, barY, barWidth, barHeight, kProgressBarBackground );
    pushQuad( barX, barY, barWidth * Clamp( progress, 0.0f, 1.0f ), barHeight,
              kProgressBarFill );

    gpRender->getRenderPass<eRenderPass::RP_After2D>()
        .pScene->enqueueDynamicDrawList( &drawList );

    const float scaleX =
        static_cast<float>( gpRender->getWidth() ) / viewport2D.Width;
    const float scaleY =
        static_cast<float>( gpRender->getHeight() ) / viewport2D.Height;

    char pText[64];
    snprintf( pText, sizeof( pText ), "Preparing shaders... %u%%",
              static_cast<uint32_t>( progress * 100.0f ) );

    gTextRenderer.setColor( gColorWhite );
    DrawDebugText(
        static_cast<uint32_t>( barX * scaleX ),
        static_cast<uint32_t>( ( barY - viewport2D.Height * 0.04f ) * scaleY ),
        pText );
}
