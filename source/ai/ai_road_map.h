#pragma once

#include <cstddef>

struct AiRoadShape;

struct AiRoadProfile {
    struct Point {
        float X;
        float Y;
        uint32_t Flags;
    };

    uint32_t ChunkID;
    uint32_t ID;
    bool bIsSymetric;
    float DistToRoadsign;
    uint32_t NumPoints;
    uint32_t NumLeftLanes;
    uint32_t NumRightLanes;
    float InnerWidth;
    float OuterWidth;
    Point Points[1];

    inline size_t getSize() const {
        return ( NumPoints + 3 ) * sizeof( Point );
    }

    void computeLaneOffset( const bool bWay,
                            const int32_t lane,
                            float &outLateral,
                            float &outHeight ) const;
    void computeWidthOffset( float widthRatio,
                             float &outLateral,
                             float &outHeight ) const;
    void computeLaneBounds( const bool bWay,
                            const int32_t lane,
                            float &outLateralStart,
                            float &outLateralEnd,
                            float &outHeightStart,
                            float &outHeightEnd ) const;
    void computeLaneWidthRatios( const int32_t lane,
                                 const bool bWay,
                                 float &outRatioStart,
                                 float &outRatioEnd ) const;
    bool isTunnel() const;
};
OTDU_SIZE_MUST_MATCH( AiRoadProfile, 0x30 );

// Level AI Road Map (used by GPS, path finding, bots, etc.)
class AiRoadMap {
public:
    struct Road;
    struct RoadDetail;

    struct Vertex {
        float X;
        float Y;
        float Z;

        inline Eigen::Vector4f toVector4() const {
            // FUN_00406e90
            return Eigen::Vector4f( X, Y, Z, 1.0f );
        }
    };

    struct Curve {
        Vertex CV2;
        Vertex CV3;
        Vertex CV4;
        float TwistEnd;
    };
    OTDU_SIZE_MUST_MATCH( Curve, 0x28 );

    // One-way 3 roads junction
    struct JunctionSplit {
        enum eType : int32_t {
            JST_None = 0,
            JST_SymmetricSplit = 1,
            JST_SymmetricMerge = 2,
            JST_Merge = 3,
            JST_Split = 4,
        };

        eType Type = JST_None;
        int32_t PairIndexA = 0;
        int32_t PairIndexB = 0;
        int32_t SingleIndex = 0;
        int32_t RoadIndex = 0;
    };

    struct Junction {
        uint32_t ChunkID;
        uint32_t ID;
        uint8_t NumRoads;
        uint8_t NumNeighbors;
        uint8_t Flags;
        Vertex Position;
        void *pDetail;
        Road *pRoads[1];

        static inline size_t GetSize( const uint8_t numRoads ) {
            return offsetof( Junction, pRoads ) + numRoads * sizeof( Road * );
        }

        inline Road *getRoad( int32_t index ) const {
            // FUN_0092e010
            if ( index < 0 ) {
                index = 0;
            }
            return ( index < NumRoads ) ? pRoads[index] : pRoads[NumRoads - 1];
        }

        int32_t getRoadIndex( const Road *pRoad ) const;
        int32_t getRoadIndex( const Road *pRoad,
                              const int32_t occurrence ) const;
        Junction *getNeighbor( const int32_t index ) const;
        float computeRoadAngle( const int32_t index ) const;
        void classifySplit( JunctionSplit &outSplit,
                            const uint32_t roadID ) const;
        bool canTurnInto( const int32_t fromIndex,
                          const int32_t toIndex ) const;
        bool crossesCircle( const Eigen::Vector2f &centreXZ,
                            const float radiusSq ) const;
        int32_t pickStraightestRoad( const int32_t fromIndex,
                                     float *pOutAngle ) const;
        int32_t computeTurnType( const int32_t fromIndex,
                                 const int32_t toIndex,
                                 float *pOutAngle ) const;
        int32_t computeSideOfLine( const Eigen::Vector2f &pointXZ ) const;
        bool containsPoint( const Eigen::Vector2f &pointXZ ) const;
        bool findClosestOutlinePoint( const Eigen::Vector4f &position,
                                      const float maxDistance,
                                      float *pOutDistance,
                                      Eigen::Vector4f *pOutPoint ) const;
    };

    struct Road {
        uint32_t ChunkID;
        uint32_t ID;
        uint8_t MinLeftLane;
        uint8_t MinRightLane;
        uint8_t Curvature;
        uint8_t field_0xb;
        uint8_t CtrlDeviceBegin;
        uint8_t CtrlDeviceEnd;
        uint8_t field_0xe[2];
        uint16_t Distance;
        int8_t AngleSlotBegin;
        int8_t AngleSlotEnd;
        int8_t LaneFlags[8];
        Junction *pJunctionBegin;
        Junction *pJunctionEnd;
        void *pRoadProfile;
        RoadDetail *pDetail;
        AiRoadShape *pShape;

        AiRoadShape *getShape();

        uint8_t getNumLanesAt( const bool bEnd ) const;
    };

    struct Entity {
        uint16_t NumVertex;
        uint8_t SpeedLimitIndice;
        uint8_t Flags;
        AiRoadProfile *pProfile;
        uint8_t NumLeftLanes;
        uint8_t NumRightLanes;
        uint16_t NameOffset;
        uint32_t DynaMapID;
        uint8_t DensityZoneIndice;
        uint8_t Layer;
        uint16_t Length;
        Vertex FirstCV1;
        float FirstTwist;
#if OTDU_64_BITS
        // Keeps Curves 8-aligned so the trailing curves stay contiguous.
        uint32_t Padding;
#endif
        Curve Curves[1];

        inline int32_t getNumExtraCurves() const {
            const int32_t numExtraCurves = ( NumVertex - 1 ) / 3 - 1;
            return ( numExtraCurves < 0 ) ? 0 : numExtraCurves;
        }

        inline uint8_t *getEnd() {
            return reinterpret_cast<uint8_t *>( this ) + sizeof( Entity ) +
                   getNumExtraCurves() * sizeof( Curve );
        }

        inline int32_t getNumWaypoints() const { return ( NumVertex + 2 ) / 3; }

        inline int32_t getNumCurves() const { return getNumWaypoints() - 1; }

        inline const Vertex &getWaypoint( const int32_t index ) const {
            return ( index == 0 ) ? FirstCV1 : Curves[index - 1].CV4;
        }

        inline float getWaypointTwist( const int32_t index ) const {
            return ( index == 0 ) ? FirstTwist : Curves[index - 1].TwistEnd;
        }

        Eigen::Vector4f computePosition( const int32_t curveIndex,
                                         const float t ) const;
        Eigen::Vector4f computeCurveMidpoint( const int32_t curveIndex ) const;
        Eigen::Vector4f computeOffsetPosition( const int32_t curveIndex,
                                               const float t,
                                               const float lateral,
                                               const float height ) const;
        Eigen::Vector4f computeTangent( const int32_t curveIndex,
                                        const float t ) const;
        Eigen::Vector4f computeDirection( const int32_t curveIndex,
                                          const float t ) const;
    };

    struct RoadDetail {
        uint32_t ChunkID;
        uint32_t RoadID;
        uint8_t NumEntities;
        Entity *pEntities[1];
    };

    struct JunctionDetail {
        uint32_t ChunkID;
        uint32_t JunctionID;
        uint8_t field_0x8;
        uint8_t field_0x9;
        uint8_t field_0xa;
        uint8_t field_0xb;

        inline size_t getSize() const {
            return sizeof( JunctionDetail ) +
                   ( field_0x8 + field_0x9 + field_0xa ) * 8;
        }
    };
    OTDU_SIZE_MUST_MATCH( JunctionDetail, 0xc );

    struct EntityRef {
        Road *pRoad;
        uint32_t EntityIndex;
    };

    struct EntityCluster {
        uint32_t NumEntities;
        EntityRef Refs[1];
    };

    struct EntityArray {
        float ClusterWidth;
        float ClusterHeight;
        int32_t NumColumns;
        int32_t NumRows;
        EntityCluster *pClusters[1];

        int32_t gatherClusters( EntityCluster **ppOutClusters,
                                const int32_t maxClusters,
                                const Eigen::Vector4f &boundsMin,
                                const Eigen::Vector4f &boundsMax ) const;
    };

    struct FileHeader {
        uint32_t Magic[2];
        int32_t NumJunction;
        int32_t NumRoads;
        int32_t NumProfile;
        int32_t RoadsOffset;       // Relative to this field
        int32_t ProfilesOffset;    // Relative to this field
        int32_t PoolNamesOffset;   // Relative to the end of this field
        int32_t EntityArrayOffset; // Relative to the end of this field
        int32_t NumTrafficSignalTypes;
        int32_t NumSpeedLimits;
    };
    OTDU_SIZE_MUST_MATCH( FileHeader, 0x2c );

    struct DetailsFileHeader {
        uint32_t Magic[2];
        int32_t NumRoadDetails;
    };
    OTDU_SIZE_MUST_MATCH( DetailsFileHeader, 0xc );

#if OTDU_32_BITS
    OTDU_SIZE_MUST_MATCH( Junction, 0x20 );
    OTDU_SIZE_MUST_MATCH( Road, 0x30 );
    OTDU_SIZE_MUST_MATCH( Entity, 0x4c );
    OTDU_SIZE_MUST_MATCH( RoadDetail, 0x10 );
    OTDU_SIZE_MUST_MATCH( EntityRef, 0x8 );
    OTDU_SIZE_MUST_MATCH( EntityCluster, 0xc );
    OTDU_SIZE_MUST_MATCH( EntityArray, 0x14 );
#else
    OTDU_SIZE_MUST_MATCH( Junction, 0x28 );
    OTDU_SIZE_MUST_MATCH( Road, 0x48 );
    OTDU_SIZE_MUST_MATCH( Entity, 0x58 );
    OTDU_SIZE_MUST_MATCH( RoadDetail, 0x18 );
    OTDU_SIZE_MUST_MATCH( EntityRef, 0x10 );
    OTDU_SIZE_MUST_MATCH( EntityCluster, 0x18 );
    OTDU_SIZE_MUST_MATCH( EntityArray, 0x18 );
#endif

public:
    inline int32_t getNumRoads() const { return NumRoads; }
    inline int32_t getNumJunctions() const { return NumJunction; }
    inline Road *getRoads() const { return pRoads; }
    inline int32_t getNumSpeedLimits() const { return NumSpeedLimits; }
    inline float getSpeedLimit( const int32_t index ) const {
        return pArraySpeedLimit[index];
    }
    inline const EntityArray *getEntityArray() const { return pEntityArray; }

    inline Road *getRoad( const uint32_t roadID ) const {
        if ( roadID == 0u || roadID > static_cast<uint32_t>( NumRoads ) ) {
            return nullptr;
        }
        Road *pRoad = &pRoads[roadID - 1];
        return ( pRoad->ID == roadID ) ? pRoad : nullptr;
    }

    inline Junction *getJunction( const uint32_t junctionID ) const {
        if ( junctionID == 0u ||
             junctionID > static_cast<uint32_t>( NumJunction ) ) {
            return nullptr;
        }
        Junction *pJunction = ppJunctions[junctionID - 1];
        return ( pJunction != nullptr && pJunction->ID == junctionID )
                   ? pJunction
                   : nullptr;
    }

    inline AiRoadProfile *getProfile( const uint32_t profileID ) const {
        if ( profileID == 0u ||
             profileID > static_cast<uint32_t>( NumProfile ) ) {
            return nullptr;
        }
        AiRoadProfile *pProfile = ppProfile[profileID - 1];
        return ( pProfile != nullptr && pProfile->ID == profileID ) ? pProfile
                                                                    : nullptr;
    }

public:
    AiRoadMap();
    ~AiRoadMap();

    void reset();
    void load( void *pStream, const uint32_t streamSize );
    void installDetails( void *pStream, const uint32_t streamSize );

    Road *findNearestRoad( const Eigen::Vector4f &position,
                           const bool bIgnoreDiscovery ) const;
    Road *findNearestRoadExhaustive( const Matrix4x4f_t &frame,
                                     const bool bIgnoreDiscovery ) const;
    Road *findNearestRoadInRadius( const Eigen::Vector4f &position,
                                   const float radius ) const;

private:
    void installEntityArray();

private:
    bool bSelfAllocated;
    bool bDetailInstalled;
    int32_t NumTrafficSignalTypes;
    int32_t NumSpeedLimits;
    float *pArraySpeedLimit;
    int32_t NumJunction;
    Junction **ppJunctions;
    int32_t NumRoads;
    Road *pRoads;
    int32_t NumProfile;
    AiRoadProfile **ppProfile;
    uint32_t SizePoolNames;
    char *pPoolNames;
    EntityArray *pEntityArray;
    uint32_t SizeRoadmap;
    uint32_t SizeDetails;
};

// TODO: Share this with render file upgraders
// The fixups store IDs and stream-relative offsets in pointer slots before
// patching them into real pointers.
template <typename T> inline uint32_t PointerSlotToValue( T *pSlotValue ) {
    return static_cast<uint32_t>( reinterpret_cast<uintptr_t>( pSlotValue ) );
}

template <typename T> inline T *ValueToPointerSlot( const uint32_t value ) {
    return reinterpret_cast<T *>( static_cast<uintptr_t>( value ) );
}

template <typename T> inline T *AlignToPointerSlot( T *pData ) {
    const uintptr_t alignment = alignof( void * );
    const uintptr_t address = reinterpret_cast<uintptr_t>( pData );
    return reinterpret_cast<T *>( ( address + alignment - 1 ) &
                                  ~( alignment - 1 ) );
}
