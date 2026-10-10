#pragma once

#include "ai/ai_road_map.h"

struct AiRoadIt;

struct ObjectLocationRoad {
    uint32_t RoadID = 0;
    float FractionMin = 0.0f;
    float FractionMax = 0.0f;
    float LateralMin = 0.0f;
    float LateralMax = 0.0f;
};

struct ObjectLocationOther {
    uint32_t RoadID = 0;
    float PositionX = 0.0f;
    float PositionZ = 0.0f;
};

struct ObjectWorldLocation {
    enum eLocationType : uint8_t {
        OWL_None = 0,
        OWL_Road = 1,
        OWL_Junction = 2,
        OWL_OffRoad = 3,
    };

    float VelocityX;
    float VelocityZ;
    uint8_t LocationType : 2;
    uint8_t bWay : 1;
    uint8_t bNearJunction : 1;

    union {
        ObjectLocationRoad LocationRoad;
        ObjectLocationOther LocationOther;
    };

    ObjectWorldLocation()
        : VelocityX( 0.0f ),
          VelocityZ( 0.0f ),
          LocationType( OWL_None ),
          bWay( 0 ),
          bNearJunction( 0 ),
          LocationRoad() {}

    void computeWorldPosition( Eigen::Vector4f &outPosition ) const;
};

class P2PPlayer;
class Camera;

class GameObject {
public:
    inline uint32_t getID() const { return objectID; }
    inline const ObjectWorldLocation &getWorldLocation() const {
        return worldLocation;
    }

public:
    GameObject();
    ~GameObject();

    bool isInFrustum( Camera *param_2,
                      float inSphereRadius,
                      Eigen::Vector4f *pSpherePos );

    void setRoadLocation( const AiRoadIt &it, const bool bDebug );
    void setJunctionLocation( const Matrix4x4f_t &worldMatrix,
                              const AiRoadMap::Junction *pJunction );

    virtual int32_t getType() = 0;
    virtual Matrix4x4f_t &getWorldMatrix() = 0;
    virtual Eigen::Vector4f getWorldPosition() = 0;
    virtual void retrieveBoundingSphere( Eigen::Vector4f *pOutSpherePosition,
                                         float *pOutSphereRadius ) = 0;
    virtual Eigen::Vector4f &getLinearVelocity();
    virtual Eigen::Vector4f &getDimensions() = 0;
    virtual void retrieveBoundingBox( Matrix4x4f_t &pOutBoundingBox ) = 0;
    virtual void retrieveAcceleration( Eigen::Vector4f &param_1 ) = 0;

protected:
    friend class GameObjectDico;
    friend class AiRoadBuckets;

    uint32_t objectID;
    uint32_t parentID;
    GameObject *pNextDico;
    GameObject *pNextInBucket;
    ObjectWorldLocation worldLocation;
    uint16_t netID;
    P2PPlayer *pNetOwner;
};

// Registry of the located game objects (DAT_01449428)
class GameObjectDico {
public:
    GameObjectDico();

    void registerObject( GameObject *pObject );
    void unregisterObject( GameObject *pObject );
    GameObject *findObject( const uint32_t id ) const;

    void updateLocations( const float deltaTime, const bool bDebug );
    void locate( GameObject *pObject,
                 AiRoadMap::Road *pRoadHint,
                 const bool bDebug );

private:
    uint32_t idCounter;
    GameObject *pFirstObject;
    int32_t numObjects;
};

extern GameObjectDico gGameObjectDico;
