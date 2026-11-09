#include "shared.h"
#include "postfx_motion_blur.h"

// OTDU_ADDITION: per-pixel motion blur (camera and vehicles).
//
// Two frame graph nodes, each: copy the scene, a velocity pass, a gather pass
// back into the scene (resources/shaders/postfx/motion_blur_*.frag).
//  - WORLD, right after the radial blur's slot (after RP_Sun, before the fake
//    DOF and the player's car): the world, traffic, parked cars, their wheels
//    and, when it is drawn with the world (no RP_CarPlayer pass this frame),
//    the player's car.
//  - PLAYER, after RP_CarCompo: only the player's car, when the car is drawn
//    in its own pass (chase cameras), so the world blur never smears it.
// The wheels' spin is drawn by their geometry (render/wheel_blur.cpp).
//
// The world is static: a pixel's motion is the camera's. A pixel inside a
// listed vehicle's box (or a wheel's cylinder, for the tyre below the box)
// moves with that vehicle (its previous transform).
//
// Positions are tracked in absolute coordinates (render space + world
// streaming origin, double precision) so an origin step is invisible, and
// handed to the shaders relative to this frame's camera.

#include "core/arg_parser.h"
#include "game/vehicle/car_state.h"
#include "game/vehicle/gs_car_state.h"
#include "game/vehicle/vehicle_base.h"
#include "gs_timer.h"
#include "render/2db.h"
#include "render/camera/camera_base.h"
#include "render/camera/gs_camera.h"
#include "render/ed_render.h"
#include "render/frame_graph.h"
#include "render/frustum.h"
#include "render/gs_render.h"
#include "render/gs_render_helper.h"
#include "render/hiearchy_node.h"
#include "render/instance.h"
#include "render/postfx/gs_postfx.h"
#include "render/postfx/postfx_instance.h"
#include "render/postfx/postfx_node.h"
#include "render/postfx/postfx_renderer.h"
#include "render/render_api.h"
#include "render/render_context.h"
#include "render/render_scene.h"
#include "render/render_target.h"
#include "render/scene_renderer.h"
#include "render/shaders/shader_types.h"
#include "render/vulkan/render_device.h"
#include "world/gs_world_streaming.h"

namespace {
// Shader table entries (shader_table_postfx.inl) / resources/shaders/manifest.txt
constexpr uint64_t kVelocityPSHashcode = 0x0d7b4d4200000001ull;
constexpr uint64_t kGatherPSHashcode = 0x0d7b4d4200000002ull;
constexpr uint64_t kVelocityMSAAPSHashcode = 0x0d7b4d4200000003ull; // MB_DEPTH_MSAA

// Must match resources/shaders/include/tdu_motion_blur.glsl
constexpr uint32_t kDataWidth = 256;
constexpr uint32_t kDataHeight = 4;
constexpr uint32_t kMaxVehicles = 32;
constexpr uint32_t kMaxWheels = 128;
constexpr uint32_t kWheelBase = kMaxVehicles * 7;
constexpr uint32_t kNumConstants = 20; // c0..c19
static_assert( kWheelBase + kMaxWheels * 3 <= kDataWidth * kDataHeight,
               "motion blur data texture too small" );

// The exposure is a fixed TIME, not a share of the frame: the streaks keep
// their length at any frame rate (a share of one frame's motion shrank to
// nothing at 144 fps). A pixel's blur RADIUS is half its motion over the
// exposure (the frame's motion scaled by exposure / frame time), capped at a
// share of the screen height.
// Tuned in game: 22.4 ms, cap 3.92 % of the height.
float gShutterSeconds = 0.0224f; // -mbshutter <seconds>
constexpr float kMaxRadiusShare = 0.0392f; // of the screen height (42 px at 1080p)
constexpr float kMaxExposureScale = 12.0f; // exposure / frame time

// Share of the CAMERA'S ROTATION kept in the blur (the world's and other cars'
// pixels; the player's car always keeps its true motion, the camera follows
// it): the full rotation blur was too strong next to the speed blur, which
// keeps all of the camera's movement. -mbrotation <0..1>
float gRotationShare = 0.3f;

// A camera that jumps further than this in one frame is a cut: no blur.
constexpr double kCutDistance = 25.0;
constexpr double kCutCosAngle = 0.5;
// A gap longer than this between two world passes (blur off, menus) restarts
// the history instead of blurring over it.
constexpr float kMaxHistoryGapSeconds = 0.25f;

constexpr float kDefaultTyreHalfWidth = 0.18f; // ~0.3 m tyre + margin
constexpr float kBoxMargin = 0.05f;

bool gbEnabled = true;
int32_t gDebugView = 0;

static CmdLineArg CmdLineArgNoMotionBlur( "nomotionblur",
                                          []( const char * ) {
                                              gbEnabled = false;
                                          } );
// 1 = blur vectors, 2 = vehicles (blue) / wheels (red)
static CmdLineArg CmdLineArgMotionBlurDebug( "mbdebug", []( const char *pArg ) {
    gDebugView = ( pArg != nullptr ) ? atoi( pArg ) : 1;
} );
// Exposure times for tuning in game (seconds; 0 turns that part off)
static CmdLineArg CmdLineArgMotionBlurShutter( "mbshutter", []( const char *pArg ) {
    if ( pArg != nullptr ) {
        gShutterSeconds = Clamp( static_cast<float>( atof( pArg ) ), 0.0f, 0.25f );
    }
} );
static CmdLineArg CmdLineArgMotionBlurRotation( "mbrotation", []( const char *pArg ) {
    if ( pArg != nullptr ) {
        gRotationShare = Clamp( static_cast<float>( atof( pArg ) ), 0.0f, 1.0f );
    }
} );

using Mat4d = Eigen::Matrix<double, 4, 4, Eigen::RowMajor>;
using Vec3d = Eigen::Vector3d;

Mat4d Translate( const Vec3d &t ) {
    Mat4d m = Mat4d::Identity();
    m( 3, 0 ) = t.x();
    m( 3, 1 ) = t.y();
    m( 3, 2 ) = t.z();
    return m;
}

Vec3d StreamingOrigin() {
    if ( gpWorldStreaming != nullptr && gpWorldStreaming->isEnabled() ) {
        const Eigen::Vector4f &p = gpWorldStreaming->getPosition();
        return Vec3d( p.x(), p.y(), p.z() );
    }
    return Vec3d::Zero();
}

Mat4d ToAbsolute( const Matrix4x4f_t &renderSpace, const Vec3d &origin ) {
    Mat4d m = renderSpace.cast<double>();
    m( 3, 0 ) += origin.x();
    m( 3, 1 ) += origin.y();
    m( 3, 2 ) += origin.z();
    return m;
}

// Row vector maths (p' = p * M): the shader's dot( p, row_i ) wants column i
void StoreColumns( float *pOut, const Mat4d &m, uint32_t numColumns ) {
    for ( uint32_t c = 0; c < numColumns; c++ ) {
        for ( uint32_t r = 0; r < 4; r++ ) {
            pOut[c * 4 + r] = static_cast<float>( m( r, c ) );
        }
    }
}

struct VehicleTrack {
    const VehicleBase *pVehicle = nullptr;
    Mat4d PreviousAbsolute = Mat4d::Identity();
    bool bValid = false;
};

struct PassData {
    std::vector<float> Texels; // RGBA32F, kDataWidth x kDataHeight
    uint32_t NumVehicles = 0;
    uint32_t NumWheels = 0;

    void clear() {
        Texels.assign( kDataWidth * kDataHeight * 4, 0.0f );
        NumVehicles = 0;
        NumWheels = 0;
    }
    float *texel( uint32_t index ) { return &Texels[index * 4]; }
};

struct FrameState {
    float Constants[kNumConstants][4] = {};
    PassData World;
    PassData Player;
    bool bPlayerPass = false; // the player's car is drawn in RP_CarPlayer
    bool bReady = false;      // built by this frame's world pass

    bool bHaveHistory = false;
    Mat4d PreviousViewProjAbsolute = Mat4d::Identity();
    Mat4d PreviousProjection = Mat4d::Identity();
    Vec3d PreviousCameraPosition = Vec3d::Zero();
    Vec3d PreviousCameraForward = Vec3d::UnitZ();
    float LastUpdateTime = -1.0f;
    std::array<VehicleTrack, kMaxVehicles> Tracks;
};

FrameState gState;
bool gbWorking = false; // last world pass drew (MotionBlur::IsActive)

// A vehicle as the shaders see it, built from its render hierarchy
struct VehicleSample {
    Mat4d CurrentAbsolute;
    Vec3d BoxCentre;
    Vec3d BoxHalfExtents;
    uint32_t NumWheels = 0;
    Vec3d WheelHub[4];
    Vec3d WheelAxle[4];
    float WheelRadius[4] = {};
};

bool SampleVehicle( CarState &carState,
                    VehicleBase *pVehicle,
                    const Vec3d &origin,
                    VehicleSample &out ) {
    HiearchyNode *pRoot = pVehicle->getNodeByIndex( 0 );
    if ( pRoot == nullptr || pRoot->pInstance == nullptr ) {
        return false;
    }
    out.CurrentAbsolute = ToAbsolute( pRoot->pInstance->getModelMatrix(), origin );
    const Mat4d toLocal = out.CurrentAbsolute.inverse();

    const Eigen::Vector4f &dims = carState.getBody()->BodyDimensions;
    const double width = ( dims.x() > 0.5f ) ? dims.x() : 1.9;
    const double height = ( dims.y() > 0.3f ) ? dims.y() : 1.4;
    const double length = ( dims.z() > 1.0f ) ? dims.z() : 4.6;

    double halfX = width * 0.5;
    double halfZ = length * 0.5;
    double minY = -0.1;
    double maxY = height;

    const VehicleBase::eType type = pVehicle->getType();
    const bool bHasCarWheels = type == VehicleBase::eType::VBT_Car ||
                               type == VehicleBase::eType::VBT_TrafficCar;
    out.NumWheels = 0;
    if ( bHasCarWheels ) {
        double lowestWheel = 1e9;
        for ( uint32_t w = 0; w < 4; w++ ) {
            HiearchyNode *pDisc = pVehicle->getNodeByIndex( 4 + w ); // DISC_xx
            if ( pDisc == nullptr || pDisc->pInstance == nullptr ) {
                continue;
            }
            const Mat4d disc =
                ToAbsolute( pDisc->pInstance->getModelMatrix(), origin );
            const Vec3d hub = disc.row( 3 ).head<3>().transpose();
            Vec3d axle = disc.row( 0 ).head<3>().transpose();
            if ( axle.norm() < 1e-6 ) {
                continue;
            }
            axle.normalize();

            const WheelState &wheel = carState.getWheel( w );
            const float radius =
                ( wheel.WheelRadius > 0.1f && wheel.WheelRadius < 1.5f )
                    ? wheel.WheelRadius
                    : 0.32f;
            const uint32_t index = out.NumWheels++;
            out.WheelHub[index] = hub;
            out.WheelAxle[index] = axle;
            out.WheelRadius[index] = radius + 0.01f;

            // The box has to hold the wheels (it decides how they move)
            Eigen::Matrix<double, 1, 4> hubRow( hub.x(), hub.y(), hub.z(), 1.0 );
            const Eigen::Matrix<double, 1, 4> local = hubRow * toLocal;
            lowestWheel = Min( lowestWheel, local( 1 ) - radius );
            halfX = Max( halfX, std::abs( local( 0 ) ) + kDefaultTyreHalfWidth );
            halfZ = Max( halfZ, std::abs( local( 2 ) ) + radius );
        }
        if ( lowestWheel < 1e8 ) {
            // Stop just above the road the car stands on
            minY = lowestWheel + 0.03;
        }
    }

    halfX += kBoxMargin;
    halfZ += kBoxMargin;
    maxY += kBoxMargin;
    out.BoxCentre = Vec3d( 0.0, ( minY + maxY ) * 0.5, 0.0 );
    out.BoxHalfExtents = Vec3d( halfX, ( maxY - minY ) * 0.5, halfZ );
    return true;
}

void AppendVehicle( PassData &pass,
                    const VehicleSample &sample,
                    const Mat4d &previousAbsolute,
                    const Vec3d &cameraPosition,
                    bool bFollowedByCamera ) {
    float vehicleIndex = -1.0f; // the wheels' car (-1: list full, wheels only)
    if ( pass.NumVehicles < kMaxVehicles ) {
        vehicleIndex = static_cast<float>( pass.NumVehicles );
        const Mat4d toBox = Translate( cameraPosition ) *
                            sample.CurrentAbsolute.inverse() *
                            Translate( -sample.BoxCentre );
        const Mat4d boxToPrevious = Translate( sample.BoxCentre ) *
                                    previousAbsolute *
                                    Translate( -cameraPosition );

        const uint32_t base = pass.NumVehicles * 7;
        float columns[16];
        StoreColumns( columns, toBox, 3 );
        for ( uint32_t i = 0; i < 3; i++ ) {
            memcpy( pass.texel( base + i ), &columns[i * 4], sizeof( float ) * 4 );
        }
        StoreColumns( columns, boxToPrevious, 3 );
        for ( uint32_t i = 0; i < 3; i++ ) {
            memcpy( pass.texel( base + 3 + i ), &columns[i * 4],
                    sizeof( float ) * 4 );
        }
        float *pExtents = pass.texel( base + 6 );
        pExtents[0] = static_cast<float>( sample.BoxHalfExtents.x() );
        pExtents[1] = static_cast<float>( sample.BoxHalfExtents.y() );
        pExtents[2] = static_cast<float>( sample.BoxHalfExtents.z() );
        // 1: keeps its true motion (the camera's rotation is not damped)
        pExtents[3] = bFollowedByCamera ? 1.0f : 0.0f;
        pass.NumVehicles++;
    }

    for ( uint32_t w = 0; w < sample.NumWheels && pass.NumWheels < kMaxWheels;
          w++ ) {
        const uint32_t base = kWheelBase + pass.NumWheels * 3;
        const Vec3d hub = sample.WheelHub[w] - cameraPosition;
        float *pHub = pass.texel( base );
        pHub[0] = static_cast<float>( hub.x() );
        pHub[1] = static_cast<float>( hub.y() );
        pHub[2] = static_cast<float>( hub.z() );
        pHub[3] = sample.WheelRadius[w];
        float *pAxle = pass.texel( base + 1 );
        pAxle[0] = static_cast<float>( sample.WheelAxle[w].x() );
        pAxle[1] = static_cast<float>( sample.WheelAxle[w].y() );
        pAxle[2] = static_cast<float>( sample.WheelAxle[w].z() );
        pAxle[3] = kDefaultTyreHalfWidth;
        pass.texel( base + 2 )[0] = 0.0f; // spin: render/wheel_blur.cpp
        pass.texel( base + 2 )[1] = vehicleIndex;
        pass.NumWheels++;
    }
}

// Builds this frame's camera constants and both vehicle lists. Returns false
// when there is nothing sensible to blur with (no camera yet).
bool BuildFrameState() {
    RenderPass &nearPass = gpRender->getRenderPass<eRenderPass::RP_Near>();
    if ( nearPass.pCamera == nullptr || nearPass.pScene == nullptr ) {
        return false;
    }

    const float now = ( gpTimer != nullptr ) ? gpTimer->SystemTotalTime : 0.0f;
    const float deltaTime =
        ( gpTimer != nullptr ) ? Max( gpTimer->GameDeltaTime, 0.0f ) : 0.0f;
    const Vec3d origin = StreamingOrigin();

    // The matrices RP_Near was drawn with this frame (render space)
    const Mat4d worldToCam = nearPass.pCamera->getWorldToCam().cast<double>();
    const Mat4d projection =
        nearPass.pScene->getFrustumWrite().getProjectionMatrix().cast<double>();
    const Mat4d camToWorld = worldToCam.inverse();

    const Vec3d cameraPosition =
        camToWorld.row( 3 ).head<3>().transpose() + origin;
    const Vec3d cameraForward =
        -camToWorld.row( 2 ).head<3>().transpose().normalized();
    const Mat4d viewProjAbsolute =
        Translate( -origin ) * worldToCam * projection;

    bool bHistory = gState.bHaveHistory && gState.LastUpdateTime >= 0.0f &&
                    ( now - gState.LastUpdateTime ) <= kMaxHistoryGapSeconds;
    if ( bHistory &&
         ( ( cameraPosition - gState.PreviousCameraPosition ).norm() >
               kCutDistance ||
           cameraForward.dot( gState.PreviousCameraForward ) < kCutCosAngle ) ) {
        bHistory = false; // camera cut
    }

    const Mat4d viewProjRelative = Translate( cameraPosition ) * viewProjAbsolute;
    const Mat4d previousViewProjRelative =
        bHistory ? Mat4d( Translate( cameraPosition ) *
                          gState.PreviousViewProjAbsolute )
                 : viewProjRelative;
    // The previous camera turned to THIS frame's orientation: where a point
    // was through the camera's movement alone (its translation and zoom).
    // The shader blends between the two by gRotationShare.
    const Mat4d rotation =
        Translate( camToWorld.row( 3 ).head<3>().transpose() ) * worldToCam;
    const Mat4d previousStillRelative =
        bHistory ? Mat4d( Translate( cameraPosition -
                                     gState.PreviousCameraPosition ) *
                          rotation * gState.PreviousProjection )
                 : viewProjRelative;

    float( *c )[4] = gState.Constants;
    memset( c, 0, sizeof( gState.Constants ) );
    StoreColumns( &c[0][0], viewProjRelative.inverse(), 4 );
    StoreColumns( &c[4][0], previousViewProjRelative, 4 );
    StoreColumns( &c[8][0], viewProjRelative, 4 );
    StoreColumns( &c[16][0], previousStillRelative, 4 );
    c[14][2] = gRotationShare;
    // c12 (screen size) and c13 (mode / counts) are set per pass
    c[13][3] = static_cast<float>( gDebugView );
    // Blur radius = half the motion over the exposure; the frame's motion
    // covers deltaTime. c14.y (the cap in pixels) is set per pass.
    const float exposureScale =
        ( deltaTime > 1e-4f )
            ? Min( gShutterSeconds / deltaTime, kMaxExposureScale )
            : 0.0f;
    c[14][0] = bHistory ? exposureScale * 0.5f : 0.0f;
    c[15][0] = static_cast<float>( cameraForward.x() );
    c[15][1] = static_cast<float>( cameraForward.y() );
    c[15][2] = static_cast<float>( cameraForward.z() );

    // Vehicles
    gState.World.clear();
    gState.Player.clear();
    gState.bPlayerPass = false;

    CarState *pBoundCar =
        ( gpCamera != nullptr ) ? gpCamera->getBoundCarState() : nullptr;
    std::array<bool, kMaxVehicles> bSeen = {};
    if ( gpCarState != nullptr ) {
        CarState *pCarStates = gpCarState->getCarStates();
        for ( int32_t i = 0; i < static_cast<int32_t>( kMaxVehicles ); i++ ) {
            if ( !gpCarState->isCarStateUsed( i ) ) {
                continue;
            }
            CarState &carState = pCarStates[i];
            VehicleBase *pVehicle = carState.getVehicleInstance();
            if ( pVehicle == nullptr || !pVehicle->isInitialized() ||
                 !pVehicle->isDisplayed() ) {
                continue;
            }

            VehicleSample sample;
            if ( !SampleVehicle( carState, pVehicle, origin, sample ) ) {
                continue;
            }

            VehicleTrack &track = gState.Tracks[i];
            const bool bTracked =
                bHistory && track.bValid && track.pVehicle == pVehicle;
            const Mat4d previous =
                bTracked ? track.PreviousAbsolute : sample.CurrentAbsolute;

            const bool bPlayer = ( &carState == pBoundCar );

            if ( bPlayer &&
                 pVehicle->getRenderPass() == eRenderPass::RP_CarPlayer ) {
                gState.bPlayerPass = true;
                AppendVehicle( gState.Player, sample, previous, cameraPosition,
                               true );
            } else {
                AppendVehicle( gState.World, sample, previous, cameraPosition,
                               bPlayer );
            }

            track.pVehicle = pVehicle;
            track.PreviousAbsolute = sample.CurrentAbsolute;
            track.bValid = true;
            bSeen[i] = true;
        }
    }
    for ( uint32_t i = 0; i < kMaxVehicles; i++ ) {
        if ( !bSeen[i] ) {
            gState.Tracks[i].bValid = false;
        }
    }

    gState.PreviousViewProjAbsolute = viewProjAbsolute;
    gState.PreviousProjection = projection;
    gState.PreviousCameraPosition = cameraPosition;
    gState.PreviousCameraForward = cameraForward;
    gState.LastUpdateTime = now;
    gState.bHaveHistory = true;
    return true;
}

// Owned render targets / textures, sized to the scene
struct Resources {
    RuntimeRender2DB CopyBlob;
    RuntimeRender2DB VelocityBlob;
    RuntimeRender2DB WorldDataBlob;
    RuntimeRender2DB PlayerDataBlob;
    RenderTarget *pCopyRT = nullptr;
    RenderTarget *pVelocityRT = nullptr;
    uint32_t Width = 0;
    uint32_t Height = 0;
    eViewFormat Format = VF_Invalid;
    bool bDataCreated = false;

    void releaseTargets() {
        if ( pCopyRT != nullptr ) {
            ReleasePooledRenderTarget( pCopyRT );
            pCopyRT = nullptr;
            CopyBlob.destroy();
        }
        if ( pVelocityRT != nullptr ) {
            ReleasePooledRenderTarget( pVelocityRT );
            pVelocityRT = nullptr;
            VelocityBlob.destroy();
        }
        Width = 0;
        Height = 0;
    }

    void release() {
        releaseTargets();
        if ( bDataCreated ) {
            WorldDataBlob.destroy();
            PlayerDataBlob.destroy();
            bDataCreated = false;
        }
    }

    bool ensure( RenderTarget *pScene ) {
        if ( !bDataCreated ) {
            if ( !WorldDataBlob.allocateAndCreate( kDataWidth, kDataHeight, 1,
                                                   1, VF_A32B32G32R32F, 0,
                                                   "MotionBlurDataWorld" ) ||
                 !PlayerDataBlob.allocateAndCreate( kDataWidth, kDataHeight, 1,
                                                    1, VF_A32B32G32R32F, 0,
                                                    "MotionBlurDataPlayer" ) ) {
                return false;
            }
            bDataCreated = true;
        }

        const uint32_t width = pScene->getWidth();
        const uint32_t height = pScene->getHeight();
        const eViewFormat format = pScene->getFormat();
        if ( width == Width && height == Height && format == Format &&
             pCopyRT != nullptr && pVelocityRT != nullptr ) {
            return true;
        }

        releaseTargets();
        if ( !CopyBlob.allocateAndCreate( width, height, 1, 1, format, 0x200,
                                          "MotionBlurScene" ) ||
             !VelocityBlob.allocateAndCreate( width, height, 1, 1,
                                              VF_A16B16G16R16F, 0x200,
                                              "MotionBlurVelocity" ) ) {
            return false;
        }
        // 0x4: no depth-stencil (the velocity pass samples the scene depth)
        pCopyRT = CreateRenderTargetFrom2DB( &CopyBlob, 0x4 );
        pVelocityRT = CreateRenderTargetFrom2DB( &VelocityBlob, 0x4 );
        if ( pCopyRT == nullptr || pVelocityRT == nullptr ) {
            releaseTargets();
            return false;
        }
        Width = width;
        Height = height;
        Format = format;
        OTDU_LOG_INFO( "Motion blur targets: %ux%u\n", width, height );
        return true;
    }
};

Resources gResources;

void UploadData( RuntimeRender2DB &blob, const PassData &pass ) {
    Texture *pTexture = blob.getFirstBitmap();
    void *pTexels = Render2DB::GetTexelsPointer( pTexture, 0, 0 );
    memcpy( pTexels, pass.Texels.data(), pass.Texels.size() * sizeof( float ) );
    gEdRender.pRenderDevice->uploadTexture( pTexture );
}

enum class ePass { World, Player };

class PostFXMotionBlur : public PostFX {
public:
    PostFXMotionBlur( PostFXNode *pOwner, ePass pass )
        : PostFX( pOwner ), pass( pass ), pVelocityShader( nullptr ) {}

    bool initialize() override {
        if ( !PostFX::initialize() ) {
            return false;
        }
        pVelocityShader =
            (PixelShader *)gpRenderContext->getShaderHashTable().getShader(
                kVelocityPSHashcode );
        pVelocityMSAAShader =
            (PixelShader *)gpRenderContext->getShaderHashTable().getShader(
                kVelocityMSAAPSHashcode );
        return pVelocityShader != nullptr && pVelocityMSAAShader != nullptr;
    }

    void releaseResources() override {
        for ( PixelShader **ppShader : { &pVelocityShader, &pVelocityMSAAShader } ) {
            if ( *ppShader != nullptr ) {
                ( *ppShader )->destroy();
                *ppShader = nullptr;
            }
        }
        PostFX::releaseResources();
    }

    uint64_t getPixelShaderHashcode() const override {
        return kGatherPSHashcode;
    }

    bool execute() override {
        if ( pass == ePass::World ) {
            gState.bReady = false;
            gbWorking = false;
        }
        if ( !gbEnabled ) {
            return true;
        }
        if ( !PostFX::execute() ) {
            return true; // shaders missing: leave the radial blur in charge
        }

        RenderTarget *pScene =
            gpRender->getRenderPass<eRenderPass::RP_Near>()
                .pFramebufferAttachments->pAttachments[0];
        const bool bMSAA =
            pScene != nullptr && pScene->getTextureColor() != nullptr &&
            pScene->getTextureColor()->SampleCount != VK_SAMPLE_COUNT_1_BIT;
        // Under MSAA the scene is read from its resolved single sample copy
        // (the render target's bound 2DB, what resolveMSAA writes)
        Render2DB *pResolved2DB =
            ( pScene != nullptr ) ? pScene->getBound2DB() : nullptr;
        if ( pScene == nullptr || pScene->getTextureColor() == nullptr ||
             pScene->getDepthStencil() == nullptr ||
             ( bMSAA && ( pResolved2DB == nullptr ||
                          pResolved2DB->getFirstBitmap() == nullptr ||
                          pResolved2DB->getFirstBitmap()->pTexture == nullptr ) ) ) {
            char reason[96];
            snprintf( reason, sizeof( reason ),
                      "skipped (colour %d, depth %d, AA method %d)",
                      ( pScene && pScene->getTextureColor() ) ? 1 : 0,
                      ( pScene && pScene->getDepthStencil() ) ? 1 : 0,
                      pScene ? (int32_t)pScene->getAntiAliasingMethod() : -1 );
            logDiagnostics( pScene, gState.World, reason );
            return true;
        }

        if ( pass == ePass::World ) {
            if ( !BuildFrameState() ) {
                logDiagnostics( pScene, gState.World, "skipped (no camera)" );
                return true;
            }
            gState.bReady = true;
        } else if ( !gState.bReady || !gState.bPlayerPass ) {
            return true;
        }

        if ( !gResources.ensure( pScene ) ) {
            logDiagnostics( pScene, gState.World, "skipped (targets)" );
            return true;
        }

        PassData &data =
            ( pass == ePass::World ) ? gState.World : gState.Player;
        RuntimeRender2DB &dataBlob = ( pass == ePass::World )
                                         ? gResources.WorldDataBlob
                                         : gResources.PlayerDataBlob;
        UploadData( dataBlob, data );

        const float width = static_cast<float>( gResources.Width );
        const float height = static_cast<float>( gResources.Height );
        float( *c )[4] = gState.Constants;
        c[12][0] = width;
        c[12][1] = height;
        c[12][2] = 1.0f / width;
        c[12][3] = 1.0f / height;
        c[13][0] = ( pass == ePass::World ) ? 0.0f : 1.0f;
        c[13][1] = static_cast<float>( data.NumVehicles );
        c[13][2] = static_cast<float>( data.NumWheels );
        c[14][1] = kMaxRadiusShare * height;

        RenderDevice *pDevice = gEdRender.pRenderDevice;

        // 1. A single sample copy of the scene (the gather reads it and writes
        // the scene): resolved under MSAA, else copied
        Texture *pCopyTexture = nullptr;
        if ( bMSAA ) {
            pDevice->resolveMSAA( pScene, 0 );
            pCopyTexture = pResolved2DB->getFirstBitmap();
        } else {
            pDevice->blit( pScene->getTextureColor(), gResources.pCopyRT, false );
            pCopyTexture = gResources.pCopyRT->getBound2DB()->getFirstBitmap();
        }

        Texture *pDataTexture = dataBlob.getFirstBitmap();
        Texture *pVelocityTexture =
            gResources.pVelocityRT->getBound2DB()->getFirstBitmap();

        GPUTexture *pDepth = pScene->getDepthStencil();
        depthTexture.Hashcode = 0x0d7b4d42deb7400dull;
        depthTexture.Width = static_cast<uint16_t>( pDepth->Width );
        depthTexture.Height = static_cast<uint16_t>( pDepth->Height );
        depthTexture.Depth = 1;
        depthTexture.NumMipmap = 1;
        depthTexture.NumPackedMipmap = 1;
        // A render target's texture: nothing to upload (0x200), already on
        // the GPU (else RenderContext::bindTexture copies texels from after
        // this struct)
        depthTexture.Flags = 0x200;
        depthTexture.bUploaded = 1;
        depthTexture.Format = VF_D24S8;
        depthTexture.pTexture = pDepth;

        // 2. Velocity
        gEdRender.bindRenderTargetAndSetViewport( gResources.pVelocityRT, 0 );
        gPostFXRenderer.bindShaders(
            nullptr, ( pDepth->SampleCount != VK_SAMPLE_COUNT_1_BIT )
                         ? pVelocityMSAAShader
                         : pVelocityShader );
        gPostFXRenderer.setRenderPassDimensions( width, height );
        pDevice->setFloatConstants( eShaderType::ST_Pixel, &c[0][0], 0,
                                    kNumConstants );
        gpRenderContext->bindTexture( &depthTexture, 1 );
        gpRenderContext->bindTexture( pDataTexture, 2 );
        gPostFXRenderer.drawFullscreenQuad( false, false, false, 0xffffffff );
        // The depth must not stay bound: it is the next passes' attachment
        gpRenderContext->bindTexture( nullptr, 1 );

        // 3. Gather into the scene
        gEdRender.bindRenderTargetAndSetViewport( pScene, 0 );
        gPostFXRenderer.bindShaders( nullptr, pPixelShader );
        gPostFXRenderer.setRenderPassDimensions( width, height );
        pDevice->setFloatConstants( eShaderType::ST_Pixel, &c[0][0], 0,
                                    kNumConstants );
        SetSamplerFilter( pCopyTexture, 0, 5, TF_Linear );
        SetSamplerFilter( pCopyTexture, 0, 6, TF_Linear );
        gpRenderContext->bindTexture( pCopyTexture, 0 );
        gpRenderContext->bindTexture( pVelocityTexture, 1 );
        gpRenderContext->bindTexture( pDataTexture, 2 );
        gPostFXRenderer.drawFullscreenQuad( false, false, false, 0xffffffff );
        gpRenderContext->bindTexture( nullptr, 0 );
        gpRenderContext->bindTexture( nullptr, 1 );
        gpRenderContext->bindTexture( nullptr, 2 );

        // Later passes sample the render target's bound 2DB, which a scene
        // fills when it ends (RenderContextBase::endScene). The world pass is
        // followed by the car scenes; the player pass by none, so without this
        // its output never reaches the screen.
        pDevice->resolveMSAA( pScene, 0 );

        if ( pass == ePass::World ) {
            gbWorking = true;
        }
        logDiagnostics( pScene, data, "drew" );
        return true;
    }

    // One state line whenever the outcome changes (drew / skipped why)
    void logDiagnostics( RenderTarget *pScene, const PassData &data,
                         const char *pWhat ) {
        if ( strncmp( lastWhat, pWhat, sizeof( lastWhat ) - 1 ) == 0 ) {
            return;
        }
        strncpy( lastWhat, pWhat, sizeof( lastWhat ) - 1 );
        RenderScene *pLastScene = gSceneRenderer.pActiveScene;
        OTDU_LOG_INFO( "Motion blur %s: %s after scene '%s', target %ux%u, "
                       "vehicles %u, wheels %u, radius share %.2f, player "
                       "pass %d\n",
                       ( pass == ePass::World ) ? "world" : "player", pWhat,
                       ( pLastScene != nullptr ) ? pLastScene->getName()
                                                 : "<none>",
                       pScene != nullptr ? pScene->getWidth() : 0u,
                       pScene != nullptr ? pScene->getHeight() : 0u,
                       data.NumVehicles, data.NumWheels,
                       gState.Constants[14][0], gState.bPlayerPass ? 1 : 0 );
    }
    char lastWhat[96] = {};

private:
    ePass pass;
    PixelShader *pVelocityShader;
    PixelShader *pVelocityMSAAShader = nullptr;
    Texture depthTexture = {};
};

class PostFXNodeMotionBlur : public PostFXNode {
public:
    explicit PostFXNodeMotionBlur( ePass pass ) : PostFXNode( 0, 0 ) {
        pPostFX = new ( TestDrive::Alloc( sizeof( PostFXMotionBlur ) ) )
            PostFXMotionBlur( this, pass );
    }
    ~PostFXNodeMotionBlur() { destroyEffect(); }
    uint32_t getType() const override { return 0x7b; }
};

PostFXNodeMotionBlur *gpWorldNode = nullptr;
PostFXNodeMotionBlur *gpPlayerNode = nullptr;
} // namespace

void MotionBlur::Initialize() {
    if ( gpWorldNode != nullptr ) {
        return;
    }

    gpWorldNode = new ( TestDrive::Alloc( sizeof( PostFXNodeMotionBlur ) ) )
        PostFXNodeMotionBlur( ePass::World );
    gpWorldNode->setName( "MotionBlurWorld" );
    gpPlayerNode = new ( TestDrive::Alloc( sizeof( PostFXNodeMotionBlur ) ) )
        PostFXNodeMotionBlur( ePass::Player );
    gpPlayerNode->setName( "MotionBlurPlayer" );

    // World: after RP_Sun and the radial blur node, before the fake DOF
    PostFXStep *pRadialBlur = gpPostFX->getEffect<ePostFXStep::PFXS_RadialBlur>();
    RenderObjectBase *pAfter =
        ( pRadialBlur != nullptr && pRadialBlur->getInput() != nullptr )
            ? static_cast<RenderObjectBase *>( pRadialBlur->getInput() )
            : static_cast<RenderObjectBase *>(
                  gpRender->getRenderPass<eRenderPass::RP_Sun>().pScene );
    gEdRender.pFrameGraph->insertAfterObject( gpWorldNode, pAfter );

    // Player: right after the player's car is composited
    gEdRender.pFrameGraph->insertAfterObject(
        gpPlayerNode,
        gpRender->getRenderPass<eRenderPass::RP_CarCompo>().pScene );

    OTDU_LOG_INFO( "Motion blur %s\n", gbEnabled ? "enabled" : "disabled "
                                                              "(-nomotionblur)" );
}

void MotionBlur::Destroy() {
    for ( PostFXNodeMotionBlur **ppNode : { &gpWorldNode, &gpPlayerNode } ) {
        if ( *ppNode == nullptr ) {
            continue;
        }
        gEdRender.pFrameGraph->removeObject( *ppNode );
        ( *ppNode )->~PostFXNodeMotionBlur();
        TestDrive::Free( *ppNode );
        *ppNode = nullptr;
    }
    gResources.release();
    gbWorking = false;
    gState.bHaveHistory = false;
}

bool MotionBlur::IsActive() { return gbEnabled && gbWorking; }
