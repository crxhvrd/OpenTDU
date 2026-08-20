#include "shared.h"
#include "avatar_bnk.h"

#include "config/gs_config.h"
#include "core/hash/tdu_concat.h"
#include "filesystem/gs_file.h"

// Suffix the bank uses to tag the material stream of a part
static constexpr char kMaterialEntrySuffix[] = "_MAT";
static constexpr size_t kMaterialEntrySuffixLength = 4;
static constexpr int32_t kAvatarBankSubTypeSkeleton = 7;

static std::string GetEntryBaseName( const std::string &filename ) {
    // FUN_00411990
    const size_t separator = filename.find_last_of( '\\' );
    const size_t start = ( separator == std::string::npos ) ? 0 : separator + 1;
    const size_t length = filename.size() - start;
    if ( length <= kMaterialEntrySuffixLength ) {
        return std::string();
    }

    return filename.substr( start, length - kMaterialEntrySuffixLength );
}

static bool IsMaterialEntryName( const std::string &name ) {
    return name.size() > kMaterialEntrySuffixLength &&
           name.compare( name.size() - kMaterialEntrySuffixLength,
                         kMaterialEntrySuffixLength,
                         kMaterialEntrySuffix ) == 0;
}

uint64_t AvatarCalcHashcode( const char *pName ) {
    // FUN_0040fd50
    return TestDriveConcatToString8( pName, strlen( pName ) ).AsInteger64;
}

AvatarBnk::AvatarBnk( const bool bCommon ) : Bank(), bCommonBank( bCommon ) {}

AvatarBnk::~AvatarBnk() {}

bool AvatarBnk::load( const char *pCharacterFolder,
                      const char *pBankName,
                      const bool bAsync,
                      const bool bCritical,
                      const bool bUseBootBank ) {
    // FUN_008ed7e0
    if ( LoadingStatus == ABL_Requested ) {
        gpFile->flushPendingAsync();

        if ( LoadingStatus == ABL_Installed ) {
            release();
        }
    }

    NumReferences = 0;
    bInstallOK = false;

    std::string bankPath = pCharacterFolder;
    bankPath += pBankName;
    bankPath += ".bnk";

    if ( bUseBootBank && pMemoryPool == nullptr ) {
        LoadingStatus = ABL_Requested;
        bAsyncLoading = false;

        if ( !loadBootBank( bankPath.c_str(), nullptr ) ) {
            LoadingStatus = ABL_None;
            OTDU_LOG_WARN( "Failed to load avatar bank '%s'\n",
                           bankPath.c_str() );
            return false;
        }

        Name = pBankName;
        return true;
    }

    if ( !gpFile->fileExists( bankPath.c_str() ) ) {
        OTDU_LOG_WARN( "bank %s doesnt exist\n", bankPath.c_str() );
        return false;
    }

    LoadingStatus = ABL_Requested;
    bAsyncLoading = bAsync;

    (void)bCritical;

    if ( !loadBankWithMemory( gpFile, bankPath.c_str(), pMemoryPool,
                              MemoryPoolSize, bAsync ) ) {
        LoadingStatus = ABL_None;
        return false;
    }

    Name = pBankName;
    return true;
}

void AvatarBnk::release() {
    // FUN_008ed730
    if ( LoadingStatus == ABL_Requested ) {
        gpFile->flushPendingAsync();
    }

    if ( LoadingStatus != ABL_Installed ) {
        LoadingStatus = ABL_None;
        return;
    }

    releaseParsedEntries();
    unload();

    Name = kAvatarBnkEmptyName;
    bInstallOK = false;
    LoadingStatus = ABL_None;
}

void AvatarBnk::allocateMemoryPool() {
    // FUN_008ecd60
    pMemoryPool = TestDrive::AllocAligned( kMemoryPoolSize, 0x10 );
    if ( pMemoryPool != nullptr ) {
        MemoryPoolSize = kMemoryPoolSize;
    }
}

void AvatarBnk::freeMemoryPool() {
    // FUN_008ecd30
    if ( pMemoryPool != nullptr ) {
        TestDrive::FreeAligned( pMemoryPool );
        pMemoryPool = nullptr;
        MemoryPoolSize = 0;
    }
}

void AvatarBnk::loadContent() {
    parseEntries();
    bInstallOK = true;
    LoadingStatus = ABL_Installed;
}

void AvatarBnk::releaseParsedEntries() {
    // FUN_008ed730 (inlined)
    for ( int32_t i = 0; i < NumMaterials; i++ ) {
        MaterialEntry &material = Materials[i];
        if ( material.Material.getHeader() != nullptr ) {
            material.Material.destroy();
        }
        material.Hashcode = 0;
    }
    NumMaterials = 0;

    for ( int32_t i = 0; i < NumBitmaps; i++ ) {
        if ( Bitmaps[i].getHeader() != nullptr ) {
            Bitmaps[i].destroy();
        }
    }
    NumBitmaps = 0;

    for ( int32_t i = 0; i < NumGeometries; i++ ) {
        if ( Geometries[i].getHeader() != nullptr ) {
            Geometries[i].destroy();
        }
    }
    NumGeometries = 0;
}

void AvatarBnk::parseEntries() {
    ParsedBankEntry entry;

    const int32_t numElements = getNumElements();
    for ( int32_t i = 0; i < numElements; i++ ) {
        getEntry( i, entry );
        if ( entry.pData == nullptr ) {
            continue;
        }

        if ( bCommonBank && entry.SubType == AT_3DG &&
             NumGeometries < kMaxGeometries ) {
            Geometries[NumGeometries++].initialize( entry.pData );
        }

        if ( bCommonBank && entry.SubType == AT_2DB &&
             NumBitmaps < kMaxBitmaps ) {
            Bitmaps[NumBitmaps++].initialize( entry.pData );
        }

        if ( entry.SubType == AT_2DM && NumMaterials < kMaxMaterials ) {
            const std::string entryName = GetEntryBaseName( entry.Filename );
            if ( IsMaterialEntryName( entryName ) ) {
                MaterialEntry &material = Materials[NumMaterials];
                material.Material.initialize( entry.pData );
                material.Hashcode = AvatarCalcHashcode( entryName.c_str() );
                NumMaterials++;
            }
        }
    }
}

Render3DG *AvatarBnk::find3DG( const uint64_t hashcode ) {
    // FUN_008ec5e0
    for ( int32_t i = 0; i < kMaxGeometries; i++ ) {
        Render3DG &geometry = Geometries[i];
        if ( geometry.getHeader() == nullptr ) {
            continue;
        }

        if ( geometry.findGeometryForHashcode( hashcode ) != nullptr ) {
            return &geometry;
        }
    }

    return nullptr;
}

Render2DB *AvatarBnk::find2DB( const uint64_t hashcode ) {
    // FUN_008ec560
    for ( int32_t i = 0; i < kMaxBitmaps; i++ ) {
        Render2DB &bitmap = Bitmaps[i];
        if ( bitmap.getHeader() == nullptr ||
             bitmap.getBitmapSection() == nullptr ) {
            continue;
        }

        if ( bitmap.getBitmapHashcode() == hashcode ) {
            return &bitmap;
        }
    }

    OTDU_LOG_ERROR( "The 2db ( %llu ) is not found in bank: %s\n", hashcode,
                    Name.c_str() );

    return nullptr;
}

void *AvatarBnk::findStreamByHashcode( const int32_t subType,
                                       const uint64_t hashcode ) {
    // FUN_008ed110 (AT_3DG) / FUN_008ed230 (AT_2DB)
    if ( !isLoaded() ) {
        return nullptr;
    }

    ParsedBankEntry entry;

    const int32_t numElements = getNumElements();
    for ( int32_t i = 0; i < numElements; i++ ) {
        getEntry( i, entry );
        if ( entry.SubType != subType ) {
            continue;
        }

        const std::string entryName = GetEntryBaseName( entry.Filename );
        if ( AvatarCalcHashcode( entryName.c_str() ) == hashcode ) {
            return entry.pData;
        }
    }

    return nullptr;
}

void *AvatarBnk::findGeometryStream( const uint64_t hashcode ) {
    // FUN_008ed110
    return findStreamByHashcode( AT_3DG, hashcode );
}

void *AvatarBnk::findBitmapStream( const uint64_t hashcode ) {
    // FUN_008ed230
    return findStreamByHashcode( AT_2DB, hashcode );
}

Render2DM *AvatarBnk::getMaterial( const uint64_t hashcode ) {
    // FUN_008f25c0 (inlined)
    for ( int32_t i = 0; i < kMaxMaterials; i++ ) {
        MaterialEntry &material = Materials[i];
        if ( material.Hashcode == hashcode &&
             material.Material.getHeader() != nullptr ) {
            return &material.Material;
        }
    }

    return nullptr;
}

void *AvatarBnk::getEntryData( const char *pPartName,
                               const char *pExtension,
                               const int32_t subType ) {
    if ( !isLoaded() ) {
        return nullptr;
    }

    std::string entryName = pPartName;
    entryName += pExtension;

    ParsedBankEntry entry;
    bank.getEntry( const_cast<char *>( entryName.c_str() ), entry );

    return ( entry.SubType == subType ) ? entry.pData : nullptr;
}

void *AvatarBnk::get2DM( const char *pPartName ) {
    // FUN_008ed030
    if ( pPartName == nullptr || pPartName[0] == '\0' ) {
        return nullptr;
    }

    return getEntryData( pPartName, ".2DM", AT_2DM );
}

void *AvatarBnk::get3DD( const char *pPartName ) {
    // FUN_008ece80
    return getEntryData( pPartName, ".3DD", AT_3DD );
}

void *AvatarBnk::getSkeleton( const char *pPartName ) {
    // FUN_008ecda0
    return getEntryData( pPartName, ".XSB", kAvatarBankSubTypeSkeleton );
}

AvatarBnkCache::AvatarBnkCache() {}

AvatarBnkCache::~AvatarBnkCache() {}

void AvatarBnkCache::initialize() {
    // FUN_009cb890 (tail)
    for ( AvatarBnk &node : Nodes ) {
        node.allocateMemoryPool();
    }
}

void AvatarBnkCache::terminate() {
    pHead = nullptr;

    for ( AvatarBnk &node : Nodes ) {
        node.release();
        node.freeMemoryPool();
    }
}

AvatarBnk *AvatarBnkCache::findLoadedBank( const char *pBankName ) {
    // FUN_008ec8d0
    for ( AvatarBnk *pBnk = pHead; pBnk != nullptr;
          pBnk = pBnk->pNextInCache ) {
        if ( strcmp( pBnk->getName(), pBankName ) == 0 ) {
            return pBnk;
        }
    }

    return nullptr;
}

AvatarBnk *AvatarBnkCache::findFreeNode() {
    // FUN_008ecb00
    for ( AvatarBnk &node : Nodes ) {
        if ( node.getNumReferences() < 1 &&
             strcmp( node.getName(), kAvatarBnkEmptyName ) == 0 ) {
            return &node;
        }
    }

    return nullptr;
}

AvatarBnk *AvatarBnkCache::getOldestBank() {
    // FUN_008ec960
    AvatarBnk *pBnk = pHead;
    while ( pBnk != nullptr && pBnk->pNextInCache != nullptr ) {
        pBnk = pBnk->pNextInCache;
    }

    return pBnk;
}

AvatarBnk *AvatarBnkCache::removeOldestBank() {
    // FUN_008ec990
    AvatarBnk *pPrevious = pHead;
    AvatarBnk *pBnk = pHead;

    while ( pBnk != nullptr && pBnk->pNextInCache != nullptr ) {
        pPrevious = pBnk;
        pBnk = pBnk->pNextInCache;
    }

    if ( pPrevious != nullptr ) {
        pPrevious->pNextInCache = nullptr;
    }

    if ( pBnk == pHead ) {
        pHead = nullptr;
    }

    return pBnk;
}

void AvatarBnkCache::pushFront( AvatarBnk *pBnk ) {
    // FUN_008ec9d0
    pBnk->pNextInCache = pHead;
    pHead = pBnk;
}

void AvatarBnkCache::moveToFront( AvatarBnk *pBnk ) {
    // FUN_008ec9f0
    if ( pHead == pBnk || pHead == nullptr ) {
        return;
    }

    AvatarBnk *pPrevious = pHead;
    AvatarBnk *pIterator = pHead;
    while ( pIterator != pBnk ) {
        pPrevious = pIterator;
        pIterator = pIterator->pNextInCache;

        if ( pIterator == nullptr ) {
            return;
        }
    }

    pPrevious->pNextInCache = pIterator->pNextInCache;
    pIterator->pNextInCache = pHead;
    pHead = pIterator;
}

bool AvatarBnkCache::getBank( const char *pBankName, AvatarBnk **ppOutBnk ) {
    // FUN_008eca80
    AvatarBnk *pBnk = findLoadedBank( pBankName );
    CacheRequest++;

    if ( pBnk == nullptr ) {
        return true;
    }

    CacheHit++;
    moveToFront( pBnk );
    *ppOutBnk = pBnk;

    return false;
}

eAvatarBankRequest AvatarBnkCache::requestBank( const char *pCharacterFolder,
                                                const char *pBankName,
                                                const bool bAsync,
                                                const bool bCritical,
                                                AvatarBnk **ppOutBnk ) {
    // FUN_008ecc10
    AvatarBnk *pCachedBnk = nullptr;
    if ( !getBank( pBankName, &pCachedBnk ) ) {
        *ppOutBnk = pCachedBnk;
        return ABR_Ready;
    }

    AvatarBnk *pNode = findFreeNode();
    if ( pNode == nullptr ) {
        AvatarBnk *pOldest = getOldestBank();
        if ( pOldest == nullptr || pOldest->getNumReferences() > 0 ) {
            *ppOutBnk = nullptr;
            CacheWait++;
            return ABR_Pending;
        }

        pNode = removeOldestBank();
        pNode->release();
        CacheRemove++;
    }

    if ( !pNode->load( pCharacterFolder, pBankName, bAsync, bCritical,
                       false ) ) {
        *ppOutBnk = nullptr;
        return ABR_Failed;
    }

    pushFront( pNode );
    *ppOutBnk = pNode;

    return ABR_Ready;
}

void AvatarBnkManager::initialize() {
    // FUN_009cb890
    std::string bankFolder = gpConfig->getResRootPath();
    bankFolder += "Avatar/";

    if ( commonBodies.load( bankFolder.c_str(), "COMMON_BODIES", false, false,
                            true ) ) {
        commonBodies.addReference();
    }

    bankFolder += "CLOTHES/";
    if ( commonClothes.load( bankFolder.c_str(), "COMMON_CLOTHES", false, false,
                             true ) ) {
        commonClothes.addReference();
    }

    bankCache.initialize();
}

void AvatarBnkManager::terminate() {
    bankCache.terminate();

    commonClothes.unload();
    commonBodies.unload();
}
