#include "shared.h"
#include "postfx_envmap_scene.h"

#include "render/ed_render.h"
#include "render/gs_render.h"
#include "render/gs_render_helper.h"
#include "render/material.h"
#include "render/postfx/postfx_renderer.h"
#include "render/render_context.h"
#include "render/render_target.h"

PostFXNodeEnvMapScene::PostFXNodeEnvMapScene()
    : PostFXNode( 1, 0 ),
      blendOP( eBlendOP::BO_Disabled ),
      blendSrc( eBlendMode::BM_One ),
      blendDst( eBlendMode::BM_Zero ) {
    // FUN_0050cd70
    pPostFX = new ( TestDrive::Alloc( sizeof( PostFXEnvMapScene ) ) )
        PostFXEnvMapScene( this );
}

PostFXNodeEnvMapScene::~PostFXNodeEnvMapScene() { destroyEffect(); }

uint32_t PostFXNodeEnvMapScene::getType() const { return 0x13; }

PostFXEnvMapScene::PostFXEnvMapScene( PostFXNodeEnvMapScene *pOwner )
    : PostFX( pOwner ) {
    // FUN_005f7280
}

PostFXEnvMapScene::~PostFXEnvMapScene() {
    // FUN_005fc710
}

bool PostFXEnvMapScene::execute() {
    // FUN_005f72d0
    bool bVar1 = PostFX::execute();
    if ( !bVar1 ) {
        return false;
    }

    RenderTarget *pInput = nullptr;
    PostFX *peVar5 = pOwner->getInputNode( 0 );
    if ( peVar5 != nullptr ) {
        uint32_t uVar6 = pOwner->getInputLinkIndex( 0 );
        pInput = peVar5->getOutput( uVar6 );
    }

    RenderTarget *pBoundRT = gEdRender.getBoundRenderTargetAtIndex( 0 );
    gEdRender.bindRenderTargetAndSetViewport( pBoundRT, 0 );

    gPostFXRenderer.bindShaders( nullptr, nullptr );

    uint32_t uVar12 = pBoundRT->getHeight();
    uint32_t uVar13 = pBoundRT->getWidth();
    gPostFXRenderer.setRenderPassDimensions( (float)uVar13, (float)uVar12 );

    Material eStack_e0;
    gPostFXRenderer.bindDefaultBlendState( &eStack_e0 );

    PostFXNodeEnvMapScene *pNode = (PostFXNodeEnvMapScene *)pOwner;
    eStack_e0.BlendOP = pNode->getBlendOP();
    eStack_e0.SrcBlend = pNode->getBlendSrc();
    eStack_e0.DstBlend = pNode->getBlendDst();

    gPostFXRenderer.bindRasterState( &eStack_e0 );

    gpRenderContext->bindTexture( pInput->getBound2DB()->getFirstBitmap(), 0 );

    uint32_t uVar6 = pNode->getUnknownMask();
    if ( uVar6 != 0xffffffff ) {
        uVar13 = 1 << ( (int8_t)uVar6 & 0x1f );
    } else {
        uVar13 = 0xffffffff;
    }
    gPostFXRenderer.drawFullscreenQuad( uVar6 != 0xffffffff, false, false,
                                        uVar13 );

    gPostFXRenderer.bindShaders();
    gPostFXRenderer.bindRasterState( nullptr );

    if ( ( pOwner->getFlags() & 1 ) != 0 ) {
        gEdRender.pRenderDevice->resolveMSAA( pBoundRT, 0 );
    }
    return true;
}
