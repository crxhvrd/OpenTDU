#pragma once

#include "shared.h"

#include "core/maths_helper.h"
#include "game/gs_base.h"
#include "render/draw_list.h"

struct Material;

static constexpr uint32_t kNumClothInstances = 0x14;
static constexpr uint32_t kClothSimBufferSize = 0x2800;
static constexpr uint32_t kClothSimPoolSize =
    kNumClothInstances * kClothSimBufferSize;
static constexpr int32_t kClothMaxNeighbors = 8;
static constexpr int32_t kClothMaxGridSize = 8;

struct ClothParticle {
    Eigen::Vector4f CurrentPos;
    Eigen::Vector4f PreviousPos;
    Eigen::Vector4f ForceAccum;
    Eigen::Vector4f NormalAccum;
    Eigen::Vector4f Normal;
    float U;
    float V;
    float Damping;
    uint32_t Flags;
    float RestLengthSq[kClothMaxNeighbors];
    int32_t NeighborIndex[kClothMaxNeighbors];
};
OTDU_SIZE_MUST_MATCH( ClothParticle, 0xa0 );

struct ClothInstance {
    void *pRegistration = nullptr;
    ClothInstance *pPrev = nullptr;
    ClothInstance *pNext = nullptr;
    int32_t State = 0;
    uint8_t bVisible = 0;
    uint8_t bGroundCollision = 0;
    uint8_t bWindReactive = 0;
    uint8_t bSpecialMaterial = 0;
    int32_t GridWidth = 0;
    int32_t GridHeight = 0;
    float PhysicalWidth = 1.0f;
    float PhysicalHeight = 1.0f;
    ClothParticle *pParticles = nullptr;
    Material *pMaterial = nullptr;
    Eigen::Vector4f Origin = Eigen::Vector4f::Zero();
    Eigen::Vector4f BoundsCenter = Eigen::Vector4f::Zero();
    float CullRadius = 0.0f;
    float TimeAccumulator = 0.0f;
    int32_t SolverIterations = 1;
    Eigen::Vector4f Acceleration = Eigen::Vector4f::Zero();
    int32_t field_0x80 = 0;

    void setup( int32_t gridWidth,
                int32_t gridHeight,
                float physWidth,
                float physHeight,
                const Matrix4x4f_t &transform,
                Material *pMat,
                int32_t numNeighbors );
    void simulate( float deltaTime );
    void buildGeometry( DrawList &drawList );
    void unregister();

private:
    ClothParticle &particleAt( int32_t row, int32_t col ) {
        return pParticles[row * GridWidth + col];
    }

    void integrate( float dt );
    void solveConstraints();
    void finalize();
    void groundClamp();
};

class GSClothSystem : public GameSystem {
public:
    const char *getName() const override {
        // FUN_00414340
        return "Service : ClothSystem";
    }

public:
    GSClothSystem();
    ~GSClothSystem();

    bool initialize( TestDriveGameInstance * ) override;
    void tick( float totalTime, float deltaTime ) override;
    void draw( float deltaTime ) override;
    void terminate() override;
    ClothInstance *allocateCloth();

private:
    ClothInstance clothes[kNumClothInstances];
    void *pSimulationPool;
    DrawList clothDrawList;
};

extern GSClothSystem *gpClothSystem;
