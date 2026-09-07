#pragma once

#include "game/gm_base.h"
#include "game/scenaric/scenaric_bank.h"
#include "world/gs_world_streaming.h"

class Bank;
class Entity;
struct HiearchyNode;
struct SpotMap;
struct SpotSector;

// Scene for game modes using interior banks (e.g. GMLiving, GMGarage, GMHome,
// GMShop, GMDressing, ...)
class SceneInterior : public StreamingCallback {
public:
    enum eState : int32_t {
        SIS_Idle = 0,
        SIS_Instanciate = 1,
        SIS_Streaming = 2,
        SIS_Ready = 3,
        SIS_Live = 4,
    };

public:
    inline eState getState() const { return state; }
    inline bool isEntered() const { return bEntered; }
    inline SpotMap *getSourceSpot() const { return pSpot; }
    inline HiearchyNode *getRootNode() const { return pRootNode; }
    inline ScenaricBank &getBank() { return bank; }
    inline void setDrawInDOFPass( const bool value ) { bDrawInDOFPass = value; }
    inline bool isDrawnInDOFPass() const { return bDrawInDOFPass; }
    inline void setModeActive( const bool value ) {
        // FUN_007745d0
        bModeActive = value;
    }
    inline void setPlacementGameMode( const eGameMode mode ) {
        placementGameMode = mode;
    }
    eGameMode getPlacementGameMode() const;

public:
    SceneInterior();
    ~SceneInterior() override;

    bool onWorldStreamed( Eigen::Vector4f *param_1,
                          Eigen::Vector4f *param_2,
                          Eigen::Vector4f *param_3 ) override;

    void enter( Bank *pInteriorBank, bool bReentry );
    void leave();
    void tick();
    void draw();

    Eigen::Vector4f getWorldPosition() const;

private:
    void instanciate();
    bool waitForSpotSector();
    void applyWeatherDescriptor();

private:
    HiearchyNode *pRootNode;
    ScenaricBank bank;

    Entity *pShopData; // ET_SHDA
    eState state;
    bool bEntered;
    Bank *pInteriorBank;
    SpotMap *pSpot;
    SpotSector *pSpotSector;
    bool bReentry;
    float savedFovVsLodFactor;
    bool bOverrideFovVsLod;

    bool bDrawInDOFPass;
    bool bModeActive;
    eGameMode placementGameMode;
    int32_t spotSectorWaitTicks;
};

bool BuildInteriorBankPath( eGameMode gameMode, char *pOutPath );
extern bool gbSceneInteriorLeaving;
