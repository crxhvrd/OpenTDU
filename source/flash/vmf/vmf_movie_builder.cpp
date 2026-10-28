#include "shared.h"
#include "vmf_movie_builder.h"

#include "core/binary_helpers.h"

#include <map>
#include <sstream>

static constexpr uint32_t kTagDoAction = 0x0c;
static constexpr uint32_t kTagSetBackgroundColor = 0x09;
static constexpr uint32_t kTagDefineButton = 0x07;
static constexpr uint32_t kTagFrameLabel = 0x2b;
static constexpr uint32_t kTagRemoveObject2 = 0x1c;

// The button's shape (+0x18 on the tag, see FlashUnknown draw)
static constexpr size_t kButtonShapeOffset = 0x10;

static bool IsLeafResource( const uint32_t tagId ) {
    switch ( tagId ) {
    case 0x0a: // DefineFont
    case 0x0e: // DefineSound
    case 0x14: // DefineBitsLossless
    case 0x40: // Material reference
        return true;
    default:
        return false;
    }
}

static std::string GetFrameLabel( const VMFTag &tag ) {
    const char *pLabel = (const char *)tag.Payload.data();
    return std::string( pLabel, strnlen( pLabel, tag.Payload.size() ) );
}

static VMFTag CreateFrameLabel( const std::string &label ) {
    VMFTag tag;
    tag.TagId = kTagFrameLabel;
    tag.Payload.assign( label.begin(), label.end() );
    tag.Payload.resize( Align( label.size() + 1, size_t( 4 ) ), 0 );
    return tag;
}

static const VMFTag *FindNamedPlacement( const VMFTag &sprite,
                                         const std::string &name,
                                         VMFPlacement &outPlacement ) {
    for ( const VMFTag &child : sprite.Children ) {
        if ( child.TagId == kVMFTagPlaceObject2 &&
             outPlacement.decode( child.Payload ) &&
             ( outPlacement.Flags & VMFPlacement::PF_HasCharacter ) != 0 &&
             outPlacement.Name == name ) {
            return &child;
        }
    }
    return nullptr;
}

static void CollectPlacedCharacters( const VMFTag &sprite,
                                     std::vector<uint16_t> &outCharacters ) {
    for ( const VMFTag &child : sprite.Children ) {
        if ( child.TagId != kVMFTagPlaceObject2 ) {
            continue;
        }

        VMFPlacement placement;
        if ( placement.decode( child.Payload ) &&
             ( placement.Flags & VMFPlacement::PF_HasCharacter ) != 0 ) {
            outCharacters.push_back( placement.CharacterIndex );
        }
    }
}

VMFMovieBuilder::VMFMovieBuilder( const VMFDocument &source )
    : source( source ) {
    for ( const VMFTag &child : source.Root.Children ) {
        uint32_t index = 0;
        if ( child.getCharacterIndex( index ) ) {
            definitions.emplace( uint16_t( index ), &child );
        }
    }
}

bool VMFMovieBuilder::addRootInstance( const char *pInstanceName ) {
    for ( const VMFTag &child : source.Root.Children ) {
        if ( child.TagId != kVMFTagPlaceObject2 ) {
            continue;
        }

        VMFPlacement placement;
        if ( placement.decode( child.Payload ) &&
             ( placement.Flags & VMFPlacement::PF_HasCharacter ) != 0 &&
             placement.Name == pInstanceName ) {
            placements.push_back( placement );
            return true;
        }
    }

    error = std::string( "no root placement named '" ) + pInstanceName + "'";
    return false;
}

void VMFMovieBuilder::addPlacement( const VMFPlacement &placement ) {
    placements.push_back( placement );
}

void VMFMovieBuilder::addRootFrameActions( uint16_t frameIndex ) {
    uint16_t currentFrame = 1;
    for ( const VMFTag &child : source.Root.Children ) {
        if ( child.TagId == kVMFTagShowFrame ) {
            currentFrame++;
        } else if ( child.TagId == kTagDoAction &&
                    currentFrame == frameIndex ) {
            actions.push_back( &child );
        }
    }
}

const VMFTag *VMFMovieBuilder::findSprite( const char *pSpritePath ) const {
    const VMFTag *pSprite = &source.Root;
    std::istringstream path( pSpritePath );
    std::string name;
    while ( std::getline( path, name, '/' ) ) {
        VMFPlacement placement;
        if ( FindNamedPlacement( *pSprite, name, placement ) == nullptr ) {
            return nullptr;
        }

        auto it = definitions.find( placement.CharacterIndex );
        if ( it == definitions.end() || !it->second->isSprite() ) {
            return nullptr;
        }
        pSprite = it->second;
    }
    return pSprite;
}

bool VMFMovieBuilder::addFrameLabel( const char *pSpritePath,
                                     const char *pExistingLabel,
                                     const int32_t frameOffset,
                                     const char *pLabel ) {
    const VMFTag *pSprite = findSprite( pSpritePath );
    if ( pSprite == nullptr || pSprite == &source.Root ) {
        error = std::string( "no sprite at '" ) + pSpritePath + "'";
        return false;
    }

    int32_t frameIndex = 1;
    for ( const VMFTag &child : pSprite->Children ) {
        if ( child.TagId == kVMFTagShowFrame ) {
            frameIndex++;
        } else if ( child.TagId == kTagFrameLabel &&
                    GetFrameLabel( child ) == pExistingLabel ) {
            const int32_t labelFrame = frameIndex + frameOffset;
            if ( labelFrame < 1 || labelFrame > pSprite->getNumFrames() ) {
                error = std::string( "frame out of '" ) + pSpritePath + "'";
                return false;
            }

            frameLabels.push_back( { uint16_t( pSprite->CharacterIndex ),
                                     uint16_t( labelFrame ), pLabel } );
            return true;
        }
    }

    error = std::string( "'" ) + pSpritePath + "' has no label '" +
            pExistingLabel + "'";
    return false;
}

const VMFTag *VMFMovieBuilder::findSpritePlacement(
    const char *pSpritePath,
    const char *pFrameLabel,
    const std::function<bool( const VMFPlacement & )> &match,
    size_t &outChildIndex ) {
    const VMFTag *pSprite = findSprite( pSpritePath );
    if ( pSprite == nullptr || pSprite == &source.Root ) {
        error = std::string( "no sprite at '" ) + pSpritePath + "'";
        return nullptr;
    }

    bool bPastLabel = false;
    for ( size_t i = 0; i < pSprite->Children.size(); i++ ) {
        const VMFTag &child = pSprite->Children[i];
        if ( child.TagId == kTagFrameLabel &&
             GetFrameLabel( child ) == pFrameLabel ) {
            bPastLabel = true;
        }

        VMFPlacement placement;
        if ( bPastLabel && child.TagId == kVMFTagPlaceObject2 &&
             placement.decode( child.Payload ) &&
             ( placement.Flags & VMFPlacement::PF_HasCharacter ) != 0 &&
             match( placement ) ) {
            outChildIndex = i;
            return pSprite;
        }
    }

    error = std::string( "no matching placement past '" ) + pFrameLabel +
            "' in '" + pSpritePath + "'";
    return nullptr;
}

bool VMFMovieBuilder::findPlacement( const char *pSpritePath,
                                     const char *pFrameLabel,
                                     const char *pInstanceName,
                                     VMFPlacement &outPlacement ) {
    size_t childIndex = 0;
    const VMFTag *pSprite = findSpritePlacement(
        pSpritePath, pFrameLabel,
        [&]( const VMFPlacement &placement ) {
            return placement.Name == pInstanceName;
        },
        childIndex );
    return pSprite != nullptr &&
           outPlacement.decode( pSprite->Children[childIndex].Payload );
}

bool VMFMovieBuilder::setPlacement( const char *pSpritePath,
                                    const char *pFrameLabel,
                                    const uint16_t depth,
                                    const VMFPlacement *pPlacement ) {
    size_t childIndex = 0;
    const VMFTag *pSprite = findSpritePlacement(
        pSpritePath, pFrameLabel,
        [&]( const VMFPlacement &placement ) {
            return placement.Depth == depth;
        },
        childIndex );
    if ( pSprite == nullptr ) {
        return false;
    }

    PlacementEdit edit;
    edit.CharacterIndex = uint16_t( pSprite->CharacterIndex );
    edit.ChildIndex = childIndex;
    edit.bRemove = ( pPlacement == nullptr );
    if ( pPlacement != nullptr ) {
        edit.Placement = *pPlacement;
        edit.Placement.Depth = depth;
    }
    placementEdits.push_back( edit );
    return true;
}

static void MergePlacement( const VMFPlacement &move, VMFPlacement &target ) {
    if ( move.Flags & VMFPlacement::PF_HasCharacter ) {
        target.CharacterIndex = move.CharacterIndex;
    }
    if ( move.Flags & VMFPlacement::PF_HasMatrix ) {
        static constexpr uint16_t kMatrixFlags =
            VMFPlacement::PF_HasMatrix | VMFPlacement::PF_HasRotateSkew |
            VMFPlacement::PF_HasScale | VMFPlacement::PF_HasTranslate;
        target.Flags =
            ( target.Flags & ~kMatrixFlags ) | ( move.Flags & kMatrixFlags );
        std::copy( std::begin( move.Scale ), std::end( move.Scale ),
                   std::begin( target.Scale ) );
        std::copy( std::begin( move.RotateSkew ), std::end( move.RotateSkew ),
                   std::begin( target.RotateSkew ) );
        std::copy( std::begin( move.Translate ), std::end( move.Translate ),
                   std::begin( target.Translate ) );
    }
    if ( move.Flags & VMFPlacement::PF_HasColorTransform ) {
        target.Flags |= VMFPlacement::PF_HasColorTransform;
        std::copy( std::begin( move.ColorTransform ),
                   std::end( move.ColorTransform ),
                   std::begin( target.ColorTransform ) );
    }
    if ( move.Flags & VMFPlacement::PF_HasRatio ) {
        target.Flags |= VMFPlacement::PF_HasRatio;
        target.Ratio = move.Ratio;
    }
    if ( move.Flags & VMFPlacement::PF_HasName ) {
        target.Flags |= VMFPlacement::PF_HasName;
        target.Name = move.Name;
    }
    if ( move.Flags & VMFPlacement::PF_HasClipDepth ) {
        target.Flags |= VMFPlacement::PF_HasClipDepth;
        target.ClipDepth = move.ClipDepth;
    }
}

bool VMFMovieBuilder::snapshotFrame(
    const char *pSpritePath,
    const char *pFrameLabel,
    const int32_t frameOffset,
    std::vector<VMFPlacement> &outPlacements ) {
    const VMFTag *pSprite = findSprite( pSpritePath );
    if ( pSprite == nullptr || pSprite == &source.Root ) {
        error = std::string( "no sprite at '" ) + pSpritePath + "'";
        return false;
    }

    int32_t labelFrame = -1;
    int32_t frame = 1;
    for ( const VMFTag &child : pSprite->Children ) {
        if ( child.TagId == kVMFTagShowFrame ) {
            frame++;
        } else if ( child.TagId == kTagFrameLabel &&
                    GetFrameLabel( child ) == pFrameLabel ) {
            labelFrame = frame;
            break;
        }
    }
    const int32_t targetFrame = labelFrame + frameOffset;
    if ( labelFrame < 0 || targetFrame < 1 ||
         targetFrame > pSprite->getNumFrames() ) {
        error = std::string( "no frame '" ) + pFrameLabel + "' in '" +
                pSpritePath + "'";
        return false;
    }

    std::map<uint16_t, VMFPlacement> displayList;
    frame = 1;
    for ( const VMFTag &child : pSprite->Children ) {
        if ( child.TagId == kVMFTagShowFrame ) {
            if ( frame++ == targetFrame ) {
                break;
            }
        } else if ( child.TagId == kTagRemoveObject2 &&
                    child.Payload.size() >= sizeof( uint16_t ) ) {
            displayList.erase( Read<uint16_t>( child.Payload.data() ) );
        } else if ( child.TagId == kVMFTagPlaceObject2 ) {
            VMFPlacement placement;
            if ( !placement.decode( child.Payload ) ) {
                continue;
            }

            auto it = displayList.find( placement.Depth );
            if ( ( placement.Flags & VMFPlacement::PF_Move ) != 0 &&
                 it != displayList.end() ) {
                MergePlacement( placement, it->second );
            } else if ( placement.Flags & VMFPlacement::PF_HasCharacter ) {
                placement.Flags &= ~VMFPlacement::PF_Move;
                displayList[placement.Depth] = placement;
            }
        }
    }

    outPlacements.clear();
    for ( auto &entry : displayList ) {
        entry.second.Trailing.clear();
        outPlacements.push_back( entry.second );
    }
    return true;
}

uint16_t
VMFMovieBuilder::addSprite( const std::vector<VMFPlacement> &placements ) {
    uint16_t characterIndex = 0;
    for ( const auto &definition : definitions ) {
        characterIndex = std::max( characterIndex, definition.first );
    }
    characterIndex = uint16_t( characterIndex + 1 + addedSprites.size() );

    VMFTag sprite;
    sprite.TagId = kVMFTagDefineSprite;
    sprite.CharacterIndex = characterIndex;
    for ( const VMFPlacement &placement : placements ) {
        VMFTag tag;
        tag.TagId = kVMFTagPlaceObject2;
        tag.Payload = placement.encode();
        sprite.Children.push_back( std::move( tag ) );
        sprite.NumDepths =
            std::max<uint16_t>( sprite.NumDepths, placement.Depth + 1 );
    }

    VMFTag stop;
    stop.TagId = kTagDoAction;
    stop.Payload = { 0x07, 0x00, 0x00, 0x00 }; // Stop, End
    sprite.Children.push_back( std::move( stop ) );

    VMFTag showFrame;
    showFrame.TagId = kVMFTagShowFrame;
    sprite.Children.push_back( std::move( showFrame ) );

    addedSprites.push_back( std::move( sprite ) );
    return characterIndex;
}

uint32_t
VMFMovieBuilder::getDefinitionTagId( const uint16_t characterIndex ) const {
    auto it = definitions.find( characterIndex );
    return ( it != definitions.end() ) ? it->second->TagId : 0u;
}

void VMFMovieBuilder::collectDependencies(
    uint16_t characterIndex, std::set<uint16_t> &outCharacters ) const {
    std::vector<uint16_t> pending = { characterIndex };
    while ( !pending.empty() ) {
        const uint16_t index = pending.back();
        pending.pop_back();
        if ( !outCharacters.insert( index ).second ) {
            continue;
        }

        auto it = definitions.find( index );
        if ( it == definitions.end() ) {
            continue;
        }

        const VMFTag &definition = *it->second;
        if ( definition.isSprite() ) {
            CollectPlacedCharacters( definition, pending );
        } else if ( definition.TagId == kTagDefineButton &&
                    definition.Payload.size() >=
                        kButtonShapeOffset + sizeof( uint16_t ) ) {
            pending.push_back( Read<uint16_t>( definition.Payload.data(),
                                               kButtonShapeOffset ) );
        }
    }
}

VMFDocument VMFMovieBuilder::build() const {
    VMFDocument movie;
    movie.HeaderPadding = source.HeaderPadding;
    movie.VersionMajor = source.VersionMajor;
    movie.VersionMinor = source.VersionMinor;
    movie.ExporterVersion = source.ExporterVersion;
    movie.AuthorName = source.AuthorName;
    std::copy( std::begin( source.FrameSize ), std::end( source.FrameSize ),
               std::begin( movie.FrameSize ) );
    movie.FrameRate = source.FrameRate;
    movie.NumCharacters = source.NumCharacters;
    movie.NumDisplayList = source.NumDisplayList;
    movie.NumSprites = source.NumSprites;
    movie.AtlasEntries = source.AtlasEntries;

    movie.Root.TagId = source.Root.TagId;
    movie.Root.CharacterIndex = source.Root.CharacterIndex;
    movie.Root.NumDepths = source.Root.NumDepths;

    std::set<uint16_t> characters;
    for ( const VMFPlacement &placement : placements ) {
        collectDependencies( placement.CharacterIndex, characters );
    }
    for ( const PlacementEdit &edit : placementEdits ) {
        if ( !edit.bRemove ) {
            collectDependencies( edit.Placement.CharacterIndex, characters );
        }
    }
    for ( const VMFTag &sprite : addedSprites ) {
        std::vector<uint16_t> placed;
        CollectPlacedCharacters( sprite, placed );
        for ( const uint16_t characterIndex : placed ) {
            collectDependencies( characterIndex, characters );
        }
    }

    std::vector<VMFTag> &children = movie.Root.Children;
    for ( const VMFTag &child : source.Root.Children ) {
        if ( child.TagId == kTagSetBackgroundColor ) {
            children.push_back( child );
            break;
        }
    }

    for ( const VMFTag &child : source.Root.Children ) {
        uint32_t index = 0;
        if ( child.getCharacterIndex( index ) &&
             ( IsLeafResource( child.TagId ) ||
               characters.count( uint16_t( index ) ) != 0 ) ) {
            children.push_back( child );
        }
    }

    // Placed by the copied sprites (edited below), so defined after them
    children.insert( children.end(), addedSprites.begin(), addedSprites.end() );

    // Back to front, so removals keep the other child indices valid
    std::vector<PlacementEdit> edits = placementEdits;
    std::sort( edits.begin(), edits.end(),
               []( const PlacementEdit &a, const PlacementEdit &b ) {
                   return a.ChildIndex > b.ChildIndex;
               } );
    for ( const PlacementEdit &edit : edits ) {
        for ( VMFTag &child : children ) {
            if ( !child.isSprite() ||
                 child.CharacterIndex != edit.CharacterIndex ) {
                continue;
            }

            if ( edit.bRemove ) {
                child.Children.erase( child.Children.begin() +
                                      edit.ChildIndex );
            } else {
                child.Children[edit.ChildIndex].Payload =
                    edit.Placement.encode();
            }
        }
    }

    for ( const FrameLabel &frameLabel : frameLabels ) {
        for ( VMFTag &child : children ) {
            if ( !child.isSprite() ||
                 child.CharacterIndex != frameLabel.CharacterIndex ) {
                continue;
            }

            // Frame N starts past the (N-1)th ShowFrame
            auto it = child.Children.begin();
            for ( uint16_t frame = 1; frame < frameLabel.FrameIndex; frame++ ) {
                it = std::find_if( it, child.Children.end(),
                                   []( const VMFTag &tag ) {
                                       return tag.TagId == kVMFTagShowFrame;
                                   } ) +
                     1;
            }
            child.Children.insert( it, CreateFrameLabel( frameLabel.Label ) );
        }
    }

    for ( const VMFPlacement &placement : placements ) {
        VMFTag tag;
        tag.TagId = kVMFTagPlaceObject2;
        tag.Payload = placement.encode();
        children.push_back( std::move( tag ) );
    }

    for ( const VMFTag *pActions : actions ) {
        children.push_back( *pActions );
    }

    VMFTag stop;
    stop.TagId = kTagDoAction;
    stop.Payload = { 0x07, 0x00, 0x00, 0x00 }; // Stop, End
    children.push_back( std::move( stop ) );

    VMFTag showFrame;
    showFrame.TagId = kVMFTagShowFrame;
    children.push_back( std::move( showFrame ) );

    // Added sprites are indexed past the source's last character
    movie.NumCharacters =
        std::max( movie.NumCharacters, movie.computeNumCharacters() );

    return movie;
}
