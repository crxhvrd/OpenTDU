#pragma once

#include "game/mng_base.h"

#include "ai/ai_road_map.h"
#include "render/2dm.h"
#include "render/draw_list.h"

struct Material;

// Road network wireframe drawn on the GMMap/GPS HUD
class MngRoadNetwork : public Manager {
public:
    const char *getName() const override {
        // FUN_00432270
        return "RoadNetwork";
    }

public:
    MngRoadNetwork();
    ~MngRoadNetwork();

    bool initialize( TestDriveGameInstance * ) override;
    void tick( float, float ) override;
    void reset() override;
    void draw() override;
    void pause() override;
    void terminate() override;

private:
    static constexpr int32_t kMaxNumClusters = 0x400;
    static constexpr int32_t kMaxNumEntries = 0x9c4;
    static constexpr int32_t kNumLayers = 3;

    enum eDrawMode : uint32_t {
        DM_Hidden = 0,
        DM_Line = 1,
        DM_Ribbon = 2,
    };

    // Camera distance band
    struct Band {
        float MaxDistance;
        float FadeStart;
        float HalfExtent;
        uint32_t Mode[kNumLayers]; // eDrawMode, indexed by Entity::Layer
        float AlphaScale;
    };

    struct Entry {
        Entry *pNext;
        AiRoadMap::Entity *pEntity;
        AiRoadMap::Road *pRoad;
        uint16_t EntityIndex;
        // Samples per curve (both ends included)
        uint16_t NumSamples;
        bool bStartCap;
        bool bEndCap;
        uint32_t Mode;
    };

    struct Writer {
        int32_t PrimitiveIndex = -1;
        Eigen::Vector3f *pPosition = nullptr;
        uint32_t *pDiffuse = nullptr;
        uint16_t *pIndex = nullptr;
        uint16_t NextIndex = 0;

        void emit( const Eigen::Vector4f &position, const uint32_t color );
    };

    static const Band kBands[4];

private:
    bool updateBand();
    uint16_t computeNumSamples( const AiRoadMap::Road *pRoad,
                                const AiRoadMap::Entity *pEntity ) const;
    void drawDebugBounds();
    uint32_t computeColor( const Entry &entry, const uint8_t alpha ) const;
    void emitRibbon( const Entry &entry );
    void emitLine( const Entry &entry );
    void allocateWriter( Writer &writer,
                         const ePrimitiveType type,
                         const uint32_t numVertex,
                         const uint32_t numIndex );

private:
    bool bInitialized;
    DrawList roadList;
    Writer ribbonWriter;
    Writer lineWriter;
    void *p2DMBuffer;
    Render2DM road2DM;
    Material *pRoadMaterial;
    const Band *pBand;
    const Band *pNextBand;
    uint8_t layerAlpha[kNumLayers];
    float layerWidth[kNumLayers];
    uint8_t ribbonAlpha;
    float cameraDistance;
    float halfExtentX;
    float halfExtentZ;
    Eigen::Vector4f viewCenter;
    bool bLastBand;
    AiRoadMap::EntityCluster *pClusters[kMaxNumClusters];
    int32_t numClusters;
    Entry entries[kMaxNumEntries];
    int32_t numEntries;
    uint32_t totalNumVertex;
    uint32_t totalNumIndex;
    bool bDebugDrawBounds;
};

extern MngRoadNetwork *gpMngRoadNetwork;
