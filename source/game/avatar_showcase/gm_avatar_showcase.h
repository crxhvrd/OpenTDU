#pragma once

#include "core/color.h"
#include "flash/mng_flash.h"
#include "game/gm_base.h"
#include "game/avatar/avatar_configuration.h"
#include "game/scenaric/mng_actor.h"
#include "game/scenaric/scenaric_bank.h"
#include "render/mng_number.h"

class EntityAnmSet;
class EntityAvatar;
class FlashPlayer;
class GMAvatarShowcase;

class MngFlashAvatarShowcase : public MngFlash {
public:
    MngFlashAvatarShowcase( GMAvatarShowcase *pOwner );

    bool onMessage( FlashMessage *pMessage, FlashPlayer *pPlayer ) override;

private:
    GMAvatarShowcase *pGameModeInstance;
};

// Standalone avatar face editor
class GMAvatarShowcase : public GameMode {
public:
    GMAvatarShowcase();
    ~GMAvatarShowcase();

    void initialize() override;
    void reset() override;
    void tick( float deltaTime, float totalTime ) override;
    void draw( float deltaTime, float totalTime ) override {
        // FUN_007b65f0
        // NOP
    }
    void pause() override;
    void destroy() override;

    bool handleFlashMessage( FlashMessage *pMessage );

private:
    bool setFaceParam( const uint32_t value, const int32_t index );
    bool setFaceParamFromVariable( const char *pVariableName,
                                   const int32_t index );
    int32_t getNumAnimations() const;

private:
    ScenaricBank animSetBank;
    AvatarConfiguration avatarConfig;
    MngActor mngActor;
    MngNumber mngNumber;
    MngFlashAvatarShowcase mngFlash;

    EntityAvatar *pAvatar;
    FlashPlayer *pFlashPlayer;
    ColorRGBA backgroundColor;
    ColorRGBA clearColor;
    int32_t animIndex;
    EntityAnmSet *pAnimSet;
};
