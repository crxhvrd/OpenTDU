#pragma once

class Sector;
class Bank;
class ScenaricBank;
class Entity;
class EntityCinematic;
class FlashPlayer;
struct DBEntryCarPhysicsData;

// TODO: INCOMPLETE
enum eSpotType : uint8_t {
    ST_House = 0,
    ST_CarDealer = 1,
    ST_BikeDealer = 2,
    ST_Goodies = 3,
    ST_AfterMarket = 4,
    ST_Club = 5,
    ST_DriveIn = 6,
    ST_Challenge = 7,
    ST_Unknown8 = 8, // gated in simu mode

    ST_Count
};

// GMMap spot data
struct SpotMap {
    uint64_t Hashcode;
    uint64_t SourceHashcode;
    float X;
    float Y;
    float Z;
    uint8_t Type;
    uint8_t SubType;
    uint8_t MinLevelRequired;
    uint8_t bLocked;
    uint16_t RoadID;
    uint8_t RoadSegment;
    uint8_t VehicleType;
    uint32_t VehicleModel;
    uint64_t VehicleBrand;
    uint8_t VehicleRestriction; // 0 none, 1 brand, 2 model, 3 class, 4 range
    uint8_t VehicleCategory;
    uint8_t VehicleCategoryMax;
    uint8_t VehicleCategoryMin;
    uint32_t Unknown0x34;
    uint64_t NameHashcode; // database 7
};
OTDU_SIZE_MUST_MATCH( SpotMap, 64 );

struct SpotIconInfo {
    uint8_t Kind = 0; // 1 = challenge, 2 = spot
    uint32_t ChallengeFlags = 0u;
    uint32_t SpotFlags = 0u;
    uint32_t Unknown0xC = 0u;
    uint32_t Unknown0x10 = 0u;
};

extern uint8_t gMapActiveFilterMode;
extern uint32_t gMapChallengeFilterMask;
extern uint32_t gMapServiceFilterMask;
extern uint32_t gMapPlayerFilterMask;
extern uint32_t gMapBookmarkFilterMask;
extern uint8_t gMinimapFilterMode;

inline uint32_t GetMapFilterMask( const uint8_t mode ) {
    // FUN_00742380
    if ( ( mode & 1 ) != 0 ) {
        return gMapChallengeFilterMask;
    }
    if ( ( mode & 2 ) != 0 ) {
        return gMapServiceFilterMask;
    }
    if ( ( mode & 4 ) != 0 ) {
        return gMapPlayerFilterMask;
    }
    return ( ( mode & 8 ) != 0 ) ? gMapBookmarkFilterMask : 0u;
}

inline void SetMapFilterMask( const uint8_t mode, const uint32_t mask ) {
    // FUN_007423b0
    if ( ( mode & 1 ) != 0 ) {
        gMapChallengeFilterMask = mask;
    } else if ( ( mode & 2 ) != 0 ) {
        gMapServiceFilterMask = mask;
    } else if ( ( mode & 4 ) != 0 ) {
        gMapPlayerFilterMask = mask;
    } else if ( ( mode & 8 ) != 0 ) {
        gMapBookmarkFilterMask = mask;
    }
}

inline uint8_t GetEffectiveMapFilterMode() {
    // FUN_007423e0
    return ( ( gMapActiveFilterMode & 0x10 ) != 0 ) ? gMinimapFilterMode
                                                    : gMapActiveFilterMode;
}

bool IsSpotAvailableInCurrentMode( const SpotMap *pSpot );
bool IsSpotHidden( uint64_t spotHashcode );
uint32_t GetCarGroupIconFlag( const DBEntryCarPhysicsData *pCar );
void GetSpotIconInfo( const SpotMap *pSpot, SpotIconInfo &outInfo );
bool IsSpotIconVisible( const SpotIconInfo &info, bool bMinimap );
void SetSpotLockedTextFromReward( FlashPlayer *pPlayer,
                                  uint64_t rewardHashcode );
bool IsSpotRewardLocked( uint64_t rewardHashcode );
void GetChallengeCarAvailability( const SpotMap *pSpot,
                                  bool *pOutLevelTooLow,
                                  bool *pOutHasEligibleCar );
ScenaricBank *FindStreamedSpotBank( const struct SpotSector *pSpotSector );
EntityCinematic *
FindStreamedSpotCinematic( const struct SpotSector *pSpotSector,
                           const char *pName,
                           ScenaricBank **ppOutBank = nullptr );
Matrix4x4f_t BuildSpotSectorRotation( const struct SpotSector *pSector );
bool LaunchSpotLevel( SpotMap *pSpot, const bool bAllowFreeRide );

struct SpotSector {
    uint64_t Hashcode; //  ASCII spot id (e.g. "RSFE0022")
    uint64_t SourceHashcode;
    float EulerX; // Rotation (XYZ euler, radians)
    float EulerY;
    float EulerZ;
    uint16_t Id0;
    uint16_t Id1;
    float X; //  world position
    float Y;
    float Z;
    float Angle; // radians, around Y
    uint16_t Tiles[32];
    Matrix4x4fUnaligned_t Zone;
    Matrix4x4fUnaligned_t ZoneSmall;
};
OTDU_SIZE_MUST_MATCH( SpotSector, 240 );

union SpotData {
    SpotMap *pWorld;
    SpotSector *pSector;
};

struct SpotsList {
    void *pFile = nullptr;
    uint32_t NumSpots = 0;
    SpotData pSpotsMapOrSector = {};
    Sector *pSector = nullptr;
    bool bInstalled = false;
    bool bMeshesSpawned = false;
};

class Spots {
public:
    static constexpr int32_t kNumSectorLists = 7;

public:
    inline void setUseOnlyValid( bool param_1 ) { bUseOnlyValid = param_1; }
    inline uint32_t getNumWorldSpots() const { return spotsWorld.NumSpots; }
    inline SpotMap *getWorldSpot( int32_t index ) {
        return &spotsWorld.pSpotsMapOrSector.pWorld[index];
    }
    inline SpotMap *getWorldSpotChecked( int32_t index ) {
        if ( index < 0 || index >= (int32_t)spotsWorld.NumSpots ) {
            return nullptr;
        }
        return &spotsWorld.pSpotsMapOrSector.pWorld[index];
    }
    inline const SpotsList &getSectorList( int32_t index ) const {
        return spotsSectors[index];
    }

public:
    Spots() = default;

    int32_t getSpotMapIndex( uint64_t hashcode ) const;
    SpotMap *getSpotMap( uint64_t hashcode );
    SpotMap *findSpotMapByType( uint8_t type,
                                uint8_t subType,
                                int32_t occurrence,
                                int32_t *pOutIndex );
    SpotSector *getSpotSector( uint64_t hashcode );
    void updateSpotMeshValidity();
    void setSingleValidSpot( SpotMap *pSpot );
    void showSpotMeshes( bool bShow );

    bool installWorldSpots( void *pResource );
    void resetWorldSpots();
    bool installSectorSpots( Sector *pSector, Bank *pSpotsBank );
    void spawnPendingSectorSpotMeshes();
    void despawnSectorSpotMeshes();
    int32_t countWorldSpots( const int32_t type, const int32_t subType );
    SpotMap *getWorldSpotOfType( int32_t index,
                                 const int32_t type,
                                 const int32_t subType );
    void removeSectorSpots( void *pBlob );

private:
    bool isSpotValid( const SpotMap *pSpot ) const;
    void spawnSectorSpotMeshes( Sector *pSector, SpotsList &slot );
    static Entity *selectSpotTemplate( ScenaricBank &meshBank,
                                       int32_t spotType,
                                       int32_t ownedFlag );

private:
    SpotsList spotsWorld;
    SpotsList spotsSectors[7];
    bool bUseOnlyValid = false;
    SpotMap *pValidSpots[8] = {};
};

extern bool gbParseLogChallengesInfos;