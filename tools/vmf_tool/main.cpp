#include <map>
#include <sstream>
#include <unordered_map>

#include "core/arg_parser.h"
#include "core/assert.h"
#include "core/binary_helpers.h"
#include "core/logger.h"

#include "filesystem/bank.h"
#include "filesystem/bank_writer.h"
#include "flash/vmf/vmf_document.h"
#include "flash/vmf/vmf_movie_builder.h"

#if OTDU_DEVBUILD
bool gBreakOnAssertFailure = true; // DAT_00f47931
static CmdLineArg CmdLineArgDbgBreakOnAssertFailure( "break_on_assert_failure",
                                                     []( const char *pArg ) {
                                                         gBreakOnAssertFailure =
                                                             true;
                                                     } );

bool gDisableAssert = false;
static CmdLineArg CmdLineArgDisableAssert( "disable_assert",
                                           []( const char *pArg ) {
                                               gDisableAssert = true;
                                           } );
#endif

static constexpr uint32_t kBankMagic = 0x42414e4b; // KNAB

static char gpInputPath[OTDU_MAX_PATH];
static CmdLineArg CmdLineArgInput( "input", []( const char *pArg ) {
    strncpy( gpInputPath, pArg, OTDU_MAX_PATH - 1 );
} );

static char gpOutputPath[OTDU_MAX_PATH];
static CmdLineArg CmdLineArgOutput( "output", []( const char *pArg ) {
    strncpy( gpOutputPath, pArg, OTDU_MAX_PATH - 1 );
} );

static std::string gMode = "roundtrip";

static std::string gInstances;
static CmdLineArg CmdLineArgInstances( "instances", []( const char *pArg ) {
    gInstances = pArg;
} );
static CmdLineArg CmdLineArgMode( "mode",
                                  []( const char *pArg ) { gMode = pArg; } );

[[noreturn]] static void PrintUsage() {
    OTDU_LOG_ALWAYS(
        "USAGE: opentdu_vmfTool -input <path> [-mode <mode>] [-output <path>]\n"
        "\n"
        "Reads .vmf (edFlash) movies, either loose or straight from the .bnk\n"
        "banks that hold them.\n"
        "\n"
        "-input:  A .vmf file, a .bnk bank, or a folder searched recursively\n"
        "         for both (e.g. EURO/BNK/FrontEnd).\n"
        "-mode:   roundtrip (default): parse every movie, write it back and\n"
        "                   compare byte for byte.\n"
        "         dump:      print each movie's tag tree (to -output, a "
        "folder,\n"
        "                   or stdout).\n"
        "         extract:   write each movie found in a bank to -output.\n"
        "         lift:      copy the root instances listed in -instances\n"
        "                   (comma separated) with every character they\n"
        "                   use into a new movie, written to -output.\n"
        "         bankcheck: rewrite entry 0 of every .bnk with its own\n"
        "                   content and compare (bank writer self-test).\n" );
    exit( 1 );
}

struct Movie {
    std::string Name;
    std::vector<uint8_t> Data;
};

static bool ReadFile( const std::filesystem::path &path,
                      std::vector<uint8_t> &outData ) {
    std::ifstream file( path, std::ios::binary );
    if ( !file ) {
        return false;
    }
    outData.assign( std::istreambuf_iterator<char>( file ),
                    std::istreambuf_iterator<char>() );
    return true;
}

static void CollectFromBank( const std::filesystem::path &path,
                             const std::vector<uint8_t> &bank,
                             std::vector<Movie> &outMovies ) {
    if ( bank.size() < 8 + sizeof( BankHeader ) ) {
        return;
    }

    BankHeader header;
    std::memcpy( &header, bank.data() + 8, sizeof( BankHeader ) );
    if ( Read<uint32_t>( header.Magic ) != kBankMagic ) {
        return;
    }

    const size_t tableOffset = 8 + size_t( header.SectionOffsetTable );
    for ( int32_t i = 0; i < header.NumEntries; i++ ) {
        const size_t sectionOffset = tableOffset + i * sizeof( BankSection );
        if ( sectionOffset + sizeof( BankSection ) > bank.size() ) {
            break;
        }

        BankSection section;
        std::memcpy( &section, bank.data() + sectionOffset,
                     sizeof( BankSection ) );
        if ( section.Offset < 0 || section.Size < 4 ||
             size_t( section.Offset ) + section.Size > bank.size() ||
             Read<uint32_t>( bank.data(), section.Offset ) != kVMFMagic ) {
            continue;
        }

        Movie movie;
        movie.Name = path.stem().string() + "_" + std::to_string( i );
        movie.Data.assign( bank.begin() + section.Offset,
                           bank.begin() + section.Offset + section.Size );
        outMovies.push_back( std::move( movie ) );
    }
}

static void CollectFromFile( const std::filesystem::path &path,
                             std::vector<Movie> &outMovies ) {
    std::vector<uint8_t> data;
    if ( !ReadFile( path, data ) ) {
        OTDU_LOG_ALWAYS( "Cannot read '%s'\n", path.string().c_str() );
        return;
    }

    if ( data.size() >= 4 && Read<uint32_t>( data.data() ) == kVMFMagic ) {
        outMovies.push_back( { path.stem().string(), std::move( data ) } );
    } else {
        CollectFromBank( path, data, outMovies );
    }
}

static std::vector<Movie> CollectMovies( const std::filesystem::path &input ) {
    std::vector<Movie> movies;
    std::error_code errorCode;
    if ( !std::filesystem::is_directory( input, errorCode ) ) {
        CollectFromFile( input, movies );
        return movies;
    }

    std::vector<std::filesystem::path> files;
    for ( const auto &entry :
          std::filesystem::recursive_directory_iterator( input, errorCode ) ) {
        std::string extension = entry.path().extension().string();
        std::transform( extension.begin(), extension.end(), extension.begin(),
                        ::tolower );
        if ( entry.is_regular_file() &&
             ( extension == ".vmf" || extension == ".bnk" ) ) {
            files.push_back( entry.path() );
        }
    }
    std::sort( files.begin(), files.end() );

    for ( const std::filesystem::path &file : files ) {
        const size_t firstMovie = movies.size();
        CollectFromFile( file, movies );

        // Prefix with the parent folder (HiRes/LowRes share bank names)
        const std::string folder = file.parent_path().filename().string();
        for ( size_t i = firstMovie; i < movies.size(); i++ ) {
            movies[i].Name = folder + "_" + movies[i].Name;
        }
    }
    return movies;
}

static const char *GetTagName( const uint32_t tagId ) {
    switch ( tagId ) {
    case 0x00:
        return "End";
    case 0x01:
        return "ShowFrame";
    case 0x02:
        return "DefineShape";
    case 0x07:
        return "DefineButton";
    case 0x09:
        return "SetBackgroundColor";
    case 0x0a:
        return "DefineFont";
    case 0x0b:
        return "DefineText";
    case 0x0c:
        return "DoAction";
    case 0x0e:
        return "DefineSound";
    case 0x0f:
        return "StartSound";
    case 0x14:
        return "DefineBitsLossless";
    case 0x1a:
        return "PlaceObject2";
    case 0x1c:
        return "RemoveObject2";
    case 0x25:
        return "DefineEditText";
    case 0x27:
        return "DefineSprite";
    case 0x2b:
        return "FrameLabel";
    case 0x2e:
        return "DefineMorphShape";
    case 0x40:
        return "MaterialReference";
    default:
        return "Unknown";
    }
}

static const char *GetActionName( const uint8_t opcode ) {
    switch ( opcode ) {
    case 0x04:
        return "NextFrame";
    case 0x05:
        return "PrevFrame";
    case 0x06:
        return "Play";
    case 0x07:
        return "Stop";
    case 0x0a:
        return "Add";
    case 0x0b:
        return "Subtract";
    case 0x0c:
        return "Multiply";
    case 0x0d:
        return "Divide";
    case 0x0e:
        return "Equals";
    case 0x0f:
        return "Less";
    case 0x10:
        return "And";
    case 0x11:
        return "Or";
    case 0x12:
        return "Not";
    case 0x13:
        return "StringEquals";
    case 0x14:
        return "StringLength";
    case 0x17:
        return "Pop";
    case 0x18:
        return "ToInteger";
    case 0x1c:
        return "GetVariable";
    case 0x1d:
        return "SetVariable";
    case 0x20:
        return "SetTarget2";
    case 0x21:
        return "StringAdd";
    case 0x22:
        return "GetProperty";
    case 0x23:
        return "SetProperty";
    case 0x24:
        return "CloneSprite";
    case 0x25:
        return "RemoveSprite";
    case 0x26:
        return "Trace";
    case 0x29:
        return "StringLess";
    case 0x30:
        return "GetRandom";
    case 0x34:
        return "GetTime";
    case 0x81:
        return "GotoFrame";
    case 0x83:
        return "GetURL";
    case 0x8b:
        return "SetTarget";
    case 0x8c:
        return "GotoLabel";
    case 0x8d:
        return "WaitForFrame";
    case 0x96:
        return "Push";
    case 0x99:
        return "Jump";
    case 0x9a:
        return "GetURL2";
    case 0x9d:
        return "If";
    case 0x9e:
        return "Call";
    case 0x9f:
        return "GotoFrame2";
    default:
        return nullptr;
    }
}

// Action streams use 2-byte opcodes; actions >= 0x80 carry a u16 length and
// data padded to an even size
static void DumpActions( FILE *pOutput,
                         const std::vector<uint8_t> &actions,
                         const int32_t indent ) {
    size_t cursor = 0;
    while ( cursor + 2 <= actions.size() ) {
        const uint16_t opcode =
            Read<uint16_t>( actions.data(), uint32_t( cursor ) );
        cursor += 2;
        if ( opcode == 0 ) {
            return;
        }

        const char *pName = GetActionName( uint8_t( opcode ) );
        fprintf( pOutput, "%*s", indent, "" );
        if ( pName != nullptr ) {
            fprintf( pOutput, "%s", pName );
        } else {
            fprintf( pOutput, "Action 0x%x", opcode );
        }

        if ( opcode >= 0x80 && cursor + 2 <= actions.size() ) {
            const uint16_t length =
                Read<uint16_t>( actions.data(), uint32_t( cursor ) );
            cursor += 2;
            const size_t end = Min( cursor + length, actions.size() );
            const char *pData = (const char *)actions.data() + cursor;
            switch ( opcode ) {
            case 0x96:
                for ( size_t i = cursor; i < end; ) {
                    const uint8_t type = actions[i++];
                    if ( type == 0 ) {
                        const char *pString = (const char *)actions.data() + i;
                        const size_t stringLength = strnlen( pString, end - i );
                        fprintf( pOutput, " '%.*s'", int( stringLength ),
                                 pString );
                        i += stringLength + 1;
                    } else if ( type == 1 && i + 4 <= end ) {
                        fprintf( pOutput, " %g",
                                 Read<float>( actions.data(), uint32_t( i ) ) );
                        i += 4;
                    } else {
                        fprintf( pOutput, " <type %u>", type );
                        break;
                    }
                }
                break;
            case 0x83:
            case 0x8b:
            case 0x8c:
                fprintf( pOutput, " '%.*s'",
                         int( strnlen( pData, end - cursor ) ), pData );
                break;
            case 0x99:
            case 0x9d:
                if ( length >= 2 ) {
                    fprintf(
                        pOutput, " %+d",
                        Read<int16_t>( actions.data(), uint32_t( cursor ) ) );
                }
                break;
            case 0x81:
                if ( length >= 2 ) {
                    fprintf(
                        pOutput, " %u",
                        Read<uint16_t>( actions.data(), uint32_t( cursor ) ) );
                }
                break;
            default:
                break;
            }
            cursor += length + ( length & 1 );
        }
        fprintf( pOutput, "\n" );
    }
}

// Button condition records start at tag +0x10 + 4: {u16 size to the next
// record (0 = last), u16 condition mask, actions} (see FUN_0044b3f0)
static void DumpButtonActions( FILE *pOutput,
                               const std::vector<uint8_t> &payload,
                               const int32_t indent ) {
    static constexpr size_t kTagHeaderSize = 8;
    if ( payload.size() < 0x10 + 4 - kTagHeaderSize ) {
        return;
    }

    size_t cursor = Read<uint32_t>( payload.data(), 0x10 - kTagHeaderSize ) +
                    4 - kTagHeaderSize;
    while ( cursor + 4 <= payload.size() ) {
        const uint16_t recordSize =
            Read<uint16_t>( payload.data(), uint32_t( cursor ) );
        const uint16_t condition =
            Read<uint16_t>( payload.data(), uint32_t( cursor + 2 ) );
        const size_t end =
            recordSize != 0 ? cursor + recordSize : payload.size();
        fprintf( pOutput, "%*sCondition 0x%04x (key %u)\n", indent, "",
                 condition, condition >> 9 );
        DumpActions( pOutput,
                     std::vector<uint8_t>( payload.begin() + cursor + 4,
                                           payload.begin() +
                                               std::min( end, payload.size() ) ),
                     indent + 4 );
        if ( recordSize == 0 ) {
            break;
        }
        cursor = end;
    }
}

static void DumpSprite( FILE *pOutput, const VMFTag &sprite, int32_t indent ) {
    fprintf( pOutput, "%*sDefineSprite id=%u frames=%u depths=%u\n", indent, "",
             sprite.CharacterIndex, sprite.getNumFrames(), sprite.NumDepths );

    indent += 2;
    uint32_t frameIndex = 1;
    for ( const VMFTag &child : sprite.Children ) {
        if ( child.isSprite() ) {
            DumpSprite( pOutput, child, indent );
            continue;
        }

        const uint8_t *pPayload = child.Payload.data();
        const size_t payloadSize = child.Payload.size();
        switch ( child.TagId ) {
        case kVMFTagShowFrame:
            fprintf( pOutput, "%*sShowFrame (end of frame %u)\n", indent, "",
                     frameIndex++ );
            break;
        case 0x0c:
            fprintf( pOutput, "%*sDoAction size=%zu\n", indent, "",
                     payloadSize );
            DumpActions( pOutput, child.Payload, indent + 4 );
            break;
        case 0x2b: {
            const std::string label(
                (const char *)pPayload,
                strnlen( (const char *)pPayload, payloadSize ) );
            fprintf( pOutput, "%*sFrameLabel '%s'\n", indent, "",
                     label.c_str() );
            break;
        }
        case kVMFTagPlaceObject2: {
            VMFPlacement placement;
            if ( !placement.decode( child.Payload ) ) {
                fprintf( pOutput, "%*sPlaceObject2 (undecodable)\n", indent,
                         "" );
                break;
            }
            fprintf( pOutput, "%*sPlaceObject2 depth=%u flags=0x%x", indent, "",
                     placement.Depth, placement.Flags );
            if ( placement.Flags & VMFPlacement::PF_HasCharacter ) {
                fprintf( pOutput, " id=%u", placement.CharacterIndex );
            }
            if ( placement.Flags & VMFPlacement::PF_HasName ) {
                fprintf( pOutput, " name='%s'", placement.Name.c_str() );
            }
            if ( placement.Flags & VMFPlacement::PF_HasTranslate ) {
                fprintf( pOutput, " at=(%.0f,%.0f)", placement.Translate[0],
                         placement.Translate[1] );
            }
            if ( placement.Flags & VMFPlacement::PF_HasScale ) {
                fprintf( pOutput, " scale=(%.3f,%.3f)", placement.Scale[0],
                         placement.Scale[1] );
            }
            fprintf( pOutput, "\n" );
            break;
        }
        case 0x1c:
            fprintf( pOutput, "%*s%s depth=%u size=%zu\n", indent, "",
                     GetTagName( child.TagId ),
                     payloadSize >= 2 ? Read<uint16_t>( pPayload ) : 0u,
                     payloadSize );
            break;
        case 0x07:
            fprintf( pOutput, "%*s%s id=%u size=%zu\n", indent, "",
                     GetTagName( child.TagId ),
                     payloadSize >= 2 ? Read<uint16_t>( pPayload ) : 0u,
                     payloadSize );
            DumpButtonActions( pOutput, child.Payload, indent + 4 );
            break;
        case 0x02:
        case 0x0a:
        case 0x0b:
        case 0x0e:
        case 0x14:
        case 0x25:
        case 0x2e:
        case 0x40:
            fprintf( pOutput, "%*s%s id=%u size=%zu\n", indent, "",
                     GetTagName( child.TagId ),
                     payloadSize >= 2 ? Read<uint16_t>( pPayload ) : 0u,
                     payloadSize );
            break;
        default:
            fprintf( pOutput, "%*s%s (0x%x) size=%zu\n", indent, "",
                     GetTagName( child.TagId ), child.TagId, payloadSize );
            break;
        }
    }
}

static void
Dump( FILE *pOutput, const Movie &movie, const VMFDocument &document ) {
    fprintf( pOutput,
             "%s: version %u.%u, stage %.0fx%.0f twips, %u fps, %u characters, "
             "%u display list entries, %u sprite instances, %zu atlas "
             "entries\n",
             movie.Name.c_str(), document.VersionMajor, document.VersionMinor,
             document.FrameSize[1] - document.FrameSize[0],
             document.FrameSize[3] - document.FrameSize[2], document.FrameRate,
             document.NumCharacters, document.NumDisplayList,
             document.NumSprites, document.AtlasEntries.size() );
    DumpSprite( pOutput, document.Root, 0 );
}

static void CheckPlacements( const VMFTag &sprite,
                             size_t &numPlacements,
                             size_t &numBadPlacements ) {
    for ( const VMFTag &child : sprite.Children ) {
        if ( child.isSprite() ) {
            CheckPlacements( child, numPlacements, numBadPlacements );
        } else if ( child.TagId == kVMFTagPlaceObject2 ) {
            VMFPlacement placement;
            numPlacements++;
            if ( !placement.decode( child.Payload ) ||
                 placement.encode() != child.Payload ) {
                numBadPlacements++;
            }
        }
    }
}

// Every character a placement or a button uses must be defined
static size_t CountMissingReferences( const VMFDocument &document,
                                      const std::string &name ) {
    std::unordered_map<uint16_t, uint32_t> definedTypes;
    for ( const VMFTag &child : document.Root.Children ) {
        uint32_t index = 0;
        if ( child.getCharacterIndex( index ) ) {
            definedTypes[uint16_t( index )] = child.TagId;
        }
    }

    size_t numMissing = 0;
    auto check = [&]( uint16_t index, const char *pUser, uint32_t userIndex ) {
        if ( definedTypes.count( index ) == 0 ) {
            if ( numMissing++ < 8 ) {
                OTDU_LOG_ALWAYS( "  %s: %s %u uses undefined character %u\n",
                                 name.c_str(), pUser, userIndex, index );
            }
        }
    };

    std::vector<const VMFTag *> sprites = { &document.Root };
    for ( const VMFTag &child : document.Root.Children ) {
        uint32_t index = 0;
        if ( child.isSprite() ) {
            sprites.push_back( &child );
        } else if ( child.TagId == 0x07 && child.getCharacterIndex( index ) ) {
            const uint16_t shape = Read<uint16_t>( child.Payload.data(), 0x10 );
            check( shape, "button", index );
            if ( definedTypes.count( shape ) != 0 &&
                 definedTypes[shape] != 0x02 ) {
                OTDU_LOG_ALWAYS( "  %s: button %u uses %s %u\n", name.c_str(),
                                 index, GetTagName( definedTypes[shape] ),
                                 shape );
            }
        }
    }

    for ( const VMFTag *pSprite : sprites ) {
        for ( const VMFTag &child : pSprite->Children ) {
            VMFPlacement placement;
            if ( child.TagId == kVMFTagPlaceObject2 &&
                 placement.decode( child.Payload ) &&
                 ( placement.Flags & VMFPlacement::PF_HasCharacter ) != 0 ) {
                check( placement.CharacterIndex, "sprite",
                       pSprite->CharacterIndex );
            }
        }
    }
    return numMissing;
}

static bool Lift( const Movie &movie,
                  const VMFDocument &document,
                  const std::filesystem::path &outputPath ) {
    VMFMovieBuilder builder( document );
    std::stringstream instances( gInstances );
    std::string instance;
    while ( std::getline( instances, instance, ',' ) ) {
        if ( !builder.addRootInstance( instance.c_str() ) ) {
            OTDU_LOG_ALWAYS( "FAIL %s: %s\n", movie.Name.c_str(),
                             builder.getError().c_str() );
            return false;
        }
    }

    const VMFDocument lifted = builder.build();
    const std::vector<uint8_t> data = lifted.serialize();

    VMFDocument reparsed;
    if ( !reparsed.parse( data.data(), data.size() ) ||
         !reparsed.getWarnings().empty() ) {
        OTDU_LOG_ALWAYS( "FAIL %s: the lifted movie does not parse back\n",
                         movie.Name.c_str() );
        return false;
    }
    if ( CountMissingReferences( reparsed, "lifted" ) != 0 ) {
        return false;
    }

    std::map<std::string, std::pair<size_t, size_t>> types;
    for ( const VMFTag &child : reparsed.Root.Children ) {
        auto &stats = types[GetTagName( child.TagId )];
        stats.first++;
        stats.second += child.Payload.size();
    }
    for ( const auto &[typeName, stats] : types ) {
        OTDU_LOG_ALWAYS( "  %-20s %5zu tags %8zu bytes\n", typeName.c_str(),
                         stats.first, stats.second );
    }

    std::ofstream file( outputPath, std::ios::binary );
    file.write( (const char *)data.data(), data.size() );
    OTDU_LOG_ALWAYS( "OK   %s -> %s (%zu of %zu bytes)\n", movie.Name.c_str(),
                     outputPath.string().c_str(), data.size(),
                     movie.Data.size() );
    return true;
}

static bool RoundTrip( const Movie &movie, const VMFDocument &document ) {
    for ( const std::string &warning : document.getWarnings() ) {
        OTDU_LOG_ALWAYS( "  %s: %s\n", movie.Name.c_str(), warning.c_str() );
    }

    const uint16_t numCharacters = document.computeNumCharacters();
    if ( numCharacters != document.NumCharacters ) {
        OTDU_LOG_ALWAYS( "  %s: header has %u characters, tree needs %u\n",
                         movie.Name.c_str(), document.NumCharacters,
                         numCharacters );
    }

    size_t numPlacements = 0;
    size_t numBadPlacements = 0;
    CheckPlacements( document.Root, numPlacements, numBadPlacements );
    if ( numBadPlacements != 0 ) {
        OTDU_LOG_ALWAYS( "FAIL %s: %zu of %zu placements do not re-encode\n",
                         movie.Name.c_str(), numBadPlacements, numPlacements );
        return false;
    }

    if ( CountMissingReferences( document, movie.Name ) != 0 ) {
        OTDU_LOG_ALWAYS( "FAIL %s: undefined characters\n",
                         movie.Name.c_str() );
        return false;
    }

    const std::vector<uint8_t> output = document.serialize();
    const size_t commonSize = Min( output.size(), movie.Data.size() );
    const auto mismatch = std::mismatch(
        output.begin(), output.begin() + commonSize, movie.Data.begin() );
    if ( mismatch.first == output.begin() + commonSize &&
         output.size() == movie.Data.size() ) {
        OTDU_LOG_ALWAYS( "OK   %s (%zu bytes)\n", movie.Name.c_str(),
                         output.size() );
        return true;
    }

    OTDU_LOG_ALWAYS( "FAIL %s: %zu bytes written, %zu read, first difference "
                     "at 0x%zx\n",
                     movie.Name.c_str(), output.size(), movie.Data.size(),
                     size_t( mismatch.first - output.begin() ) );
    return false;
}

// Rewrites entry 0 of every bank with its own content; must be a no-op
static int32_t CheckBanks( const std::filesystem::path &input ) {
    std::vector<std::filesystem::path> files;
    std::error_code errorCode;
    if ( std::filesystem::is_directory( input, errorCode ) ) {
        for ( const auto &entry : std::filesystem::recursive_directory_iterator(
                  input, errorCode ) ) {
            std::string extension = entry.path().extension().string();
            std::transform( extension.begin(), extension.end(),
                            extension.begin(), ::tolower );
            if ( entry.is_regular_file() && extension == ".bnk" ) {
                files.push_back( entry.path() );
            }
        }
    } else {
        files.push_back( input );
    }

    size_t numChecked = 0;
    size_t numSkipped = 0;
    size_t numFailed = 0;
    for ( const std::filesystem::path &file : files ) {
        std::vector<uint8_t> bank;
        BankHeader header;
        if ( !ReadFile( file, bank ) || bank.size() < 8 + sizeof( header ) ) {
            numSkipped++;
            continue;
        }
        std::memcpy( &header, bank.data() + 8, sizeof( header ) );
        if ( Read<uint32_t>( header.Magic ) != kBankMagic ||
             header.NumEntries < 1 ) {
            numSkipped++;
            continue;
        }

        BankSection entry;
        std::memcpy( &entry, bank.data() + header.SectionOffsetTable + 8,
                     sizeof( entry ) );
        const std::vector<uint8_t> content( bank.begin() + entry.Offset,
                                            bank.begin() + entry.Offset +
                                                entry.Size );

        std::vector<uint8_t> output;
        std::string error;
        numChecked++;
        if ( !ReplaceBankEntry( bank, 0, content, output, error ) ) {
            if ( numFailed++ < 10 ) {
                OTDU_LOG_ALWAYS( "FAIL %s: %s\n", file.string().c_str(),
                                 error.c_str() );
            }
            continue;
        }
        if ( output != bank ) {
            size_t offset = 0;
            while ( offset < Min( output.size(), bank.size() ) &&
                    output[offset] == bank[offset] ) {
                offset++;
            }
            if ( numFailed++ < 10 ) {
                OTDU_LOG_ALWAYS( "FAIL %s: %zu bytes written, %zu read, first "
                                 "difference at 0x%zx\n",
                                 file.string().c_str(), output.size(),
                                 bank.size(), offset );
            }
        }
    }

    OTDU_LOG_ALWAYS( "%zu bank(s) rewritten, %zu skipped, %zu failed\n",
                     numChecked, numSkipped, numFailed );
    return numFailed == 0 ? 0 : 1;
}

int main( int argc, char *argv[] ) {
    memset( gpInputPath, 0, sizeof( gpInputPath ) );
    memset( gpOutputPath, 0, sizeof( gpOutputPath ) );

    GetArgParserRegister().parseCmdLineArgs( (const char **)argv, argc );

    if ( *gpInputPath == '\0' ) {
        OTDU_LOG_ALWAYS( "Missing cmdline arg 'input'!\n" );
        PrintUsage();
    }
    if ( gMode == "bankcheck" ) {
        return CheckBanks( gpInputPath );
    }
    if ( gMode != "roundtrip" && gMode != "dump" && gMode != "extract" &&
         gMode != "lift" ) {
        OTDU_LOG_ALWAYS( "Unknown mode '%s'\n", gMode.c_str() );
        PrintUsage();
    }

    const std::filesystem::path outputPath( gpOutputPath );
    if ( gMode == "extract" && outputPath.empty() ) {
        OTDU_LOG_ALWAYS( "Mode 'extract' needs an -output folder\n" );
        PrintUsage();
    }
    if ( gMode == "lift" && ( outputPath.empty() || gInstances.empty() ) ) {
        OTDU_LOG_ALWAYS( "Mode 'lift' needs -instances and an -output file\n" );
        PrintUsage();
    }

    std::error_code errorCode;
    if ( !outputPath.empty() && gMode != "lift" ) {
        std::filesystem::create_directories( outputPath, errorCode );
    }

    const std::vector<Movie> movies = CollectMovies( gpInputPath );
    if ( movies.empty() ) {
        OTDU_LOG_ALWAYS( "No .vmf movie found in '%s'\n", gpInputPath );
        return 1;
    }

    size_t numFailed = 0;
    for ( const Movie &movie : movies ) {
        if ( gMode == "extract" ) {
            const std::filesystem::path path =
                outputPath / ( movie.Name + ".vmf" );
            std::ofstream file( path, std::ios::binary );
            file.write( (const char *)movie.Data.data(), movie.Data.size() );
            OTDU_LOG_ALWAYS( "%s\n", path.string().c_str() );
            continue;
        }

        VMFDocument document;
        if ( !document.parse( movie.Data.data(), movie.Data.size() ) ) {
            OTDU_LOG_ALWAYS( "FAIL %s: %s\n", movie.Name.c_str(),
                             document.getError().c_str() );
            numFailed++;
            continue;
        }

        if ( gMode == "lift" ) {
            numFailed += Lift( movie, document, outputPath ) ? 0 : 1;
            break;
        }

        if ( gMode == "dump" ) {
            FILE *pOutput = stdout;
            if ( !outputPath.empty() ) {
                const std::filesystem::path path =
                    outputPath / ( movie.Name + ".txt" );
                pOutput = fopen( path.string().c_str(), "w" );
                if ( pOutput == nullptr ) {
                    OTDU_LOG_ALWAYS( "Cannot write '%s'\n",
                                     path.string().c_str() );
                    numFailed++;
                    continue;
                }
            }
            Dump( pOutput, movie, document );
            if ( pOutput != stdout ) {
                fclose( pOutput );
            }
        } else if ( !RoundTrip( movie, document ) ) {
            numFailed++;
        }
    }

    OTDU_LOG_ALWAYS( "%zu movie(s), %zu failed\n", movies.size(), numFailed );
    return numFailed == 0 ? 0 : 1;
}
