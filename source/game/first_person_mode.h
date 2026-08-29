#pragma once

#include "first_person_pointer.h"
#include "physics/gs_physics.h"

class CameraFirstPerson;
class CameraGame;
class EntityCinematic;
class PhysicsCharacter;
class SceneInterior;

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

private:
    bool installCollision( SceneInterior *pScene );
    float measureOpenSpace( const Eigen::Vector4f &from );
    void removeCollision();

private:
    CameraFirstPerson *pCamera;
    PhysicsCharacter *pCharacter;
    CameraGame *pPreviousCamera;
    EntityCinematic *pLoopCinematic;
    const void *pCollisionKey;
    RigidBody_t pCollisionBody;

    FirstPersonPointer pointer;

    Eigen::Vector4f spawnPosition;
    Eigen::Vector4f hullMin;
    Eigen::Vector4f hullMax;
    uint32_t hullNumTriangles;
    const char *pSpawnSource;

    bool bActive;
    bool bToggleKeyDown;
    bool bSavedFlashCursorVisible;
};
