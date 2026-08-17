#include "shared.h"
#include "avatar_bnk.h"

#include "config/gs_config.h"
#include "core/hash/tdu_concat.h"

// Suffix the bank uses to tag the material stream of a part
static constexpr char kMaterialEntrySuffix[] = "_MAT";
static constexpr size_t kMaterialEntrySuffixLength = 4;
static constexpr int32_t kAvatarBankSubTypeSkeleton = 7;
static constexpr uintptr_t kAvatarSkeletonDataOffset =
    2 * sizeof( RenderFile::Section );

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

AvatarBnk::AvatarBnk() : Bank() {}

AvatarBnk::~AvatarBnk() {}

bool AvatarBnk::load( const char *pCharacterFolder, const char *pBankName ) {
    // FUN_008ed7e0
    NumReferences = 0;
    bInstallOK = false;

    std::string bankPath = pCharacterFolder;
    bankPath += pBankName;
    bankPath += ".bnk";

    if ( !loadBootBank( bankPath.c_str(), nullptr ) ) {
        OTDU_LOG_WARN( "Failed to load avatar bank '%s'\n", bankPath.c_str() );
        return false;
    }

    Name = pBankName;
    return true;
}

void AvatarBnk::loadContent() {
    parseEntries();
    bInstallOK = true;
}

void AvatarBnk::parseEntries() {
    ParsedBankEntry entry;

    const int32_t numElements = getNumElements();
    for ( int32_t i = 0; i < numElements; i++ ) {
        getEntry( i, entry );
        if ( entry.pData == nullptr ) {
            continue;
        }

        if ( entry.SubType == AT_3DG && NumGeometries < kMaxGeometries ) {
            Geometries[NumGeometries++].initialize( entry.pData );
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

void AvatarBnkManager::initialize() {
    // FUN_009cb890
    std::string bankFolder = gpConfig->getResRootPath();
    bankFolder += "Avatar/";

    if ( commonBodies.load( bankFolder.c_str(), "COMMON_BODIES" ) ) {
        commonBodies.addReference();
    }

    bankFolder += "CLOTHES/";
    if ( commonClothes.load( bankFolder.c_str(), "COMMON_CLOTHES" ) ) {
        commonClothes.addReference();
    }

    OTDU_UNIMPLEMENTED_ONCE;
}

void AvatarBnkManager::terminate() {
    commonClothes.unload();
    commonBodies.unload();
}
