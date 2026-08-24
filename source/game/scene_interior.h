#pragma once

#include "game/gm_base.h"
#include "game/scenaric/scenaric_bank.h"

class Bank;
class Entity;
struct HiearchyNode;
struct SpotMap;
struct SpotSector;

// Scene for game modes using interior banks (e.g. GMLiving, GMGarage, GMHome, GMShop, GMDressing, ...)
class SceneInterior {
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

public:
    SceneInterior();
    ~SceneInterior();

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

    Entity *pShopData;         // ET_SHDA
    eState state;             
    bool bEntered;             
    Bank *pInteriorBank;       
    SpotMap *pSpot;            
    SpotSector *pSpotSector;  
    bool bReentry;            
    float savedFovVsLodFactor; 
    bool bOverrideFovVsLod;   

    bool bDrawInDOFPass;
    int32_t spotSectorWaitTicks;
};

bool BuildInteriorBankPath( eGameMode gameMode,
                            char *pOutPath );
extern bool gbSceneInteriorLeaving;
