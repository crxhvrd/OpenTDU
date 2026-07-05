#include "shared.h"
#include "gs_cloth_system.h"

#include "render/gs_render.h"
#include "render/material.h"
#include "render/render_scene.h"
#include "system/system_shared.h"

#include <cstdlib>

GSClothSystem *gpClothSystem = nullptr; // DAT_010e751c

namespace {
    constexpr float kClothFixedTimestep = 0.013333793f; // DAT_00f41c30
    constexpr float kClothConstraintBias = 0.5f;        // FLOAT_00f41674
    constexpr float kClothMinFaceLength = 0.001f;       // FLOAT_00f4167c
    constexpr float kClothGravity = -9.8f;              // DAT_00f41f1c
    constexpr float kClothAABBInit = 1000000.0f;        // FLOAT_00f41814

    // DAT_00f7f200
    constexpr int32_t kClothNeighborStencil[8][2] = {
        {0,  -1},
        {-1, 0 },
        {0,  1 },
        {1,  0 },
        {1,  -1},
        {-1, -1},
        {-1, 1 },
        {1,  1 },
    };

    // DAT_00f41714
    constexpr float kClothRandNormalizer = 1.0f / 32768.0f;
    constexpr float kClothDampingBase = 0.97099f;    // DAT_00f41e20
    constexpr float kClothDampingRange = 0.02f;      // DAT_00f41e24
    constexpr float kClothDefaultRestLength = -1.0f; // FLOAT_00f41664

    constexpr uint64_t kClothSpecialMaterialGUID = 0x643cdcb28a4efd79ULL;

    void ComputeWindForce( const Eigen::Vector4f &center,
                           const Eigen::Vector4f &n1,
                           const Eigen::Vector4f &n2,
                           const Eigen::Vector4f &windAccel,
                           Eigen::Vector4f &outNormal,
                           Eigen::Vector4f &outForce ) {
        // FUN_008aaaa0
        outForce.setZero();

        const Eigen::Vector3f edge1 = ( n1 - center ).head<3>();
        const Eigen::Vector3f edge2 = ( n2 - center ).head<3>();
        Eigen::Vector3f normal = edge2.cross( edge1 );

        outNormal.head<3>() = normal;
        outNormal.w() = 0.0f;

        const float length = normal.norm();
        if ( length >= kClothMinFaceLength ) {
            normal /= length;
            outNormal.head<3>() = normal;
            outNormal.w() = 0.0f;

            const float flux = windAccel.head<3>().dot( normal );
            outForce.head<3>() = normal * flux;
            outForce.w() = 0.0f;
        }
    }
} // namespace

GSClothSystem::GSClothSystem() : GameSystem(), pSimulationPool( nullptr ) {
    // FUN_008a82d0
    gpClothSystem = this;
}

GSClothSystem::~GSClothSystem() {
    // FUN_008a8260
    for ( ClothInstance &cloth : clothes ) {
        cloth.unregister();
    }
}

bool GSClothSystem::initialize( TestDriveGameInstance *pInstance ) {
    // FUN_008a8180
    pGameInstance = pInstance;
    bPaused = false;

    pSimulationPool = TestDrive::AllocAligned( kClothSimPoolSize );
    uint8_t *pSlice = static_cast<uint8_t *>( pSimulationPool );
    for ( ClothInstance &cloth : clothes ) {
        cloth.pParticles = reinterpret_cast<ClothParticle *>( pSlice );
        pSlice += kClothSimBufferSize;
    }

    DrawStreams streams;
    streams.Normals = 0;
    streams.Diffuse = 1;
    streams.UVMap = 1;
    clothDrawList.initialize( kNumClothInstances * ( kClothMaxGridSize - 1 ),
                              0x8c0, 0x8c, true, &streams, "ServiceClothSystem",
                              false );
    return true;
}

ClothInstance *GSClothSystem::allocateCloth() {
    // FUN_008a7070 (inlined)
    for ( ClothInstance &cloth : clothes ) {
        if ( cloth.State == 0 ) {
            cloth.State = 1;
            return &cloth;
        }
    }
    return nullptr;
}

void GSClothSystem::tick( float totalTime, float deltaTime ) {
    // FUN_008ac400
    clothDrawList.reset();
    clothDrawList.setIdentityTransform();

    for ( ClothInstance &cloth : clothes ) {
        if ( cloth.State == 2 ) {
            cloth.simulate( deltaTime );
        }
    }
}

void GSClothSystem::draw( float deltaTime ) {
    // FUN_008ac360
    for ( ClothInstance &cloth : clothes ) {
        if ( cloth.State == 2 ) {
            cloth.buildGeometry( clothDrawList );
        }
    }

    RenderScene *pScene =
        gpRender->getRenderPass<eRenderPass::RP_Near>().pScene;
    if ( pScene != nullptr ) {
        pScene->enqueueDynamicDrawList( &clothDrawList );
    }
}

void GSClothSystem::terminate() {
    // FUN_008a8110
    if ( clothDrawList.isInitialized() ) {
        clothDrawList.destroy();
    }
    if ( pSimulationPool != nullptr ) {
        TestDrive::FreeAligned( pSimulationPool );
        pSimulationPool = nullptr;
    }
}

void ClothInstance::setup( int32_t gridWidth,
                           int32_t gridHeight,
                           float physWidth,
                           float physHeight,
                           const Matrix4x4f_t &transform,
                           Material *pMat,
                           int32_t numNeighbors ) {
    // FUN_008aac20
    if ( State == 0 || pMat == nullptr ) {
        return;
    }

    if ( gridWidth < 2 || gridWidth > 8 ) {
        gridWidth = 8;
    }
    if ( gridHeight < 2 || gridHeight > 8 ) {
        gridHeight = 8;
    }
    if ( numNeighbors < 0 || numNeighbors > 8 ) {
        numNeighbors = 8;
    }

    GridWidth = gridWidth;
    GridHeight = gridHeight;
    PhysicalWidth = physWidth;
    PhysicalHeight = physHeight;
    SolverIterations = 1;
    Origin = transform.row( 3 );

    for ( int32_t row = 0; row < gridHeight; ++row ) {
        for ( int32_t col = 0; col < gridWidth; ++col ) {
            ClothParticle &p = particleAt( row, col );

            const float u = (float)col / (float)( gridWidth - 1 );
            const float v = (float)row / (float)( gridHeight - 1 );
            const Eigen::Vector4f local( 0.0f, -v * physHeight, u * physWidth,
                                         0.0f );
            const Eigen::Vector4f world = TDUMat4x4MulVec( transform, local );

            p.CurrentPos = world;
            p.PreviousPos = world;
            p.ForceAccum.setZero();
            p.NormalAccum.setZero();
            p.U = u;
            p.V = v;
            p.Damping = ( rand() & 0x7fff ) * kClothRandNormalizer *
                            kClothDampingRange +
                        kClothDampingBase;
            p.Flags = 0;
        }
    }

    for ( int32_t row = 0; row < gridHeight; ++row ) {
        for ( int32_t col = 0; col < gridWidth; ++col ) {
            ClothParticle &p = particleAt( row, col );
            for ( int32_t n = 0; n < kClothMaxNeighbors; ++n ) {
                const int32_t nx = col + kClothNeighborStencil[n][0];
                const int32_t ny = row + kClothNeighborStencil[n][1];
                if ( n >= numNeighbors || nx < 0 || ny < 0 || nx >= gridWidth ||
                     ny >= gridHeight ) {
                    p.NeighborIndex[n] = -1;
                    p.RestLengthSq[n] = kClothDefaultRestLength;
                } else {
                    const int32_t idx = ny * gridWidth + nx;
                    p.NeighborIndex[n] = idx;
                    p.RestLengthSq[n] =
                        ( p.CurrentPos - pParticles[idx].CurrentPos )
                            .head<3>()
                            .squaredNorm();
                }
            }
        }
    }

    BoundsCenter =
        Eigen::Vector4f( 0.0f, physHeight * -0.5f, physWidth * -0.5f, 0.0f );
    CullRadius = ( physWidth < physHeight ) ? physHeight : physWidth;

    MaterialParameter *pParam = pMat->getParameterByIndex( 0 );
    bWindReactive = pParam->Flags.pCustomFlags[1] != 0;
    bSpecialMaterial = ( pParam->Hashcode == kClothSpecialMaterialGUID );

    TimeAccumulator = 0.0f;
    State = 2;
    bVisible = 1;
    pMaterial = pMat;
    bGroundCollision = 0;
    field_0x80 = 0;
    Acceleration.setZero();
}

void ClothInstance::simulate( float deltaTime ) {
    // FUN_008ac2e0
    if ( !bVisible ) {
        return;
    }

    TimeAccumulator += deltaTime;
    if ( TimeAccumulator < kClothFixedTimestep ) {
        return;
    }

    float t = kClothFixedTimestep;
    do {
        integrate( t );
        solveConstraints();
        t += kClothFixedTimestep;
    } while ( t < TimeAccumulator );

    TimeAccumulator = 0.0f;
    finalize();

    if ( bGroundCollision ) {
        groundClamp();
    }
}

void ClothInstance::integrate( float dt ) {
    // FUN_008ab2c0
    const float dtSq = dt * dt;
    const Eigen::Vector4f windAccel = Acceleration * dt;

    for ( int32_t row = 0; row < GridHeight - 1; ++row ) {
        for ( int32_t col = 0; col < GridWidth - 1; ++col ) {
            ClothParticle &p00 = particleAt( row, col );
            ClothParticle &p01 = particleAt( row, col + 1 );
            ClothParticle &p10 = particleAt( row + 1, col );
            ClothParticle &p11 = particleAt( row + 1, col + 1 );

            Eigen::Vector4f normal;
            Eigen::Vector4f force;

            ComputeWindForce( p00.CurrentPos, p01.CurrentPos, p10.CurrentPos,
                              windAccel, normal, force );
            p00.ForceAccum += force;
            p00.NormalAccum += normal;
            p01.ForceAccum += force;
            p01.NormalAccum += normal;
            p10.ForceAccum += force;
            p10.NormalAccum += normal;

            ComputeWindForce( p01.CurrentPos, p11.CurrentPos, p10.CurrentPos,
                              windAccel, normal, force );
            p01.ForceAccum += force;
            p01.NormalAccum += normal;
            p11.ForceAccum += force;
            p11.NormalAccum += normal;
            p10.ForceAccum += force;
            p10.NormalAccum += normal;
        }
    }

    const int32_t count = GridWidth * GridHeight;
    for ( int32_t i = 0; i < count; ++i ) {
        ClothParticle &p = pParticles[i];

        if ( ( p.Flags & 1 ) == 0 ) {
            const Eigen::Vector4f previous = p.CurrentPos;
            p.CurrentPos.x() +=
                ( p.CurrentPos.x() - p.PreviousPos.x() ) * p.Damping +
                p.ForceAccum.x();
            p.CurrentPos.y() +=
                ( p.CurrentPos.y() - p.PreviousPos.y() ) * p.Damping +
                dtSq * kClothGravity + p.ForceAccum.y();
            p.CurrentPos.z() +=
                ( p.CurrentPos.z() - p.PreviousPos.z() ) * p.Damping +
                p.ForceAccum.z();
            p.CurrentPos.w() = 0.0f;
            p.PreviousPos = previous;
        }

        p.Normal = p.NormalAccum;
        p.NormalAccum.setZero();
        p.ForceAccum.setZero();
    }
}

void ClothInstance::solveConstraints() {
    // FUN_008a8720
    const int32_t count = GridWidth * GridHeight;
    for ( int32_t iter = 0; iter < SolverIterations; ++iter ) {
        for ( int32_t i = 0; i < count; ++i ) {
            ClothParticle &p = pParticles[i];
            for ( int32_t n = 0; n < kClothMaxNeighbors; ++n ) {
                if ( p.NeighborIndex[n] < 0 ) {
                    continue;
                }

                ClothParticle &q = pParticles[p.NeighborIndex[n]];
                const float restSq = p.RestLengthSq[n];
                const float dx = q.CurrentPos.x() - p.CurrentPos.x();
                const float dy = q.CurrentPos.y() - p.CurrentPos.y();
                const float dz = q.CurrentPos.z() - p.CurrentPos.z();
                const float scale =
                    restSq / ( dx * dx + dy * dy + dz * dz + restSq ) -
                    kClothConstraintBias;

                if ( ( p.Flags & 1 ) == 0 ) {
                    p.CurrentPos.x() -= dx * scale;
                    p.CurrentPos.y() -= dy * scale;
                    p.CurrentPos.z() -= dz * scale;
                    p.CurrentPos.w() = 0.0f;
                }
                if ( ( q.Flags & 1 ) == 0 ) {
                    q.CurrentPos.x() += dx * scale;
                    q.CurrentPos.y() += dy * scale;
                    q.CurrentPos.z() += dz * scale;
                    q.CurrentPos.w() = 0.0f;
                }
            }
        }
    }
}

void ClothInstance::finalize() {
    // FUN_008a8510
    const int32_t count = GridWidth * GridHeight;
    Eigen::Vector3f mins( kClothAABBInit, kClothAABBInit, kClothAABBInit );
    Eigen::Vector3f maxs( -kClothAABBInit, -kClothAABBInit, -kClothAABBInit );

    for ( int32_t i = 0; i < count; ++i ) {
        ClothParticle &p = pParticles[i];
        mins = mins.cwiseMin( p.CurrentPos.head<3>() );
        maxs = maxs.cwiseMax( p.CurrentPos.head<3>() );
        p.Normal.normalize();
    }

    BoundsCenter.head<3>() = ( maxs + mins ) * 0.5f;
    BoundsCenter.w() = 0.0f;
}

void ClothInstance::groundClamp() {
    // FUN_008aa8a0
    OTDU_UNIMPLEMENTED;
}

void ClothInstance::buildGeometry( DrawList &drawList ) {
    // FUN_008abee0

    // TODO: frustum cull / LOD / weather tint
    bVisible = 1;

    Matrix4x4f_t transform = Matrix4x4f_t::Identity();
    transform.row( 3 ) = Origin;

    drawList.setActiveTransformMatrix( transform );
    drawList.setActiveMaterial( pMaterial );

    const ColorRGBA white( (uint32_t)0xffffffff );
    for ( int32_t row = 0; row < GridHeight - 1; ++row ) {
        drawList.setDiffuse( 0, white );
        if ( !drawList.beginPrimitive( PT_TriangleStrip, GridWidth * 2 ) ) {
            break;
        }
        for ( int32_t col = 0; col < GridWidth; ++col ) {
            const ClothParticle &top = particleAt( row, col );
            const ClothParticle &bottom = particleAt( row + 1, col );

            drawList.setTexCoords( 0, Eigen::Vector2f( top.U, top.V ) );
            drawList.pushVertex( top.CurrentPos.x(), top.CurrentPos.y(),
                                 top.CurrentPos.z() );
            drawList.setTexCoords( 0, Eigen::Vector2f( bottom.U, bottom.V ) );
            drawList.pushVertex( bottom.CurrentPos.x(), bottom.CurrentPos.y(),
                                 bottom.CurrentPos.z() );
        }
        drawList.commitPrimitive();
    }
}

void ClothInstance::unregister() {
    // FUN_008a8260 (inlined)
    if ( pRegistration == nullptr ) {
        return;
    }

    if ( pPrev == nullptr ) {
        OTDU_UNIMPLEMENTED;
    } else {
        pPrev->pNext = pNext;
    }
    if ( pNext != nullptr ) {
        pNext->pPrev = pPrev;
    }

    pRegistration = nullptr;
    pPrev = nullptr;
    pNext = nullptr;
}
