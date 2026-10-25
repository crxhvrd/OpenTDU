#include "shared.h"
#include "setup_node.h"

#include "ed_render.h"

// DAT_00fe7028
struct NodeCacheEntry {
    int32_t NumNodes = -1;
    std::list<std::tuple<SetupNode *, uint32_t>> Nodes;
};

static uint32_t gNumLights = 0;       // DAT_00facb20
static Light *DAT_00facb18 = nullptr; // DAT_00facb18
static NodeCacheEntry gNodeCache[7];
static int32_t gRenderSetupCacheFlags = 0; // DAT_00fe7008
Light *gBoundLights[kMaxNumLightPerScene] = { nullptr, nullptr, nullptr,
                                              nullptr }; // DAT_00facb04

ShaderParameter *gpActiveStaticParams = nullptr; // DAT_00facb68
size_t gActiveNumStaticParams = 0ull;            // DAT_00facb6c

Eigen::Vector4f *gpActivePlanes = nullptr; // DAT_00faccac
uint32_t gNumActivePlanes = 0;             // DAT_00faccb0
Scissor *gpScissorOverride = nullptr;

SetupNode::SetupNode() : flags( 0 ) {}

uint32_t SetupNode::getFlags() const { return flags; }

void SetupNode::setFlags( const uint32_t param_1 ) { flags = param_1; }

LightSetupNode::LightSetupNode() : SetupNode() { flags = 0x103; }

LightSetupNode::~LightSetupNode() { lights.clear(); }

void LightSetupNode::execute() {
    // FUN_0050b8a0
    for ( uint32_t i = 0; i < lights.size(); i++ ) {
        gBoundLights[i] = lights[i];
    }

    for ( size_t j = lights.size(); j < kMaxNumLightPerScene; j++ ) {
        gBoundLights[j] = nullptr;
    }
}

uint32_t LightSetupNode::getType() const {
    // FUN_0047a8a0
    return 0;
}

void LightSetupNode::addLight( Light *light ) {
    if ( lights.size() < kMaxNumLightPerScene ) {
        lights.push_back( light );

        gNumLights++;
        DAT_00facb18 = lights.back();
    }
}

void LightSetupNode::removeLight( Light *light ) {
    // FUN_0050b7a0
    auto it = std::find_if( lights.begin(), lights.end(),
                            [=]( Light *x ) { return x == light; } );
    if ( it != lights.end() ) {
        lights.erase( it );

        gNumLights--;
        DAT_00facb18 = ( !lights.empty() ) ? lights.back() : nullptr;
    }
}

bool LightSetupNode::isEmpty() const { return lights.empty(); }

SetupGraph::SetupGraph() : flags( 0 ) {}

SetupGraph::~SetupGraph() {}

SetupNode *SetupGraph::findNodeByType( uint32_t type ) const {
    // FUN_0050dda0
    for ( SetupNode *pNode : nodes ) {
        if ( pNode == nullptr ) {
            return nullptr;
        }

        if ( pNode->getType() == type ) {
            return pNode;
        }
    }

    return nullptr;
}

SetupNode *SetupGraph::findLightNode() const {
    // FUN_0050de60
    if ( ( flags & 1 ) == 0 ) {
        return nullptr;
    }

    return findNodeByType( 0x0 );
}

SetupNode *SetupGraph::findShaderParameterNode() const {
    // FUN_0050de80
    if ( ( ( flags >> 1 ) & 1 ) == 0 ) {
        return nullptr;
    }

    return findNodeByType( 0x1 );
}

SetupNode *SetupGraph::findClipPlaneNode() const {
    // FUN_0050dea0
    if ( ( ( flags >> 2 ) & 1 ) == 0 ) {
        return nullptr;
    }

    return findNodeByType( 0x2 );
}

bool SetupGraph::addNode( SetupNode *node ) {
    // FUN_0050dec0
    nodes.push_front( node );

    uint8_t nodeType = static_cast<uint8_t>( node->getType() );
    flags |= 1 << ( nodeType & 0x1f );

    return true;
}

void SetupGraph::removeNode( SetupNode *node ) {
    // FUN_0050dde0
    bool bFound = false;
    for ( SetupNode *pNode : nodes ) {
        if ( pNode == node ) {
            bFound = true;
            break;
        }
    }

    if ( !bFound ) {
        return;
    }

    uint8_t nodeType = static_cast<uint8_t>( node->getType() );
    flags &= ~( 1 << ( nodeType & 0x1f ) );

    nodes.remove( node );
}

void SetupGraph::bind( uint32_t param_1 ) const {
    for ( SetupNode *peVar1 : nodes ) {
        if ( ( peVar1->getFlags() >> 8 & 1 ) != 0 ) {
            uint32_t iVar2 = peVar1->getType();
            gNodeCache[iVar2].NumNodes++;
            gNodeCache[iVar2].Nodes.push_back(
                std::make_tuple( peVar1, param_1 ) );
            gRenderSetupCacheFlags |= 1 << ( iVar2 & 0x1f );
        }
    }
}

void SetupGraph::Unbind( uint32_t param_1 ) {
    // FUN_0050e640
    if ( gNodeCache[0].NumNodes != -1 &&
         std::get<1>( gNodeCache[0].Nodes.back() ) == param_1 ) {
        gNodeCache[0].NumNodes--;
        gNodeCache[0].Nodes.pop_back();
        if ( gNodeCache[0].NumNodes == -1 ) {
            gRenderSetupCacheFlags = gRenderSetupCacheFlags & 0xfffffffe;
        }
    }

    if ( gNodeCache[1].NumNodes != -1 &&
         std::get<1>( gNodeCache[1].Nodes.back() ) == param_1 ) {
        gNodeCache[1].NumNodes--;
        gNodeCache[1].Nodes.pop_back();
        if ( gNodeCache[1].NumNodes == -1 ) {
            gRenderSetupCacheFlags = gRenderSetupCacheFlags & 0xfffffffd;
        }
    }

    if ( gNodeCache[2].NumNodes != -1 &&
         std::get<1>( gNodeCache[2].Nodes.back() ) == param_1 ) {
        gNodeCache[2].NumNodes--;
        gNodeCache[2].Nodes.pop_back();
        if ( gNodeCache[2].NumNodes == -1 ) {
            gRenderSetupCacheFlags = gRenderSetupCacheFlags & 0xfffffffb;
        }
    }
}

bool SetupGraph::ExecuteCached( uint32_t param_1 ) {
    // FUN_00513410
    uint32_t uVar1 = gNodeCache[param_1].NumNodes;
    if ( uVar1 != 0xffffffff &&
         std::get<0>( gNodeCache[param_1].Nodes.back() ) != nullptr ) {
        SetupNode *puVar2 = std::get<0>( gNodeCache[param_1].Nodes.back() );
        // Flag bit 9 calls vtable slot 0 first, which is TestDrive::NOP
        // (FUN_00614df0) in every retail setup class.
        puVar2->execute();
        return true;
    }

    return false;
}

bool SetupGraph::applyScissor() const {
    for ( SetupNode *peVar1 : nodes ) {
        if ( peVar1->getType() == 3 ) {
            peVar1->execute();
            return true;
        }
    }

    return false;
}

ShaderParameterSetupNode::ShaderParameterSetupNode() : SetupNode() {}

ShaderParameterSetupNode::~ShaderParameterSetupNode() {}

void ShaderParameterSetupNode::execute() {
    // FUN_0050e490
    gpActiveStaticParams = pParameters;
    gActiveNumStaticParams = numParameters;
}

uint32_t ShaderParameterSetupNode::getType() const {
    // FUN_00423f20
    return 1;
}

ClipPlaneSetupNode::ClipPlaneSetupNode() : SetupNode(), numActivePlanes( 0 ) {
    flags = 0x101;
}

ClipPlaneSetupNode::~ClipPlaneSetupNode() {}

void ClipPlaneSetupNode::execute() {
    // FUN_00430b80
    gpActivePlanes = clipPlanes;
    gNumActivePlanes = numActivePlanes;
}

uint32_t ClipPlaneSetupNode::getType() const {
    // FUN_00441620
    return 2;
}

void ClipPlaneSetupNode::setPlane( Eigen::Vector4f &plane,
                                   const int32_t index ) {
    OTDU_ASSERT( index >= 0 && index < 6 );
    clipPlanes[index] = plane;

    numActivePlanes |= ( 1 << index );
}

ScissorSetupNode::ScissorSetupNode() : SetupNode(), scissorRegion() {}

ScissorSetupNode::~ScissorSetupNode() {}

void ScissorSetupNode::execute() { gpScissorOverride = &scissorRegion; }

uint32_t ScissorSetupNode::getType() const { return 3; }

void ScissorSetupNode::setScissor( uint32_t x,
                                   uint32_t y,
                                   uint32_t width,
                                   uint32_t height ) {
    scissorRegion.X = x;
    scissorRegion.Y = y;
    scissorRegion.Width = width;
    scissorRegion.Height = height;
}
