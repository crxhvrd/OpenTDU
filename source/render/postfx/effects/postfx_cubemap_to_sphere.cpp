#include "shared.h"
#include "postfx_cubemap_to_sphere.h"

#include "render/ed_render.h"
#include "render/gs_render.h"
#include "render/gs_render_helper.h"
#include "render/postfx/postfx_renderer.h"
#include "render/render_context.h"
#include "render/render_target.h"

#include "render/shaders/shader_constant_map.h"
#include "render/shaders/shader_types.h"

PostFXNodeCubemapToSphere::PostFXNodeCubemapToSphere()
    : PostFXNode( 1, 1 ),
      saturation( 1.0f ), // FLOAT_00f4165c
      lumFactor( 0.0f ),
      hoodFactor( 0.0f ) {
    // FUN_0050ce00
    pPostFX = new ( TestDrive::Alloc( sizeof( PostFXCubemapToSphere ) ) )
        PostFXCubemapToSphere( this );
}

PostFXNodeCubemapToSphere::~PostFXNodeCubemapToSphere() {
    destroyEffect();
}

uint32_t PostFXNodeCubemapToSphere::getType() const { return 0x14; }

void PostFXNodeCubemapToSphere::setSaturation( float param_1 ) {
    // FUN_004302d0
    if ( param_1 < 0.0f ) {
        saturation = 0.0f;
        return;
    }
    saturation = Min( param_1, 1.0f /* FLOAT_00f4165c */ );
}

void PostFXNodeCubemapToSphere::setLumFactor( float param_1 ) {
    lumFactor = param_1;
}

void PostFXNodeCubemapToSphere::setHoodFactor( float param_1 ) {
    hoodFactor = param_1;
}

PostFXCubemapToSphere::PostFXCubemapToSphere(
    PostFXNodeCubemapToSphere *pOwner )
    : PostFX( pOwner ) {
    // FUN_005f9320
}

PostFXCubemapToSphere::~PostFXCubemapToSphere() {
    // FUN_005fc710
}

bool PostFXCubemapToSphere::execute() {
    // FUN_005f9370
    bool bVar1 = PostFX::execute();
    if ( !bVar1 ) {
        return false;
    }

    RenderTarget *pInput = nullptr;
    PostFX *peVar7 = pOwner->getInputNode( 0 );
    if ( peVar7 != nullptr ) {
        uint32_t uVar8 = pOwner->getInputLinkIndex( 0 );
        pInput = peVar7->getOutput( uVar8 );
    }

    gEdRender.bindRenderTargetAndSetViewport( pRenderTargets[0], 0 );
    gPostFXRenderer.bindShaders( pVertexShader, pPixelShader );

    RenderTarget *peVar3 = pRenderTargets[0];
    uint32_t uVar8 = peVar3->getHeight();
    uint32_t uVar9 = peVar3->getWidth();
    gPostFXRenderer.setRenderPassDimensions( (float)uVar9, (float)uVar8 );

    PostFXNodeCubemapToSphere *pNode = (PostFXNodeCubemapToSphere *)pOwner;

    Eigen::Vector4f pTVar10 = { pNode->hoodFactor, 0.0f, 0.0f, 0.0f };
    UploadConstant( &pVertexShader->getConstantMap()->FloatVS, 0x2c4,
                    pTVar10.data(), 1 );

    Eigen::Vector4f pTVar11 = { pNode->saturation, pNode->lumFactor, 0.0f,
                                0.0f };
    UploadConstant( &pPixelShader->getConstantMap()->FloatPS, 0x220,
                    pTVar11.data(), 1 );

    gpRenderContext->bindTexture( pInput->getBound2DB()->getFirstBitmap(), 0 );

    uint32_t pIVar5 = pNode->getUnknownMask();
    if ( pIVar5 != 0xffffffff ) {
        uVar8 = 1 << ( (int8_t)pIVar5 & 0x1f );
    } else {
        uVar8 = 0xffffffff;
    }
    gPostFXRenderer.drawFullscreenQuad( pIVar5 != 0xffffffff, false, false,
                                        uVar8 );

    gPostFXRenderer.bindShaders();
    if ( ( pOwner->getFlags() & 1 ) != 0 ) {
        gEdRender.pRenderDevice->resolveMSAA( pRenderTargets[0], 0 );
    }
    return true;
}

uint64_t PostFXCubemapToSphere::getVertexShaderHashcode() const {
    // FUN_005fc5d0
    return 0xecf48bb0f00643c7;
}

uint64_t PostFXCubemapToSphere::getPixelShaderHashcode() const {
    // FUN_005fc5e0
    return 0x6c6cdc43df2bc9da;
}
