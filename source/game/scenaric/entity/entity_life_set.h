#pragma once

#include "base_entity.h"

class MngLifeSet;
class EntityCinematic;

class EntityLifeSet : public Entity {
public:
    inline bool isLoaded() const { return bLoaded; }
    inline bool isPlaying() const { return bPlaying; }
    inline uint32_t getCinematicGroupCount() const {
        return bank.NumLifeSetCinematics;
    }
    inline uint32_t getMinPlayed() const { return cinematicIndexRange[0]; }
    inline uint32_t debugOrientationMask() const {
        uint32_t mask = 0;
        for ( uint32_t g = 0; g < bank.NumLifeSetCinematics; g++ ) {
            const LifeSetCinematic &grp = bank.pLifeSetCinematics[g];
            for ( uint32_t c = 0; c < grp.NumLifeCinematics; c++ ) {
                mask |= grp.pLifeCinematics[c].Orientation;
            }
        }
        return mask;
    }
    inline void setOrientation( uint32_t orientation ) { flags = orientation; }
    inline void markInterior() { bLoaded = true; }

public:
    EntityLifeSet();
    ~EntityLifeSet();

    void initialize() override;
    void loadResources( ScenaricBank *pBank ) override;
    void unknownFunc() override;
    void unloadResources() override;
    void tick( PlayerGameMech *pGameMech,
               float deltaTime,
               float totalTime ) override;
    void deserialize( BinaryReader *pReader ) override;
    void instanciate( MngActor *pMngActor, Entity *param_1 ) override;
    void deinstanciate() override;
    void unknownFunc2( void *param_2 ) override;

    // Playback control, driven by MngLifeSet.
    uint32_t startPlaying( MngLifeSet *pMngLifeSet, uint32_t budget );
    uint32_t stopPlaying();

private:
    // TODO: Move this
    // Represents a *.cin file
    struct SerializedCinematic {
        EntityCinematic *pBinRefCinematic = nullptr;
    };

    struct SerializedPosition {
        SerializedTransform *pPosMatrix = nullptr;
    };

    struct LifeCinematic {
        bool bFocusOn = false;
        bool bRandomTimeStart = false;
        bool bAlignToTerrain = false;
        uint32_t Orientation = 0;
        SerializedPosition pPositions[10];
        uint32_t NumPositions = 0;
        SerializedCinematic CinematicResource;
        int32_t AvatarStyleIndex = 0;

        void deserialize( BinaryReader *pReader );
    };

    struct LifeSetCinematic {
        LifeCinematic pLifeCinematics[10];
        uint32_t NumLifeCinematics = 0;

        void prepare( ScenaricBank *pBank );
    };

    struct LifeSetBank {
        LifeSetCinematic pLifeSetCinematics[10];
        uint32_t NumLifeSetCinematics = 0;

        LifeSetBank();
        void deserialize( BinaryReader *pReader );
        void prepare( ScenaricBank *pBank );
    };

private:
    bool bLoaded;
    bool bPlaying;
    uint32_t cinematicIndexRange[2];
    LifeSetBank bank;
    uint32_t flags;
    EntityCinematic *pCurrentlyPlayingCinematics[10];
    uint32_t numCinematicsPlaying;

private:
    bool startPlayingLifeCinematic( LifeCinematic *pLifeCinematic,
                                    MngLifeSet *pMngLifeSet );
};
