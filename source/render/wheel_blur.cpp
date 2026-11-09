#include "shared.h"
#include "wheel_blur.h"

// OTDU_ADDITION: wheel spin blur drawn by the wheels' own geometry.
//
// Not a post effect: it works the same for any wheel model and never touches
// the road or any other part of the vehicle.
//
// While a car's wheel turns, everything that turns with it (its DISC_xx rotor
// node and that node's subtree: brake disc, D_RIM_xx, D_TIRE_xx) is held back
// from the main view scene's normal draw (Defer) and drawn when the scene's
// buckets are done (FlushScene) as N copies turned about the axle through the
// arc the wheel covers during the exposure:
//  1. per copy, a scratch depth is reset from the scene's depth (the wheels'
//     screen rectangle only), the copy is drawn depth only (its own front-most
//     surface, still hidden by the body and the fenders), then drawn again
//     with depth EQUAL into an accumulation target: the material's own pixel
//     shader outputs (colour in linear light, 1) / N while
//     D3D9RenderStateInfo::otduAccumWeight is set (d3d9_abi.glsl), summed by
//     additive blending;
//  2. the unturned wheel writes its depth into the scene (later passes: the
//     motion blur, particles);
//  3. postfx/wheel_blur_composite.frag lays the copies' mean colour over the
//     scene by their coverage. The wheel was never drawn into the scene, so
//     the gaps between the spokes show what really is behind them, and no
//     pixel outside the wheel's own fragments can change.
// Alpha blended wheel parts (decals) are drawn as usual after the composite.

#include "core/arg_parser.h"
#include "game/vehicle/car_state.h"
#include "game/vehicle/gs_car_state.h"
#include "game/vehicle/vehicle_base.h"
#include "gs_timer.h"
#include "render/2db.h"
#include "render/camera/camera_base.h"
#include "render/ed_render.h"
#include "render/frustum.h"
#include "render/gs_render.h"
#include "render/gs_render_helper.h"
#include "render/hiearchy_node.h"
#include "render/instance.h"
#include "render/material.h"
#include "render/postfx/postfx_renderer.h"
#include "render/render_api.h"
#include "render/render_buckets.h"
#include "render/render_context.h"
#include "render/render_scene.h"
#include "render/render_target.h"
#include "render/scene_renderer.h"
#include "render/setup_node.h"
#include "render/shaders/shader_types.h"
#include "render/state_cache.h"
#include "render/vulkan/render_device.h"
#include "world/gs_world_streaming.h"

#include <array>
#include <cmath>
#include <unordered_map>
#include <vector>

namespace {
// Shader table entries (shader_table_postfx.inl) / resources/shaders/manifest.txt
constexpr uint64_t kCompositePSHashcode = 0x0d7b4d4200000004ull;
constexpr uint64_t kCompositeMSAAPSHashcode = 0x0d7b4d4200000005ull; // WB_MSAA

constexpr float kFullTurn = 6.2831853f;
constexpr uint32_t kMaxCars = 32;
// Copies about this far apart along the arc at the tyre, at most gMaxCopies
constexpr float kCopySpacingPx = 6.0f;
// Wheels smaller than this on screen are drawn as they are
constexpr float kMinRadiusPx = 6.0f;
// A longer gap between two frames is no travel
constexpr float kMaxTravelGapSeconds = 0.25f;
// Slower wheels (rad/s, ~2.5 km/h) are drawn as they are
constexpr float kMinOmega = 2.0f;

bool gbEnabled = true;
// The wheels' exposure: the ring of copies reaches a full turn (spokes gone)
// from ~40 km/h, so the spokes vanish
float gWheelShutterSeconds = 0.2f;
uint32_t gMaxCopies = 24;

static CmdLineArg CmdLineArgNoWheelBlur( "nowheelblur", []( const char * ) {
    gbEnabled = false;
} );
static CmdLineArg CmdLineArgWheelShutter( "mbwheel", []( const char *pArg ) {
    if ( pArg != nullptr ) {
        gWheelShutterSeconds =
            Clamp( static_cast<float>( atof( pArg ) ), 0.0f, 1.0f );
    }
} );
// Every wheel turns a full turn per exposure (to check the path parked)
bool gbTest = false;
static CmdLineArg CmdLineArgWheelBlurTest( "wheelblurtest", []( const char * ) {
    gbTest = true;
} );
static CmdLineArg CmdLineArgWheelCopies( "wheelcopies", []( const char *pArg ) {
    if ( pArg != nullptr ) {
        gMaxCopies = static_cast<uint32_t>( Clamp( atoi( pArg ), 2, 64 ) );
    }
} );

struct Wheel {
    Matrix4x4f_t Disc; // the rotor (render space): turns about its local X
    Matrix4x4f_t DiscInverse;
    Eigen::Vector3f Hub;
    float Radius;
    float Arc; // rad turned during the exposure (up to a full turn)
    uint32_t Copies;
};

struct Entry {
    TransformMatrixCommand Command;
    Primitive *pPrimitive = nullptr;
    InstanceLOD *pLOD = nullptr;
    Material *pMaterial = nullptr;
    LightSetupNode *pLights = nullptr;
    uint32_t WheelIndex = 0;
};

struct CarTrack {
    const VehicleBase *pVehicle = nullptr;
    Eigen::Vector3d Position = Eigen::Vector3d::Zero(); // absolute
    float Time = -1.0f;
};

struct State {
    // This frame's spinning wheels (built at the first draw of a frame)
    std::vector<Wheel> Wheels;
    std::unordered_map<const Instance *, uint32_t> WheelOfInstance;
    std::array<CarTrack, kMaxCars> Tracks;
    uint32_t BuiltFrameIndex = ~0u;
    float BuiltTime = -1.0f;
    GPUTexture *pMainColour = nullptr;

    // The current scene's held draws
    std::vector<Entry> Entries;
    LightSetupNode *pLights = nullptr;
    bool bFlushing = false;

    // Resources
    GPUTexture *pAccumulation = nullptr;
    GPUTexture *pScratchDepth = nullptr;
    Texture AccumulationTexture = {};
    PixelShader *pComposite = nullptr;
    PixelShader *pCompositeMSAA = nullptr;
    bool bReportedFailure = false;
};
State gState;

Eigen::Vector3d StreamingOrigin() {
    if ( gpWorldStreaming != nullptr && gpWorldStreaming->isEnabled() ) {
        const Eigen::Vector4f &p = gpWorldStreaming->getPosition();
        return Eigen::Vector3d( p.x(), p.y(), p.z() );
    }
    return Eigen::Vector3d::Zero();
}

void BuildFrame() {
    gState.Wheels.clear();
    gState.WheelOfInstance.clear();
    gState.pMainColour = nullptr;

    RenderPass &nearPass = gpRender->getRenderPass<eRenderPass::RP_Near>();
    if ( nearPass.pCamera == nullptr || nearPass.pScene == nullptr ||
         nearPass.pFramebufferAttachments == nullptr ) {
        return;
    }
    RenderTarget *pMainRT = nearPass.pFramebufferAttachments->pAttachments[0];
    if ( pMainRT == nullptr || pMainRT->getTextureColor() == nullptr ) {
        return;
    }
    gState.pMainColour = pMainRT->getTextureColor();
    if ( !gbEnabled || gpCarState == nullptr || gWheelShutterSeconds <= 0.0f ) {
        return;
    }

    // How big a wheel is on screen decides its copies
    const Matrix4x4f_t &worldToCam = nearPass.pCamera->getWorldToCam();
    const Matrix4x4f_t &projection =
        nearPass.pScene->getFrustumWrite().getProjectionMatrix();
    const Matrix4x4f_t camToWorld = worldToCam.inverse();
    const Eigen::Vector3f cameraPosition( camToWorld( 3, 0 ), camToWorld( 3, 1 ),
                                          camToWorld( 3, 2 ) );
    const Eigen::Vector3f cameraForward =
        -Eigen::Vector3f( camToWorld( 2, 0 ), camToWorld( 2, 1 ),
                          camToWorld( 2, 2 ) )
             .normalized();
    const float focalPx = std::abs( projection( 1, 1 ) ) * 0.5f *
                          static_cast<float>( pMainRT->getHeight() );

    const float now = ( gpTimer != nullptr ) ? gpTimer->SystemTotalTime : 0.0f;
    const Eigen::Vector3d origin = StreamingOrigin();

    CarState *pCarStates = gpCarState->getCarStates();
    for ( uint32_t i = 0; i < kMaxCars; i++ ) {
        CarTrack &track = gState.Tracks[i];
        if ( !gpCarState->isCarStateUsed( static_cast<int32_t>( i ) ) ) {
            track.pVehicle = nullptr;
            continue;
        }
        CarState &carState = pCarStates[i];
        VehicleBase *pVehicle = carState.getVehicleInstance();
        if ( pVehicle == nullptr || !pVehicle->isInitialized() ||
             !pVehicle->isDisplayed() ) {
            track.pVehicle = nullptr;
            continue;
        }
        const VehicleBase::eType type = pVehicle->getType();
        if ( type != VehicleBase::eType::VBT_Car &&
             type != VehicleBase::eType::VBT_TrafficCar ) {
            continue;
        }
        HiearchyNode *pRoot = pVehicle->getNodeByIndex( 0 );
        if ( pRoot == nullptr || pRoot->pInstance == nullptr ) {
            continue;
        }

        // The spin the car's travel implies (rolling): the physics' wheel speed
        // can read 0 for the player's car, and traffic has none at all
        const Matrix4x4f_t &body = pRoot->pInstance->getModelMatrix();
        const Eigen::Vector3d position =
            Eigen::Vector3d( body( 3, 0 ), body( 3, 1 ), body( 3, 2 ) ) + origin;
        double travelSpeed = 0.0;
        if ( track.pVehicle == pVehicle && track.Time >= 0.0f ) {
            const float gap = now - track.Time;
            if ( gap > 1e-4f && gap <= kMaxTravelGapSeconds ) {
                travelSpeed = ( position - track.Position ).norm() / gap;
            }
        }
        track.pVehicle = pVehicle;
        track.Position = position;
        track.Time = now;

        for ( uint32_t w = 0; w < 4; w++ ) {
            HiearchyNode *pDisc = pVehicle->getNodeByIndex( 4 + w ); // DISC_xx
            if ( pDisc == nullptr || pDisc->pInstance == nullptr ) {
                continue;
            }
            const WheelState &wheelState = carState.getWheel( w );
            const float radius = ( wheelState.WheelRadius > 0.1f &&
                                   wheelState.WheelRadius < 1.5f )
                                     ? wheelState.WheelRadius
                                     : 0.32f;
            const float omega =
                Max( std::abs( wheelState.Omega ),
                     static_cast<float>( travelSpeed / radius ) );
            // A standing car's physics jitter (settling) is no spin
            if ( omega < kMinOmega && !gbTest ) {
                continue;
            }
            const float arc =
                gbTest ? kFullTurn : Min( omega * gWheelShutterSeconds, kFullTurn );

            const Matrix4x4f_t &disc = pDisc->pInstance->getModelMatrix();
            const Eigen::Vector3f hub( disc( 3, 0 ), disc( 3, 1 ), disc( 3, 2 ) );
            const float viewDepth = ( hub - cameraPosition ).dot( cameraForward );
            if ( viewDepth < 0.1f ) {
                continue;
            }
            const float radiusPx = radius * focalPx / viewDepth;
            if ( radiusPx < kMinRadiusPx ) {
                continue;
            }
            const uint32_t copies = Min(
                gMaxCopies, static_cast<uint32_t>(
                                std::ceil( arc * radiusPx / kCopySpacingPx ) ) );
            if ( copies < 2u ) {
                continue;
            }

            Wheel wheel;
            wheel.Disc = disc;
            wheel.DiscInverse = disc.inverse();
            wheel.Hub = hub;
            wheel.Radius = radius;
            wheel.Arc = arc;
            wheel.Copies = copies;
            const uint32_t index = static_cast<uint32_t>( gState.Wheels.size() );
            gState.Wheels.push_back( wheel );

            // Everything that turns with the wheel: the rotor and its subtree
            // (the hierarchy is a flat list, a node's descendants follow it)
            HiearchyNode *pNode = pDisc;
            for ( uint32_t n = 0; n <= pDisc->NumChildren && pNode != nullptr;
                  n++, pNode = pNode->pNext ) {
                if ( pNode->pInstance != nullptr ) {
                    gState.WheelOfInstance[pNode->pInstance] = index;
                }
            }
        }
    }
}

void EnsureFrame() {
    const uint32_t frameIndex = gEdRender.pRenderDevice->getFrameIndex();
    const float now = ( gpTimer != nullptr ) ? gpTimer->SystemTotalTime : 0.0f;
    if ( frameIndex == gState.BuiltFrameIndex && now == gState.BuiltTime ) {
        return;
    }
    gState.BuiltFrameIndex = frameIndex;
    gState.BuiltTime = now;
    BuildFrame();
}

void ReleaseTargets() {
    RenderDevice *pDevice = gEdRender.pRenderDevice;
    if ( pDevice == nullptr ) {
        return;
    }
    if ( gState.pAccumulation != nullptr ) {
        pDevice->destroyTexture( gState.pAccumulation );
        gState.pAccumulation = nullptr;
    }
    if ( gState.pScratchDepth != nullptr ) {
        pDevice->destroyTexture( gState.pScratchDepth );
        gState.pScratchDepth = nullptr;
    }
}

bool EnsureResources( GPUTexture *pColour, GPUTexture *pDepth ) {
    RenderDevice *pDevice = gEdRender.pRenderDevice;
    if ( pColour->SampleCount != pDepth->SampleCount ) {
        return false;
    }

    if ( gState.pComposite == nullptr ) {
        gState.pComposite =
            (PixelShader *)gpRenderContext->getShaderHashTable().getShader(
                kCompositePSHashcode );
        gState.pCompositeMSAA =
            (PixelShader *)gpRenderContext->getShaderHashTable().getShader(
                kCompositeMSAAPSHashcode );
    }
    if ( gState.pComposite == nullptr || gState.pCompositeMSAA == nullptr ) {
        return false;
    }

    GPUTexture *pAccumulation = gState.pAccumulation;
    GPUTexture *pScratch = gState.pScratchDepth;
    if ( ( pAccumulation != nullptr &&
           ( pAccumulation->Width != pColour->Width ||
             pAccumulation->Height != pColour->Height ||
             pAccumulation->SampleCount != pColour->SampleCount ) ) ||
         ( pScratch != nullptr &&
           ( pScratch->Width != pDepth->Width ||
             pScratch->Height != pDepth->Height ||
             pScratch->SampleCount != pDepth->SampleCount ||
             pScratch->ViewFormat != pDepth->ViewFormat ) ) ) {
        ReleaseTargets();
    }

    if ( gState.pAccumulation == nullptr || gState.pScratchDepth == nullptr ) {
        eAntiAliasingMethod aa = eAntiAliasingMethod::AAM_Disabled;
        if ( pDepth->SampleCount == VK_SAMPLE_COUNT_4_BIT ) {
            aa = eAntiAliasingMethod::AAM_MSAA_X4;
        } else if ( pDepth->SampleCount == VK_SAMPLE_COUNT_2_BIT ) {
            aa = eAntiAliasingMethod::AAM_MSAA_X2;
        }
        gState.pAccumulation = pDevice->createRenderTarget(
            pColour->Width, pColour->Height, VF_A16B16G16R16F, aa );
        // The scratch depth must match the scene's depth format exactly (the
        // per copy region copies)
        for ( const eViewFormat format :
              { gDepthStencilFormat, VF_D24S8F, VF_D24S8, VF_D16 } ) {
            GPUTexture *pCandidate = pDevice->createRenderTarget(
                pDepth->Width, pDepth->Height, format, aa );
            if ( pCandidate != nullptr &&
                 pCandidate->ViewFormat == pDepth->ViewFormat ) {
                gState.pScratchDepth = pCandidate;
                break;
            }
            if ( pCandidate != nullptr ) {
                pDevice->destroyTexture( pCandidate );
            }
        }
        if ( gState.pAccumulation == nullptr || gState.pScratchDepth == nullptr ||
             gState.pAccumulation->SampleCount != pColour->SampleCount ||
             gState.pScratchDepth->SampleCount != pDepth->SampleCount ||
             gState.pScratchDepth->ViewFormat != pDepth->ViewFormat ) {
            ReleaseTargets();
            return false;
        }
        OTDU_LOG_INFO( "Wheel blur targets: %ux%u, %u sample(s)\n",
                       pColour->Width, pColour->Height,
                       static_cast<uint32_t>( pDepth->SampleCount ) );
    }

    // The accumulation as a texture for the composite: a render target's
    // texture, nothing to upload (else bindTexture copies texels)
    Texture &texture = gState.AccumulationTexture;
    texture.Hashcode = 0x0d7b4d42a0cc0000ull;
    texture.Width = static_cast<uint16_t>( gState.pAccumulation->Width );
    texture.Height = static_cast<uint16_t>( gState.pAccumulation->Height );
    texture.Depth = 1;
    texture.NumMipmap = 1;
    texture.NumPackedMipmap = 1;
    texture.Flags = 0x200;
    texture.bUploaded = 1;
    texture.Format = VF_A16B16G16R16F;
    texture.pTexture = gState.pAccumulation;
    return true;
}

void BindTargets( GPUTexture *pColour, GPUTexture *pDepth ) {
    RenderDevice *pDevice = gEdRender.pRenderDevice;
    pDevice->bindRenderTarget( pColour, 0 );
    gStateCache.pRenderTargets[1] = pColour; // bindToFramebuffer's slot
    pDevice->bindDepthStencil( pDepth );
    gStateCache.pDepthStencil = pDepth;
}

enum class eDraw {
    Plain,      // as the scene would have drawn it
    DepthOnly,  // a copy's own front-most surface
    Accumulate, // a copy's colour / N where its surface is front-most
    DepthWrite, // the unturned wheel's depth into the scene
};

void DrawEntry( const Entry &entry,
                const Matrix4x4f_t &matrix,
                const eDraw draw,
                const float weight = 0.0f,
                const uint32_t flags = 0u ) {
    if ( entry.pLights != nullptr ) {
        entry.pLights->execute();
    }
    gSceneRenderer.reset();
    if ( !gpRenderContext->bindMaterial( entry.pMaterial ) ) {
        return;
    }
    gSceneRenderer.bindMaterial( entry.pMaterial );

    switch ( draw ) {
    case eDraw::DepthOnly:
    case eDraw::DepthWrite:
        gStateCache.setColorWriteChannels( false, false, false, false );
        gStateCache.setAlphaBlendingEnabled( false );
        gStateCache.setDepthWrite( true );
        break;
    case eDraw::Accumulate:
        gStateCache.setColorWriteChannels( true, true, true, true );
        gStateCache.setDepthWrite( false );
        gStateCache.setDepthFunc( eComparisonFunction::CF_Equal );
        gStateCache.setBlendOps( eBlendMode::BM_One, eBlendMode::BM_One,
                                 eBlendOP::BO_Add );
        gStateCache.setAlphaBlendingEnabled( true );
        break;
    case eDraw::Plain:
        break;
    }

    TransformMatrixCommand command = entry.Command;
    command.ModelMatrix = matrix;
    gpRenderContext->bindPrimitive( entry.pPrimitive, entry.pLOD );

    RenderDevice *pDevice = gEdRender.pRenderDevice;
    if ( draw == eDraw::Accumulate ) {
        pDevice->setAccumulationWeight( weight, flags );
    }
    command.execute( 0, entry.pPrimitive, entry.pLOD );
    if ( draw == eDraw::Accumulate ) {
        pDevice->setAccumulationWeight( 0.0f, 0u );
    }
}

// Copy k of n of a wheel: turned about the rotor's X axis, the copies evenly
// spread over the arc around the current pose
Matrix4x4f_t TurnedMatrix( const Entry &entry, const uint32_t k ) {
    const Wheel &wheel = gState.Wheels[entry.WheelIndex];
    const float angle =
        wheel.Arc * ( ( static_cast<float>( k ) + 0.5f ) /
                          static_cast<float>( wheel.Copies ) -
                      0.5f );
    const float c = std::cos( angle );
    const float s = std::sin( angle );
    Matrix4x4f_t turn = Matrix4x4f_t::Identity();
    turn( 1, 1 ) = c;
    turn( 1, 2 ) = s;
    turn( 2, 1 ) = -s;
    turn( 2, 2 ) = c;
    // Row vectors: model -> rotor space -> turned -> back to the world
    return entry.Command.ModelMatrix * wheel.DiscInverse * turn * wheel.Disc;
}

// The screen rectangle holding the held wheels (where the scratch depth is
// reset per copy)
ClearRegion WheelsRectangle( const std::vector<const Entry *> &entries,
                             const uint32_t width,
                             const uint32_t height ) {
    ClearRegion full;
    full.X = 0;
    full.Y = 0;
    full.Width = static_cast<int32_t>( width );
    full.Height = static_cast<int32_t>( height );

    RenderScene *pScene = gSceneRenderer.pActiveScene;
    if ( pScene == nullptr || pScene->getCamera() == nullptr ) {
        return full;
    }
    const Matrix4x4f_t viewProjection =
        pScene->getCamera()->getWorldToCam() *
        pScene->getFrustumWrite().getProjectionMatrix();

    float minX = static_cast<float>( width );
    float minY = static_cast<float>( height );
    float maxX = 0.0f;
    float maxY = 0.0f;
    std::vector<bool> bDone( gState.Wheels.size(), false );
    for ( const Entry *pEntry : entries ) {
        if ( bDone[pEntry->WheelIndex] ) {
            continue;
        }
        bDone[pEntry->WheelIndex] = true;
        const Wheel &wheel = gState.Wheels[pEntry->WheelIndex];
        const float extent = wheel.Radius + 0.3f;
        for ( uint32_t corner = 0; corner < 8; corner++ ) {
            const Eigen::RowVector4f point(
                wheel.Hub.x() + ( ( corner & 1 ) ? extent : -extent ),
                wheel.Hub.y() + ( ( corner & 2 ) ? extent : -extent ),
                wheel.Hub.z() + ( ( corner & 4 ) ? extent : -extent ), 1.0f );
            const Eigen::RowVector4f clip = point * viewProjection;
            if ( clip.w() <= 0.05f ) {
                return full; // reaches behind the camera
            }
            const float x = ( clip.x() / clip.w() * 0.5f + 0.5f ) *
                            static_cast<float>( width );
            const float y = ( 0.5f - clip.y() / clip.w() * 0.5f ) *
                            static_cast<float>( height );
            minX = Min( minX, x );
            minY = Min( minY, y );
            maxX = Max( maxX, x );
            maxY = Max( maxY, y );
        }
    }

    const int32_t x0 = Clamp( static_cast<int32_t>( std::floor( minX ) ) - 2, 0,
                              static_cast<int32_t>( width ) );
    const int32_t y0 = Clamp( static_cast<int32_t>( std::floor( minY ) ) - 2, 0,
                              static_cast<int32_t>( height ) );
    const int32_t x1 = Clamp( static_cast<int32_t>( std::ceil( maxX ) ) + 2, 0,
                              static_cast<int32_t>( width ) );
    const int32_t y1 = Clamp( static_cast<int32_t>( std::ceil( maxY ) ) + 2, 0,
                              static_cast<int32_t>( height ) );
    ClearRegion region;
    region.X = x0;
    region.Y = y0;
    region.Width = Max( x1 - x0, 0 );
    region.Height = Max( y1 - y0, 0 );
    return region;
}

void Composite( PixelShader *pShader, const float width, const float height ) {
    gPostFXRenderer.bindShaders( nullptr, pShader );
    gPostFXRenderer.setRenderPassDimensions( width, height );

    // Premultiplied over the scene, no depth test
    Material material;
    gPostFXRenderer.bindDefaultBlendState( &material );
    material.SrcBlend = eBlendMode::BM_One;
    material.DstBlend = eBlendMode::BM_InvSrcAlpha;
    material.BlendOP = eBlendOP::BO_Add;
    material.DepthTest = 3;
    gPostFXRenderer.bindRasterState( &material );

    gpRenderContext->bindTexture( &gState.AccumulationTexture, 0 );
    gPostFXRenderer.drawFullscreenQuad( false, false, false, 0xffffffff );
    gpRenderContext->bindTexture( nullptr, 0 );

    gPostFXRenderer.bindRasterState( nullptr );
    gPostFXRenderer.bindShaders();
}

bool DrawBlurred( const std::vector<Entry> &entries ) {
    RenderDevice *pDevice = gEdRender.pRenderDevice;
    RenderTarget *pSceneRT = gEdRender.getBoundRenderTargetAtIndex( 0 );
    GPUTexture *pSceneColour =
        ( pSceneRT != nullptr ) ? pSceneRT->getTextureColor() : nullptr;
    GPUTexture *pSceneDepth = gStateCache.pDepthStencil;
    if ( pSceneColour == nullptr || pSceneDepth == nullptr ||
         !EnsureResources( pSceneColour, pSceneDepth ) ) {
        return false;
    }
    PixelShader *pComposite = ( pSceneColour->SampleCount != VK_SAMPLE_COUNT_1_BIT )
                                  ? gState.pCompositeMSAA
                                  : gState.pComposite;

    // Opaque parts are turned; blended ones (decals) are drawn as usual last
    std::vector<const Entry *> opaque;
    std::vector<const Entry *> blended;
    uint32_t maxCopies = 0;
    for ( const Entry &entry : entries ) {
        if ( entry.pMaterial->BlendOP == eBlendOP::BO_Disabled ) {
            opaque.push_back( &entry );
            maxCopies = Max( maxCopies, gState.Wheels[entry.WheelIndex].Copies );
        } else {
            blended.push_back( &entry );
        }
    }

    // 8 bit scene: a copy's colour clamps to 1 as the scene would store it
    const uint32_t flags =
        ( pSceneColour->ViewFormat == VK_FORMAT_R16G16B16A16_SFLOAT ) ? 0u : 1u;

    if ( !opaque.empty() ) {
        const ClearRegion rectangle =
            WheelsRectangle( opaque, pSceneColour->Width, pSceneColour->Height );

        ColorRGBA black;
        black.Color = 0u;
        pDevice->clearColorRenderTarget( gState.pAccumulation, black,
                                         ClearRegion() );

        for ( uint32_t k = 0; k < maxCopies; k++ ) {
            pDevice->copyDepthStencilRegion(
                pSceneDepth, gState.pScratchDepth, rectangle.X, rectangle.Y,
                static_cast<uint32_t>( rectangle.Width ),
                static_cast<uint32_t>( rectangle.Height ) );
            BindTargets( gState.pAccumulation, gState.pScratchDepth );

            for ( const Entry *pEntry : opaque ) {
                if ( k < gState.Wheels[pEntry->WheelIndex].Copies ) {
                    DrawEntry( *pEntry, TurnedMatrix( *pEntry, k ),
                               eDraw::DepthOnly );
                }
            }
            for ( const Entry *pEntry : opaque ) {
                const Wheel &wheel = gState.Wheels[pEntry->WheelIndex];
                if ( k < wheel.Copies ) {
                    DrawEntry( *pEntry, TurnedMatrix( *pEntry, k ),
                               eDraw::Accumulate,
                               1.0f / static_cast<float>( wheel.Copies ),
                               flags );
                }
            }
        }

        BindTargets( pSceneColour, pSceneDepth );
        for ( const Entry *pEntry : opaque ) {
            DrawEntry( *pEntry, pEntry->Command.ModelMatrix, eDraw::DepthWrite );
        }
        gStateCache.setColorWriteChannels( true, true, true, true );

        Composite( pComposite, static_cast<float>( pSceneColour->Width ),
                   static_cast<float>( pSceneColour->Height ) );
    }

    for ( const Entry *pEntry : blended ) {
        DrawEntry( *pEntry, pEntry->Command.ModelMatrix, eDraw::Plain );
    }
    return true;
}
} // namespace

bool WheelBlur::Defer( const TransformMatrixCommand &command,
                       Primitive *pPrimitive,
                       InstanceLOD *pLOD ) {
    if ( !gbEnabled || gState.bFlushing || command.pInstance == nullptr ||
         command.pDrawList != nullptr || pPrimitive == nullptr ||
         gEdRender.pRenderDevice == nullptr ) {
        return false;
    }
    RenderScene *pScene = gSceneRenderer.pActiveScene;
    Material *pMaterial = gSceneRenderer.pActiveMaterial;
    if ( pScene == nullptr || pMaterial == nullptr ) {
        return false;
    }

    EnsureFrame();
    if ( gState.WheelOfInstance.empty() ) {
        return false;
    }
    auto it = gState.WheelOfInstance.find( command.pInstance );
    if ( it == gState.WheelOfInstance.end() ) {
        return false;
    }

    // Main view scenes drawn with the materials' own shaders only (not the
    // reflections, shadows, env maps, nor the occlusion / prepass scenes:
    // 0x40 / 0x02 default shaders, 0x04 no depth write, 0x08 no blending)
    if ( ( pScene->getFlags() & 0x4Eu ) != 0u ) {
        return false;
    }
    FramebufferAttachments *pFramebuffer = pScene->getFramebuffer();
    if ( pFramebuffer == nullptr || pFramebuffer->pAttachments[0] == nullptr ||
         pFramebuffer->pAttachments[0]->getTextureColor() != gState.pMainColour ) {
        return false;
    }

    Entry entry;
    entry.Command = command;
    entry.pPrimitive = pPrimitive;
    entry.pLOD = pLOD;
    entry.pMaterial = pMaterial;
    entry.pLights = gState.pLights;
    entry.WheelIndex = it->second;
    gState.Entries.push_back( entry );
    return true;
}

void WheelBlur::OnSceneSetup( LightSetupNode *pLights ) {
    gState.pLights = pLights;
}

void WheelBlur::FlushScene() {
    if ( gState.Entries.empty() ) {
        return;
    }
    gState.bFlushing = true;
    std::vector<Entry> entries;
    entries.swap( gState.Entries );

    static bool sLogged = false;
    if ( !sLogged ) {
        sLogged = true;
        uint32_t maxCopies = 0;
        for ( const Wheel &wheel : gState.Wheels ) {
            maxCopies = Max( maxCopies, wheel.Copies );
        }
        OTDU_LOG_INFO( "Wheel blur: first scene with %u wheel parts held "
                       "(%u spinning wheels this frame, up to %u copies)\n",
                       static_cast<uint32_t>( entries.size() ),
                       static_cast<uint32_t>( gState.Wheels.size() ),
                       maxCopies );
    }

    if ( !DrawBlurred( entries ) ) {
        if ( !gState.bReportedFailure ) {
            gState.bReportedFailure = true;
            OTDU_LOG_WARN( "Wheel blur unavailable (targets or shaders), "
                           "wheels drawn as they are\n" );
        }
        for ( const Entry &entry : entries ) {
            DrawEntry( entry, entry.Command.ModelMatrix, eDraw::Plain );
        }
    }

    gState.bFlushing = false;
}

void WheelBlur::Destroy() {
    ReleaseTargets();
    for ( PixelShader **ppShader :
          { &gState.pComposite, &gState.pCompositeMSAA } ) {
        if ( *ppShader != nullptr ) {
            ( *ppShader )->destroy();
            *ppShader = nullptr;
        }
    }
    gState.Entries.clear();
    gState.Wheels.clear();
    gState.WheelOfInstance.clear();
}
