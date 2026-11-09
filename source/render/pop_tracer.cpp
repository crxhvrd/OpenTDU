#include "shared.h"
#include "pop_tracer.h"

// OTDU_ADDITION: -poptrace, to find objects popping in at speed.
//
// Per frame, the set of instances drawn in the main view scenes is compared
// with the previous frame's. An instance that appears / vanishes away from
// the screen edges (not a frustum entry or exit) is a pop; for appearances the
// log tells whether the instance was already well inside its visible LOD
// range (ratio = LOD metric / last LOD threshold well below 1: it arrived
// late, streaming) or right at its range limit (ratio ~1: the LOD range
// itself). LOD switches in view are counted too. One summary line every 5 s.

#include "core/arg_parser.h"
#include "gs_timer.h"
#include "render/camera/camera_base.h"
#include "render/ed_render.h"
#include "render/frustum.h"
#include "render/gs_render.h"
#include "render/2db.h"
#include "render/gs_render_helper.h"
#include "render/instance.h"
#include "render/render_scene.h"
#include "render/render_target.h"
#include "render/scene_renderer.h"
#include "render/vulkan/render_device.h"
#include "world/gs_world_streaming.h"

#include <algorithm>
#include <windows.h>
#include <cmath>
#include <unordered_map>
#include <unordered_set>
#include <vector>

PopTracer::ImpostorCounters PopTracer::gImpostorFrame;

namespace {
bool gbEnabled = false;
static CmdLineArg CmdLineArgPopTrace( "poptrace", []( const char * ) {
    gbEnabled = true;
} );
// -popframes: bursts of consecutive frames (the main view's resolved
// scene) dumped as raw files, to see the pops offline (implies -poptrace)
bool gbFrames = false;
static CmdLineArg CmdLineArgPopFrames( "popframes", []( const char * ) {
    gbEnabled = true;
    gbFrames = true;
} );
constexpr float kBurstFirst = 20.0f;
constexpr float kBurstEvery = 12.0f;
constexpr uint32_t kBurstFrames = 16;
constexpr uint32_t kMaxBursts = 6;

constexpr float kEdge = 0.8f;          // |ndc| beyond this = a frustum edge
constexpr float kReportSeconds = 5.0f;
constexpr float kBands[4] = { 60.0f, 150.0f, 300.0f, 1e9f };

struct Draw {
    bool bSprite = false; // only an impostor sprite this frame
    uint64_t Hashcode = 0;
    uint32_t LOD = 0;
    uint32_t NumLODs = 0;
    float Distance = 0.0f;
    float Ratio = -1.0f; // LOD metric / last threshold (-1: no LODs)
    float Edge = 0.0f;   // max |ndc| (> 1: outside / behind)
};

struct Event {
    uintptr_t Caller = 0;
    uint64_t Hashcode;
    float Distance;
    float Ratio;
    uint32_t LOD;
    uint32_t NumLODs;
    bool bNew;
};

struct Stats {
    // Present (mesh or sprite) this frame, nothing last frame, mid-screen
    uint32_t FromNothingMesh[4] = {};
    uint32_t FromNothingSprite[4] = {};
    std::vector<Event> ClosestFromNothing;
    uint32_t Appear[4] = {};
    uint32_t AppearVegetation[4] = {};
    uint32_t AppearInside = 0; // ratio < 0.5
    uint32_t AppearAtLimit = 0;
    uint32_t AppearNew = 0;
    uint32_t Vanish[4] = {};
    uint32_t Switch[4] = {};
    std::vector<Event> Closest;
    // Impostor sprites, the worst frame of the window
    uint32_t ImpostorAdded[2] = {};
    uint32_t ImpostorDroppedAdd[2] = {};
    uint32_t ImpostorDroppedDraw[2] = {};
    uint32_t FramesWithDrops = 0;
    uint32_t HeldByBreak = 0;   // worst frame
    uint32_t SubmitCapHits = 0; // worst frame
};

struct State {
    uint32_t FrameIndex = ~0u;
    float FrameTime = -1.0f;
    std::unordered_map<const Instance *, Draw> Current;
    std::unordered_map<const Instance *, Draw> Previous;
    std::unordered_set<const Instance *> Ever;
    Matrix4x4f_t ViewProjection = Matrix4x4f_t::Identity();
    Eigen::Vector3f CameraPosition = Eigen::Vector3f::Zero();
    GPUTexture *pMainColour = nullptr;
    bool bFrameValid = false;

    std::vector<const Instance *> PendingSprites;
    std::unordered_map<const Instance *, uintptr_t> Callers;
    Stats Window;
    float WindowStart = -1.0f;
    uint32_t WindowFrames = 0;
    Eigen::Vector3d WindowStartPosition = Eigen::Vector3d::Zero();
};
State gState;

// Instance hashcodes are 8 character names (little endian)
void HashName( uint64_t hashcode, char out[9] ) {
    for ( int32_t i = 0; i < 8; i++ ) {
        const char c = static_cast<char>( ( hashcode >> ( i * 8 ) ) & 0xff );
        out[i] = ( c >= 32 && c < 127 ) ? c : ( c == 0 ? '\0' : '?' );
    }
    out[8] = '\0';
}

bool IsVegetation( uint64_t hashcode ) {
    char name[9];
    HashName( hashcode, name );
    return name[0] == 'V' && name[1] == '_';
}

uint32_t Band( const float distance ) {
    for ( uint32_t i = 0; i < 4; i++ ) {
        if ( distance < kBands[i] ) {
            return i;
        }
    }
    return 3;
}

Eigen::Vector3d AbsoluteCamera() {
    Eigen::Vector3d position = gState.CameraPosition.cast<double>();
    if ( gpWorldStreaming != nullptr && gpWorldStreaming->isEnabled() ) {
        const Eigen::Vector4f &origin = gpWorldStreaming->getPosition();
        position += Eigen::Vector3d( origin.x(), origin.y(), origin.z() );
    }
    return position;
}

void Report( const float now ) {
    Stats &stats = gState.Window;
    const float seconds = now - gState.WindowStart;
    const double speed =
        ( AbsoluteCamera() - gState.WindowStartPosition ).norm() /
        Max( seconds, 1e-3f ) * 3.6;

    std::sort( stats.Closest.begin(), stats.Closest.end(),
               []( const Event &a, const Event &b ) {
                   return a.Distance < b.Distance;
               } );
    char closest[512] = "";
    size_t length = 0;
    for ( size_t i = 0; i < Min( stats.Closest.size(), size_t( 5 ) ); i++ ) {
        const Event &e = stats.Closest[i];
        char name[9];
        HashName( e.Hashcode, name );
        length += snprintf( closest + length, sizeof( closest ) - length,
                            " [%s %.0fm r%.2f lod%u/%u%s]", name, e.Distance,
                            e.Ratio, e.LOD, e.NumLODs, e.bNew ? " new" : "" );
        if ( length >= sizeof( closest ) ) {
            break;
        }
    }

    std::sort( stats.ClosestFromNothing.begin(), stats.ClosestFromNothing.end(),
               []( const Event &a, const Event &b ) {
                   return a.Distance < b.Distance;
               } );
    char fromNothing[512] = "";
    size_t fnLength = 0;
    for ( size_t i = 0; i < Min( stats.ClosestFromNothing.size(), size_t( 6 ) );
          i++ ) {
        const Event &e = stats.ClosestFromNothing[i];
        char name[9];
        HashName( e.Hashcode, name );
        static const uintptr_t sModuleBase =
            reinterpret_cast<uintptr_t>( GetModuleHandleA( nullptr ) );
        fnLength += snprintf( fromNothing + fnLength,
                              sizeof( fromNothing ) - fnLength,
                              " [%s %.0fm %s rva %llx]", name, e.Distance,
                              e.bNew ? "sprite" : "mesh",
                              e.Caller != 0 ? (unsigned long long)( e.Caller -
                                                                    sModuleBase )
                                            : 0ull );
        if ( fnLength >= sizeof( fromNothing ) ) {
            break;
        }
    }
    OTDU_LOG_INFO( "Pop from nothing: mesh <60m %u, 60-150 %u, 150-300 %u, "
                   ">300 %u | sprite %u %u %u %u | held by a cell's break "
                   "(worst frame) %u, submit cap hits %u | closest:%s\n",
                   stats.FromNothingMesh[0], stats.FromNothingMesh[1],
                   stats.FromNothingMesh[2], stats.FromNothingMesh[3],
                   stats.FromNothingSprite[0], stats.FromNothingSprite[1],
                   stats.FromNothingSprite[2], stats.FromNothingSprite[3],
                   stats.HeldByBreak, stats.SubmitCapHits, fromNothing );

    OTDU_LOG_INFO(
        "Pop trace: %.1f s, %u frames, ~%.0f km/h, LODFactor %.2f, LOD "
        "quality %u | in view: appear <60m %u, 60-150 %u, 150-300 %u, >300 %u "
        "(vegetation %u %u %u %u; inside range %u, at limit %u, first time %u) "
        "| vanish %u %u %u %u | LOD switch %u %u %u %u | impostor sprites "
        "(worst frame) added %u/%u, dropped at add %u/%u, at draw %u/%u, "
        "frames with drops %u | closest non-vegetation appear:%s\n",
        seconds, gState.WindowFrames, speed, gEdRender.LODFactor,
        gpRender != nullptr ? gpRender->getLODQuality() : 0u, stats.Appear[0],
        stats.Appear[1], stats.Appear[2], stats.Appear[3],
        stats.AppearVegetation[0], stats.AppearVegetation[1],
        stats.AppearVegetation[2], stats.AppearVegetation[3], stats.AppearInside,
        stats.AppearAtLimit, stats.AppearNew, stats.Vanish[0], stats.Vanish[1],
        stats.Vanish[2], stats.Vanish[3], stats.Switch[0], stats.Switch[1],
        stats.Switch[2], stats.Switch[3], stats.ImpostorAdded[0],
        stats.ImpostorAdded[1], stats.ImpostorDroppedAdd[0],
        stats.ImpostorDroppedAdd[1], stats.ImpostorDroppedDraw[0],
        stats.ImpostorDroppedDraw[1], stats.FramesWithDrops, closest );

    gState.Window = Stats();
    gState.WindowStart = now;
    gState.WindowFrames = 0;
    gState.WindowStartPosition = AbsoluteCamera();
}

void EndFrame( const float now ) {
    {
        PopTracer::ImpostorCounters &c = PopTracer::gImpostorFrame;
        Stats &w = gState.Window;
        bool bDrop = false;
        for ( int32_t k = 0; k < 2; k++ ) {
            w.ImpostorAdded[k] = Max( w.ImpostorAdded[k], c.Added[k] );
            w.ImpostorDroppedAdd[k] =
                Max( w.ImpostorDroppedAdd[k], c.DroppedAtAdd[k] );
            w.ImpostorDroppedDraw[k] =
                Max( w.ImpostorDroppedDraw[k], c.DroppedAtDraw[k] );
            bDrop |= c.DroppedAtAdd[k] != 0 || c.DroppedAtDraw[k] != 0;
        }
        if ( bDrop ) {
            w.FramesWithDrops++;
        }
        w.HeldByBreak = Max( w.HeldByBreak, c.HeldByBreak );
        w.SubmitCapHits = Max( w.SubmitCapHits, c.SubmitCapHits );
        c = PopTracer::ImpostorCounters();
    }
    if ( !gState.bFrameValid ) {
        gState.Previous.swap( gState.Current );
        gState.Current.clear();
        return;
    }
    Stats &stats = gState.Window;
    gState.WindowFrames++;

    for ( const auto &[pInstance, draw] : gState.Current ) {
        auto previous = gState.Previous.find( pInstance );
        if ( previous == gState.Previous.end() && draw.Edge < kEdge &&
             !( draw.Distance < 10.0f && draw.NumLODs <= 1 ) ) {
            const uint32_t band = Band( draw.Distance );
            ( draw.bSprite ? stats.FromNothingSprite
                           : stats.FromNothingMesh )[band]++;
            Event event;
            event.Hashcode = draw.Hashcode;
            event.Distance = draw.Distance;
            event.Ratio = draw.Ratio;
            event.LOD = draw.LOD;
            event.NumLODs = draw.NumLODs;
            event.bNew = draw.bSprite;
            auto caller = gState.Callers.find( pInstance );
            event.Caller =
                ( caller != gState.Callers.end() ) ? caller->second : 0;
            stats.ClosestFromNothing.push_back( event );
            if ( stats.ClosestFromNothing.size() > 64 ) {
                std::sort( stats.ClosestFromNothing.begin(),
                           stats.ClosestFromNothing.end(),
                           []( const Event &a, const Event &b ) {
                               return a.Distance < b.Distance;
                           } );
                stats.ClosestFromNothing.resize( 16 );
            }
        }
        if ( draw.bSprite ) {
            continue; // the rest is about meshes
        }
        if ( previous == gState.Previous.end() || previous->second.bSprite ) {
            const bool bNew = gState.Ever.insert( pInstance ).second;
            if ( draw.Edge >= kEdge ) {
                continue; // entered at the frustum's edge
            }
            if ( draw.Distance < 10.0f && draw.NumLODs <= 1 ) {
                continue; // the camera's own car (lamp flares)
            }
            stats.Appear[Band( draw.Distance )]++;
            const bool bVegetation = IsVegetation( draw.Hashcode );
            if ( bVegetation ) {
                stats.AppearVegetation[Band( draw.Distance )]++;
            }
            if ( bNew ) {
                stats.AppearNew++;
            }
            if ( draw.Ratio >= 0.0f && draw.Ratio < 0.5f ) {
                stats.AppearInside++;
            } else if ( draw.Ratio >= 0.5f ) {
                stats.AppearAtLimit++;
            }
            Event event;
            event.Hashcode = draw.Hashcode;
            event.Distance = draw.Distance;
            event.Ratio = draw.Ratio;
            event.LOD = draw.LOD;
            event.NumLODs = draw.NumLODs;
            event.bNew = bNew;
            if ( !bVegetation ) {
                stats.Closest.push_back( event );
            }
            if ( stats.Closest.size() > 64 ) {
                std::sort( stats.Closest.begin(), stats.Closest.end(),
                           []( const Event &a, const Event &b ) {
                               return a.Distance < b.Distance;
                           } );
                stats.Closest.resize( 16 );
            }
        } else if ( previous->second.LOD != draw.LOD && draw.Edge < kEdge ) {
            stats.Switch[Band( draw.Distance )]++;
        }
    }
    for ( const auto &[pInstance, draw] : gState.Previous ) {
        if ( draw.Edge < kEdge &&
             !( draw.Distance < 10.0f && draw.NumLODs <= 1 ) &&
             gState.Current.find( pInstance ) == gState.Current.end() ) {
            stats.Vanish[Band( draw.Distance )]++;
        }
    }

    gState.Previous.swap( gState.Current );
    gState.Current.clear();

    if ( gState.WindowStart < 0.0f ) {
        gState.WindowStart = now;
        gState.WindowStartPosition = AbsoluteCamera();
        gState.Window = Stats();
        gState.WindowFrames = 0;
    } else if ( now - gState.WindowStart >= kReportSeconds ) {
        Report( now );
    }
}

void BeginFrame() {
    gState.bFrameValid = false;
    gState.pMainColour = nullptr;
    RenderPass &nearPass = gpRender->getRenderPass<eRenderPass::RP_Near>();
    if ( nearPass.pCamera == nullptr || nearPass.pScene == nullptr ||
         nearPass.pFramebufferAttachments == nullptr ||
         nearPass.pFramebufferAttachments->pAttachments[0] == nullptr ) {
        return;
    }
    gState.pMainColour =
        nearPass.pFramebufferAttachments->pAttachments[0]->getTextureColor();
    const Matrix4x4f_t &worldToCam = nearPass.pCamera->getWorldToCam();
    gState.ViewProjection =
        worldToCam * nearPass.pScene->getFrustumWrite().getProjectionMatrix();
    const Matrix4x4f_t camToWorld = worldToCam.inverse();
    gState.CameraPosition = Eigen::Vector3f( camToWorld( 3, 0 ), camToWorld( 3, 1 ),
                                             camToWorld( 3, 2 ) );
    gState.bFrameValid = true;

    // This frame's impostor sprites (queued by the update before the draws)
    for ( const Instance *pInstance : gState.PendingSprites ) {
        const Matrix4x4f_t &m = pInstance->getModelMatrix();
        Draw draw;
        draw.bSprite = true;
        draw.Hashcode = pInstance->getHashcode();
        draw.NumLODs = pInstance->getNumLODs();
        const Eigen::Vector3f position( m( 3, 0 ), m( 3, 1 ), m( 3, 2 ) );
        draw.Distance = ( position - gState.CameraPosition ).norm();
        const Eigen::RowVector4f clip =
            Eigen::RowVector4f( position.x(), position.y(), position.z(), 1.0f ) *
            gState.ViewProjection;
        draw.Edge = ( clip.w() <= 0.01f )
                        ? 2.0f
                        : Max( std::abs( clip.x() / clip.w() ),
                               std::abs( clip.y() / clip.w() ) );
        gState.Current.emplace( pInstance, draw );
    }
    gState.PendingSprites.clear();

    // The previous frame's resolved main view, in bursts
    static uint32_t sBurst = 0;
    static uint32_t sBurstFrame = 0;
    const float now = ( gpTimer != nullptr ) ? gpTimer->SystemTotalTime : 0.0f;
    if ( gbFrames && sBurst < kMaxBursts &&
         now >= kBurstFirst + kBurstEvery * static_cast<float>( sBurst ) ) {
        // The car pass's target holds the finished scene (its own 2DB)
        RenderPass &carPass = gpRender->getRenderPass<eRenderPass::RP_CarPlayer>();
        RenderTarget *pMainRT =
            ( carPass.pFramebufferAttachments != nullptr &&
              carPass.pFramebufferAttachments->pAttachments[0] != nullptr )
                ? carPass.pFramebufferAttachments->pAttachments[0]
                : nearPass.pFramebufferAttachments->pAttachments[0];
        Render2DB *pBound = pMainRT->getBound2DB();
        if ( pBound != nullptr && pBound->getFirstBitmap() != nullptr &&
             pBound->getFirstBitmap()->pTexture != nullptr ) {
            char name[64];
            snprintf( name, sizeof( name ), "pf_%u_%02u.bin", sBurst,
                      sBurstFrame );
            gEdRender.pRenderDevice->snapshotRenderTarget(
                pBound->getFirstBitmap()->pTexture, name );
            OTDU_LOG_INFO( "Pop frame %s at %.3f s, camera %.1f %.1f %.1f\n",
                           name, now, gState.CameraPosition.x(),
                           gState.CameraPosition.y(),
                           gState.CameraPosition.z() );
        }
        if ( ++sBurstFrame >= kBurstFrames ) {
            sBurstFrame = 0;
            sBurst++;
        }
    }
}
} // namespace

bool PopTracer::IsEnabled() { return gbEnabled; }

void PopTracer::OnDraw( const Instance *pInstance,
                        const Matrix4x4f_t &modelMatrix ) {
    if ( !gbEnabled || pInstance == nullptr || gpRender == nullptr ||
         gEdRender.pRenderDevice == nullptr ) {
        return;
    }

    const uint32_t frameIndex = gEdRender.pRenderDevice->getFrameIndex();
    const float now = ( gpTimer != nullptr ) ? gpTimer->SystemTotalTime : 0.0f;
    if ( frameIndex != gState.FrameIndex || now != gState.FrameTime ) {
        if ( gState.FrameTime >= 0.0f ) {
            EndFrame( now );
        }
        gState.FrameIndex = frameIndex;
        gState.FrameTime = now;
        BeginFrame();
    }
    if ( !gState.bFrameValid ) {
        return;
    }

    // Main view scenes drawn with the materials' own shaders only
    RenderScene *pScene = gSceneRenderer.pActiveScene;
    if ( pScene == nullptr || ( pScene->getFlags() & 0x4Eu ) != 0u ) {
        return;
    }
    FramebufferAttachments *pFramebuffer = pScene->getFramebuffer();
    if ( pFramebuffer == nullptr || pFramebuffer->pAttachments[0] == nullptr ||
         pFramebuffer->pAttachments[0]->getTextureColor() != gState.pMainColour ) {
        return;
    }

    Draw draw;
    draw.Hashcode = pInstance->getHashcode();
    draw.LOD = pInstance->getActiveLODIndex();
    draw.NumLODs = pInstance->getNumLODs();
    const Eigen::Vector3f position( modelMatrix( 3, 0 ), modelMatrix( 3, 1 ),
                                    modelMatrix( 3, 2 ) );
    draw.Distance = ( position - gState.CameraPosition ).norm();
    if ( draw.NumLODs > 0 ) {
        Instance *pMutable = const_cast<Instance *>( pInstance );
        const float lastThreshold =
            pMutable->getLOD( draw.NumLODs - 1 ).Distances[0];
        if ( lastThreshold > 0.0f ) {
            draw.Ratio = pInstance->getCurrentLODDistance() / lastThreshold;
        }
    }
    const Eigen::RowVector4f clip =
        Eigen::RowVector4f( position.x(), position.y(), position.z(), 1.0f ) *
        gState.ViewProjection;
    if ( clip.w() <= 0.01f ) {
        draw.Edge = 2.0f;
    } else {
        draw.Edge = Max( std::abs( clip.x() / clip.w() ),
                         std::abs( clip.y() / clip.w() ) );
    }

    auto it = gState.Current.find( pInstance );
    if ( it == gState.Current.end() ) {
        gState.Current.emplace( pInstance, draw );
    } else if ( it->second.bSprite ) {
        it->second = draw; // drawn as a mesh too
    }
}

void PopTracer::OnSprite( const Instance *pInstance ) {
    if ( gbEnabled && pInstance != nullptr ) {
        gState.PendingSprites.push_back( pInstance );
    }
}

void PopTracer::TagCaller( const Instance *pInstance, void *pReturnAddress ) {
    if ( gbEnabled && pInstance != nullptr ) {
        gState.Callers[pInstance] = reinterpret_cast<uintptr_t>( pReturnAddress );
    }
}
