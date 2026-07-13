#pragma once

class RenderTarget;

#include "render/postfx/postfx_instance.h"
#include "render/postfx/postfx_node.h"

class PostFXNodeCubemapToSphere;

// Cubemap to spherical envmap (realtime reflections)
class PostFXCubemapToSphere : public PostFX {
public:
    PostFXCubemapToSphere( PostFXNodeCubemapToSphere *pOwner );
    ~PostFXCubemapToSphere();

    virtual bool execute() override;
    virtual uint64_t getVertexShaderHashcode() const override;
    virtual uint64_t getPixelShaderHashcode() const override;
};

class PostFXNodeCubemapToSphere : public PostFXNode {
public:
    PostFXNodeCubemapToSphere();
    ~PostFXNodeCubemapToSphere();

    virtual uint32_t getType() const override;

    void setSaturation( float param_1 );
    void setLumFactor( float param_1 );
    void setHoodFactor( float param_1 );

    float getSaturation() const { return saturation; }
    float getLumFactor() const { return lumFactor; }
    float getHoodFactor() const { return hoodFactor; }

private:
    friend class PostFXCubemapToSphere;

private:
    float saturation;
    float lumFactor;
    float hoodFactor;
};
