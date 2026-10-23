#pragma once

#include "game/game_mechanism.h"
#include "game/scenaric/gs_scenaric.h"

class Entity;
class EntityAvatarPassenger;
class EntityCheckPoint;
class EntityCinematic;
class EntityExtraChallenge;
class GMMission;
class MngActor;

// GMMission extra challenge logic
class MissionExtraChallenge : public CinematicPlayer,
                              public GameMechPenaltyListener {
public:
    inline bool isOutroCinematicPending() const {
        // FUN_0073d260
        return pOutroCinematic != nullptr;
    }

public:
    explicit MissionExtraChallenge( GMMission *pOwnerGM );

    bool executeAction( int32_t action, void *pSource, void *pData ) override;
    bool onPenalty( const float remainingRatio,
                    const float pendingRatio ) override;

    void setup( MngActor *pActorManager, EntityExtraChallenge *pChallenge );
    void start();
    void stopCinematics();
    void end( const bool bWon );
    void cleanup();
    void terminate();
    bool isSucceeded() const;

private:
    void configureTrafficAndCops();
    void allocatePassenger( const int32_t avatarType );
    bool findRouteEntities();
    void placeRouteEntities();

    static void AlignFrameOnRoad( Matrix4x4f_t &frame );

private:
    GMMission *pOwner;
    EntityExtraChallenge *pChallenge;
    MngActor *pMngActor;
    EntityCheckPoint *pStartCheckpoint;
    EntityCheckPoint *pFinishCheckpoint;
    Entity *pCarSpawn;
    EntityCinematic *pOutroCinematic;
    EntityCinematic *pLoopCinematic;
    Matrix4x4f_t outroFrame;
    bool field_0x70;
    EntityAvatarPassenger *pPassenger;
    Entity *pCargo;
    bool bTeleportToStart;
    bool field_0x7d;
    bool bSavedInfAllButCollision;
    bool bInfAllButCollision;
};
