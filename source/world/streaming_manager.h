#pragma once

#include "shared.h"

#include "filesystem/bank.h"
#include "sector_manager.h"
#include "spot_stream.h"
#include "world_level.h"
#include "far_impostors.h"

class RenderScene;
struct HiearchyNode;
class Instance;

// TODO: Move to helper header
Eigen::Vector4f SampleGroundBitmap( Render2DB *pBitmap, float u, float v );
int32_t SampleGroundType( const SectorHeightmap &heightmap,
                          float worldX,
                          float worldZ );
float SampleGroundHeight( Instance *pHeightmapInstance,
                          const SectorHeightmap &heightmap,
                          float worldX,
                          float worldZ );
bool SampleGroundNormal( Instance *pHeightmapInstance,
                         const SectorHeightmap &heightmap,
                         float worldX,
                         float worldZ,
                         Eigen::Vector3f &outNormal );
float SampleHeightmapSurface( const Eigen::Vector4f &position,
                              Eigen::Vector4f *pOutNormal );
bool GetHeightmapSampleGrid( Instance *pHeightmapInstance,
                             const float **ppHeights,
                             int32_t *pStride,
                             float *pQuadSize,
                             const float **ppTileErrors = nullptr,
                             int32_t *pNumTileX = nullptr,
                             int32_t *pNumTileZ = nullptr );

int32_t GetLevelHeightmapTrianglesInBBox( const Eigen::Vector4f &bboxMin,
                                          const Eigen::Vector4f &bboxMax,
                                          Eigen::Vector4f *pOutVertices,
                                          int32_t maxVertices,
                                          bool bSkipFlatTiles );

class StreamingManager {
public:
    void setActiveRenderLayers( uint8_t value ) { activeRenderLayers = value; }
    void setForceStreamUpdate( bool value ) { bForceStreamUpdate = value; }

    Level &getLevel() { return level; }

    // Heightmap LOD layers: 1 = sectors, 2 = areas, 4 = level.
    void resetHeightmapLODCoefs( uint32_t layerMask );
    void setUnderwaterTilesRefined( bool bRefined );
    void scaleHeightmapLODCoefs( uint32_t layerMask, float scale );

    void *getPanelSymbolStream() const { return level.getPanelSymbolStream(); }
    void *getPanelLayoutStream() const { return level.getPanelLayoutStream(); }
    void *getElectricalLayoutStream() const {
        return level.getElectricalLayoutStream();
    }
    void *getElectrical2DBStream() const {
        return level.getElectrical2DBStream();
    }
    void *getSectorNameStream() const { return level.getSectorNameStream(); }

    bool isFirstInitDone() const { return bFirstInitDone; }
    const FarImpostorField &getFarImpostors() const { return farImpostors; }

public:
    StreamingManager( uint32_t numSubSectors,
                      uint32_t numSectors,
                      uint32_t numSubAreas,
                      uint32_t numAreas );
    ~StreamingManager();

    void initialize( Eigen::Vector4f *pInitialPosition );
    void shutdown();

    void
    tick( Eigen::Vector4f *pStreamPosition, float totalTime, float deltaTime );
    void render();

    void pause();
    void pauseSectorActors();
    void resume();
    void reinit();

    bool computeGroundColor( Eigen::Vector4f *pOutColor,
                             const Eigen::Vector4f *pWorldPos,
                             const uint32_t flags );

    void computeCellWorldPosition( Eigen::Vector4f &outPosition,
                                   const GridPos2D &sectorCell,
                                   const GridPos2D &areaCell ) const;

    void debugRecenter( const Eigen::Vector4f &worldPos );
    void onWorldOriginStepped( const Eigen::Vector4f &newOrigin );
    void requestDebugJump( const Eigen::Vector4f &worldPos );

    void refreshSectorSpots();
    void refreshSectorProps();

    void gatherSectorEnvImpostors( Sector &sector,
                                   RenderScene *pScene,
                                   float farPlaneCoeff );

    void tickWoosh( const float *pListenerXZ, float listenerSpeed );
    void updateSectorWoosh( Sector &sector, float listenerX, float listenerZ );

    static void RemoveWooshForCell( SectorGeometry &geometry,
                                    int32_t cellIndex );
    static void RemoveAllWooshForSector( SectorGeometry &geometry );

public:
    SectorManager sectorManager;
    SpotStream spotStream;

    AreaGrid areaGrid;

    GridPos2D startArea;
    GridPos2D startSector;
    Eigen::Vector4f position = Eigen::Vector4f::Zero();

private:
    uint32_t numSubSectors;
    uint32_t numSectors;
    uint32_t numSubAreas;
    uint32_t numAreas;

    bool bFirstInitDone = false;
    uint8_t bStreamingActive : 1;
    uint8_t activeRenderLayers = 1;
    bool bField3 = false;
    bool bField4 = false;

    // Debug-only: bypass the game-freeze debounce in the per-frame sector
    // re-eval (set while the fly camera drives streaming) (Not part of the
    // original WorldStreaming)
    bool bForceStreamUpdate = false;

    // Debug-only: a queued teleport, consumed at the start of tick() so the
    // recenter never frees geometry mid-draw.
    bool bDebugJumpPending = false;
    Eigen::Vector4f debugJumpPos = Eigen::Vector4f::Zero();

    Level level;

    FarImpostorField farImpostors; // OTDU_ADDITION

private:
    void centerUpdate( Eigen::Vector4f *pStreamPosition );

    bool updateSectorStreaming( const Eigen::Vector4f &streamPosition,
                                bool bForce = false );
    bool updateAreaStreaming( const Eigen::Vector4f &streamPosition,
                              bool bForce = false );
    void resetSectorGrid();
    void resetAreaGrid();
    Sector *findSectorByFootprint( const GridPos2D &areaCell,
                                   const GridPos2D &sectorCell );
    eCardinalDirection findShiftDirection( const Sector &targetSector );
    void applySectorShift( eCardinalDirection direction,
                           const GridPos2D &newCentreArea,
                           const GridPos2D &newCentreSector );
    eCardinalDirection findAreaShiftDirection( const Area &targetArea );
    void reshuffleAreaConfig( int32_t newConfig );
    void tickSectors( float totalTime, float deltaTime );
    void tickSector( Sector &sector, float totalTime, float deltaTime );
    void
    updateSectorImpostors( Sector &sector, float totalTime, float deltaTime );
    void submitSector( Sector &sector );
    void submitSectorHeightmap( Sector &sector );
    void submitSectorImpostors( Sector &sector, RenderScene *pScene );
    void submitDynamicProps( Sector &sector, RenderScene *pScene );
    void submitArea( Area &area, uint32_t &numSubInstances );
    void submitAreaHeightmap( Area &area );
    void submitAreaHeightmaps();
    void submitLevelHeightmap();
    void updateHeightmapExclusionMasks();
    void stitchAreaEdgeToLevel( Area &area, eCardinalDirection direction );
    void stitchActiveAreaSeams();
    void resetHeightmapTileMasks();
    void stitchActiveAreaCardinalSeams();
    void stitchSectorEdgeToArea( Sector &sector, eCardinalDirection direction );
    void stitchSectorsToArea();
    void
    stitchSectorToSector( Sector &a, Sector &b, eCardinalDirection direction );
    void stitchSectorToSectorSeams();
    void stitchAreaToArea( Area &a, Area &b, int32_t dx, int32_t dy );
    void stitchAreaToAreaSeams();
    Area *findAreaEntry( const GridPos2D &areaCell );
    void loadActiveLevel();
    void restreamAroundStartSector();
    void setupActiveAreas( const GridPos2D &areaCell,
                           const GridPos2D &sectorCell );
    void installArea( Area &area, const GridPos2D &areaCell );
    void loadAreaBank( Area &area,
                       const char *pBankSubPath,
                       int32_t areaBaseX,
                       int32_t areaBaseZ );
    void streamSectors( const GridPos2D &areaCell,
                        const GridPos2D &sectorCell );
    void installSector( Sector &sector,
                        const GridPos2D &areaCell,
                        const GridPos2D &sectorCell );
    void loadSectorGeometry( Sector &sector,
                             const char *pBankSubPath,
                             int32_t areaBaseX,
                             int32_t areaBaseZ,
                             int32_t worldX,
                             int32_t worldZ );
};
