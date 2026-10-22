#pragma once

#include "shared.h"

#include "filesystem/bank.h"
#include "filesystem/streamed_resource.h"
#include "gs_world_streaming.h"
#include "sector_geometry.h"

class Sector;
struct SpotSector;
struct Material;
class ScenaricBank;

enum eSpotStatus : int32_t {
    SS_None = 0,
    SS_Loaded = 1,
    SS_Loading = 2,
};

class Spot : public Bank, public StreamedResource, public StreamingCallback {
public:
    // Ground materials the spot exterior can shade with the sector shadow map.
    static constexpr int32_t kMaxNumGroundMaterial = 12;

public:
    Spot();
    ~Spot();

    void loadContent() override;
    void instanciate() override;
    bool onWorldStreamed( Eigen::Vector4f *param_1,
                          Eigen::Vector4f *param_2,
                          Eigen::Vector4f *param_3 ) override;

    void unload();
    bool loadFromSource( const char *pSourceName );
    void destroy();

    void setupGroundMaterials();
    void launchHouseGateCine( struct SpotMap *pWorld );

    eSpotStatus status = SS_None;
    void *pSpotPool = nullptr; // slice of the shared intro pool
    ScenaricBank *pSCBank = nullptr;
    SpotSector *pSpotSector = nullptr;
    Sector *pSector = nullptr;
    int32_t numGroundMaterial = 0;
    Material *pMaterials[kMaxNumGroundMaterial] = {};
    SectorGeometry geometry;
};

struct SpotCell {
    uint32_t flags = 0;
    Spot spot;
    Sector *pSector = nullptr; // owning sector of the loaded spot
    SpotSector *pSpotSector =
        nullptr; // the loaded spot (null when the cell is free)
};

class SpotStream {
public:
    static constexpr int32_t kNumCells = 3;

    void arm();
    void updateRendering();
    void tick();
    void unload();

    void updateCenter( const Eigen::Vector4f &position );
    // OTDU_ADDITION: the address lies in a loaded spot bank. Night lit windows
    // skip spot materials: spots keep baked light maps in the building gloss
    // slot.
    bool isInLoadedSpot( const void *pAddress ) const;
    void unloadSpotsForSector( Sector *pSector );
    bool isLoading() const {
        // FUN_006fbfa0
        for ( const SpotCell &cell : cells ) {
            if ( cell.spot.status == SS_Loading ) {
                return true;
            }
        }
        return false;
    }

    bool bDraw = false;
    bool bStream = false;
    bool bInitialized = false;
    bool bNoPendingUpdate = false;

    SpotCell cells[kNumCells];

    // Candidate spot used while diffing the nearby set against the loaded set.
    struct Candidate {
        void *pCategory = nullptr; // SpotsList* (nearby) / unused (loaded)
        float distSq = 0.0f;
        SpotSector *pSpot = nullptr;
    };

private:
    int32_t gatherNearby( const Eigen::Vector4f &position,
                          Candidate *pOut,
                          int32_t maxCount );
    int32_t gatherLoaded( const Eigen::Vector4f &position,
                          Candidate *pOut,
                          int32_t maxCount );
    static bool setsDiffer( const Candidate *pA,
                            int32_t numA,
                            const Candidate *pB,
                            int32_t numB );
    void unloadStale( const Candidate *pNearby, int32_t numNearby );
    bool loadIntoCell( const Candidate &nearby, SpotCell &cell );

    void waitForPendingLoads();
};
