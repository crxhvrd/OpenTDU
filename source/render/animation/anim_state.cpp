#include "shared.h"
#include "anim_state.h"

#include "clip.h"

bool AnimStateInstance::initialize( void *pData ) {
    // FUN_00429690
    if ( pData == nullptr ) {
        return false;
    }

    const uint8_t *p = static_cast<const uint8_t *>( pData );
    for ( int32_t guard = 0; guard < 32; guard++ ) {
        const uint32_t magic = *reinterpret_cast<const uint32_t *>( p );
        const uint32_t size = *reinterpret_cast<const uint32_t *>( p + 4 );
        const uint8_t *pChunkPayload = p + 8;

        if ( magic == kChunkData ) {
            pPayload = pChunkPayload;
        } else if ( magic == kChunkRefs ) {
            pRefs = pChunkPayload;
            refsSize = size;
        } else if ( magic != kChunkBas ) {
            break; // EOF! or an unexpected tag
        }

        p = pChunkPayload + size;
    }

    if ( pPayload == nullptr ) {
        return false;
    }

    const uint32_t recordOffset =
        *reinterpret_cast<const uint32_t *>( pPayload );
    pRecord =
        reinterpret_cast<const AnimStateRecord *>( pPayload + recordOffset );
    return true;
}

void AnimStateInstance::resolveReferences( ClipResolver resolver,
                                           void *pUserData ) {
    // FUN_007da420
    if ( pRefs == nullptr || resolver == nullptr ||
         refsSize < sizeof( uint32_t ) ) {
        return;
    }

    const uint32_t *p =
        reinterpret_cast<const uint32_t *>( pRefs + sizeof( uint32_t ) );
    const uint32_t *pEnd =
        reinterpret_cast<const uint32_t *>( pRefs + refsSize );

    while ( p < pEnd ) {
        const uint32_t header = p[0];
        const uint32_t count = p[1];
        const uint32_t type = header & 0xff;
        const uint32_t entrySize = header >> 16;

        if ( entrySize < sizeof( uint32_t ) ) {
            break;
        }

        const bool bHasReference =
            ( ( type == kRefTypeAnm || type == kRefTypeBas ) &&
              ( header & 0x100 ) != 0 ) ||
            type == 4;

        if ( type == kRefTypeAnm && bHasReference ) {
            const uint32_t id = p[count + 2];
            const uint32_t hash = p[count + 3];
            ClipInstance *pClip = resolver( pUserData, id, hash );

            for ( uint32_t i = 0; i < count; i++ ) {
                clipRefs.push_back( { p[i + 2], pClip } );
            }
        }

        p = reinterpret_cast<const uint32_t *>(
            reinterpret_cast<const uint8_t *>( p ) + entrySize );
    }
}

int32_t AnimStateInstance::getNumDistinctClips() const {
    int32_t distinct = 0;
    for ( size_t i = 0; i < clipRefs.size(); i++ ) {
        ClipInstance *pClip = clipRefs[i].pClip;
        if ( pClip == nullptr ) {
            continue;
        }

        bool bSeen = false;
        for ( size_t j = 0; j < i && !bSeen; j++ ) {
            bSeen = ( clipRefs[j].pClip == pClip );
        }
        if ( !bSeen ) {
            distinct++;
        }
    }
    return distinct;
}

const uint8_t *AnimStateInstance::getEntry( uint32_t index ) const {
    if ( pRecord == nullptr || index >= pRecord->NumEntries ) {
        return nullptr;
    }
    return reinterpret_cast<const uint8_t *>( pRecord ) +
           sizeof( AnimStateRecord ) + index * kRecordEntryStride;
}

ClipInstance *
AnimStateInstance::getClipAtOffset( uint32_t payloadOffset ) const {
    for ( const AnimStateClipRef &ref : clipRefs ) {
        if ( ref.PayloadOffset == payloadOffset ) {
            return ref.pClip;
        }
    }
    return nullptr;
}

ClipInstance *AnimStateInstance::getFirstClip() const {
    for ( const AnimStateClipRef &ref : clipRefs ) {
        if ( ref.pClip != nullptr ) {
            return ref.pClip;
        }
    }
    return nullptr;
}

uint32_t AnimStateInstance::getTrailerOffset() const {
    if ( pRecord == nullptr || pPayload == nullptr ) {
        return 0;
    }
    const uint32_t recordOffset = static_cast<uint32_t>(
        reinterpret_cast<const uint8_t *>( pRecord ) - pPayload );
    return recordOffset + sizeof( AnimStateRecord ) +
           pRecord->NumEntries * kRecordEntryStride;
}

const uint32_t *AnimStateInstance::getTrailer() const {
    if ( pRecord == nullptr || pPayload == nullptr ) {
        return nullptr;
    }
    return reinterpret_cast<const uint32_t *>( pPayload + getTrailerOffset() );
}

uint32_t AnimStateInstance::getCurveMode() const {
    const uint32_t *pTrailer = getTrailer();
    return pTrailer != nullptr ? pTrailer[kTrailerCurveMode / 4] : 0;
}

int32_t AnimStateInstance::getNumBlendClips() const {
    const uint32_t *pTrailer = getTrailer();
    if ( pTrailer == nullptr ) {
        return 0;
    }
    return static_cast<int32_t>( pTrailer[kTrailerClipCount / 4] );
}

bool AnimStateInstance::getBlendEntry( int32_t index,
                                       AnimStateBlendEntry &out ) const {
    const uint32_t *pTrailer = getTrailer();
    if ( pTrailer == nullptr || index < 0 || index >= getNumBlendClips() ) {
        return false;
    }

    const int32_t numClips = getNumBlendClips();
    const uint32_t clipOffset =
        getTrailerOffset() + kTrailerClipArray +
        static_cast<uint32_t>( index ) * sizeof( uint32_t );
    const float *pRatios = reinterpret_cast<const float *>(
        pTrailer + kTrailerClipArray / 4 + numClips );

    out.pClip = getClipAtOffset( clipOffset );
    out.Weight = pRatios[index * 2];
    out.Ratio = pRatios[index * 2 + 1];
    return true;
}
