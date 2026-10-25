#pragma once

#include "ai/ai_path.h"
#include "game/mng_base.h"
#include "game/scenaric/entity/base_entity.h"
#include "game/scenaric/mng_actor.h"

// Race road closing ("forc" event parameter)
class MngBlckRds : public Manager {
public:
    const char *getName() const override { return "BlckRds"; }

    inline void setEnabled( const bool bEnabled ) { this->bEnabled = bEnabled; }
    inline bool isEnabled() const { return bEnabled; }
    inline MngActor *getMngActor() { return &mngActor; }
    inline void requestRelease() {
        // FUN_00727250
        bReleaseRequested = true;
    }
    inline void setBlockersVisible( const bool bVisible ) {
        for ( Entity *pBlocker : pBlockerPool ) {
            if ( pBlocker != nullptr ) {
                pBlocker->setVisible( bVisible );
            }
        }
    }

public:
    MngBlckRds();
    ~MngBlckRds();

    void tick( float deltaTime, float totalTime ) override;
    void pause() override;
    void terminate() override;

    void enable();

private:
    static constexpr int32_t kMaxJunctionExits = 8;
    static constexpr int32_t kMaxBlockersPerJunction = 32;
    static constexpr int32_t kNumBlockersPerSide = 100;
    static constexpr int32_t kNumBlockers = kNumBlockersPerSide * 2;
    static constexpr float kSpawnDistanceSq = 360000.0f;   // 600 m
    static constexpr float kReleaseDistanceSq = 810000.0f; // 900 m
    static constexpr float kSameExitTolerance = 0.1f;

    enum eExitState : uint8_t {
        ES_Route = 0,
        ES_BlockedLeft = 1,  // SS_VirG_act
        ES_BlockedRight = 2, // SS_VirD_act
        ES_Untouched = 3
    };

    struct JunctionExits {
        uint8_t States[kMaxJunctionExits];
        AiCompressedRoadIt Exits[kMaxJunctionExits];

        JunctionExits() {
            // LAB_007272f0
            for ( uint8_t &state : States ) {
                state = ES_Untouched;
            }
        }
    };

    struct BlockedJunction {
        JunctionExits Exits;
        Entity *pBlockers[kMaxBlockersPerJunction] = {};
        AiRoadMap::Junction *pJunction = nullptr;
        float field_0xcc = 0.0f;
        float DistanceSq = 0.0f;
        float PositionX = 0.0f;
        float PositionZ = 0.0f;
        bool bReleased = true;
    };

private:
    bool buildRoute();
    void markRouteJunctions();
    void markJunction( const int32_t junctionIndex,
                       const AiRoadIt &routeExit,
                       const AiRoadIt &routeEntry );
    void compactJunctions();
    void update();
    void releaseAll();
    void releaseActiveBlockers();
    void spawnBlockers( const AiCompressedRoadIt &exit,
                        const uint8_t state,
                        Entity **ppBlockers );
    Entity *acquireBlocker( const uint8_t state );
    void releaseBlocker( Entity *pBlocker );

    static int32_t CollectJunctionExits( const AiRoadMap::Junction *pJunction,
                                         AiRoadIt *pOutExits );
    static void
    ComputeLaneFrame( AiRoadIt it, const int32_t lane, Matrix4x4f_t &outFrame );

private:
    Entity *pTemplateLeft;
    Entity *pTemplateRight;
    std::vector<JunctionExits> junctionExits;
    MngActor mngActor;
    std::array<Entity *, kNumBlockers> pBlockerPool;
    std::array<bool, kNumBlockers> bBlockerActive;
    bool bRouteBuilt;
    AiPath route;
    bool bReleaseRequested;
    std::vector<BlockedJunction> blockedJunctions;
    bool bEnabled;
};
