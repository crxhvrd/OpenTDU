#include "shared.h"
#include "mod_manager.h"

#include "file_map.h"
#include "gs_file.h"
#include "mod_installer.h"

static ModManager gModManager = {};
ModManager *gpModManager = &gModManager;

static constexpr const char *kModsFolderPath = "opentdu_data/mods";
static constexpr const char *kModsStateFilename = "opentdu_data/mods/mods.ini";
static constexpr const char *kPackMapFilename = "CarPack.map";
static constexpr const char *kPackBigFilename = "CarPack.big";
static constexpr const char *kPackSidecarFilename = "files.txt";

struct BuiltInPackDesc {
    const char *pId;
    const char *pPath;
};

static constexpr BuiltInPackDesc kBuiltInPacks[] = {
    { "OpenTDU Localization", "opentdu_data/localization" },
};

ModManager::ModManager() : bPendingChanges( false ) {}

ModManager::~ModManager() {}

ModPack *ModManager::findPack( const char *pId ) {
    if ( pId == nullptr ) {
        return nullptr;
    }

    for ( ModPack &pack : packs ) {
        if ( StringCmpI( pack.Id.c_str(), pId ) == 0 ) {
            return &pack;
        }
    }

    return nullptr;
}

bool ModManager::isEnabled( const char *pId ) const {
    const ModPack *pPack = const_cast<ModManager *>( this )->findPack( pId );
    return ( pPack != nullptr ) && pPack->bEnabled;
}

void ModManager::setEnabled( const char *pId, const bool bEnabled ) {
    ModPack *pPack = findPack( pId );
    if ( pPack == nullptr || pPack->bMandatory ||
         pPack->bEnabled == bEnabled ) {
        return;
    }

    pPack->bEnabled = bEnabled;
    bPendingChanges = true;

    detectConflicts();
}

void ModManager::loadEnabledState() {
    std::ifstream stream( kModsStateFilename );
    if ( !stream.is_open() ) {
        return;
    }

    ModPack *pCurrentPack = nullptr;

    std::string line;
    while ( std::getline( stream, line ) ) {
        const size_t comment = line.find_first_of( ";#" );
        if ( comment != std::string::npos ) {
            line.erase( comment );
        }

        const size_t first = line.find_first_not_of( " \t\r\n" );
        if ( first == std::string::npos ) {
            continue;
        }
        const size_t last = line.find_last_not_of( " \t\r\n" );
        line = line.substr( first, last - first + 1 );

        if ( line.front() == '[' ) {
            const size_t close = line.find( ']' );
            pCurrentPack = ( close != std::string::npos )
                               ? findPack( line.substr( 1, close - 1 ).c_str() )
                               : nullptr;
            continue;
        }

        const size_t equals = line.find( '=' );
        if ( equals == std::string::npos || pCurrentPack == nullptr ) {
            continue;
        }

        std::string key = line.substr( 0, equals );
        std::string value = line.substr( equals + 1 );
        key.erase( key.find_last_not_of( " \t" ) + 1 );
        const size_t valueStart = value.find_first_not_of( " \t" );
        value = ( valueStart == std::string::npos )
                    ? ""
                    : value.substr( valueStart );

        if ( StringCmpI( key.c_str(), "Enabled" ) == 0 &&
             !pCurrentPack->bMandatory ) {
            pCurrentPack->bEnabled = ( atoi( value.c_str() ) != 0 );
        }
    }
}

bool ModManager::save() {
    std::string content =
        "; OpenTDU mod manager state. One section per folder in Mods/.\n"
        "; A pack that is not listed here is off until you turn it on.\n";

    for ( const ModPack &pack : packs ) {
        if ( pack.bMandatory ) {
            continue;
        }

        content += "\n[";
        content += pack.Id;
        content += "]\nEnabled = ";
        content += pack.bEnabled ? "1" : "0";
        content += "\n";
    }

    const bool bSaved = GSFile::SaveFile( kModsStateFilename, content.data(),
                                          (uint32_t)content.size() );
    if ( !bSaved ) {
        OTDU_LOG_ERROR( "Failed to write '%s'\n", kModsStateFilename );
    }

    return bSaved;
}

static bool CollectPackPaths( const ModPack &pack,
                              std::vector<std::string> &outPaths ) {
    std::filesystem::path sidecarPath( pack.Path );
    sidecarPath /= kPackSidecarFilename;

    std::ifstream stream( sidecarPath, std::ifstream::in );
    if ( !stream.is_open() ) {
        return false;
    }

    std::string line;
    while ( std::getline( stream, line ) ) {
        while ( !line.empty() &&
                ( line.back() == '\r' || line.back() == '\n' ) ) {
            line.pop_back();
        }
        outPaths.push_back( line );
    }

    return true;
}

static bool CollectPackEntries( const ModPack &pack,
                                std::vector<uint32_t> &outCRCs ) {
    std::filesystem::path mapPath( pack.Path );
    mapPath /= kPackMapFilename;

    std::ifstream stream( mapPath, std::ifstream::in | std::ifstream::binary );
    if ( !stream.is_open() ) {
        return false;
    }

    stream.seekg( 0, std::ios::end );
    const size_t fileSize = (size_t)stream.tellg();
    stream.seekg( 0, std::ios::beg );

    if ( fileSize <= 5 ) {
        return false;
    }

    std::vector<uint8_t> content( fileSize );
    stream.read( (char *)content.data(), (std::streamsize)fileSize );
    if ( (size_t)stream.gcount() != fileSize ) {
        return false;
    }

    if ( content[0] != 'M' || content[1] != 'A' || content[2] != 'P' ||
         content[4] != 0 ) {
        return false;
    }

    FileMapEntry entry;
    for ( size_t offset = 5; offset + 0x18 <= fileSize; offset += 0x18 ) {
        entry.deserialize( content.data() + offset );
        outCRCs.push_back( entry.CRC );
    }

    return true;
}

void ModManager::detectConflicts() {
    std::vector<std::pair<uint32_t, size_t>> claims;
    std::vector<std::pair<uint32_t, std::string>> knownPaths;

    for ( ModPack &pack : packs ) {
        pack.bConflicting = pack.bIncomplete;
        pack.Status = pack.bIncomplete ? pack.IncompleteReason : std::string();
    }

    for ( size_t i = 0; i < packs.size(); i++ ) {
        ModPack &pack = packs[i];
        if ( !pack.bEnabled || pack.bIncomplete || pack.bDescriptorOnly ) {
            continue;
        }

        std::vector<uint32_t> crcs;
        if ( !CollectPackEntries( pack, crcs ) ) {
            pack.Status = "its CarPack.map could not be read";
            pack.bConflicting = true;
            continue;
        }

        pack.NumEntries = (uint32_t)crcs.size();
        for ( const uint32_t crc : crcs ) {
            claims.push_back( { crc, i } );
        }

        std::vector<std::string> paths;
        if ( CollectPackPaths( pack, paths ) ) {
            if ( paths.size() != crcs.size() ) {
                OTDU_LOG_WARN( "Mod '%s': files.txt lists %zu path(s) for %zu "
                               "map record(s); ignoring it\n",
                               pack.Id.c_str(), paths.size(), crcs.size() );
            } else {
                for ( size_t entry = 0; entry < crcs.size(); entry++ ) {
                    knownPaths.push_back( { crcs[entry], paths[entry] } );
                }
            }
        }
    }

    std::sort( claims.begin(), claims.end() );
    std::sort( knownPaths.begin(), knownPaths.end() );

    uint32_t numCollisions = 0;
    for ( size_t i = 1; i < claims.size(); i++ ) {
        if ( claims[i].first != claims[i - 1].first ||
             claims[i].second == claims[i - 1].second ) {
            continue;
        }

        ModPack &left = packs[claims[i - 1].second];
        ModPack &right = packs[claims[i].second];

        const uint32_t crc = claims[i].first;

        auto it = std::lower_bound(
            knownPaths.begin(), knownPaths.end(), crc,
            []( const std::pair<uint32_t, std::string> &lhs,
                const uint32_t rhs ) { return lhs.first < rhs; } );

        char pDescription[288];
        if ( it != knownPaths.end() && it->first == crc ) {
            snprintf( pDescription, sizeof( pDescription ), "'%s'",
                      it->second.c_str() );
        } else {
            snprintf( pDescription, sizeof( pDescription ),
                      "the file with CRC %08X", crc );
        }

        numCollisions++;

        if ( left.bMandatory || right.bMandatory ) {
            ModPack &loser = left.bMandatory ? right : left;
            const ModPack &winner = left.bMandatory ? left : right;
            if ( !loser.bMandatory ) {
                if ( !loser.bConflicting ) {
                    OTDU_LOG_ERROR( "Mod '%s' replaces %s, which the built-in "
                                    "pack '%s' owns; it will not be mounted\n",
                                    loser.Id.c_str(), pDescription,
                                    winner.Id.c_str() );
                }
                loser.bConflicting = true;
                loser.Status = "it replaces file(s) owned by the built-in '" +
                               winner.Id + "'";
                continue;
            }
        }

        if ( !left.bConflicting || !right.bConflicting ) {
            OTDU_LOG_ERROR(
                "Mods '%s' and '%s' both replace %s; neither will be mounted "
                "until one is turned off\n",
                left.Id.c_str(), right.Id.c_str(), pDescription );
        }

        left.bConflicting = true;
        right.bConflicting = true;
        left.Status = "it replaces the same file(s) as '" + right.Id + "'";
        right.Status = "it replaces the same file(s) as '" + left.Id + "'";
    }

    if ( numCollisions != 0 ) {
        OTDU_LOG_WARN( "%u file collision(s) between enabled mods\n",
                       numCollisions );
    }
}

static void DetectPackContent( ModPack &pack ) {
    std::error_code errorCode;
    const std::filesystem::path packPath( pack.Path );

    pack.Description.Value = pack.Id;
    ModInstaller::ReadPackDescription( pack.Path, pack.Description );

    const bool bHasMap = std::filesystem::is_regular_file(
        packPath / kPackMapFilename, errorCode );
    const bool bHasBig = std::filesystem::is_regular_file(
        packPath / kPackBigFilename, errorCode );

    if ( !bHasMap && !bHasBig &&
         std::filesystem::is_regular_file( packPath / kContentDescFilename,
                                           errorCode ) ) {
        pack.bDescriptorOnly = true;
        return;
    }

    if ( !bHasMap || !bHasBig ) {
        pack.bIncomplete = true;
        pack.IncompleteReason = "it has no ";
        pack.IncompleteReason += bHasMap ? kPackBigFilename : kPackMapFilename;
    }
}

void ModManager::addBuiltInPacks() {
    std::error_code errorCode;
    std::wstring_convert<std::codecvt_utf8<wchar_t>> converter;

    std::vector<ModPack> builtInPacks;
    for ( const BuiltInPackDesc &desc : kBuiltInPacks ) {
        if ( !std::filesystem::is_directory( desc.pPath, errorCode ) ) {
            OTDU_LOG_WARN( "Built-in pack '%s' is missing ('%s')\n", desc.pId,
                           desc.pPath );
            continue;
        }

        ModPack pack;
        pack.Id = desc.pId;
        pack.Path = desc.pPath;
        pack.DisplayName = converter.from_bytes( pack.Id );
        pack.bEnabled = true;
        pack.bMandatory = true;
        DetectPackContent( pack );
        builtInPacks.push_back( pack );
    }

    packs.insert( packs.begin(), builtInPacks.begin(), builtInPacks.end() );
}

void ModManager::refresh() {
    std::vector<std::pair<std::string, bool>> previousState;
    for ( const ModPack &pack : packs ) {
        if ( !pack.bMandatory ) {
            previousState.push_back( { pack.Id, (bool)pack.bEnabled } );
        }
    }

    packs.clear();

    std::error_code errorCode;
    const std::filesystem::path modsFolder( kModsFolderPath );
    if ( std::filesystem::is_directory( modsFolder, errorCode ) ) {
        std::wstring_convert<std::codecvt_utf8<wchar_t>> converter;

        for ( const auto &entry :
              std::filesystem::directory_iterator( modsFolder, errorCode ) ) {
            if ( !entry.is_directory() ) {
                continue;
            }

            ModPack pack;
            pack.Id = entry.path().filename().string();
            pack.Path = entry.path().string();
            pack.DisplayName = converter.from_bytes( pack.Id );
            DetectPackContent( pack );
            packs.push_back( pack );
        }

        std::sort(
            packs.begin(), packs.end(),
            []( const ModPack &a, const ModPack &b ) { return a.Id < b.Id; } );
    }

    addBuiltInPacks();

    for ( const auto &state : previousState ) {
        ModPack *pPack = findPack( state.first.c_str() );
        if ( pPack != nullptr && !pPack->bMandatory ) {
            pPack->bEnabled = state.second;
        }
    }

    loadEnabledState();

    detectConflicts();

    for ( ModPack &pack : packs ) {
        pack.bMounted = false;
        pack.FilemapIndex = -1;
        if ( pack.Status.empty() && !pack.bEnabled ) {
            pack.Status = "disabled";
        }
    }
}
