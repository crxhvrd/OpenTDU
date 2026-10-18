#pragma once

#include "base_entity.h"

enum eVolumeType : int32_t {
    VT_Plane = 0,    // z = 0 quad, tested against a segment
    VT_Sphere = 1,   // |p| <= 1
    VT_Box = 2,      // |p.x|, |p.y|, |p.z| <= 0.5
    VT_Cylinder = 3, // |p.y| <= 0.5 and p.xz length <= 1
    VT_Cone = 4,     // |p.y| <= 0.5 and p.xz length <= (0.5 - p.y)
    VT_ConeAlt = 5,  // identical to VT_Cone
};

// Trigger/exclusion volume
class EntityVolume : public Entity {
public:
    inline eVolumeType getVolumeType() const {
        return static_cast<eVolumeType>( Type );
    }
    inline void setVolumeType( eVolumeType volumeType ) { Type = volumeType; }
    inline const Matrix4x4f_t &getInverseTransform() const {
        return InverseTransform;
    }

public:
    EntityVolume();
    ~EntityVolume();

    void initialize() override;
    void unloadResources() override;
    void deserialize( BinaryReader *param_2 ) override;
    void propagateTransform( Matrix4x4f_t &transformMatrix,
                             bool param_1 ) override;
    void propagatePosition( Eigen::Vector4f &position, bool param_1 ) override;
    void instanciate( MngActor *pMngActor, Entity *param_1 ) override;
    bool isPointInside( const Eigen::Vector4f &point,
                        const Eigen::Vector4f *pPreviousPoint ) const;

protected:
    Matrix4x4f_t InverseTransform;
    Matrix4x4f_t LocalTransform;
    int32_t Type;
    uint32_t InstanceFlags;
};
