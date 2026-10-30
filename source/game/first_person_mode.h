#pragma once

#include "first_person_pointer.h"
#include "physics/gs_physics.h"

class CameraFirstPerson;
class FlashPlayer;
class FlashSprite;
struct MoviePlayer;
class CameraGame;
class Entity;
class EntityAvatar;
class EntityCinematic;
class PhysicsCharacter;
class SceneCarPark;
class SceneInterior;

void GatherParkedCarBodies( SceneCarPark &carPark,
                            std::vector<const JPH::Body *> &bodies );

// Applies the car specs gauges and class of the datas_base clip (only done by
// the movie when the clip is placed, i.e. on a regular car change)
void ApplyCarSpecsGauges( FlashPlayer *pPlayer, const char *pPanelPath );

// First Person Mode for interior game modes (GMLiving/GMGarage/GMGoodies/etc.).
class FirstPersonMode {
public:
    inline bool isActive() const { return bActive; }
    inline const Eigen::Vector4f &getHullMin() const { return hullMin; }
    inline const Eigen::Vector4f &getHullMax() const { return hullMax; }
    inline uint32_t getHullNumTriangles() const { return hullNumTriangles; }
    inline const char *getSpawnSource() const { return pSpawnSource; }
    inline CameraFirstPerson *getCamera() { return pCamera; }
    inline PhysicsCharacter *getCharacter() { return pCharacter; }
    inline int32_t getHoveredCar() const { return hoveredCar; }
    // Hovered car clicked this frame (-1 if none)
    inline int32_t getClickedCar() const { return clickedCar; }
#ifdef OTDU_DEVBUILD
    inline const Eigen::Vector4f &getSpawnPosition() const {
        return spawnPosition;
    }
#endif

public:
    FirstPersonMode();
    ~FirstPersonMode();

    void tick( SceneInterior *pScene, EntityCinematic *pLoopCinematic );
    void enter( SceneInterior *pScene, EntityCinematic *pLoopCinematic );
    void leave();

    void
    setExtraCollisionBodies( const std::vector<const JPH::Body *> &bodies );
    void updateHoveredCar( SceneCarPark &carPark );

    // Draws a single clip of a game mode movie (paused while in first person)
    void showMoviePanel( const char *pMovieName, const char *pClipPath );
    void hideMoviePanel();

private:
    bool installCollision( SceneInterior *pScene );
    void hideAvatars( std::vector<EntityAvatar *> &avatars );
    void restoreHiddenAvatars();
    float measureOpenSpace( const Eigen::Vector4f &from );
    void removeCollision();

private:
    CameraFirstPerson *pCamera;
    PhysicsCharacter *pCharacter;
    CameraGame *pPreviousCamera;
    EntityCinematic *pLoopCinematic;
    const void *pCollisionKey;
    RigidBody_t pCollisionBody;
    std::vector<const JPH::Body *> extraCollisionBodies;

    FirstPersonPointer pointer;
    std::vector<const JPH::Body *> hoverBodies;
    std::vector<int32_t> hoverCarIndices;
    int32_t hoveredCar;
    int32_t clickedCar;

    MoviePlayer *pPanelMovie;
    std::vector<std::pair<FlashSprite *, bool>> panelClipsVisibility;
    std::vector<std::pair<EntityAvatar *, bool>> hiddenAvatars;

    Eigen::Vector4f spawnPosition;
    Eigen::Vector4f hullMin;
    Eigen::Vector4f hullMax;
    uint32_t hullNumTriangles;
    const char *pSpawnSource;

    bool bActive;
    bool bSavedFlashCursorVisible;
};
