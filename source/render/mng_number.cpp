#include "shared.h"
#include "mng_number.h"

#include "draw_list.h"
#include "render_scene.h"

#include "render/gs_render.h"
#include "render/text_renderer.h"

MngNumber *gpMngNumber = nullptr;

MngNumber::MngNumber()
    : Manager(), pDrawList( new DrawList() ), bEnabled( false ) {
    // FUN_006f84a0
    drawCommands.reserve( kMaxNumbers );
    gpMngNumber = this;
}

MngNumber::~MngNumber() {
    drawCommands.clear();
    delete pDrawList;
    gpMngNumber = nullptr;
}

bool MngNumber::initialize( TestDriveGameInstance * ) {
    // FUN_006f8410
    return pDrawList->initialize( 100, 1000, 0, true, nullptr, "MngNumber",
                                  false );
}

void MngNumber::tick( float, float ) {
    // FUN_006f8200
    pDrawList->reset();
    drawCommands.clear();
}

void MngNumber::draw() {
    // FUN_006f84f0
    if ( !bEnabled ) {
        return;
    }

    gpRender->getRenderPass<eRenderPass::RP_2D>()
        .pScene->enqueueDynamicDrawList( pDrawList );
}

void MngNumber::terminate() {
    // FUN_006f81c0
    pDrawList->destroy();
}

void MngNumber::addNumber( Eigen::Vector3f &position,
                           const uint32_t size,
                           const ColorRGBA &color,
                           int32_t number ) {
    // FUN_006f8220
    if ( drawCommands.size() >= kMaxNumbers || !bEnabled ) {
        return;
    }

    DrawCommand drawCommand;
    drawCommand.Position = position;
    drawCommand.Size = size;
    drawCommand.Color = color;
    drawCommand.Number = number;

    drawCommands.push_back( drawCommand );
}
