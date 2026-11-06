#pragma once

#include "render/draw_list.h"
#include "world/far_impostor_pack.h"
#include "world/mng_impostor.h"

class Instance;
class Level;
struct AreaGeometry;
struct AreaGrid;
class SectorManager;
class Frustum;
struct GPUBuffer;
struct GPUComputePipeline;
struct GPUVertexLayout;
struct Material;
struct RenderObject;

// Level impostors
class FarImpostorField {
public:
    static constexpr int32_t kMaxNumPrimitives = 0x100;

public:
    inline uint32_t getNumSprites() const { return numSprites; }

public:
    FarImpostorField();
    ~FarImpostorField();

    FarImpostorField( const FarImpostorField & ) = delete;
    FarImpostorField &operator=( const FarImpostorField & ) = delete;

    void initialize( Level &level,
                     const char *pLevelName,
                     uint32_t numSectors,
                     uint32_t numSubSectors );
    void release();

    void tick( const SectorManager &sectorManager );
    void gatherTerrain( const AreaGrid &areaGrid,
                        const AreaGeometry &levelGeometry );
    void submit();

private:
    struct GPUDraw : public DrawListCustomDraw {
        FarImpostorField *pField = nullptr;
        uint32_t TypeIndex = 0u;
        bool bFadePass = false;

        void draw( Primitive *pPrimitive ) override;
    };

    struct TypeInfo {
        Instance *pPrototype = nullptr;
        const RenderObject *pObject = nullptr;
        Material *pMaterial = nullptr;       // Shared sector impostor material
        Material *pMaterialOpaque = nullptr; // Opaque pass (kOpaqueBucket)
        Material *pMaterialFar = nullptr;    // Fade-out pass (RB_ImpostorFar)
        uint8_t pBufferMaterialOpaque[512] = {};
        uint8_t pBufferMaterialFar[512] = {};
        float SizeDelta = 0.0f;
        float StretchUV = 0.0f;
        float FadeStart = 0.0f;
        float FarDist = 0.0f;
        Eigen::Vector3f SphereCenter = Eigen::Vector3f::Zero();
        bool bDrawn[2] = { false, false }; // Opaque, fade: jobs this frame
        GPUDraw gpuDraws[2];
    };

    struct SectorInfo {
        const FarImpostorPack::Sector *pSector = nullptr;
        Eigen::Vector3f WorldCorner = Eigen::Vector3f::Zero();
        uint32_t FirstGPUCell = 0u;
        uint32_t NumGPUCells = 0u;
    };

    struct GPUCell {
        Eigen::Vector3f Center = Eigen::Vector3f::Zero(); // Absolute
        float Radius = 0.0f;
        uint32_t FirstInstance = 0u;
        uint32_t NumInstances = 0u;
        uint16_t TypeIndex = 0u;
    };

    bool loadPack( const char *pLevelName );
    void bindTypes( Level &level );
    bool createGPUResources();
    void releaseGPUResources();
    bool bindGPUShader();
    void emitGPUDraws();
    bool createComputeResources();
    void addTerrainMap( uint32_t slot, const AreaGeometry &geometry );
    void uploadTerrainHeights();
    void releaseComputeResources();
    void prepareComputeFrame( float distanceScaleSq );
    void sortJobsBySegment();
    void recordCompute();
    void submitGPUCells( const SectorInfo &info,
                         Frustum &frustum,
                         const Matrix4x4f_t &worldToCam,
                         const Eigen::Vector3f &camPos,
                         const Eigen::Vector3f &streamPos,
                         float hideDistanceUnit,
                         float distanceScaleSq );

private:
    std::vector<uint8_t> packData;
    const FarImpostorPack::Run *pRuns;
    const FarImpostorPack::Record *pRecords;
    std::vector<SectorInfo> sectors;
    std::vector<TypeInfo> types;
    uint32_t numSectors;
    uint32_t numSubSectors;
    float maxFarDist;
    float maxScale;

    Level *pLevel;
    DrawList drawList;
    uint32_t numSprites; // Culling candidates
    bool bHasSprites;

    std::vector<GPUCell> gpuCells;
    std::vector<float> gpuScales; // Per instance (instance buffer order)
    GPUBuffer *pInstanceBuffer;
    GPUBuffer *pQuadVertexBuffer;
    GPUBuffer *pQuadIndexBuffer;

    Eigen::Vector4f gpuOrigin;
    Eigen::Vector4f gpuCamera;
    Eigen::Vector4f gpuRight;
    Eigen::Vector4f gpuUp;

    static constexpr uint32_t kMaxGPUSprites = 1u << 20;
    static constexpr uint32_t kMaxGPUJobs = 65535u;
    struct GPUJob {
        uint32_t FirstInstance;
        uint32_t NumInstances;
        uint32_t Segment;
        uint32_t FirstThread;
    };
    GPUComputePipeline *pCullPipeline;
    GPUBuffer *pSpriteBuffer;
    GPUBuffer *pArgumentBuffer;
    GPUBuffer *pJobBuffers[PendingFrameCount];
    GPUBuffer *pTypeParamBuffers[PendingFrameCount];
    GPUVertexLayout *pExpandedVertexLayout;
    std::vector<GPUJob> gpuJobs;
    std::vector<GPUJob> sortedGPUJobs;
    std::vector<uint32_t> segmentJobCursors;
    std::vector<uint32_t> segmentCounts;
    std::vector<DrawIndexedIndirectCommand> segmentArguments;
    std::vector<Eigen::Vector4f> typeParams;
    uint32_t numJobInstances;

    static constexpr uint32_t kNumTerrainSlots = 5; // 4 areas + the level
    static constexpr uint32_t kTerrainGridSize = 257;
    static constexpr uint32_t kTerrainNumTile = 18; // kHeightmapNumRealTile
    struct GPUTerrainMap {
        float OriginX;
        float OriginZ;
        float QuadSize;
        float InvQuadSize;
        uint32_t HeightBase;
        uint32_t Padding[3];
        uint32_t LODs[( kTerrainNumTile * kTerrainNumTile + 3 ) / 4 + 3];
    };
    struct GPUTerrainHeader {
        uint32_t NumMaps;
        uint32_t Padding[3];
    };
    struct GPUTerrain {
        GPUTerrainHeader Header;
        GPUTerrainMap Maps[kNumTerrainSlots];
    };
    GPUBuffer *pHeightBuffer;
    GPUBuffer *pTerrainBuffers[PendingFrameCount];
    GPUTerrain gpuTerrain;

    struct TerrainSlotKey {
        const float *pHeights = nullptr;
        float OriginX = 0.0f;
        float OriginZ = 0.0f;
        bool operator==( const TerrainSlotKey &other ) const {
            return pHeights == other.pHeights && OriginX == other.OriginX &&
                   OriginZ == other.OriginZ;
        }
    };
    TerrainSlotKey uploadedTerrain[kNumTerrainSlots];
    TerrainSlotKey frameTerrain[kNumTerrainSlots];
    uint32_t beginSceneCallback;
    bool bComputePending;
};
