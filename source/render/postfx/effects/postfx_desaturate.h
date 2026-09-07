#pragma once

class RenderTarget;

#include "render/postfx/postfx_instance.h"
#include "render/postfx/postfx_node.h"

class PostFXNodeDesaturate;

class PostFXNodeDesaturate : public PostFXNode {
public:
    inline float getSaturation() const { return saturation; }
    inline float getContrast() const { return contrast; }
    inline float getRedFactor() const { return rgbFactor[0]; }
    inline float getGreenFactor() const { return rgbFactor[1]; }
    inline float getBlueFactor() const { return rgbFactor[2]; }

public:
    PostFXNodeDesaturate();
    ~PostFXNodeDesaturate();

    virtual uint32_t getType() const override;

    void setSaturation( float param_1 );
    void setContrast( float param_1 );
    void setRGBFactor( float param_1, float param_2, float param_3 );

private:
    friend class PostFXDesaturate;

private:
    float rgbFactor[3];
    float saturation;
    float contrast;
};

class PostFXDesaturate : public PostFX {
public:
    PostFXDesaturate( PostFXNodeDesaturate *pOwner );
    ~PostFXDesaturate();

    virtual bool execute() override;
    virtual uint64_t getPixelShaderHashcode() const override;
};

struct PostFXStepDesaturate : public PostFXStep {
    PostFXStepDesaturate();
    ~PostFXStepDesaturate();

    bool initialize() override;
    void tick() override;
    void destroy() override;
    bool onConfigurationChanged() override { /* FUN_0042df00 */ return true; }
    PostFXNode *getInput() override { /* FUN_00419de0 */ return pNode; }

    PostFXNodeDesaturate *pNode;
};
