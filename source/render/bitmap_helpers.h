#pragma once

// Quality used by MngScreenshots::instanciate (FUN_006ce570).
static constexpr int32_t kDefaultQuality = 100;

bool CompressToJPEG( const uint8_t *pSrcBGRA,
                     uint32_t srcWidth,
                     uint32_t srcHeight,
                     uint32_t dstWidth,
                     uint32_t dstHeight,
                     int32_t quality,
                     const char *pComment,
                     void **ppOutContent,
                     uint32_t *pOutContentSize );

bool DecompressJPEG( const void *pContent,
                     uint32_t contentSize,
                     uint8_t *pDstBGRA,
                     uint32_t dstWidth,
                     uint32_t dstHeight );

// Box-filtered downscale / nearest upscale between two 32bpp BGRA surfaces.
void ResampleBGRA( const uint8_t *pSrc,
                   uint32_t srcWidth,
                   uint32_t srcHeight,
                   uint8_t *pDst,
                   uint32_t dstWidth,
                   uint32_t dstHeight );