#pragma once

class RenderTarget;

#include "render/render_api.h"
#include "render/postfx/postfx_instance.h"
#include "render/postfx/postfx_node.h"

class PostFXNodeEnvMapScene;

class PostFXEnvMapScene : public PostFX {
public:
    PostFXEnvMapScene( PostFXNodeEnvMapScene *pOwner );
    ~PostFXEnvMapScene();

    virtual bool execute() override;
};

class PostFXNodeEnvMapScene : public PostFXNode {
public:
    PostFXNodeEnvMapScene();
    ~PostFXNodeEnvMapScene();

    virtual uint32_t getType() const override;

    eBlendOP getBlendOP() const { return blendOP; }
    eBlendMode getBlendSrc() const { return blendSrc; }
    eBlendMode getBlendDst() const { return blendDst; }

private:
    eBlendOP blendOP;
    eBlendMode blendSrc;
    eBlendMode blendDst;
};
