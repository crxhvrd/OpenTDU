#include "shared.h"
#include "roadmap_upgrader.h"

#if OTDU_64_BITS

#include "ai/ai_road_map.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <map>
#include <vector>

namespace {
    using Junction = AiRoadMap::Junction;
    using Road = AiRoadMap::Road;
    using Entity = AiRoadMap::Entity;
    using RoadDetail = AiRoadMap::RoadDetail;
    using Curve = AiRoadMap::Curve;
    using EntityRef = AiRoadMap::EntityRef;
    using EntityCluster = AiRoadMap::EntityCluster;
    using EntityArray = AiRoadMap::EntityArray;
    using FileHeader = AiRoadMap::FileHeader;
    using DetailsFileHeader = AiRoadMap::DetailsFileHeader;

    // On-disk (32-bit) layout
    constexpr size_t kDiskJunctionDetailOffset = 0x18;
    constexpr size_t kDiskJunctionRoadsOffset = 0x1c;
    constexpr size_t kDiskRoadSize = 0x30;
    constexpr size_t kDiskRoadPointersOffset = 0x1c;
    constexpr size_t kDiskRoadDetailEntitiesOffset = 0xc;
    constexpr size_t kDiskEntitySize = 0x4c;
    constexpr size_t kDiskEntityProfileOffset = 0x4;
    constexpr size_t kDiskEntityLanesOffset = 0x8;
    constexpr size_t kDiskEntityCurvesOffset = 0x24;
    constexpr size_t kDiskEntityArrayClustersOffset = 0x10;
    constexpr size_t kDiskEntityRefSize = 0x8;

    class DiskReader {
    public:
        DiskReader( const void *pStream, size_t size )
            : pBase( static_cast<const uint8_t *>( pStream ) ), size( size ) {}

        bool contains( size_t offset, size_t length ) const {
            return offset <= size && length <= size - offset;
        }
        uint8_t u8( size_t offset ) const { return pBase[offset]; }
        uint16_t u16( size_t offset ) const {
            uint16_t value;
            memcpy( &value, pBase + offset, sizeof( value ) );
            return value;
        }
        uint32_t u32( size_t offset ) const {
            uint32_t value;
            memcpy( &value, pBase + offset, sizeof( value ) );
            return value;
        }
        const uint8_t *at( size_t offset ) const { return pBase + offset; }
        size_t getSize() const { return size; }

    private:
        const uint8_t *pBase;
        size_t size;
    };

    class NativeWriter {
    public:
        size_t reserve( size_t length, size_t alignment ) {
            const size_t offset =
                ( blob.size() + alignment - 1 ) & ~( alignment - 1 );
            blob.resize( offset + length, 0 );
            return offset;
        }

        size_t append( const uint8_t *pData, size_t length, size_t alignment ) {
            const size_t offset = reserve( length, alignment );
            if ( length != 0 ) {
                memcpy( blob.data() + offset, pData, length );
            }
            return offset;
        }

        // Only valid until the next reserve()
        template <typename T> T *at( size_t offset ) {
            return reinterpret_cast<T *>( blob.data() + offset );
        }

        void *release( size_t *pOutSize ) {
            void *pResult = std::malloc( blob.size() );
            if ( pResult == nullptr ) {
                return nullptr;
            }
            memcpy( pResult, blob.data(), blob.size() );
            if ( pOutSize != nullptr ) {
                *pOutSize = blob.size();
            }
            return pResult;
        }

    private:
        std::vector<uint8_t> blob;
    };

    int32_t RelativeTo( size_t target, size_t base ) {
        return static_cast<int32_t>( static_cast<int64_t>( target ) -
                                     static_cast<int64_t>( base ) );
    }

    bool UpgradeEntityArray( const DiskReader &disk,
                             NativeWriter &native,
                             size_t diskArray,
                             size_t &outNativeArray ) {
        if ( !disk.contains( diskArray, kDiskEntityArrayClustersOffset ) ) {
            return false;
        }

        const int32_t numColumns =
            static_cast<int32_t>( disk.u32( diskArray + 8 ) );
        const int32_t numRows =
            static_cast<int32_t>( disk.u32( diskArray + 12 ) );
        if ( numColumns < 0 || numRows < 0 ) {
            return false;
        }
        const size_t numCells = size_t( numColumns ) * size_t( numRows );
        const size_t diskCells = diskArray + kDiskEntityArrayClustersOffset;
        if ( !disk.contains( diskCells, numCells * sizeof( uint32_t ) ) ) {
            return false;
        }

        const size_t nativeArray =
            native.reserve( offsetof( EntityArray, pClusters ) +
                                numCells * sizeof( EntityCluster * ),
                            alignof( EntityArray ) );
        memcpy( native.at<EntityArray>( nativeArray ), disk.at( diskArray ),
                offsetof( EntityArray, pClusters ) );

        std::vector<uint32_t> clusterOffsets( numCells );
        for ( size_t i = 0; i < numCells; i++ ) {
            clusterOffsets[i] = disk.u32( diskCells + i * sizeof( uint32_t ) );
        }
        std::vector<uint32_t> sortedOffsets = clusterOffsets;
        std::sort( sortedOffsets.begin(), sortedOffsets.end() );
        sortedOffsets.erase(
            std::unique( sortedOffsets.begin(), sortedOffsets.end() ),
            sortedOffsets.end() );

        // Clusters are emitted in stream order; any bytes between them (chunk
        // tags) are kept verbatim.
        std::map<uint32_t, size_t> nativeClusterOffsets;
        size_t diskCursor = diskCells + numCells * sizeof( uint32_t );
        for ( const uint32_t clusterOffset : sortedOffsets ) {
            const size_t diskCluster = diskArray + clusterOffset;
            if ( diskCluster < diskCursor ||
                 !disk.contains( diskCluster, sizeof( uint32_t ) ) ) {
                return false;
            }
            native.append( disk.at( diskCursor ), diskCluster - diskCursor, 1 );

            const uint32_t numEntities = disk.u32( diskCluster );
            const size_t diskRefs = diskCluster + sizeof( uint32_t );
            if ( !disk.contains( diskRefs,
                                 numEntities * kDiskEntityRefSize ) ) {
                return false;
            }

            const size_t nativeCluster =
                native.reserve( offsetof( EntityCluster, Refs ) +
                                    numEntities * sizeof( EntityRef ),
                                alignof( EntityCluster ) );
            EntityCluster *pCluster = native.at<EntityCluster>( nativeCluster );
            pCluster->NumEntities = numEntities;
            for ( uint32_t i = 0; i < numEntities; i++ ) {
                const size_t diskRef = diskRefs + i * kDiskEntityRefSize;
                pCluster->Refs[i].pRoad =
                    ValueToPointerSlot<Road>( disk.u32( diskRef ) );
                pCluster->Refs[i].EntityIndex = disk.u32( diskRef + 4 );
            }

            nativeClusterOffsets[clusterOffset] = nativeCluster - nativeArray;
            diskCursor = diskRefs + numEntities * kDiskEntityRefSize;
        }
        native.append( disk.at( diskCursor ), disk.getSize() - diskCursor, 1 );

        EntityArray *pArray = native.at<EntityArray>( nativeArray );
        for ( size_t i = 0; i < numCells; i++ ) {
            pArray->pClusters[i] =
                ValueToPointerSlot<EntityCluster>( static_cast<uint32_t>(
                    nativeClusterOffsets[clusterOffsets[i]] ) );
        }

        outNativeArray = nativeArray;
        return true;
    }

} // namespace

void *UpgradeRoadMapStream( const void *pDiskStream,
                            size_t diskSize,
                            size_t *pOutSize ) {
    DiskReader disk( pDiskStream, diskSize );
    NativeWriter native;

    if ( !disk.contains( 0, sizeof( FileHeader ) ) ) {
        OTDU_LOG_ERROR( "Roadmap stream is too small\n" );
        return nullptr;
    }
    FileHeader header;
    memcpy( &header, disk.at( 0 ), sizeof( FileHeader ) );
    if ( header.NumJunction < 0 || header.NumRoads < 0 ||
         header.NumSpeedLimits < 0 ) {
        OTDU_LOG_ERROR( "Invalid roadmap header\n" );
        return nullptr;
    }

    const size_t diskSpeedLimitsEnd =
        sizeof( FileHeader ) + header.NumSpeedLimits * sizeof( float );
    if ( !disk.contains( 0, diskSpeedLimitsEnd ) ) {
        OTDU_LOG_ERROR( "Truncated roadmap speed limits\n" );
        return nullptr;
    }
    native.append( disk.at( 0 ), diskSpeedLimitsEnd, alignof( FileHeader ) );

    size_t diskCursor = diskSpeedLimitsEnd;
    for ( int32_t i = 0; i < header.NumJunction; i++ ) {
        if ( !disk.contains( diskCursor, kDiskJunctionRoadsOffset ) ) {
            OTDU_LOG_ERROR( "Truncated roadmap junction %d\n", i );
            return nullptr;
        }
        const uint8_t numRoads = disk.u8( diskCursor + 8 );
        if ( !disk.contains( diskCursor, kDiskJunctionRoadsOffset +
                                             numRoads * sizeof( uint32_t ) ) ) {
            OTDU_LOG_ERROR( "Truncated roadmap junction %d\n", i );
            return nullptr;
        }

        const size_t nativeJunction = native.reserve(
            Junction::GetSize( numRoads ), alignof( Junction ) );
        Junction *pJunction = native.at<Junction>( nativeJunction );
        memcpy( pJunction, disk.at( diskCursor ),
                offsetof( Junction, pDetail ) );
        pJunction->pDetail = ValueToPointerSlot<void>(
            disk.u32( diskCursor + kDiskJunctionDetailOffset ) );
        for ( uint8_t j = 0; j < numRoads; j++ ) {
            pJunction->pRoads[j] = ValueToPointerSlot<Road>(
                disk.u32( diskCursor + kDiskJunctionRoadsOffset +
                          j * sizeof( uint32_t ) ) );
        }
        diskCursor += kDiskJunctionRoadsOffset + numRoads * sizeof( uint32_t );
    }

    if ( !disk.contains( diskCursor, header.NumRoads * kDiskRoadSize ) ) {
        OTDU_LOG_ERROR( "Truncated roadmap road table\n" );
        return nullptr;
    }
    const size_t nativeRoads =
        native.reserve( header.NumRoads * sizeof( Road ), alignof( Road ) );
    for ( int32_t i = 0; i < header.NumRoads; i++ ) {
        Road *pRoad = native.at<Road>( nativeRoads ) + i;
        memcpy( pRoad, disk.at( diskCursor ),
                offsetof( Road, pJunctionBegin ) );
        const size_t diskSlots = diskCursor + kDiskRoadPointersOffset;
        pRoad->pJunctionBegin =
            ValueToPointerSlot<Junction>( disk.u32( diskSlots ) );
        pRoad->pJunctionEnd =
            ValueToPointerSlot<Junction>( disk.u32( diskSlots + 0x4 ) );
        pRoad->pRoadProfile =
            ValueToPointerSlot<void>( disk.u32( diskSlots + 0x8 ) );
        pRoad->pDetail =
            ValueToPointerSlot<RoadDetail>( disk.u32( diskSlots + 0xc ) );
        pRoad->pShape =
            ValueToPointerSlot<AiRoadShape>( disk.u32( diskSlots + 0x10 ) );
        diskCursor += kDiskRoadSize;
    }

    // Profiles and the name pool hold no pointers and are kept verbatim.
    const size_t diskProfiles =
        offsetof( FileHeader, ProfilesOffset ) + header.ProfilesOffset;
    const size_t diskPoolNames = offsetof( FileHeader, PoolNamesOffset ) +
                                 sizeof( int32_t ) + header.PoolNamesOffset;
    const size_t diskEntityArray = offsetof( FileHeader, EntityArrayOffset ) +
                                   sizeof( int32_t ) + header.EntityArrayOffset;
    if ( diskProfiles < diskCursor || diskPoolNames < diskCursor ||
         diskEntityArray < diskCursor || diskEntityArray > diskSize ) {
        OTDU_LOG_ERROR( "Unexpected roadmap section order\n" );
        return nullptr;
    }
    const size_t nativeRegion =
        native.append( disk.at( diskCursor ), diskEntityArray - diskCursor,
                       sizeof( uint32_t ) );
    const size_t nativeProfiles = nativeRegion + ( diskProfiles - diskCursor );
    const size_t nativePoolNames =
        nativeRegion + ( diskPoolNames - diskCursor );

    size_t nativeEntityArray = 0;
    if ( !UpgradeEntityArray( disk, native, diskEntityArray,
                              nativeEntityArray ) ) {
        OTDU_LOG_ERROR( "Invalid roadmap entity array\n" );
        return nullptr;
    }

    FileHeader *pHeader = native.at<FileHeader>( 0 );
    pHeader->RoadsOffset =
        RelativeTo( nativeRoads, offsetof( FileHeader, RoadsOffset ) );
    pHeader->ProfilesOffset =
        RelativeTo( nativeProfiles, offsetof( FileHeader, ProfilesOffset ) );
    pHeader->PoolNamesOffset =
        RelativeTo( nativePoolNames, offsetof( FileHeader, PoolNamesOffset ) +
                                         sizeof( int32_t ) );
    pHeader->EntityArrayOffset = RelativeTo(
        nativeEntityArray,
        offsetof( FileHeader, EntityArrayOffset ) + sizeof( int32_t ) );

    return native.release( pOutSize );
}

void *UpgradeRoadDetailsStream( const void *pDiskStream,
                                size_t diskSize,
                                size_t *pOutSize ) {
    DiskReader disk( pDiskStream, diskSize );
    NativeWriter native;

    if ( !disk.contains( 0, sizeof( DetailsFileHeader ) ) ) {
        OTDU_LOG_ERROR( "Road details stream is too small\n" );
        return nullptr;
    }
    native.append( disk.at( 0 ), sizeof( DetailsFileHeader ),
                   alignof( DetailsFileHeader ) );
    const int32_t numRoadDetails = static_cast<int32_t>(
        disk.u32( offsetof( DetailsFileHeader, NumRoadDetails ) ) );

    size_t diskCursor = sizeof( DetailsFileHeader );
    for ( int32_t i = 0; i < numRoadDetails; i++ ) {
        if ( !disk.contains( diskCursor, kDiskRoadDetailEntitiesOffset ) ) {
            OTDU_LOG_ERROR( "Truncated road detail %d\n", i );
            return nullptr;
        }
        const uint8_t numEntities = disk.u8( diskCursor + 8 );
        if ( numEntities == 0 ||
             !disk.contains( diskCursor,
                             kDiskRoadDetailEntitiesOffset +
                                 numEntities * sizeof( uint32_t ) ) ) {
            OTDU_LOG_ERROR( "Invalid road detail %d\n", i );
            return nullptr;
        }

        const size_t nativeDetail =
            native.reserve( offsetof( RoadDetail, pEntities ) +
                                numEntities * sizeof( Entity * ),
                            alignof( RoadDetail ) );
        memcpy( native.at<RoadDetail>( nativeDetail ), disk.at( diskCursor ),
                offsetof( RoadDetail, NumEntities ) + sizeof( uint8_t ) );

        size_t diskEntityEnd = 0;
        for ( uint8_t j = 0; j < numEntities; j++ ) {
            const size_t diskEntity =
                diskCursor +
                disk.u32( diskCursor + kDiskRoadDetailEntitiesOffset +
                          j * sizeof( uint32_t ) );
            if ( !disk.contains( diskEntity, kDiskEntitySize ) ) {
                OTDU_LOG_ERROR( "Truncated road detail %d entity %d\n", i, j );
                return nullptr;
            }
            int32_t numExtraCurves = ( disk.u16( diskEntity ) - 1 ) / 3 - 1;
            numExtraCurves = std::max( numExtraCurves, 0 );
            const size_t diskEntitySize =
                kDiskEntitySize + numExtraCurves * sizeof( Curve );
            if ( !disk.contains( diskEntity, diskEntitySize ) ) {
                OTDU_LOG_ERROR( "Truncated road detail %d entity %d\n", i, j );
                return nullptr;
            }

            const size_t nativeEntity = native.reserve(
                sizeof( Entity ) + numExtraCurves * sizeof( Curve ),
                alignof( Entity ) );
            uint8_t *pNative = native.at<uint8_t>( nativeEntity );
            Entity *pEntity = reinterpret_cast<Entity *>( pNative );
            memcpy( pNative, disk.at( diskEntity ),
                    offsetof( Entity, pProfile ) );
            pEntity->pProfile = ValueToPointerSlot<AiRoadProfile>(
                disk.u32( diskEntity + kDiskEntityProfileOffset ) );
            memcpy( pNative + offsetof( Entity, NumLeftLanes ),
                    disk.at( diskEntity + kDiskEntityLanesOffset ),
                    kDiskEntityCurvesOffset - kDiskEntityLanesOffset );
            memcpy( pNative + offsetof( Entity, Curves ),
                    disk.at( diskEntity + kDiskEntityCurvesOffset ),
                    diskEntitySize - kDiskEntityCurvesOffset );

            native.at<RoadDetail>( nativeDetail )->pEntities[j] =
                ValueToPointerSlot<Entity>(
                    static_cast<uint32_t>( nativeEntity - nativeDetail ) );
            diskEntityEnd = diskEntity + diskEntitySize;
        }

        diskCursor = diskEntityEnd;
    }

    // Junction details hold no pointers and are kept verbatim.
    native.append( disk.at( diskCursor ), diskSize - diskCursor,
                   sizeof( uint32_t ) );

    return native.release( pOutSize );
}

#else

void *UpgradeRoadMapStream( const void *, size_t, size_t * ) { return nullptr; }

void *UpgradeRoadDetailsStream( const void *, size_t, size_t * ) {
    return nullptr;
}

#endif
