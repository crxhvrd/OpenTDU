#include "shared.h"
#include "mng_fade.h"

#include "gs_flash.h"
#include "gs_timer.h"
#include "modal_message_box.h"
#include "render/draw_list.h"
#include "render/gs_render.h"

#include "tdu_instance.h"

MngFade *gpMngFade = nullptr; // DAT_010e7fe4

MngFade::MngFade()
    : Manager(),
      pDrawListTransition( new DrawList() ),
      transitionColor( 0u ),
      flags( 6 ),
      transitionDuration( 1.0f ),
      pCallback( nullptr ),
      pCallbackSource( nullptr ),
      pCallbackData( nullptr ),
      position( 0.0f, 0.0f ),
      width( 1.0f ),
      height( 1.0f ),
      elapsedTime( 0.0f ),
      logoElapsedTime( -1.0f ),
      pDrawListLogo( new DrawList() ),
      p2DMBuffer( nullptr ),
      pMaterial( nullptr ),
      bRenderLogo( false ),
      bWaitForPopup( false ) {
    // FUN_006cf960
    if ( gpMngFade == nullptr ) {
        gpMngFade = this;
    }
}

MngFade::~MngFade() {}

void MngFade::startTransition( const uint32_t newFlags,
                               const float durationInSeconds,
                               PostTransitionCallback_t callback,
                               void *pSource,
                               void *pData ) {
    transitionDuration = durationInSeconds;
    pCallback = callback;
    pCallbackSource = pSource;
    pCallbackData = pData;
    flags = newFlags;
    elapsedTime = 0.0f;
}

bool MngFade::initialize( TestDriveGameInstance * ) {
    // FUN_006cfb30
    DrawStreams streams;
    streams.Normals = '\0';
    streams.Diffuse = '\x01';
    streams.Specular = '\0';
    streams.UVMap = '\0';
    streams.Tangent = '\0';
    streams.Binormal = '\0';
    streams.bIndex16 = '\x01';
    streams.Custom = '\0';
    pDrawListTransition->initialize( 1, 4, 0, true, &streams, "Trans_rlist2d",
                                     false );

    streams.UVMap = '\x01';
    pDrawListLogo->initialize( 1, 4, 0, true, &streams, "Logo_rlist2d", false );

    return true;
}

void MngFade::tick( float deltaTime, float ) {
    // FUN_006cfc40
    if ( ( deltaTime <= 0.0f ) && ( gpTimer->GameSpeed <= 0.0f ) ) {
        deltaTime = gpTimer->SystemDeltaTime;
    }
    if ( 0.1f < deltaTime ) {
        deltaTime = 0.1f;
    }
    if ( pDrawListTransition == nullptr ) {
        return;
    }

    pDrawListTransition->reset();
    bool bVar2 = false;
    uint8_t alpha = 0x0;
    float fVar7 = 0.0f;
    if ( ( flags & 1 ) != 0 ) {
        float fVar6 = ( elapsedTime / transitionDuration ) * 255.0f;
        elapsedTime += deltaTime;
        bVar2 = true;
        fVar7 = fVar6;
        if ( ( flags >> 2 & 1 ) != 0 ) {
            fVar7 = 255.0f - fVar6;
        }
        alpha = static_cast<uint8_t>(
            Clamp( static_cast<int32_t>( fVar7 ), 0, 255 ) );

        if ( 255.0 < fVar6 ) {
            flags &= 0xfffffffe;
            elapsedTime = 0.0f;

            if ( pCallback != nullptr ) {
                PostTransitionCallback_t callback = std::move( pCallback );
                pCallback = nullptr;
                callback( pCallbackData, pCallbackSource );
            }

            if ( ( flags >> 2 & 1 ) != 0 ) {
                transitionColor[0] = '\0';
                transitionColor[1] = '\0';
                transitionColor[2] = '\0';
            }
        }
    }

    if ( ( flags >> 3 & 1 ) != 0 ) {
        alpha = 0xff;
    }

    fVar7 = 0.0f;
    if ( ( flags >> 1 & 1 ) == 0 ) {
        fVar7 = -1.0f;
    }

    if ( bVar2 || ( flags >> 3 & 1 ) != 0 ) {
        encodeTransitionDrawList( alpha, fVar7 );
    }

    if ( pDrawListLogo != nullptr && bRenderLogo ) {
        pDrawListLogo->reset();
        encodeLogoDrawList();

        if ( !bWaitForPopup ) {
            FlashPlayer *pPlayer = gpFlash->getFlashPlayer( "GENERAL" );
            OTDU_ASSERT( pPlayer );

            if ( gpActiveGameMode != nullptr && !gbLoadingInProgress ) {
                bWaitForPopup = gpFlashMessageBox->display(
                    0x36c66a2, 4.0f, 0, nullptr, nullptr, false );
            }
        }

        if ( !bRenderLogo && !gbLoadingInProgress ) {
            return;
        }
    }

    if ( bWaitForPopup ) {
        FlashPlayer *peVar3 = gpFlash->getFlashPlayer( "GENERAL" );
        if ( peVar3 != nullptr ) {
            bWaitForPopup = false;
            gpFlashMessageBox->close();
        }
    }
}

void MngFade::terminate() {
    // FUN_006cf8c0
    pDrawListLogo->destroy();
    if ( pDrawListLogo != nullptr ) {
        delete pDrawListLogo;
    }
    pDrawListLogo = nullptr;

    pDrawListTransition->destroy();
    if ( pDrawListTransition != nullptr ) {
        delete pDrawListTransition;
    }
    pDrawListTransition = nullptr;
}

void MngFade::draw() {
    // FUN_006cfa90
    if ( pDrawListTransition != nullptr ) {
        RenderPass &pass =
            ( ( flags >> 1 & 1 ) == 0 )
                ? gpRender->getRenderPass<eRenderPass::RP_After2D>()
                : gpRender->getRenderPass<eRenderPass::RP_Before2D>();

        pass.pScene->enqueueDynamicDrawList( pDrawListTransition );
    }

    if ( pDrawListLogo != nullptr ) {
        gpRender->getRenderPass<eRenderPass::RP_After2D>()
            .pScene->enqueueDynamicDrawList( pDrawListLogo );
    }
}

void MngFade::pause() {
    // FUN_006cfa50
    if ( bWaitForPopup ) {
        FlashPlayer *peVar1 = gpFlash->getFlashPlayer( "GENERAL" );
        if ( peVar1 != nullptr ) {
            bWaitForPopup = false;
            gpFlashMessageBox->close();
        }
    }
}

void MngFade::encodeTransitionDrawList( const uint8_t alpha,
                                        const float fVar7 ) {
    pDrawListTransition->beginPrimitive( ePrimitiveType::PT_TriangleStrip, 4 );

    ColorRGBA color = transitionColor;
    color.A = alpha;
    pDrawListTransition->setDiffuse( 0, color );

    pDrawListTransition->pushVertex( position[0], position[1], fVar7 );
    pDrawListTransition->pushVertex( position[0] + width, position[1], fVar7 );
    pDrawListTransition->pushVertex( position[0], position[1] + height, fVar7 );
    pDrawListTransition->pushVertex( position[0] + width, position[1] + height,
                                     fVar7 );

    pDrawListTransition->commitPrimitive();
}

void MngFade::encodeLogoDrawList() {
    // FUN_006cf730
    static constexpr float FLOAT_00fabf34 = 0.05f;
    // TODO: Looks like this is never called? (p2DMBuffer is always null at
    // runtime)
    if ( p2DMBuffer != nullptr ) {
        OTDU_UNIMPLEMENTED;
    }
}
