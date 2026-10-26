#pragma once

#include <functional>
#include <set>
#include <unordered_map>

#include "vmf_document.h"

class VMFMovieBuilder {
public:
    inline const std::string &getError() const { return error; }

public:
    explicit VMFMovieBuilder( const VMFDocument &source );

    // Copies the placement that first puts 'pInstanceName' on the source's
    // root timeline (same depth, matrix and name), with its character.
    bool addRootInstance( const char *pInstanceName );
    void addPlacement( const VMFPlacement &placement );
    // Copies the frame scripts of the source's root frame 'frameIndex'
    // (1-based)
    void addRootFrameActions( uint16_t frameIndex );
    // Labels the frame 'frameOffset' frames past the one of 'pExistingLabel'
    // in the sprite found at 'pSpritePath' (instance names from the root,
    // e.g. "menus/ss_menus")
    bool addFrameLabel( const char *pSpritePath,
                        const char *pExistingLabel,
                        const int32_t frameOffset,
                        const char *pLabel );

    // The placements below are the ones that put a character on the timeline
    // of the sprite at 'pSpritePath', first one at or past the frame of
    // 'pFrameLabel'.
    bool findPlacement( const char *pSpritePath,
                        const char *pFrameLabel,
                        const char *pInstanceName,
                        VMFPlacement &outPlacement );
    // Replaces the placement at 'depth', or removes it when 'pPlacement' is
    // null
    bool setPlacement( const char *pSpritePath,
                       const char *pFrameLabel,
                       const uint16_t depth,
                       const VMFPlacement *pPlacement );

    // Display list of the sprite at 'pSpritePath' once the frame
    // 'frameOffset' frames past 'pFrameLabel' is shown, one placement per
    // depth (character, matrix, color transform and name merged across moves)
    bool snapshotFrame( const char *pSpritePath,
                        const char *pFrameLabel,
                        const int32_t frameOffset,
                        std::vector<VMFPlacement> &outPlacements );
    // New one-frame sprite placing 'placements' (then stopping); returns its
    // character index
    uint16_t addSprite( const std::vector<VMFPlacement> &placements );
    // Tag id of the source definition of 'characterIndex' (0 if unknown)
    uint32_t getDefinitionTagId( const uint16_t characterIndex ) const;

    VMFDocument build() const;

    // Characters reached from 'characterIndex' (itself included)
    void collectDependencies( uint16_t characterIndex,
                              std::set<uint16_t> &outCharacters ) const;

private:
    struct FrameLabel {
        uint16_t CharacterIndex;
        uint16_t FrameIndex; // 1-based
        std::string Label;
    };

    struct PlacementEdit {
        uint16_t CharacterIndex;
        size_t ChildIndex;
        bool bRemove;
        VMFPlacement Placement;
    };

private:
    const VMFTag *findSprite( const char *pSpritePath ) const;
    const VMFTag *findSpritePlacement(
        const char *pSpritePath,
        const char *pFrameLabel,
        const std::function<bool( const VMFPlacement & )> &match,
        size_t &outChildIndex );

private:
    const VMFDocument &source;
    std::unordered_map<uint16_t, const VMFTag *> definitions;
    std::vector<VMFPlacement> placements;
    std::vector<const VMFTag *> actions;
    std::vector<FrameLabel> frameLabels;
    std::vector<PlacementEdit> placementEdits;
    std::vector<VMFTag> addedSprites;
    std::string error;
};
