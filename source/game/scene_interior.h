#pragma once

#include "game/gm_base.h"
#include "game/scenaric/scenaric_bank.h"
#include "world/gs_world_streaming.h"

class Bank;
class CameraPath;
class Entity;
class EntityShopData;
class Path;
class Render2DB;
struct HiearchyNode;
struct SpotMap;
struct SpotSector;

// Scene for game modes using interior banks (e.g. GMLiving, GMGarage, GMHome,
// GMShop, GMDressing, ...)
class PhysicsVehicle;

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
    inline EntityShopData *getShopData() const {
        return reinterpret_cast<EntityShopData *>( pShopData );
    }
    inline CameraPath *getInteriorCamera() const { return pInteriorCamera; }
    inline void setDrawInDOFPass( const bool value ) { bDrawInDOFPass = value; }
    inline bool isDrawnInDOFPass() const { return bDrawInDOFPass; }
    inline void setModeActive( const bool value ) {
        // FUN_007745d0
        bModeActive = value;
    }
    inline void setOverrideFovVsLod( const bool value ) {
        bOverrideFovVsLod = value;
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

    void
    enter( Bank *pInteriorBank, bool bReentry, bool bInstanciateNow = false );
    void leave();
    void tick();

    Eigen::Vector4f getWorldPosition() const;
    Matrix4x4f_t getInteriorTransform() const;
    Render2DB *getBankBitmap( const uint64_t hashcode );
    Path *getPositionPath( int32_t positionIndex ) const;
    void getPositionMatrix( int32_t positionIndex,
                            Matrix4x4f_t &outMatrix ) const;

    void focusCamera( const int32_t positionIndex );
    void moveCameraTo( const int32_t positionIndex );
    void
    startCameraPath( Path *pPath, const bool bInvert, const bool bStartAtEnd );
    void setupShowcaseCamera( const int32_t positionIndex,
                              PhysicsVehicle *pPhysicsVehicle,
                              const Matrix4x4f_t &carBoundingBox,
                              const bool bNoBlend );
    void restoreInteriorCamera();
    void resume();
    void suspend();
    void applyWeatherDescriptor();

private:
    void instanciate();
    bool waitForSpotSector();

private:
    HiearchyNode *pRootNode;
    ScenaricBank bank;

    Entity *pShopData; // ET_SHDA
    CameraPath *pInteriorCamera;
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

    bool bShowcaseCamera;
    Eigen::Vector4f showcaseTarget;
    float showcaseMaxDistance;
    Matrix4x4f_t showcaseCarBoundingBox;
};

bool BuildInteriorBankPath( eGameMode gameMode, char *pOutPath );
extern bool gbSceneInteriorLeaving;
