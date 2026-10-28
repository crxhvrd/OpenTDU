#pragma once

#include "gm_scene.h"

#include "world/mng_world.h"
#include "world/mng_clouds_plane.h"
#include "world/mng_background.h"
#include "world/mng_ocean.h"
#include "world/mng_sun_flare.h"
#include "world/mng_tall_grass.h"
#include "world/mng_sector_name.h"
#include "world/mng_impostor.h"
#include "world/mng_electrical_network.h"

#include "game/vehicle/mng_car_tire_skid.h"
#include "game/vehicle/mng_car_alpha.h"

#include "game/mng_hud.h"

#include "render/mng_env_map.h"

class EventParameters;
class PhysicsVehicle;
class GameMechanism;
class PlayerGameMech;

// Game Mode running with the world active.
// e.g. GMShop; GMHome; GMBoatTour
class GameModeWorldBase : public GameModeScene {
public:
    GameModeWorldBase();
    ~GameModeWorldBase();

    void initialize() override;
    void destroy() override;

    bool onMessage( FlashMessage *pMessage ) override {
        // FUN_004182d0
        return false;
    }

protected:
    MngWorld mngWorld;
    MngCloudsPlane mngCloudsPlane;
    MngBackground mngBackground;
    MngOcean mngOcean;
    MngCarTireSkid mngCarTireSkid;
    MngSunFlare mngSunFlare;
    MngEnvMap mngEnvMap;
    MngImpostor mngImpostor;
    MngImpostorSimple mngImpostorSimple;
    MngTallGrass mngTallGrass;
    MngElectricalNetwork mngElectricalNetwork;
    MngCarAlpha mngCarAlpha;
    MngSectorName mngSectorName;
};

// Game Mode World with driving mechanics (e.g. GMFreeRide, GMTestCar)
class GameModeWorld : public GameModeWorldBase {
public:
    bool isPaused() override {
        // FUN_0042b4e0
        return bPaused;
    }

    inline bool isPauseMenuActive() const { return bPauseMenuActive; }
    inline bool isPauseMenuLocked() const { return bPauseMenuLocked; }
    inline void setPauseMenuLocked( const bool bLocked ) {
        bPauseMenuLocked = bLocked;
    }
    virtual bool usesFrontEndDirections() const { return false; }
    inline GameMechanism *getGameMech() const { return pGameMech; }
    PlayerGameMech *getPlayerGameMech() const;
    inline int32_t getGameMechType() const { return gameMechType; }
    inline uint32_t getGameMechID() const { return gameMechID; }
    inline void setGameMechType( const int32_t type ) { gameMechType = type; }
    inline void setGameMechID( const uint32_t id ) { gameMechID = id; }
    inline uint32_t getGameModeFlags() const { return flags; }
    inline MngHudChallenge &getHudChallenge() { return mngHudChallenge; }

public:
    // gameModeFlags:
    //  0 - vehicle is externally driven (not bound to MngActor/HUD)
    //  1 - is pause menu allowed
    //  2 - spawn the player car from the current garage slot on reset
    explicit GameModeWorld( uint32_t gameModeFlags );
    ~GameModeWorld();

    void initialize() override;
    void reset() override;
    void tick( float deltaTime, float totalTime ) override;
    void draw( float deltaTime, float totalTime ) override;
    void pause() override;
    void destroy() override;

    void onResourceStreamed() override;
    bool onMessage( FlashMessage *pMessage ) override;

    virtual bool restart() {
        // FUN_0042df00
        return true;
    }
    virtual bool isSpotInteriorVisible() {
        // FUN_00630320
        return false;
    }

    virtual EventParameters *getMissionDescription();
    virtual bool isDrivingInputScripted() { return false; }
    bool canEnterSpot() const;
    void unloadLocalPlayerCar();
    void respawnLocalPlayerCar( const Matrix4x4f_t &transform );
#if OTDU_DEVBUILD
    bool respawnLocalPlayerCarWithID( const Matrix4x4f_t &transform,
                                      const int32_t carConfigID,
                                      const int32_t rimID );
#endif
    void teleportLocalPlayerCarToPose( const Matrix4x4f_t &pose );

    // Pause-menu flow: onPauseMenuRequested (flash clip flag or focus loss)
    // -> pauseMenuActivated -> "pain" (pause in) -> "afok" (fade anim over)
    // -> "paou" (pause out).
    virtual void onPauseMenuRequested( bool bOpened );
    virtual void pauseMenuActivated();
    virtual void onMenuPauseEnter() {
        // FUN_0063f2b0
        // NOP
    }
    virtual void onMenuPauseAnimOver();
    virtual void onMenuPauseExit( bool bRestoreState );
    void setBlockPauseByStart( const bool bBlock );

protected:
    bool createGameMech( uint32_t mechID, int32_t mechType );
    void destroyGameMech();
    void updatePauseMenuTrigger();
    void updatePauseFade();

public:
    void updateOdometerAutosave();
    void updateOdometer();

protected:
    void uninstallLocalPlayerCar();
    void updateLocalPlayerDrivingInput( float deltaTime );
    void resetAutomaticGearbox();
    void teleportLocalPlayerCar( const Eigen::Vector3f &worldPos );
    void updateCarHotReload();
    void loadHotReloadedCar();

protected:
    MngHudChallenge mngHudChallenge;

    GameMechanism *pGameMech;
    int32_t
        gameMechType; // field4272_0x1031f4 — -1 default; 4: player car; 5: none
    uint32_t
        gameMechID; // field4273_0x1031f8 — 0xc0c3 default; GMFreeRide: 0xc0c1

    uint32_t flags;

    bool bPaused;          // 0x10320c
    bool bPauseMenuActive; // 0x10320d
    bool bPauseMenuLocked; // 0x103228 — blocks the pause-menu trigger
    bool bCarPhysicsFlag;  // 0x103229

    int32_t carColorIndex; // garage entry +0x58
    int32_t carRimIndex;   // garage entry +0x6c
    int32_t carTireIndex;  // garage entry +0x70

    int32_t lastOdometerKms;
    float lastDistance;
    float initializeTime; // GSTimer total time at initialize()
    float pauseFadeTimer; // Pause radial-blur fade countdown

    // HACK: REMOVE ME:
    bool bCarGrounded = false;
    PhysicsVehicle *pPlayerPhysicsVehicle = nullptr;
    // HACK: REMOVE ME:
};

extern float gFXPFadeInDelay;
extern float gFXPFadeOutDelay;
extern float gFXPLumThreshold;
extern float gFXPBlurSize;
extern float gFXPModCoeff0;
extern float gFXPModCoeff1;

extern GameModeWorld *gpActiveGM;

extern int32_t gRequestedPlayerCarID;
extern int32_t gRequestedPlayerCarColorID;
extern int32_t gRequestedPlayerCarRimID;
extern int32_t gRequestedPlayerCarInteriorID;
extern bool gRequestedCarChange;
extern bool gRequestedPlayerTeleport;
extern Eigen::Vector3f gRequestedPlayerTeleportPos;
