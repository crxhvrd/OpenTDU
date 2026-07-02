#pragma once

#include "avatar_clothes.h"
#include "render/gs_render_helper.h"

class AvatarPool : public RenderConfigurationChangeListener {
public:
    static constexpr int32_t kNumInstances = 9;

public:
    inline PooledAvatarEntry &getInstance( int32_t index ) {
        return pInstances[index];
    }

public:
    AvatarPool();

    void initialize();
    void tick();
    void terminate();

    bool onConfigurationChanged() override;

private:
    PooledAvatarEntry pInstances[kNumInstances];
};
