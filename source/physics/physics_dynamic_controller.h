#pragma once

#include <utility>
#include <vector>

// Rigid bodies with a set of constraints. Used by breakable props
class PhysicsDynamicController {
public:
    inline bool isCloned() const { return !members.empty(); }
    inline uint64_t getTemplateHashcode() const { return templateHashcode; }
    inline const std::vector<std::pair<uint64_t, void *>> &getMembers() const {
        return members;
    }
    inline const Matrix4x4f_t &getPrimaryLocalInverse() const {
        return primaryLocalInv;
    }

public:
    PhysicsDynamicController();
    ~PhysicsDynamicController();

    void bindTemplate( const uint64_t hashcode,
                       const float linear,
                       const float angular,
                       const bool bStaticBody );
    void cloneAndAdd( const PhysicsDynamicController &original,
                      const Matrix4x4f_t &worldTransform );
    void destroyAsClone();
    void setWorld( const PhysicsDynamicController &source,
                   const Matrix4x4f_t &worldTransform );
    bool getWorldMatrix( Matrix4x4f_t &outMatrix, bool &outActive ) const;
    bool isActive() const;

private:
    uint64_t templateHashcode;
    float linearDamping;
    float angularDamping;
    bool bStatic;

    std::vector<std::pair<uint64_t, void *>> members;
    std::vector<Matrix4x4f_t> memberLocalTransforms;
    Matrix4x4f_t primaryLocalInv;
};
