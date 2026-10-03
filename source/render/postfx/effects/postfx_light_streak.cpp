#include "shared.h"
#include "postfx_light_streak.h"

#include "render/postfx/postfx_renderer.h"
#include "render/shaders/shader_types.h"
#include "render/shaders/shader_constant_map.h"
#include "render/state_cache.h"
#include "render/render_context.h"
#include "render/render_target.h"

#include <cmath>

PostFXLightStreak::PostFXLightStreak( PostFXNodeLightStreak *pOwner )
    : PostFX( pOwner ) {
    // FUN_005f9910
}

PostFXLightStreak::~PostFXLightStreak() {
    // FUN_005fc710
}

// Diagonal streak directions
static const float kStreakDirs[4][2] = {
    {1.0f,  1.0f },
    {-1.0f, 1.0f },
    {1.0f,  -1.0f},
    {-1.0f, -1.0f},
};

// Per-tap chromatic tint for each of the 4 taps
static const float kStreakTints[4][4] = {
    {1.0f, 1.0f, 1.0f, 1.0f},
    {1.0f, 0.7f, 0.7f, 1.0f},
    {0.7f, 1.0f, 0.7f, 1.0f},
    {0.7f, 0.7f, 1.0f, 1.0f},
};

bool PostFXLightStreak::execute() {
    // FUN_005f9960
    bool bVar1 = PostFX::execute();
    if ( !bVar1 ) {
        return false;
    }

    PostFXNodeLightStreak *pNode = (PostFXNodeLightStreak *)pOwner;

    // Resolve the input node's bright-pass output (the streak source image).
    RenderTarget *peStack_16c = nullptr;
    PostFX *pInputEffect = pNode->getInputNode( 0 );
    if ( pInputEffect != nullptr ) {
        uint32_t uInputLink = pNode->getInputLinkIndex( 0 );
        peStack_16c = pInputEffect->getOutput( uInputLink );
    }

    gPostFXRenderer.bindShaders( pVertexShader, pPixelShader );

    const uint32_t numPass = pNode->numPass;

    RenderTarget **ppRenderTargets = pNode->ppRenderTargets;
    RenderTarget *peVar4 = ppRenderTargets[4];
    RenderTarget *peVar5 = ppRenderTargets[3];
    RenderTarget *peVar9 = nullptr;
    RenderTarget *peStack_170 = nullptr;
    RenderTarget *peVar16 = nullptr;
    RenderTarget *peVar12 = nullptr;
    RenderTarget *peVar17 = nullptr;

    int32_t iStack_158 = 0;
    do {
        if ( 0 < numPass ) {
            peVar16 = peVar17;
            uint32_t iStack_168 = 0;
            // Inner loop: geometrically-growing blur passes (Kawase star).
            do {
                peVar12 = peStack_16c;
                if ( numPass == 1 ) {
                    peVar17 = ppRenderTargets[iStack_158];
                    peVar9 = peVar12;
                } else {
                    peVar12 = peVar4;
                    peVar17 = peVar16;
                    peVar9 = peStack_170;
                    if ( numPass == 2 ) {
                        if ( iStack_168 == 0 ) {
                            peStack_170 = peStack_16c;
                            peVar17 = peVar4;
                            peVar9 = peStack_170;
                        } else if ( iStack_168 == 1 ) {
                            peVar17 = ppRenderTargets[iStack_158];
                            peVar9 = peVar12;
                        }
                    } else if ( numPass == 3 ) {
                        if ( iStack_168 == 0 ) {
                            peStack_170 = peStack_16c;
                            peVar17 = peVar5;
                            peVar9 = peStack_170;
                        } else {
                            peVar17 = peVar4;
                            peVar9 = peVar5;
                            if ( ( iStack_168 != 1 ) &&
                                 ( peVar17 = peVar16, peVar9 = peStack_170,
                                   iStack_168 == 2 ) ) {
                                peVar17 = ppRenderTargets[iStack_158];
                                peVar9 = peVar12;
                            }
                        }
                    }
                }
                peStack_170 = peVar9;

                float fStride = std::pow( 4.0f, (float)iStack_168 );

                float afStack_50[16];
                for ( int32_t s = 0; s < 4; s++ ) {
                    float fWeight = std::pow( 0.9f, (float)s * fStride );
                    afStack_50[s * 4 + 0] = kStreakTints[s][0] * fWeight;
                    afStack_50[s * 4 + 1] = kStreakTints[s][1] * fWeight;
                    afStack_50[s * 4 + 2] = kStreakTints[s][2] * fWeight;
                    afStack_50[s * 4 + 3] = kStreakTints[s][3] * fWeight;
                }

                float fInvHeight = 1.0f / (float)peStack_170->getHeight();
                float fInvWidth = 1.0f / (float)peStack_170->getWidth();

                float fStack_120[4];
                fStack_120[0] =
                    kStreakDirs[iStack_158][0] * fInvWidth * fStride;
                fStack_120[1] =
                    fInvHeight * fStride * kStreakDirs[iStack_158][1];
                fStack_120[2] = 0.0f;
                fStack_120[3] = 0.0f;

                gPostFXRenderer.setRenderPassDimensions(
                    (float)peVar17->getWidth(), (float)peVar17->getHeight() );

                UploadConstant( &pVertexShader->getConstantMap()->FloatVS,
                                0x22c, fStack_120, 1 );
                UploadConstant( &pPixelShader->getConstantMap()->FloatPS, 0x250,
                                afStack_50, 4 );

                Render2DB *peVar15 = peStack_170->getBound2DB();
                Texture *peVar15Tex = peVar15->getFirstBitmap();
                gpRenderContext->bindTexture( peVar15Tex, 0 );

                SetSamplerFilter( peVar15Tex, 0, 5, TF_Linear );
                SetSamplerFilter( peVar15Tex, 0, 6, TF_Linear );

                gEdRender.bindRenderTarget( peVar17, 0, false );

                Viewport viewport;
                viewport.Width = (float)peVar17->getWidth();
                viewport.Height = (float)peVar17->getHeight();
                viewport.Flags = 0;

                gEdRender.pRenderDevice->setViewport( viewport );

                uint32_t uMask = pNode->getUnknownMask();
                uint32_t uLayerBit = 0xffffffff;
                if ( uMask != 0xffffffff ) {
                    uLayerBit = 1 << ( (int8_t)uMask & 0x1f );
                }
                gPostFXRenderer.drawFullscreenQuad( uMask != 0xffffffff, false,
                                                    false, uLayerBit );

                if ( ( pNode->getFlags() & 1 ) != 0 ) {
                    gEdRender.pRenderDevice->resolveMSAA( peVar17, 0 );
                }

                iStack_168 = iStack_168 + 1;
                peVar16 = peVar17;
            } while ( iStack_168 < numPass );
        }

        iStack_158 = iStack_158 + 1;
        if ( 3 < iStack_158 ) {
            PixelShader *pCombinePS =
                (PixelShader *)gpRenderContext->getShaderHashTable().getShader(
                    0x6c6cdc43c12bc9da );

            RenderTarget *pOutput = pRenderTargets[0];
            gEdRender.bindRenderTarget( pOutput, 0, false );

            Viewport DStack_150;
            DStack_150.Width = (float)pOutput->getWidth();
            DStack_150.Height = (float)pOutput->getHeight();
            DStack_150.Flags = 0;

            gEdRender.pRenderDevice->setViewport( DStack_150 );

            gPostFXRenderer.bindShaders( nullptr, pCombinePS );

            gPostFXRenderer.setRenderPassDimensions(
                (float)pOutput->getWidth(), (float)pOutput->getHeight() );

            for ( int32_t i = 0; i < 4; i++ ) {
                Render2DB *peVar14 = pNode->ppRenderTargets[i]->getBound2DB();
                Texture *peVar14Tex = peVar14->getFirstBitmap();

                gpRenderContext->bindTexture( peVar14Tex, i );

                SetSamplerFilter( peVar14Tex, i, 5, TF_Linear );
                SetSamplerFilter( peVar14Tex, i, 6, TF_Linear );
            }

            uint32_t uMask = pNode->getUnknownMask();
            if ( uMask == 0xffffffff ) {
                gPostFXRenderer.drawFullscreenQuad( false, false, false,
                                                    0xffffffff );
            } else {
                gPostFXRenderer.drawFullscreenQuad(
                    true, false, false, 1 << ( (int8_t)uMask & 0x1f ) );
            }
            gPostFXRenderer.bindShaders();

            if ( ( pOwner->getFlags() & 1 ) != 0 ) {
                gEdRender.pRenderDevice->resolveMSAA( pRenderTargets[0], 0 );
            }

            for ( int32_t i = 0; i < 4; i++ ) {
                gStateCache.setSamplerStateMinMagFilter( 0, TF_Point );
            }

            return true;
        }
    } while ( true );
}

uint64_t PostFXLightStreak::getVertexShaderHashcode() const {
    // FUN_005fc600
    return 0xecf48bb0ed0643c7;
}

uint64_t PostFXLightStreak::getPixelShaderHashcode() const {
    // FUN_005fc610
    return 0x6c6cdc43c22bc9da;
}

PostFXNodeLightStreak::PostFXNodeLightStreak()
    : PostFXNode( 1, 1 ), numPass( 3 ) {
    // FUN_0050cf30
    pPostFX = new ( TestDrive::Alloc( sizeof( PostFXLightStreak ) ) )
        PostFXLightStreak( this );
}

PostFXNodeLightStreak::~PostFXNodeLightStreak() {
    // FUN_0050db30
    destroyEffect();
}

uint32_t PostFXNodeLightStreak::getType() const {
    // FUN_0050d4f0
    return 0x17;
}
