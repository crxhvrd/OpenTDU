#include "shared.h"
#include "entity_boat_path.h"

#include "entity_bytecode.h"
#include "filesystem/binary.h"

EntityBoatPath::EntityBoatPath()
    : Entity(), pRecordArray( nullptr ), Floats{}, bFlag( false ) {
    // FUN_009da324 (inlined)
}

EntityBoatPath::~EntityBoatPath() {
    // FUN_00424120
}

void EntityBoatPath::loadResources( ScenaricBank *param_2 ) {
    // FUN_009c6850
    (void)param_2;
    waypointVolumes.clear();
    if ( pRecordArray == nullptr ) {
        return;
    }

    const uint32_t count = *reinterpret_cast<const uint32_t *>( pRecordArray );
    const uint8_t *pRecords = pRecordArray + sizeof( uint32_t );
    waypointVolumes.reserve( count );
    for ( uint32_t i = 0; i < count; ++i ) {
        const uint64_t guid =
            *reinterpret_cast<const uint64_t *>( pRecords + i * kRecordStride );
        Entity *pVolume = ( guid != 0 && gpScenaric != nullptr )
                              ? gpScenaric->getSCVolume( guid )
                              : nullptr;
        waypointVolumes.push_back( pVolume );
    }
}

const uint8_t *EntityBoatPath::getRecord( int32_t index ) const {
    if ( pRecordArray == nullptr || index < 0 ) {
        return nullptr;
    }
    const uint32_t count = *reinterpret_cast<const uint32_t *>( pRecordArray );
    if ( static_cast<uint32_t>( index ) >= count ) {
        return nullptr;
    }
    return pRecordArray + sizeof( uint32_t ) + index * kRecordStride;
}

bool EntityBoatPath::getWaypointPosition( int32_t index,
                                          Eigen::Vector4f &outPos ) const {
    const Matrix4x4f_t *pMatrix = getWaypointMatrix( index );
    if ( pMatrix == nullptr ) {
        return false;
    }
    outPos = pMatrix->row( 3 );
    return true;
}

const Matrix4x4f_t *EntityBoatPath::getWaypointMatrix( int32_t index ) const {
    if ( index < 0 ||
         index >= static_cast<int32_t>( waypointVolumes.size() ) ) {
        return nullptr;
    }
    Entity *pVolume = waypointVolumes[index];
    if ( pVolume == nullptr ) {
        return nullptr;
    }
    return &pVolume->getModelMatrix();
}

uint32_t EntityBoatPath::getWaypointMode( int32_t index ) const {
    const uint8_t *pRecord = getRecord( index );
    if ( pRecord == nullptr ) {
        return 0;
    }
    return *reinterpret_cast<const uint32_t *>( pRecord + sizeof( uint64_t ) );
}

float EntityBoatPath::getWaypointParameter( int32_t index ) const {
    const uint8_t *pRecord = getRecord( index );
    if ( pRecord == nullptr ) {
        return 0.0f;
    }
    return *reinterpret_cast<const float *>( pRecord + sizeof( uint64_t ) +
                                             sizeof( uint32_t ) );
}

void EntityBoatPath::deserialize( BinaryReader *param_2 ) {
    // FUN_009c6790
    Entity::deserialize( param_2 );

    pRecordArray = SkipBytecodeRecordArray( param_2 );

    for ( int32_t i = 0; i < 4; ++i ) {
        Floats[i] = *reinterpret_cast<float *>( param_2->pCursor );
        param_2->pCursor += sizeof( float );
    }

    int32_t n = *reinterpret_cast<int32_t *>( param_2->pCursor );
    param_2->pCursor += sizeof( int32_t );
    bFlag = ( n != 0 );
}
