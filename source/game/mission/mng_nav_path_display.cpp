#include "shared.h"
#include "mng_nav_path_display.h"

#include "ai/ai_vehicle.h"
#include "core/maths_helper.h"
#include "game/scenaric/gs_scenaric.h"
#include "game/vehicle/car_state.h"
#include "render/gs_render.h"
#include "render/material.h"
#include "render/render_scene.h"
#include "world/gs_world_streaming.h"

MngNavPathDisplay *gpMngNavPathDisplay = nullptr; // DAT_010e8050

// Same alpha-blended material as the road network (MngRoadNetwork).
static constexpr uint64_t kPathMaterialHashcode = 0x73078865ca37336dull;
static constexpr uint32_t kPathColor = 0x5000ff00;
static constexpr float kPathHeight = 0.4f;            // DAT_00f4175c
static constexpr float kPathHalfWidth = 0.25f;        // DAT_00f416dc
static constexpr float kTargetSpeedThreshold = 1.38f; // DAT_00f41b14
static constexpr float kMaxAlpha = 255.0f;            // DAT_00f41688
static constexpr float kAlphaBlendDuration = 0.5f;    // DAT_00f41674
static constexpr float kMinHistoryDistanceSq = 1.0f;

void MngNavPathDisplay::Writer::emit( const Eigen::Vector4f &position,
                                      const uint32_t color ) {
    // FUN_00432730
    *pPosition++ = position.head<3>();
    *pDiffuse++ = color;
    *pIndex++ = NextIndex++;
}

MngNavPathDisplay::MngNavPathDisplay()
    : Manager(),
      drawList(),
      writer(),
      p2DMBuffer( nullptr ),
      path2DM(),
      pMaterial( nullptr ),
      bEnabled( false ),
      lastTargetSpeed( 0.0f ),
      alphaFromA( 0.0f ),
      alphaFromB( 0.0f ),
      alphaToA( 0.0f ),
      alphaToB( 0.0f ),
      alphaA( 0.0f ),
      alphaB( 0.0f ),
      blendTime( 0.0f ),
      history(),
      numHistory( 0 ),
      historyStart( 0 ) {
    // FUN_006b3230
    gpMngNavPathDisplay = this;
}

MngNavPathDisplay::~MngNavPathDisplay() {
    // FUN_006b2a60
    gpMngNavPathDisplay = nullptr;
}

bool MngNavPathDisplay::initialize( TestDriveGameInstance * ) {
    // FUN_006b2910
    if ( !gpScenaric->isTrainingLevel() ) {
        return true;
    }

    DrawStreams streams;
    streams.Diffuse = 1;
    streams.bIndex16 = 1;
    if ( !drawList.initialize( 1, kMaxNumVertex, kMaxNumVertex, false, &streams,
                               "MngNavPathDisplay", true ) ) {
        return false;
    }

    p2DMBuffer = TestDrive::Alloc( Render2DM::CalcSize( 0, 0 ) );
    if ( p2DMBuffer == nullptr ) {
        return false;
    }

    pMaterial =
        Render2DM::Create( p2DMBuffer, kPathMaterialHashcode, 0, 0, 0, 1 );
    if ( pMaterial == nullptr ) {
        return false;
    }
    pMaterial->SrcBlend = (eBlendMode)5;
    pMaterial->DstBlend = (eBlendMode)6;
    pMaterial->BlendOP = (eBlendOP)1;
    pMaterial->DepthWrite = 2;
    pMaterial->AlphaTest = (eComparisonFunction)1;
    pMaterial->AlphaRef = 10;
    pMaterial->BackfaceCullingMode = (eCullMode)3;
    path2DM.initialize( p2DMBuffer );
    pMaterial->OT = 0x26;

    bEnabled = true;
    return true;
}

void MngNavPathDisplay::reset() {
    // FUN_006b28b0
    lastTargetSpeed = 0.0f;
    alphaA = 0.0f;
    alphaToA = 0.0f;
    alphaFromA = 0.0f;
    alphaB = kMaxAlpha;
    alphaToB = kMaxAlpha;
    alphaFromB = kMaxAlpha;
    numHistory = 0;
    historyStart = 0;
}

void MngNavPathDisplay::pause() {
    // FUN_006b28a0
}

void MngNavPathDisplay::terminate() {
    // FUN_006b2830
    if ( p2DMBuffer != nullptr ) {
        path2DM.destroy();
        TestDrive::Free( p2DMBuffer );
        pMaterial = nullptr;
        p2DMBuffer = nullptr;
    }
    if ( drawList.isInitialized() ) {
        drawList.destroy();
    }
    bEnabled = false;
}

void MngNavPathDisplay::draw() {
    // FUN_006b32a0
    OTDU_UNIMPLEMENTED_ONCE; // TODO: REPLAY (DAT_00fa97dc == 5 hides the path)
    if ( bEnabled && drawList.getNumPrimitives() > 0 ) {
        gpRender->getRenderPass<eRenderPass::RP_Near>()
            .pScene->enqueueDynamicDrawList( &drawList );
    }
}

bool MngNavPathDisplay::beginStrip( const int32_t numVertex ) {
    // FUN_004326e0
    if ( drawList.allocatePrimitive( PT_TriangleStrip, numVertex, numVertex ) ==
         -1 ) {
        return false;
    }
    ActivePrimitive &primitive = drawList.getActivePrimitive();
    writer.pPosition = primitive.pVertexPosition;
    writer.pDiffuse = primitive.pVertexDiffuse[0];
    writer.pIndex = primitive.pVertexIndices16;
    writer.NextIndex = 0;
    return true;
}

void MngNavPathDisplay::updateHistory( const Eigen::Vector4f &position,
                                       const Eigen::Vector4f &direction ) {
    // FUN_006b2ab0
    int32_t slot = historyStart;
    for ( int32_t i = 0; i < numHistory; i++ ) {
        const Eigen::Vector4f delta = position - history[slot].Position;
        if ( delta.dot( delta ) < kMinHistoryDistanceSq ) {
            numHistory = i;
            return;
        }
        if ( delta.dot( history[slot].Direction ) < 0.0f ) {
            numHistory = i;
            break;
        }
        slot = ( slot + 1 ) % kHistorySize;
    }

    history[slot].Position = position;
    history[slot].Direction = direction;
    if ( numHistory < kHistorySize ) {
        numHistory++;
    } else {
        historyStart = ( historyStart + 1 ) % kHistorySize;
    }
}

static Eigen::Vector4f ComputeSide( const Eigen::Vector4f &direction ) {
    Eigen::Vector4f side =
        Vector4DCross( direction, Eigen::Vector4f( 0.0f, 1.0f, 0.0f, 0.0f ) );
    side.y() = 0.0f;
    return Normalize( side );
}

void MngNavPathDisplay::tick( float deltaTime, float totalTime ) {
    // FUN_006b2c40
    if ( !bEnabled ) {
        return;
    }
    drawList.reset();
    OTDU_UNIMPLEMENTED_ONCE; // TODO: REPLAY (DAT_00fa97dc == 5 hides the path)

    CarState *pCar = gpScenaric->getLocalPlayer().getCarState();
    if ( pCar == nullptr || pCar->getAIController() == nullptr ) {
        return;
    }
    AiVehicle *pVehicle = static_cast<AiVehicle *>( pCar->getAIController() );
    AiVehNavigation *pNav = pVehicle->getNavigation();
    if ( pNav == nullptr || pVehicle->getNavigationType() != 1 ) {
        return;
    }
    const NavPath &path = static_cast<AiVehNavOpponent *>( pNav )->getPath();
    const int32_t numPoints = path.getNumPoints();
    if ( numPoints < 2 ) {
        return;
    }

    Eigen::Vector4f shift = Eigen::Vector4f::Zero();
    if ( gpWorldStreaming != nullptr && gpWorldStreaming->isEnabled() ) {
        shift = gpWorldStreaming->getPosition();
    }
    Matrix4x4f_t transform = Matrix4x4f_t::Identity();
    transform.row( 3 ) =
        Eigen::Vector4f( -shift.x(), kPathHeight, -shift.z(), 1.0f )
            .transpose();
    drawList.setActiveTransformMatrix( transform );
    drawList.setActiveMaterial( pMaterial );

    const Eigen::Vector4f &firstPosition = path.getPoint( 0 ).Position;
    Eigen::Vector4f direction =
        Normalize( path.getPoint( 1 ).Position - firstPosition );
    updateHistory( firstPosition, direction );
    if ( !beginStrip( ( numHistory + numPoints ) * 2 ) ) {
        return;
    }

    float targetSpeed = 0.0f;
    pNav->getTarget( direction, targetSpeed );
    const float carSpeed = pCar->getBody()->SpeedInKmh;
    if ( std::abs( targetSpeed - lastTargetSpeed ) > kTargetSpeedThreshold ) {
        lastTargetSpeed = targetSpeed;
        if ( carSpeed < targetSpeed ) {
            alphaToA = 0.0f;
            alphaToB = kMaxAlpha;
        } else {
            alphaToA = kMaxAlpha;
            alphaToB = 0.0f;
        }
        alphaFromA = alphaA;
        alphaFromB = alphaB;
        blendTime = 0.0f;
    }
    blendTime = std::min( blendTime + deltaTime, kAlphaBlendDuration );
    const float blend = blendTime / kAlphaBlendDuration;
    alphaA = std::clamp( ( alphaToA - alphaFromA ) * blend + alphaFromA, 0.0f,
                         kMaxAlpha );
    alphaB = std::clamp( ( alphaToB - alphaFromB ) * blend + alphaFromB, 0.0f,
                         kMaxAlpha );

    Eigen::Vector4f side = Eigen::Vector4f::Zero();
    int32_t slot = historyStart;
    for ( int32_t i = 0; i < numHistory; i++ ) {
        side = ComputeSide( history[slot].Direction );
        writer.emit( history[slot].Position + side * -kPathHalfWidth,
                     kPathColor );
        writer.emit( history[slot].Position + side * kPathHalfWidth,
                     kPathColor );
        slot = ( slot + 1 ) % kHistorySize;
    }

    for ( int32_t i = 0; i < numPoints; i++ ) {
        const Eigen::Vector4f &position = path.getPoint( i ).Position;
        if ( i < numPoints - 1 ) {
            side = ComputeSide(
                Normalize( path.getPoint( i + 1 ).Position - position ) );
        }
        writer.emit( position + side * -kPathHalfWidth, kPathColor );
        writer.emit( position + side * kPathHalfWidth, kPathColor );
    }
}
