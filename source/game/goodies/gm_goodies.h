#pragma once

#include "game/gm_world.h"

#include "game/dressing/avatar_catalog_scene.h"
#include "render/mng_reflection.h"

// Clothes shop
class GMGoodies : public GameModeWorldBase, public AvatarCatalogScene {
public:
    GMGoodies();
    ~GMGoodies();

    void initialize() override;
    void reset() override;
    void tick( float deltaTime, float totalTime ) override;
    void pause() override;
    void destroy() override;
    bool onMessage( FlashMessage *pMessage ) override;

    void onInteriorReady() override;
    void stepItem( int32_t clotheSet,
                   int32_t category,
                   const FlashMessage &message ) override;
    int32_t countItems( AvatarPartDB *pDB,
                        int32_t gender,
                        int32_t category,
                        const char *pBrand,
                        int32_t clotheSet,
                        int32_t *pOutNumPlaceholders ) override;
    int32_t indexOfItem( AvatarPartDB *pDB,
                         const AvatarClothePart *pPart,
                         int32_t clotheSet ) override;

private:
    void updateShopTitle();

private:
    MngReflection mngReflection;
};
