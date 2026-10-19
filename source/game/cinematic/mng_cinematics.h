#pragma once

#include "game/mng_base.h"
#include "world/gs_world_streaming.h"

class Entity;
class EntityCinematic;
class TestDriveGameInstance;

enum eCinematicType : int32_t {
    CT_All = 0,
    CT_Background = 1, // shots flagged 0x10
    CT_Foreground = 2,
};

class MngCinematics : public Manager, public StreamingCallback {
public:
    const char *getName() const override { /* FUN_00430be0 */
        return "MngCinematics";
    }
    inline bool isValid() const { return bIsValid; }
    inline EntityCinematic *getActiveCinematic() const {
        return pActiveCinematic;
    }

public:
    MngCinematics();
    ~MngCinematics();

    bool initialize( TestDriveGameInstance *pInstance ) override;
    void tickCinematics( float deltaTime, float totalTime );
    void pause() override;

    void addPlayingCinematic( EntityCinematic *pCinematic );
    EntityCinematic *findFocusedPlayingCinematic() const;
    bool isCinematicPlaying( eCinematicType type,
                             bool bRequireAudio,
                             bool bIncludeRequested ) const;
    void removeCinematic( EntityCinematic *pCinematic );

    bool onWorldStreamed( Eigen::Vector4f *param_1,
                          Eigen::Vector4f *param_2,
                          Eigen::Vector4f *param_3 ) override;

private:
    static constexpr int32_t kMaxCinematics = 12;

private:
    std::array<EntityCinematic *, kMaxCinematics> pCinematics;
    EntityCinematic *pActiveCinematic;
    bool bIsValid;
    int32_t numCinematics;
};

extern MngCinematics *gpMngCinematics;

void StopActiveForegroundCinematic();
void ProjectActorOnGround( Entity *pActor,
                           bool bRebaseOnWorldShift,
                           bool bQueryPhysics );
