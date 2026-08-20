#pragma once

#include "filesystem/binary.h"
#include "game/scenaric/gs_scenaric.h"
#include "render/animation/animation.h"

struct BytecodePair {
    uint8_t *pBlock = nullptr;
    uint32_t Value = 0;
};

inline uint32_t DecodeBytecodePairArray( BinaryReader *pReader,
                                         BytecodePair *pOutPairs,
                                         uint32_t maxPairs ) {
    // FUN_00427c00
    const uint32_t count = *reinterpret_cast<uint32_t *>( pReader->pCursor );
    pReader->pCursor += sizeof( uint32_t );

    for ( uint32_t i = 0; i < count; i++ ) {
        BytecodePair pair;
        if ( gpScenaric->getVersion() < 0x21 ) {
            uint8_t *pRecord = pReader->pCursor;
            pair.Value = *reinterpret_cast<uint32_t *>( pRecord + 0x28 );
            pReader->pCursor = pRecord + 0x30;
        } else {
            pair.pBlock = pReader->pCursor;
            pReader->pCursor += 8;
            pair.Value = *reinterpret_cast<uint32_t *>( pReader->pCursor );
            pReader->pCursor += sizeof( uint32_t );
        }

        if ( i < maxPairs ) {
            pOutPairs[i] = pair;
        }
    }

    OTDU_ASSERT( count <= maxPairs );
    return count;
}

inline uint8_t *SkipBytecodeRecordArray( BinaryReader *pReader ) {
    // FUN_00427c70
    uint8_t *pStart = pReader->pCursor;
    const uint32_t count = *reinterpret_cast<uint32_t *>( pReader->pCursor );
    pReader->pCursor += sizeof( uint32_t );
    pReader->pCursor += count * 0x10;
    return pStart;
}

inline uint32_t DecodeBytecodeAnimCarTable( BinaryReader *pReader,
                                            RenderAnm *pOutAnims,
                                            uint32_t maxAnims ) {
    // FUN_00424960
    const uint32_t count = *reinterpret_cast<uint32_t *>( pReader->pCursor );
    pReader->pCursor += sizeof( uint32_t );

    for ( uint32_t i = 0; i < count; i++ ) {
        RenderAnm anim;
        anim.deserialize( pReader );

        if ( pOutAnims != nullptr && i < maxAnims ) {
            pOutAnims[i] = anim;
        }
    }

    OTDU_ASSERT( pOutAnims == nullptr || count <= maxAnims );
    return count;
}

inline uint32_t SkipBytecodeAnimCarTable( BinaryReader *pReader ) {
    return DecodeBytecodeAnimCarTable( pReader, nullptr, 0 );
}
