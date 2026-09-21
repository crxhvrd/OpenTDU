#include "shared.h"
#include "bitmap_helpers.h"

#if OTDU_CLANG || OTDU_GCC
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#pragma GCC diagnostic ignored "-Wunused-function"
#elif OTDU_MSVC
#pragma warning( push, 0 )
#endif

#define STBI_ONLY_JPEG
#define STBI_NO_STDIO
#define STB_IMAGE_IMPLEMENTATION
#include "stb/stb_image.h"

#define STBI_WRITE_NO_STDIO
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb/stb_image_write.h"

#if OTDU_CLANG || OTDU_GCC
#pragma GCC diagnostic pop
#elif OTDU_MSVC
#pragma warning( pop )
#endif

static constexpr uint32_t kBytesPerTexel = 4;

// JPEG markers used to splice the metadata comment in (stb has no COM support).
static constexpr uint8_t kJPEGMarkerPrefix = 0xff;
static constexpr uint8_t kJPEGMarkerSOI = 0xd8;
static constexpr uint8_t kJPEGMarkerCOM = 0xfe;
static constexpr uint8_t kJPEGMarkerSOS = 0xda;
static constexpr uint8_t kJPEGMarkerEOI = 0xd9;

static void WriteToVector( void *pContext, void *pData, int32_t size ) {
    std::vector<uint8_t> *pStream = (std::vector<uint8_t> *)pContext;
    const uint8_t *pBytes = (const uint8_t *)pData;
    pStream->insert( pStream->end(), pBytes, pBytes + size );
}

// Inserts a COM segment right after the SOI marker. The original stores the
// per-shot metadata ("(N:%s)(ID:%d)(FMT:%d)", see FUN_006ce100) inside the JPEG
// itself; a comment segment is the standard place for it.
static void InsertJPEGComment( std::vector<uint8_t> &stream,
                               const char *pComment ) {
    if ( pComment == nullptr || pComment[0] == '\0' ) {
        return;
    }

    const size_t commentLength = std::strlen( pComment );

    // Segment length covers the two length bytes, the payload and the
    // terminator, and is stored big-endian on 16 bits.
    const size_t segmentLength = commentLength + 3;
    if ( stream.size() < 2 || segmentLength > 0xffff ) {
        return;
    }

    std::vector<uint8_t> segment;
    segment.reserve( segmentLength + 2 );
    segment.push_back( kJPEGMarkerPrefix );
    segment.push_back( kJPEGMarkerCOM );
    segment.push_back( (uint8_t)( ( segmentLength >> 8 ) & 0xff ) );
    segment.push_back( (uint8_t)( segmentLength & 0xff ) );
    segment.insert( segment.end(), pComment, pComment + commentLength );
    segment.push_back( 0 );

    stream.insert( stream.begin() + 2, segment.begin(), segment.end() );
}

void ResampleBGRA( const uint8_t *pSrc,
                   uint32_t srcWidth,
                   uint32_t srcHeight,
                   uint8_t *pDst,
                   uint32_t dstWidth,
                   uint32_t dstHeight ) {
    if ( pSrc == nullptr || pDst == nullptr || srcWidth == 0 ||
         srcHeight == 0 || dstWidth == 0 || dstHeight == 0 ) {
        return;
    }

    if ( srcWidth == dstWidth && srcHeight == dstHeight ) {
        std::memcpy( pDst, pSrc,
                     (size_t)srcWidth * srcHeight * kBytesPerTexel );
        return;
    }

    for ( uint32_t y = 0; y < dstHeight; y++ ) {
        const uint32_t srcY0 = ( y * srcHeight ) / dstHeight;
        const uint32_t srcY1 =
            Max( srcY0 + 1, ( ( y + 1 ) * srcHeight ) / dstHeight );

        for ( uint32_t x = 0; x < dstWidth; x++ ) {
            const uint32_t srcX0 = ( x * srcWidth ) / dstWidth;
            const uint32_t srcX1 =
                Max( srcX0 + 1, ( ( x + 1 ) * srcWidth ) / dstWidth );

            uint32_t accumulator[kBytesPerTexel] = { 0, 0, 0, 0 };
            uint32_t sampleCount = 0;

            for ( uint32_t srcY = srcY0; srcY < srcY1; srcY++ ) {
                const uint8_t *pRow =
                    pSrc + ( (size_t)srcY * srcWidth * kBytesPerTexel );
                for ( uint32_t srcX = srcX0; srcX < srcX1; srcX++ ) {
                    const uint8_t *pTexel = pRow + srcX * kBytesPerTexel;
                    for ( uint32_t c = 0; c < kBytesPerTexel; c++ ) {
                        accumulator[c] += pTexel[c];
                    }
                    sampleCount++;
                }
            }

            uint8_t *pOut =
                pDst + ( ( (size_t)y * dstWidth + x ) * kBytesPerTexel );
            for ( uint32_t c = 0; c < kBytesPerTexel; c++ ) {
                pOut[c] = (uint8_t)( accumulator[c] / sampleCount );
            }
        }
    }
}

bool CompressToJPEG( const uint8_t *pSrcBGRA,
                     uint32_t srcWidth,
                     uint32_t srcHeight,
                     uint32_t dstWidth,
                     uint32_t dstHeight,
                     int32_t quality,
                     const char *pComment,
                     void **ppOutContent,
                     uint32_t *pOutContentSize ) {
    if ( pSrcBGRA == nullptr || ppOutContent == nullptr ||
         pOutContentSize == nullptr ) {
        return false;
    }

    *ppOutContent = nullptr;
    *pOutContentSize = 0;

    if ( srcWidth == 0 || srcHeight == 0 || dstWidth == 0 || dstHeight == 0 ) {
        return false;
    }

    std::vector<uint8_t> scaled;
    const uint8_t *pTexels = pSrcBGRA;
    if ( srcWidth != dstWidth || srcHeight != dstHeight ) {
        scaled.resize( (size_t)dstWidth * dstHeight * kBytesPerTexel );
        ResampleBGRA( pSrcBGRA, srcWidth, srcHeight, scaled.data(), dstWidth,
                      dstHeight );
        pTexels = scaled.data();
    }

    // BGRA -> RGB (the encoder is fed tightly packed 24bpp scanlines).
    std::vector<uint8_t> rgb( (size_t)dstWidth * dstHeight * 3 );
    for ( size_t i = 0, texelCount = (size_t)dstWidth * dstHeight;
          i < texelCount; i++ ) {
        const uint8_t *pTexel = pTexels + i * kBytesPerTexel;
        uint8_t *pOut = rgb.data() + i * 3;
        pOut[0] = pTexel[2];
        pOut[1] = pTexel[1];
        pOut[2] = pTexel[0];
    }

    std::vector<uint8_t> stream;
    stream.reserve( (size_t)dstWidth * dstHeight );

    const int32_t clampedQuality = Clamp( quality, 1, 100 );
    if ( stbi_write_jpg_to_func( &WriteToVector, &stream, (int32_t)dstWidth,
                                 (int32_t)dstHeight, 3, rgb.data(),
                                 clampedQuality ) == 0 ) {
        OTDU_LOG_ERROR( "Failed to encode a %ux%u JPEG\n", dstWidth,
                        dstHeight );
        return false;
    }

    InsertJPEGComment( stream, pComment );

    void *pContent = TestDrive::AllocAligned( stream.size() );
    if ( pContent == nullptr ) {
        OTDU_LOG_ERROR( "Failed to allocate a %zu bytes JPEG buffer\n",
                        stream.size() );
        return false;
    }

    std::memcpy( pContent, stream.data(), stream.size() );

    *ppOutContent = pContent;
    *pOutContentSize = (uint32_t)stream.size();
    return true;
}

bool ExtractJPEGComment( const void *pContent,
                         uint32_t contentSize,
                         char *pOutComment,
                         size_t outCommentSize ) {
    if ( pOutComment == nullptr || outCommentSize == 0 ) {
        return false;
    }

    pOutComment[0] = '\0';

    if ( pContent == nullptr || contentSize < 4 ) {
        return false;
    }

    const uint8_t *pBytes = (const uint8_t *)pContent;
    if ( pBytes[0] != kJPEGMarkerPrefix || pBytes[1] != kJPEGMarkerSOI ) {
        return false;
    }

    uint32_t offset = 2;
    while ( offset + 4 <= contentSize ) {
        if ( pBytes[offset] != kJPEGMarkerPrefix ) {
            return false;
        }

        const uint8_t marker = pBytes[offset + 1];
        if ( marker == kJPEGMarkerSOS || marker == kJPEGMarkerEOI ) {
            return false;
        }

        const uint32_t segmentLength = ( (uint32_t)pBytes[offset + 2] << 8 ) |
                                       (uint32_t)pBytes[offset + 3];
        if ( segmentLength < 2 || offset + 2 + segmentLength > contentSize ) {
            return false;
        }

        if ( marker == kJPEGMarkerCOM ) {
            const uint8_t *pPayload = pBytes + offset + 4;
            const uint32_t payloadSize = segmentLength - 2;

            uint32_t length = 0;
            while ( length < payloadSize && pPayload[length] != '\0' ) {
                length++;
            }
            if ( length >= outCommentSize ) {
                length = uint32_t( outCommentSize - 1 );
            }

            std::memcpy( pOutComment, pPayload, length );
            pOutComment[length] = '\0';
            return true;
        }

        offset += 2 + segmentLength;
    }

    return false;
}

bool DecompressJPEG( const void *pContent,
                     uint32_t contentSize,
                     uint8_t *pDstBGRA,
                     uint32_t dstWidth,
                     uint32_t dstHeight ) {
    if ( pContent == nullptr || contentSize == 0 || pDstBGRA == nullptr ||
         dstWidth == 0 || dstHeight == 0 ) {
        return false;
    }

    int32_t width = 0;
    int32_t height = 0;
    int32_t channelCount = 0;
    stbi_uc *pDecoded =
        stbi_load_from_memory( (const stbi_uc *)pContent, (int32_t)contentSize,
                               &width, &height, &channelCount, 3 );
    if ( pDecoded == nullptr ) {
        OTDU_LOG_ERROR( "Failed to decode JPEG: %s\n", stbi_failure_reason() );
        return false;
    }

    std::vector<uint8_t> bgra( (size_t)width * height * kBytesPerTexel );
    for ( size_t i = 0, texelCount = (size_t)width * height; i < texelCount;
          i++ ) {
        const uint8_t *pTexel = pDecoded + i * 3;
        uint8_t *pOut = bgra.data() + i * kBytesPerTexel;
        pOut[0] = pTexel[2];
        pOut[1] = pTexel[1];
        pOut[2] = pTexel[0];
        pOut[3] = 0xff;
    }

    stbi_image_free( pDecoded );

    ResampleBGRA( bgra.data(), (uint32_t)width, (uint32_t)height, pDstBGRA,
                  dstWidth, dstHeight );
    return true;
}
