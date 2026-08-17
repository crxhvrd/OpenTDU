#include "shared.h"
#include "particle_file.h"

#include "core/binary_helpers.h"

constexpr uint32_t kPrtMagic = 0x5452502e; // ".PRT"
constexpr uint32_t kMatMagic = 0x2e54414d; // "MAT."

ParticleFile::ParticleFile()
    : pBlob( nullptr ), pObject( nullptr ), blobSize( 0 ) {}

bool ParticleFile::IsParticleFile( const uint8_t *p, uint32_t size ) {
    if ( p == nullptr || size < 0x20 ) {
        return false;
    }
    if ( Read<uint32_t>( p, 0xc ) != kPrtMagic ||
         Read<uint32_t>( p, 0x10 ) != kPrtMagic ||
         Read<uint16_t>( p, 0x14 ) != 4 ) {
        return false;
    }
    // Sibling ".MAT" chunk, offset relative to blob+0x10.
    const uint32_t matOffset = Read<uint32_t>( p, 0x18 );
    if ( 0x10 + matOffset + 8 > size ) {
        return false;
    }
    const uint8_t *pMat = p + 0x10 + matOffset;
    return Read<uint16_t>( pMat, 4 ) == 1 && Read<uint16_t>( pMat, 6 ) == 0 &&
           Read<uint32_t>( pMat, 0 ) == kMatMagic;
}

uint8_t *ParticleFile::arrayBase( uint32_t baseField ) const {
    const uint32_t offset = Read<uint32_t>( pBlob, baseField );
    return offset != 0 ? pBlob + offset : nullptr;
}

uint32_t ParticleFile::arrayCount( uint32_t countField ) const {
    return Read<uint32_t>( pBlob, countField );
}

bool ParticleFile::parse( uint8_t *pData, uint32_t size ) {
    // FUN_00457f60
    pBlob = nullptr;
    pObject = nullptr;
    blobSize = 0;

    if ( !IsParticleFile( pData, size ) ) {
        return false;
    }

    pBlob = pData;
    blobSize = size;
    pObject = pData + 0x20;

    // Object header defaults.
    Write<uint32_t>( pBlob, 0x24, 0xffffffff );
    Write<float>( pBlob, 0x28, 1.0f );
    Write<float>( pBlob, 0x2c, 1.0f );
    Write<float>( pBlob, 0x30, 1.0f );

    // Key array (stride 0x40).
    if ( uint8_t *pKeys = arrayBase( prtf::kKeyBase ) ) {
        const int32_t count =
            static_cast<int32_t>( arrayCount( prtf::kKeyCount ) );
        for ( int32_t i = 0; i < count; i++ ) {
            uint8_t *pRec = pKeys + i * prtf::kKeyStride;
            Write<int32_t>( pRec, 4,
                            static_cast<int32_t>( static_cast<int16_t>( i ) ) );
            Write<uint8_t>( pRec, 0, 1 );
            Write<uint8_t>( pRec, 9, 1 );
        }
    }

    if ( uint8_t *pEmitters = arrayBase( prtf::kEmitterBase ) ) {
        const int32_t count =
            static_cast<int32_t>( arrayCount( prtf::kEmitterCount ) );
        for ( int32_t i = 0; i < count; i++ ) {
            processEmitter( pEmitters + i * prtf::kEmitterStride );
        }
    }

    if ( uint8_t *pDefA = arrayBase( prtf::kDefABase ) ) {
        const int32_t count =
            static_cast<int32_t>( arrayCount( prtf::kDefATotal ) );
        for ( int32_t i = 0; i < count; i++ ) {
            uint8_t *pRec = pDefA + i * prtf::kDefStride;
            Write<uint32_t>( pRec, 0x228, Read<uint32_t>( pRec, 0x178 ) );
            Write<uint8_t>( pRec, 0, 1 );
            Write<uint8_t>( pRec, 2, 1 );
            OTDU_UNIMPLEMENTED; // TODO:
            Write<uint8_t>( pRec, 0x1a4, 0xff );
            Write<uint8_t>( pRec, 0x1a5, 0xff );
            Write<uint8_t>( pRec, 0x1a6, 0xff );
            Write<uint8_t>( pRec, 0x1e8, 0 );
            Write<uint8_t>( pRec, 0x1e9, 0 );
            Write<uint8_t>( pRec, 0x1ea, 0 );
            Write<uint8_t>( pRec, 0x19c, 0xff );
            Write<uint8_t>( pRec, 0x1e0, 0 );
        }
    }

    if ( uint8_t *pDefB = arrayBase( prtf::kDefBBase ) ) {
        const int32_t count =
            static_cast<int32_t>( arrayCount( prtf::kDefBTotal ) );
        for ( int32_t i = 0; i < count; i++ ) {
            uint8_t *pRec = pDefB + i * prtf::kDefStride;
            Write<uint8_t>( pRec, 0, 1 );
            Write<uint8_t>( pRec, 2, 1 );
        }
    }

    if ( uint8_t *pSmall = arrayBase( prtf::kSmallBase ) ) {
        const int32_t count =
            static_cast<int32_t>( arrayCount( prtf::kSmallTotal ) );
        for ( int32_t i = 0; i < count; i++ ) {
            uint8_t *pRec = pSmall + i * prtf::kSmallStride;
            Write<uint8_t>( pRec, 0, 1 );
            Write<uint8_t>( pRec, 2, 1 );
        }
    }

    if ( uint8_t *pMat = arrayBase( prtf::kMatBase ) ) {
        const int32_t count =
            static_cast<int32_t>( arrayCount( prtf::kMatTotal ) );
        for ( int32_t i = 0; i < count; i++ ) {
            uint8_t *pRec = pMat + i * prtf::kMatStride;
            Write<uint8_t>( pRec, 0, 1 );
            Write<uint8_t>( pRec, 2, 1 );
        }
    }

    Write<uint8_t>( pObject, 0, 2 ); // O state = parsed
    return true;
}

void ParticleFile::processEmitter( uint8_t *pEmitter ) {
    // FUN_00457f60
    Write<uint8_t>( pEmitter, 0, 1 );
    Write<uint8_t>( pEmitter, 2, 1 );

    Write<uint32_t>( pEmitter, 0x18,
                     Read<uint32_t>( pEmitter, 0x18 ) * prtf::kKeyStride +
                         Read<uint32_t>( pBlob, prtf::kKeyBase ) );
    Write<uint16_t>( pEmitter, 6, 0 );

    {
        uint8_t *pTable = pBlob + Read<uint32_t>( pEmitter, 0x24 );
        const int32_t count =
            static_cast<int32_t>( Read<uint32_t>( pEmitter, 0x20 ) );
        const uint32_t defABase = Read<uint32_t>( pBlob, prtf::kDefABase );
        for ( int32_t j = 0; j < count; j++ ) {
            const uint32_t recOffset =
                Read<uint32_t>( pTable, j * 4 ) * prtf::kDefStride + defABase;
            Write<uint32_t>( pTable, j * 4, recOffset );
            uint8_t *pRec = pBlob + recOffset;
            uint32_t seed = Read<uint32_t>( pEmitter, 0x80 );
            if ( seed == 0 ) {
                seed = static_cast<uint32_t>( rand() );
            }
            Write<uint32_t>( pRec, 0x21c, seed );
            for ( uint32_t k = 0; k < 16; k++ ) {
                Write<uint32_t>( pRec, 0x80 + k * 4,
                                 Read<uint32_t>( pRec, 0x40 + k * 4 ) );
            }
            Write<float>( pRec, 0x100, 1.0f );
        }
    }

    {
        uint8_t *pTable = pBlob + Read<uint32_t>( pEmitter, 0x30 );
        const int32_t count =
            static_cast<int32_t>( Read<uint32_t>( pEmitter, 0x2c ) );
        const uint32_t defBBase = Read<uint32_t>( pBlob, prtf::kDefBBase );
        for ( int32_t j = 0; j < count; j++ ) {
            const uint32_t recOffset =
                Read<uint32_t>( pTable, j * 4 ) * prtf::kDefStride + defBBase;
            Write<uint32_t>( pTable, j * 4, recOffset );
            uint8_t *pRec = pBlob + recOffset;
            for ( uint32_t k = 0; k < 16; k++ ) {
                Write<uint32_t>( pRec, 0xc0 + k * 4,
                                 Read<uint32_t>( pRec, 0x40 + k * 4 ) );
            }
        }
    }

    {
        uint8_t *pTable = pBlob + Read<uint32_t>( pEmitter, 0x3c );
        const int32_t count =
            static_cast<int32_t>( Read<uint32_t>( pEmitter, 0x38 ) );
        const uint32_t smallBase = Read<uint32_t>( pBlob, prtf::kSmallBase );
        const uint32_t defABase = Read<uint32_t>( pBlob, prtf::kDefABase );
        const uint32_t defBBase = Read<uint32_t>( pBlob, prtf::kDefBBase );
        for ( int32_t j = 0; j < count; j++ ) {
            const uint32_t smallOffset =
                Read<uint32_t>( pTable, j * 4 ) * prtf::kSmallStride +
                smallBase;
            Write<uint32_t>( pTable, j * 4, smallOffset );
            uint8_t *pSmall = pBlob + smallOffset;
            const uint8_t linkType = Read<uint8_t>( pSmall, 8 );
            const int32_t subCount =
                static_cast<int32_t>( Read<uint32_t>( pSmall, 0x10 ) );
            uint8_t *pSubTable = pBlob + Read<uint32_t>( pSmall, 0x14 );
            for ( int32_t k = 0; k < subCount; k++ ) {
                if ( linkType == 1 ) {
                    Write<uint32_t>( pSubTable, k * 4,
                                     Read<uint32_t>( pSubTable, k * 4 ) *
                                             prtf::kDefStride +
                                         defABase );
                } else if ( linkType == 2 ) {
                    Write<uint32_t>( pSubTable, k * 4,
                                     Read<uint32_t>( pSubTable, k * 4 ) *
                                             prtf::kDefStride +
                                         defBBase );
                }
            }
        }
    }

    {
        uint8_t *pTable = pBlob + Read<uint32_t>( pEmitter, 0x48 );
        const int32_t count =
            static_cast<int32_t>( Read<uint32_t>( pEmitter, 0x44 ) );
        const uint32_t matBase = Read<uint32_t>( pBlob, prtf::kMatBase );
        for ( int32_t j = 0; j < count; j++ ) {
            const uint32_t recOffset =
                Read<uint32_t>( pTable, j * 4 ) * prtf::kMatStride + matBase;
            Write<uint32_t>( pTable, j * 4, recOffset );
            uint8_t *pRec = pBlob + recOffset;
            Write<uint8_t>( pRec, 0xb, 0 );
            Write<uint8_t>( pRec, 0xd, 0 );
            Write<float>( pRec, 0x104, -1.0f );
            Write<float>( pRec, 0xc0, 1.0f );
            Write<uint32_t>( pRec, 0xc4, 0 );
            Write<uint32_t>( pRec, 0xc8, 0 );
            Write<uint32_t>( pRec, 0xcc, 0 );
            Write<uint32_t>( pRec, 0xd0, 0 );
            Write<float>( pRec, 0xd4, 1.0f );
            Write<uint32_t>( pRec, 0xd8, 0 );
            Write<uint32_t>( pRec, 0xdc, 0 );
            Write<uint32_t>( pRec, 0xe0, 0 );
            Write<uint32_t>( pRec, 0xe4, 0 );
            Write<float>( pRec, 0xe8, 1.0f );
            Write<uint32_t>( pRec, 0xec, 0 );
            Write<uint32_t>( pRec, 0xf0, 0 );
            Write<uint32_t>( pRec, 0xf4, 0 );
            Write<uint32_t>( pRec, 0xf8, 0 );
            Write<float>( pRec, 0xfc, 1.0f );
        }
    }
}
