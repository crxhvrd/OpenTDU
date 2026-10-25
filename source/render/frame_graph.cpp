#include "shared.h"
#include "frame_graph.h"

#include "render/gs_render.h"
#include "render/gs_render_helper.h"
#include "render/render_scene.h"

#include "render/postfx/postfx_node.h"
#include "render/postfx/postfx_stack.h"

#include "render/ed_render.h"
#include "render/render_context.h"
#include "render/scene_renderer.h"

#include "render/render_target.h"

FrameGraph gFrameGraph = {}; // DAT_00facb90

FrameGraph::FrameGraph()
    : numClearCommands( 0 ), bRemoveDynamicDrawCmds( false ) {}

FrameGraph::~FrameGraph() {}

void FrameGraph::initialize( FrameGraphConfig *param_2 ) {
    // FUN_0050e870
    config = *param_2;

    renderNodeAllocator.initialize(
        AllocateFromRenderPool(
            edStdAllocator<RenderObjectBase *>::GetRequiredMemory(
                param_2->MaxNumObject ) ),
        param_2->MaxNumObject );
    sceneCommandsAllocator.initialize(
        AllocateFromRenderPool(
            edStdAllocator<RenderSceneCommands>::GetRequiredMemory(
                param_2->MaxNumSceneData ) ),
        param_2->MaxNumSceneData );
    drawListAllocator.initialize(
        AllocateFromRenderPool( edStdAllocator<DrawList *>::GetRequiredMemory(
            param_2->MaxNumDrawList ) ),
        param_2->MaxNumDrawList );
    instanceAllocator.initialize(
        AllocateFromRenderPool( edStdAllocator<Instance *>::GetRequiredMemory(
            param_2->MaxNumInstance ) ),
        param_2->MaxNumInstance );
    instanceWithCustomMatAllocator.initialize(
        AllocateFromRenderPool(
            edStdAllocator<InstanceWithCustomMaterial>::GetRequiredMemory(
                param_2->MaxNumInstanceWithCustomMaterial ) ),
        param_2->MaxNumInstanceWithCustomMaterial );
    instanceArrayAllocator.initialize(
        AllocateFromRenderPool(
            edStdAllocator<std::vector<Instance *> *>::GetRequiredMemory(
                param_2->MaxNumInstanceArray ) ),
        param_2->MaxNumInstanceArray );
    hiearchyNodeAllocator.initialize(
        AllocateFromRenderPool(
            edStdAllocator<HiearchyNode *>::GetRequiredMemory(
                param_2->MaxNumHiearchyNode ) ),
        param_2->MaxNumHiearchyNode );

    bRemoveDynamicDrawCmds = true;
}

void FrameGraph::clear() {
    // FUN_0050e9c0
    renderNodeAllocator = edStdAllocator<RenderObjectBase *>();
    drawListAllocator = edStdAllocator<DrawList *>();
    instanceAllocator = edStdAllocator<Instance *>();
    hiearchyNodeAllocator = edStdAllocator<HiearchyNode *>();
    instanceWithCustomMatAllocator =
        edStdAllocator<InstanceWithCustomMaterial>();
    instanceArrayAllocator = edStdAllocator<std::vector<Instance *> *>();
    sceneCommandsAllocator = edStdAllocator<RenderSceneCommands>();
    renderNodes.clear();
    numClearCommands = 0;
}

void FrameGraph::pushObject( RenderObjectBase *param_2, bool param_3 ) {
    // FUN_0050b340
    if ( param_3 ) {
        pushBackObject( param_2 );
        return;
    }

    renderNodes.push_front( param_2 );
}

void FrameGraph::insertAfterObject( RenderObjectBase *param_2,
                                    RenderObjectBase *param_3 ) {
    // FUN_004046a0
    for ( auto it = renderNodes.begin(); it != renderNodes.end(); it++ ) {
        RenderObjectBase *peVar3 = *it;
        if ( peVar3 == param_3 ) {
            it++;
            renderNodes.insert( it, param_2 ); // FUN_004046e0 (inlined)
            return;
        }
    }
    OTDU_ASSERT( false );
}

void FrameGraph::removeObject( RenderObjectBase *pObject ) {
    renderNodes.remove( pObject );
}

static void RegenerateAutoMipmapAttachments( RenderScene *pScene ) {
    // Helper to honor D3DUSAGE_AUTOGENMIPMAP during a frame in flight
    // (for non-D3D9 backends)
    FramebufferAttachments *pFramebuffer = pScene->getFramebuffer();
    if ( pFramebuffer == nullptr ) {
        return;
    }

    for ( RenderTarget *pAttachment : pFramebuffer->pAttachments ) {
        if ( pAttachment == nullptr ) {
            continue;
        }

        Render2DB *p2DB = pAttachment->getBound2DB();
        Texture *pTexture =
            ( p2DB != nullptr ) ? p2DB->getFirstBitmap() : nullptr;
        if ( pTexture == nullptr || pTexture->NumMipmap <= 1 ||
             ( ( pTexture->Flags >> 0xe ) & 1 ) == 0 ) {
            continue;
        }

        gEdRender.pRenderDevice->generateMipchainInline( pTexture );
    }
}

void FrameGraph::submitDrawCommands( bool param_2 ) {
    // FUN_0050ea50
    if ( param_2 ) {
        for ( uint32_t uVar5 = 0; uVar5 < numClearCommands; uVar5++ ) {
            ClearCommand &cmd = clearCommands[uVar5];
            processClearCommand( cmd );
        }
        numClearCommands = 0;
    }

    gSceneRenderer.pActiveScene = nullptr;
    gSceneRenderer.pActiveInstance = nullptr;
    gSceneRenderer.pActiveDrawList = nullptr;
    gSceneRenderer.pActiveTransform = nullptr;

    if ( param_2 ) {
        uint32_t uVar5 = 0;
        for ( RenderObjectBase *pNode : renderNodes ) {
            if ( pNode->isEnabled() ) {
                uint32_t puVar2 = pNode->getObjectType();
                if ( puVar2 == 0 ) {
                    RenderScene *peVar1 = (RenderScene *)pNode;
                    gSceneRenderer.pActiveScene = peVar1;
                    peVar1->getSetup().bind( 0 );
                    gEdRender.pRenderDevice->pushDebugLabel( pNode->getName() );
                    peVar1->submitDrawCommands( uVar5 );
                    gpRenderContext->flushSubmittedScene();
                    RegenerateAutoMipmapAttachments( peVar1 );
                    gEdRender.pRenderDevice->popDebugLabel();
                    SetupGraph::Unbind( 0 );
                } else if ( puVar2 == 1 ) {
                    PostFXNode *peVar1 = (PostFXNode *)pNode;

                    gPostFXStack.beginPass();
                    bool bVar3 = peVar1->prepare();
                    if ( bVar3 ) {
                        peVar1->execute();
                    }
                    gPostFXStack.endPass();
                }
            }
            uVar5++;
        }
    }

    if ( bRemoveDynamicDrawCmds ) {
        removeDynamicDrawCommands();
    }
}

void FrameGraph::removeDynamicDrawCommands() {
    for ( RenderObjectBase *pNode : renderNodes ) {
        if ( pNode->getObjectType() == 0 ) {
            static_cast<RenderScene *>( pNode )->removeDynamicDrawCommands();
        }
    }
}

void FrameGraph::pushClearCommand( RenderTarget *param_2,
                                   const ColorRGBA param_3,
                                   ClearRegion *param_4 ) {
    // FUN_0050e7f0
    if ( param_2 == nullptr ) {
        return;
    }

    if ( numClearCommands >= clearCommands.size() ) {
        OTDU_LOG_WARN( "Clear command queue is full (%u entries); dropping "
                       "clear request!\n",
                       (uint32_t)clearCommands.size() );
        return;
    }

    ClearCommand &cmd = clearCommands[numClearCommands++];
    cmd.pRenderTarget = param_2;
    cmd.ClearColor = param_3;
    if ( param_4 != nullptr ) {
        cmd.Region = *param_4;
    } else {
        cmd.Region.X = 0;
        cmd.Region.Y = 0;
        cmd.Region.Width = param_2->getWidth();
        cmd.Region.Height = param_2->getHeight();
    }
}

void FrameGraph::dropClearCommandsFor( const RenderTarget *param_2 ) {
    for ( uint32_t i = 0; i < numClearCommands; ) {
        if ( clearCommands[i].pRenderTarget == param_2 ) {
            clearCommands[i] = clearCommands[--numClearCommands];
        } else {
            i++;
        }
    }
}

void FrameGraph::pushBackObject( RenderObjectBase *param_2 ) {
    // FUN_004047a0
    renderNodes.push_back( param_2 );
}

void FrameGraph::processClearCommand( ClearCommand &cmd ) {
    // FUN_005fcfc0
    // NOTE: Original func does all the low level calls inside this function.
    // For clarity sake, this has been moved to the RenderDevice (to isolate
    // render API func calls)
    gEdRender.pRenderDevice->clearColorRenderTarget(
        cmd.pRenderTarget->getTextureColor(), cmd.ClearColor, cmd.Region );
}
