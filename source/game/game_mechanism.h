#pragma once

#include <utility>
#include <vector>

class Entity;
class ScenaricBank;
class PhysicsDynamicController;

#pragma pack( push, 1 )
struct SerializedObject {
    uint64_t Hashcode;
};

struct SerializedDynamicController : public SerializedObject {
    float FrictionDynamic;
    float FrictionStatic;
    float MaxClimbSlope;
};

struct SerializedPureDynamicController : public SerializedObject {
    SerializedObject Controlled;
    float LinearDamping;
    float AngularDamping;
    int32_t bStatic;
};
#pragma pack( pop )
OTDU_SIZE_MUST_MATCH( SerializedPureDynamicController, 28 );

class GameMechanism {
public:
    GameMechanism();
    ~GameMechanism();

    inline bool isCreated() const { return bInitialized; }
    inline bool isStaticController() const {
        return pPureDynamicController != nullptr &&
               pPureDynamicController->bStatic != 0;
    }
    inline bool hasDynamicController() const {
        return pPureDynamicController != nullptr;
    }
    inline bool hasPhysicsController() const { return pController != nullptr; }
    inline uint64_t getPropCollisionHashcode() const {
        return propCollisionHashcode;
    }

    int32_t getBrokenJointCount() const;

    void setEntity( Entity *pEntityInstance );

    void setPureDynamicControllerDesc( SerializedPureDynamicController *pDesc );

    void loadResources( ScenaricBank *param_2 );
    void tick();
    void terminate();
    void reInit();
    void instanciate( GameMechanism *pInstance );
    void initInstance();
    void removeAndDestroy();

    void setMechanismTransform( const Matrix4x4f_t &transformMatrix );
    void setMechanismPosition( const Eigen::Vector4f &position );
    void setMechanismInitialTransform( const Matrix4x4f_t &transformMatrix );

private:
    Matrix4x4f_t initialTransformMatrix;
    Matrix4x4f_t transformMatrix;

    PhysicsDynamicController *pController;
    PhysicsDynamicController *pParentController;

    SerializedDynamicController *pDynamicController;
    SerializedPureDynamicController *pPureDynamicController;

    float linearDamping;
    float angularDamping;

    Entity *pEntity;
    GameMechanism *pParent;
    int32_t numInstance;

    uint64_t propCollisionHashcode;
    std::vector<std::pair<uint64_t, void *>> propBodies;
    Matrix4x4f_t primaryLocalInv;

    uint8_t bControlledByAvatar : 1;
    uint8_t bInitialized : 1;
    uint8_t bTransformSet : 1;
    uint8_t bPlacementTried : 1;
    uint8_t bDynamicProp : 1; // placed as a simulated body (bStatic == 0)

private:
    void placeCollisionBody( const Matrix4x4f_t &worldTransform );
};
