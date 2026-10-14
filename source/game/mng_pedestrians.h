#pragma once

#include "game/mng_base.h"

#include "ai/ai_pedestrians.h"
#include "world/avatar_common_bnk.h"

class Sector;
class ClipInstance;
class Entity;
class MngActor;

class MngPedestrians : public Manager {
public:
    struct ClipDesc {
        const char *pName;
        float NaturalSpeed; // m/s
    };

public:
    MngPedestrians();
    ~MngPedestrians();

    bool initialize( TestDriveGameInstance * ) override;
    void tick( float deltaTime, float totalTime ) override;
    void reset() override;
    void draw() override;
    void pause() override;
    void terminate() override;

private:
    static constexpr int32_t kNumSectors = 7;

    struct SlotAnimation {
        ClipInstance *pPlaying = nullptr;
        bool bWalking = false;
        float SwitchTimer = 0.0f;
    };

    struct StreamedNav {
        Sector *pSector = nullptr;
        int32_t OriginX = 0;
        int32_t OriginZ = 0;
        bool bRegistered = false;
        std::vector<uint8_t> Blob;
    };

private:
    void streamNavigation();
    void loadNavigation( StreamedNav &nav );
    void unregisterNavigation( StreamedNav &nav );
    void releaseNavigation();
    void resolveClips();
    void updateAnimations( const float deltaTime );

private:
    StreamedNav streamedNavs[kNumSectors];
    ClipInstance *pWalkClips[AvatarCommonBnk::kNumBodyTypes][2] = {};
    ClipInstance *pStandClips[AvatarCommonBnk::kNumBodyTypes][2] = {};
    SlotAnimation slotAnimations[AiPedestrianManager::kMaxNumDisplayed];
};

extern MngPedestrians *gpMngPedestrians;
extern bool gbPedestriansEnabled;

// Tennis court prefab of the sectors .PMI (S_TNS).
static constexpr uint64_t kTennisCourtHashcode = 0x000000534e545f53;

// The tennis life set (HoteE) of the world life sets bank is never placed:
// instanciate it on a court (gbPedestriansEnabled only).
Entity *CreateCourtLifeSet( MngActor &mngActor,
                            const Matrix4x4f_t &courtTransform );
